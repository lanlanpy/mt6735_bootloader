#include <malloc.h>
#ifdef MTK_GPT_SCHEME_SUPPORT
#include <platform/partition.h>
#else
#include <mt_partition.h>
#endif
#include <printf.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <video.h>
#include <dev/mrdump.h>
#include <platform/mtk_key.h>
#include <platform/mtk_wdt.h>
#include <platform/mt_gpt.h>
#include <target/cust_key.h>
#include <platform/boot_mode.h>
#include <platform/ram_console.h>
#include <arch/ops.h>

#include "aee.h"
#include "kdump.h"

extern BOOT_ARGUMENT *g_boot_arg;

#define MRDUMP_DELAY_TIME 10

struct mrdump_cblock_result *cblock_result = NULL;

static void voprintf(char type, const char *msg, va_list ap)
{
    char msgbuf[128], *p;

    p = msgbuf;
    if (msg[0] == '\r') {
        *p++ = msg[0];
        msg++;
    }

    *p++ = type;
    *p++ = ':';
    vsnprintf(p, sizeof(msgbuf) - (p - msgbuf), msg, ap);
    switch (type) {
    case 'I':
    case 'W':
    case 'E':
        video_printf("%s", msgbuf);
        break;
    }

    dprintf(CRITICAL,"%s", msgbuf);
    
    /* Write log buffer */
    p = msgbuf;
    while ((*p != 0) && (cblock_result->log_size < sizeof(cblock_result->log_buf))) {
	cblock_result->log_buf[cblock_result->log_size] = *p++;
	cblock_result->log_size++;
    }
}

void voprintf_verbose(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    voprintf('V', msg, ap);
    va_end(ap);
}

void voprintf_debug(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    voprintf('D', msg, ap);
    va_end(ap);
}

void voprintf_info(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    voprintf('I', msg, ap);
    va_end(ap);
}

void voprintf_warning(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    voprintf('W', msg, ap);
    va_end(ap);
}

void voprintf_error(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    voprintf('E', msg, ap);
    va_end(ap);
}

void vo_show_progress(int sizeM)
{
    video_set_cursor((video_get_rows() / 4) * 3, (video_get_colums() - 22)/ 2);
    video_printf("=====================\n");
    video_set_cursor((video_get_rows() / 4) * 3 + 1, (video_get_colums() - 22)/ 2);
    video_printf(">>> Written %4dM <<<\n", sizeM);
    video_set_cursor((video_get_rows() / 4) * 3 + 2, (video_get_colums() - 22)/ 2);
    video_printf("=====================\n");
    video_set_cursor(video_get_rows() - 1, 0);

    dprintf(CRITICAL,"... Written %dM\n", sizeM);
}

static void mrdump_status(const char *status, const char *fmt, va_list ap)
{
    if (cblock_result != NULL) {
        char *dest = strcpy(cblock_result->status, status);
        dest += strlen(dest);
        *dest++ = '\n';
    
        vsnprintf(dest, sizeof(cblock_result->status) - (dest - cblock_result->status), fmt, ap);
    }
}

void mrdump_status_ok(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mrdump_status("OK", fmt, ap);
    va_end(ap);
}

void mrdump_status_none(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mrdump_status("NONE", fmt, ap);
    va_end(ap);
}

void mrdump_status_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mrdump_status("FAILED", fmt, ap);
    va_end(ap);
}

uint32_t g_aee_mode = AEE_MODE_MTK_ENG;

const const char *mrdump_mode2string(uint8_t mode)
{
  switch (mode) {
  case AEE_REBOOT_MODE_NORMAL:
    return "NORMAL-BOOT";

  case AEE_REBOOT_MODE_KERNEL_OOPS:
    return "KERNEL-OOPS";

  case AEE_REBOOT_MODE_KERNEL_PANIC:
    return "KERNEL-PANIC";

  case AEE_REBOOT_MODE_NESTED_EXCEPTION:
    return "NESTED-CPU-EXCEPTION";

  case AEE_REBOOT_MODE_WDT:
    return "HWT";

  case AEE_REBOOT_MODE_EXCEPTION_KDUMP:
    return "MANUALDUMP";

  default:
    return "UNKNOWN-BOOT";
  }
}

static void kdump_ui(struct mrdump_control_block *mrdump_cblock)
{
    video_clean_screen();
    video_set_cursor(0, 0);

    mrdump_status_error("Unknown error\n");
    voprintf_info("Kdump triggerd by '%s'\n", mrdump_mode2string(mrdump_cblock->crash_record.reboot_mode));

    struct aee_timer elapse_time;
    aee_timer_init(&elapse_time);

    uint32_t total_dump_size = memory_size();
    
    aee_timer_start(&elapse_time);
    switch (mrdump_cblock->machdesc.output_device) {
    case MRDUMP_DEV_NULL:
        kdump_null_output(mrdump_cblock, total_dump_size);
        break;
#if 0
    case MRDUMP_DEV_SDCARD:
        kdump_sdcard_output(mrdump_cblock, total_dump_size);
        break;
#endif
    case MRDUMP_DEV_EMMC:
        kdump_emmc_output(mrdump_cblock, total_dump_size);
        break;

    default:
        voprintf_error("Unknown device id %d\n", mrdump_cblock->machdesc.output_device);
    }
    aee_mrdump_flush_cblock(mrdump_cblock);
    aee_timer_stop(&elapse_time);
    
    voprintf_info("Reset count down %d ...\n", MRDUMP_DELAY_TIME);
    mtk_wdt_restart();

    int timeout = MRDUMP_DELAY_TIME;
    while(timeout-- >= 0) {
        mdelay(1000);
        mtk_wdt_restart();
	voprintf_info("\rsec %d", timeout);
    }

    video_clean_screen();
    video_set_cursor(0, 0);
}

int mrdump_detection(void)
{
    struct mrdump_control_block *mrdump_cblock = aee_mrdump_get_params();
    if (mrdump_cblock == NULL) {
        return 0;
    }

    memset(&mrdump_cblock->result, 0, sizeof(struct mrdump_cblock_result));
    cblock_result = &mrdump_cblock->result;

    if (strcmp(mrdump_cblock, MRDUMP_VERSION) == 0) {
	voprintf_debug("Cold boot or kernel not support MT-RAMDUMP\n");
	mrdump_status_none("Cold boot or kernel not support MT-RAMDUMP\n");
	return 0;
    }

    if (!g_boot_arg->ddr_reserve_enable) {
	voprintf_debug("DDR reserve mode disabled\n");
	mrdump_status_none("DDR reserve mode disabled\n");
	return 0;
    }

    if (!g_boot_arg->ddr_reserve_success) {
	voprintf_debug("DDR reserve mode failed\n");
	mrdump_status_none("DDR reserve mode failed\n");
	return 0;
    }
    uint8_t reboot_mode = mrdump_cblock->crash_record.reboot_mode;
    if (mrdump_cblock->machdesc.nr_cpus == 0) {
	voprintf_debug("Runtime disabled\n");
	mrdump_status_none("Runtime disabled\n");
	return 0;
    }

    voprintf_debug("sram record with mode %d\n", reboot_mode);
    switch (reboot_mode) {
    case AEE_REBOOT_MODE_NORMAL:  {
	if (!ram_console_is_abnormal_boot()) {
	    mrdump_status_none("Normal boot\n");
	    return 0;
	}
	else {
	    /* SoC trigger HW REBOOT */
	    mrdump_cblock->crash_record.reboot_mode = AEE_REBOOT_MODE_WDT;
	    return 1;
	}
    }
    case AEE_REBOOT_MODE_KERNEL_OOPS:
    case AEE_REBOOT_MODE_KERNEL_PANIC:
    case AEE_REBOOT_MODE_NESTED_EXCEPTION:
    case AEE_REBOOT_MODE_WDT:
    case AEE_REBOOT_MODE_EXCEPTION_KDUMP:
      return 1;
    }
    return 0;
}

int mrdump_run2(void)
{
    struct mrdump_control_block *mrdump_cblock = aee_mrdump_get_params();
    if (mrdump_cblock != NULL) {
        kdump_ui(mrdump_cblock);
	return 1;
    }
    return 0;
}

void aee_timer_init(struct aee_timer *t)
{
    memset(t, 0, sizeof(struct aee_timer));
}

void aee_timer_start(struct aee_timer *t)
{
    t->start_ms = get_timer_masked();
}

void aee_timer_stop(struct aee_timer *t)
{
    t->acc_ms += (get_timer_masked() - t->start_ms);
    t->start_ms = 0;
}

void *kdump_core_header_init(const struct mrdump_control_block *kparams, uint64_t kmem_address, uint64_t kmem_size)
{
    voprintf_info("kernel page offset %llu\n", kparams->machdesc.page_offset);
    if (kparams->machdesc.page_offset <= 0xffffffffULL) {
	voprintf_info("32b kernel detected\n");
        return kdump_core32_header_init(kparams, kmem_address, kmem_size);
    }
    else {
	voprintf_info("64b kernel detected\n");
        return kdump_core64_header_init(kparams, kmem_address, kmem_size);
    }
}
