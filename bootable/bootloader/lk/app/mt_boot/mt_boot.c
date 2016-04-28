#include <app.h>
#include <debug.h>
#include <arch.h>
#include <arch/arm.h>
#include <dev/udc.h>
#include <reg.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/thread.h>
#include <arch/ops.h>
#include <ctype.h>

#include <target.h>
#include <platform.h>

#include <platform/mt_reg_base.h>
#include <platform/boot_mode.h>
#include <platform/bootimg.h>
#ifdef MTK_GPT_SCHEME_SUPPORT
#include <platform/partition.h>
#else
#include <mt_partition.h>
#endif
#include <platform/mt_disp_drv.h>
#include <platform/disp_drv.h>
#include <platform/env.h>
#include <target/cust_usb.h>
#include <platform/mt_gpt.h>
#if defined(MTK_SECURITY_SW_SUPPORT) && defined(MTK_VERIFIED_BOOT_SUPPORT)
#include "oemkey.h"
#endif

#ifdef MTK_KERNEL_POWER_OFF_CHARGING
#include "platform/mtk_wdt.h"
extern int kernel_charging_boot(void);
extern int pmic_detect_powerkey(void);
extern void mt6575_power_off(void);
extern void mt65xx_backlight_off(void);
#endif
extern void jumparch64(u32 addr, u32 arg1, u32 arg2);
extern void jumparch64_smc(u32 addr, u32 arg1, u32 arg2);

extern u32 memory_size(void);
extern unsigned *target_atag_devinfo_data(unsigned *ptr);
extern unsigned *target_atag_videolfb(unsigned *ptr);
extern unsigned *target_atag_mdinfo(unsigned *ptr);
extern unsigned *target_atag_ptp(unsigned *ptr);
extern void platform_uninit(void);
extern int mboot_android_load_bootimg_hdr(char *part_name, unsigned long addr);
extern int mboot_android_load_bootimg(char *part_name, unsigned long addr);
extern int mboot_android_load_recoveryimg_hdr(char *part_name, unsigned long addr);
extern int mboot_android_load_recoveryimg(char *part_name, unsigned long addr);
extern int mboot_android_load_factoryimg_hdr(char *part_name, unsigned long addr);
extern int mboot_android_load_factoryimg(char *part_name, unsigned long addr);
extern void custom_port_in_kernel(BOOTMODE boot_mode, char *command);
extern const char* mt_disp_get_lcm_id(void);
extern unsigned int DISP_GetVRamSize(void);
extern int mt_disp_is_lcm_connected(void);
extern int fastboot_init(void *base, unsigned size);
extern int sec_func_init(int dev_type);
extern int sec_boot_check (int try_lock);
extern int seclib_set_oemkey(u8 *key, u32 key_size);
extern BI_DRAM bi_dram[MAX_NR_BANK];
#ifdef DEVICE_TREE_SUPPORT
#include <libfdt.h>
extern unsigned int *device_tree, device_tree_size;
#endif
extern int platform_skip_hibernation(void) __attribute__((weak));

int g_is_64bit_kernel = 0;

boot_img_hdr *g_boot_hdr = NULL;
char g_CMDLINE [300] = COMMANDLINE_TO_KERNEL;
char g_boot_reason[][16]={"power_key","usb","rtc","wdt","wdt_by_pass_pwk","tool_by_pass_pwk","2sec_reboot","unknown","kernel_panic","reboot","watchdog"};
#if defined(MTK_SECURITY_SW_SUPPORT) && defined(MTK_VERIFIED_BOOT_SUPPORT)
u8 g_oemkey[OEM_PUBK_SZ] = {OEM_PUBK};
#endif

/* Please define SN_BUF_LEN in cust_usb.h */
#ifndef SN_BUF_LEN
#define SN_BUF_LEN	19	/* fastboot use 13 bytes as default, max is 19 */
#endif

#define FDT_BUFF_SIZE  1024
#define FDT_BUFF_PATTERN  "BUFFEND"

#define DEFAULT_SERIAL_NUM "0123456789ABCDEF"

/*
 * Support read barcode from /dev/pro_info to be serial number.
 * Then pass the serial number from cmdline to kernel.
 */
/* #define SERIAL_NUM_FROM_BARCODE */

#if defined(CONFIG_MTK_USB_UNIQUE_SERIAL) || (defined(MTK_SECURITY_SW_SUPPORT) && defined(MTK_SEC_FASTBOOT_UNLOCK_SUPPORT))
#define SERIALNO_LEN	38	/* from preloader */
char sn_buf[SN_BUF_LEN+1] = "";	/* will read from EFUSE_CTR_BASE */
#else
#define SERIALNO_LEN	38
char sn_buf[SN_BUF_LEN+1] = FASTBOOT_DEVNAME;
#endif

static struct udc_device surf_udc_device = {
	.vendor_id	= USB_VENDORID,
	.product_id	= USB_PRODUCTID,
	.version_id	= USB_VERSIONID,
	.manufacturer	= USB_MANUFACTURER,
	.product	= USB_PRODUCT_NAME,
};

void msg_header_error(char *img_name)
{
	dprintf(CRITICAL,"[MBOOT] Load '%s' partition Error\n", img_name);
	dprintf(CRITICAL,"\n*******************************************************\n");
	dprintf(CRITICAL,"ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR\n");
	dprintf(CRITICAL,"*******************************************************\n");
	dprintf(CRITICAL,"> If you use NAND boot\n");
	dprintf(CRITICAL,"> (1) %s is wrong !!!! \n", img_name);
	dprintf(CRITICAL,"> (2) please make sure the image you've downloaded is correct\n");
	dprintf(CRITICAL,"\n> If you use MSDC boot\n");
	dprintf(CRITICAL,"> (1) %s is not founded in SD card !!!! \n",img_name);
	dprintf(CRITICAL,"> (2) please make sure the image is put in SD card\n");
	dprintf(CRITICAL,"*******************************************************\n");
	dprintf(CRITICAL,"ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR\n");
	dprintf(CRITICAL,"*******************************************************\n");
	while(1);
}

void msg_img_error(char *img_name)
{
	dprintf(CRITICAL,"[MBOOT] Load '%s' partition Error\n", img_name);
	dprintf(CRITICAL,"\n*******************************************************\n");
	dprintf(CRITICAL,"ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR\n");
	dprintf(CRITICAL,"*******************************************************\n");
	dprintf(CRITICAL,"> Please check kernel and rootfs in %s are both correct.\n",img_name);
	dprintf(CRITICAL,"*******************************************************\n");
	dprintf(CRITICAL,"ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR.ERROR\n");
	dprintf(CRITICAL,"*******************************************************\n");
	while(1);
}

//*********
//* Notice : it's kernel start addr (and not include any debug header)
extern unsigned int g_kmem_off;

//*********
//* Notice : it's rootfs start addr (and not include any debug header)
extern unsigned int g_rmem_off;
extern unsigned int g_rimg_sz;
extern int g_nr_bank;
extern unsigned int boot_time;
extern BOOT_ARGUMENT *g_boot_arg;
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
extern bool g_boot_reason_change;
#endif
extern int has_set_p2u;
extern unsigned int g_fb_base;
extern unsigned int g_fb_size;

static void check_hibernation(char *cmdline)
{
    int hibboot = 0;

    hibboot = get_env("hibboot") == NULL ? 0 : atoi(get_env("hibboot"));

	switch (g_boot_mode) {
	case RECOVERY_BOOT:
	case FACTORY_BOOT:
	case ALARM_BOOT:
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
	case KERNEL_POWER_OFF_CHARGING_BOOT:
	case LOW_POWER_OFF_CHARGING_BOOT:
#endif
        goto SKIP_HIB_BOOT;
	}

	if (platform_skip_hibernation && platform_skip_hibernation())
		goto SKIP_HIB_BOOT;

    if (get_env("resume") != NULL) {
        if (1 == hibboot) {
            sprintf(cmdline,"%s%s%s",cmdline," resume=", get_env("resume"));
            //sprintf(cmdline, "%s%s", cmdline, " no_console_suspend");
        } else if (0 != hibboot)
            dprintf(CRITICAL,"resume = %s but hibboot = %s\n", get_env("resume"), get_env("hibboot"));
    } else {
        dprintf(CRITICAL,"resume = NULL \n");
    }

    return;

SKIP_HIB_BOOT:
    if (hibboot != 0)
        if (set_env("hibboot", "0") != 0)
            dprintf(CRITICAL,"lk_env hibboot set failed!!!\n");
    if (get_env("resume") != NULL)
        if (set_env("resume", '\0') != 0)
            dprintf(CRITICAL,"lk_evn resume set resume failed!!!\n");
}

#ifdef DEVICE_TREE_SUPPORT

void lk_jump64(u32 addr, u32 arg1, u32 arg2)
{
    dprintf(CRITICAL,"\n[LK]jump to K64 0x%x\n", addr);
#if 0
    dprintf(CRITICAL,"use warm rest to k64\n");
    jumparch64(addr, arg1, arg2);
#else
    dprintf(INFO,"smc jump\n");
    jumparch64_smc(addr, arg1, arg2);
    dprintf(CRITICAL,"Do nothing now! wait for SMC done\n");
#endif
    while(1)
        ;
}

void memcpy_u8(unsigned char *dest, unsigned char *src, unsigned int size)
{
    unsigned int i;

    for(i = 0; i < size; i++)
    {
        *(dest + i) = *(src + i);
    }
}

extern bool decompress_kernel(unsigned char *in, void *out, int inlen, int outlen);

int boot_linux_fdt(void *kernel, unsigned *tags,
                   char *cmdline, unsigned machtype,
                   void *ramdisk, unsigned ramdisk_size)
{
    void *fdt = tags;
    int ret;
    int offset;
    dt_dram_info mem_reg_property[128];

    int i;
    void (*entry)(unsigned,unsigned,unsigned*) = kernel;
    unsigned int lk_t = 0;
    unsigned int pl_t = 0;
    unsigned int boot_reason = 0;
    char buf[FDT_BUFF_SIZE+8], *ptr;

    unsigned int zimage_size;
    unsigned int dtb_addr=0;
    unsigned int dtb_size;

    if(g_is_64bit_kernel)
    {
        unsigned char *magic;
        unsigned int addr;
        unsigned int zimage_addr = (unsigned int)target_get_scratch_address();
        /* to boot k64*/
        dprintf(INFO,"64 bits kernel\n");

        dprintf(INFO,"g_boot_hdr=%p\n", g_boot_hdr);
        dprintf(INFO,"g_boot_hdr->kernel_size=0x%08x\n", g_boot_hdr->kernel_size);
        zimage_size = (g_boot_hdr->kernel_size);

        if(g_boot_hdr->kernel_addr & 0x7FFFF)
        {
            dprintf(CRITICAL,"64 bit kernel can't boot at g_boot_hdr->kernel_addr=0x%08x\n",g_boot_hdr->kernel_addr);
            dprintf(CRITICAL,"Please check your bootimg setting\n");
            while(1)
                ;
        }

        addr = (unsigned int)(zimage_addr + zimage_size);


        for(dtb_size = 0; dtb_size < zimage_size; dtb_size++, addr--)
        {
            //FDT_MAGIC 0xd00dfeed ... dtf
            //dtb append after image.gz may not 4 byte alignment
            magic = (unsigned char *)addr;
            if( *(magic + 3) == 0xED &&
                *(magic + 2) == 0xFE &&
                *(magic + 1) == 0x0D &&
                *(magic + 0) == 0xD0)

            {
                dtb_addr = addr;
                dprintf(INFO,"get dtb_addr=0x%08x, dtb_size=0x%08x\n", dtb_addr, dtb_size);
                dprintf(INFO,"copy dtb, fdt=0x%08x\n", (unsigned int)fdt);

                //fix size to 4 byte alignment
                dtb_size = (dtb_size + 0x3) & (~0x3);

                memcpy_u8(fdt, (void *)dtb_addr, dtb_size);
                dtb_addr = (unsigned int)fdt;

                break;
            }
        }

        if(dtb_size != zimage_size)
        {
            zimage_size -= dtb_size;
        }
        else
        {
            dprintf(CRITICAL,"can't find device tree\n");
        }

        dprintf(INFO,"zimage_addr=0x%08x, zimage_size=0x%08x\n", zimage_addr, zimage_size);
        dprintf(INFO,"decompress kernel image...");

        /* for 64bit decompreesed size.
         * LK start: 0x41E00000, Kernel Start: 0x40080000
         * Max is 0x41E00000 - 0x40080000 = 0x1D80000.
         * using 0x1C00000=28MB for decompressed kernel image size */
        if(decompress_kernel((unsigned char *)zimage_addr, (void *)g_boot_hdr->kernel_addr, (int)zimage_size, (int)0x1C00000))
        {
            dprintf(CRITICAL,"decompress kernel image fail!!!\n");
            while(1)
                ;
        }
    }
    else
    {
        dprintf(INFO,"32 bits kernel\n");
        zimage_size = *(unsigned int *)((unsigned int)kernel+0x2c) - *(unsigned int *)((unsigned int)kernel+0x28);
        dtb_addr = (unsigned int)kernel + zimage_size;
    }

    if(fdt32_to_cpu(*(unsigned int *)dtb_addr) == FDT_MAGIC)
    {
        dtb_size = fdt32_to_cpu(*(unsigned int *)(dtb_addr+0x4));
    }
    else
    {
        dprintf(CRITICAL,"Can't find device tree. Please check your kernel image\n");
        while(1)
            ;
        //dprintf(CRITICAL,"use default device tree\n");
        //dtb_addr = (unsigned int)&device_tree;
        //dtb_size = device_tree_size;
    }
    dprintf(INFO,"dtb_addr = 0x%08X, dtb_size = 0x%08X\n", dtb_addr, dtb_size);

    if(((unsigned int)fdt + dtb_size) > g_fb_base)
    {
        dprintf(CRITICAL,"[ERROR] dtb end address (0x%08X) is beyond the memory (0x%08X).\n", (unsigned int)fdt+dtb_size, g_fb_base);
        return FALSE;
    }
    memcpy(fdt, (void *)dtb_addr, dtb_size);

    strcpy(&buf[FDT_BUFF_SIZE], FDT_BUFF_PATTERN);

    ret = fdt_open_into(fdt, fdt, MIN(0x100000, (g_fb_base-(unsigned int)fdt)));
    if (ret) return FALSE;
    ret = fdt_check_header(fdt);
    if (ret) return FALSE;

    extern int target_fdt_jtag(void *fdt)__attribute__((weak));
    if(target_fdt_jtag)
    {
        target_fdt_jtag(fdt);
    }

    extern int target_fdt_model(void *fdt)__attribute__((weak));
    if(target_fdt_model)
    {
        target_fdt_model(fdt);
    }

    extern int target_fdt_cpus(void *fdt)__attribute__((weak));
    if(target_fdt_cpus)
    {
        target_fdt_cpus(fdt);
    }

    extern int setup_mem_property_use_mblock_info(dt_dram_info *, size_t) __attribute__((weak));
    if(setup_mem_property_use_mblock_info)
    {
        ret = setup_mem_property_use_mblock_info(
            &mem_reg_property[0],
            sizeof(mem_reg_property)/sizeof(dt_dram_info));
        if(ret) return FALSE;
    }
    else
    {
        for(i = 0; i < g_nr_bank; ++i)
        {
            unsigned int fb_size = (i == g_nr_bank-1) ? g_fb_size : 0;

#ifndef MTK_LM_MODE
            mem_reg_property[i].start_hi = cpu_to_fdt32(0);
            mem_reg_property[i].start_lo = cpu_to_fdt32(bi_dram[i].start);
            mem_reg_property[i].size_hi = cpu_to_fdt32(0);
            mem_reg_property[i].size_lo = cpu_to_fdt32(bi_dram[i].size-fb_size);

#else
            mem_reg_property[i].start_hi = cpu_to_fdt32(bi_dram[i].start>>32);
            mem_reg_property[i].start_lo = cpu_to_fdt32(bi_dram[i].start);
            mem_reg_property[i].size_hi = cpu_to_fdt32((bi_dram[i].size-fb_size)>>32);
            mem_reg_property[i].size_lo = cpu_to_fdt32(bi_dram[i].size-fb_size);

#endif
            dprintf(INFO," mem_reg_property[%d].start_hi = 0x%08X\n", i, mem_reg_property[i].start_hi);
            dprintf(INFO," mem_reg_property[%d].start_lo = 0x%08X\n", i, mem_reg_property[i].start_lo);
            dprintf(INFO," mem_reg_property[%d].size_hi = 0x%08X\n", i, mem_reg_property[i].size_hi);
            dprintf(INFO," mem_reg_property[%d].size_lo = 0x%08X\n", i, mem_reg_property[i].size_lo);
        }
    }

    extern int target_fdt_dram_dummy_read(void *fdt, unsigned int rank_num)__attribute__((weak));
    if (target_fdt_dram_dummy_read) {
        ret = target_fdt_dram_dummy_read(fdt, g_nr_bank);
        if (ret)
            dprintf(CRITICAL,"ERROR: DRAM dummy read address incorrect\n");
    }

    offset = fdt_path_offset(fdt, "/memory");
    extern int get_mblock_num(void) __attribute__((weak));
    ret = fdt_setprop(fdt, offset, "reg", mem_reg_property,
		  ((int)get_mblock_num? get_mblock_num(): g_nr_bank ) * sizeof(dt_dram_info));
    if (ret) return FALSE;

    offset = fdt_path_offset(fdt, "/chosen");
    ret = fdt_setprop_cell(fdt, offset, "linux,initrd-start",(unsigned int) ramdisk);
    if (ret) return FALSE;
    ret = fdt_setprop_cell(fdt, offset, "linux,initrd-end", (unsigned int)ramdisk + ramdisk_size);
    if (ret) return FALSE;

    ptr = (char *)target_atag_boot((unsigned *)buf);
    ret = fdt_setprop(fdt, offset, "atag,boot", buf, ptr - buf);
    if (ret) return FALSE;

	
	ptr = (char *)target_atag_imix_r((unsigned *)buf);
	ret = fdt_setprop(fdt, offset, "atag,imix_r", buf, ptr - buf);
	if (ret) return FALSE;


    ptr = (char *)target_atag_mem((unsigned *)buf);
    ret = fdt_setprop(fdt, offset, "atag,mem", buf, ptr - buf);
    if (ret) return FALSE;

    extern int platform_atag_append(void *fdt) __attribute__((weak));
    if(platform_atag_append)
    {
        ret = platform_atag_append(fdt);
        if(ret) return FALSE;

    }
#if 0
	if(target_atag_partition_data)
	{
		ptr = (char *)target_atag_partition_data((unsigned *)buf);
		if(ptr != buf)
		{
			ret = fdt_setprop(fdt, offset, "atag,mem", buf, ptr - buf);
    		if (ret) return FALSE;
		}
	}
//#ifndef MTK_EMMC_SUPPORT
	if(target_atag_nand_data)
	{
		ptr = (char *)target_atag_nand_data((unsigned *)buf);
		if(ptr != buf)
		{
			ret = fdt_setprop(fdt, offset, "atag,mem", buf, ptr - buf);
    		if (ret) return FALSE;
		}
	}
#endif
    extern unsigned int *target_atag_vcore_dvfs(unsigned *ptr)__attribute__((weak));
    if(target_atag_vcore_dvfs)
    {
        ptr = (char *)target_atag_vcore_dvfs((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "atag,vcore_dvfs", buf, ptr - buf);
        if (ret) return FALSE;
    }
    else
    {
        dprintf(CRITICAL,"Not Support VCORE DVFS\n");
    }

    //some platform might not have this function, use weak reference for
    extern unsigned *target_atag_dfo(unsigned *ptr)__attribute__((weak));
    if(target_atag_dfo)
    {
        ptr = (char *)target_atag_dfo((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "atag,dfo", buf, ptr - buf);
        if (ret) return FALSE;
    }

    if(g_boot_mode == META_BOOT || g_boot_mode == ADVMETA_BOOT || g_boot_mode == ATE_FACTORY_BOOT || g_boot_mode == FACTORY_BOOT)
    {
        ptr = (char *)target_atag_meta((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "atag,meta", buf, ptr - buf);
        if (ret) return FALSE;
#ifdef MTK_MULTI_INIT_SUPPORT
        sprintf(cmdline, "%s%s", cmdline, " rdinit=sbin/multi_init");
#endif
        unsigned int meta_com_id = g_boot_arg->meta_com_id;
		if ((meta_com_id & 0x0001) != 0)
		{
		    sprintf(cmdline, "%s%s", cmdline, " androidboot.usbconfig=1");
		}
		else
		{
		    sprintf(cmdline, "%s%s", cmdline, " androidboot.usbconfig=0");
		}
		if(g_boot_mode == META_BOOT)
		{
		    if((meta_com_id & 0x0002) != 0)
		    {
		        sprintf(cmdline, "%s%s", cmdline, " androidboot.mblogenable=0");
		    }
		    else
		    {
		        sprintf(cmdline, "%s%s", cmdline, " androidboot.mblogenable=1");
		    }
		}
    }
    ptr = (char *)target_atag_devinfo_data((unsigned *)buf);
    ret = fdt_setprop(fdt, offset, "atag,devinfo", buf, ptr - buf);
    if (ret) return FALSE;

#ifndef MACH_FPGA_NO_DISPLAY
    ptr = (char *)target_atag_videolfb((unsigned *)buf);
    ret = fdt_setprop(fdt, offset, "atag,videolfb", buf, ptr - buf);
    if (ret) return FALSE;
#endif

    extern unsigned int *target_atag_mdinfo(unsigned *ptr)__attribute__((weak));
    if(target_atag_mdinfo)
    {
        ptr = (char *)target_atag_mdinfo((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "atag,mdinfo", buf, ptr - buf);
        if (ret) return FALSE;
    }
    else
    {
        dprintf(CRITICAL,"DFO_MODEN_INFO Only support in MT6582/MT6592\n");
    }

    extern unsigned int *target_atag_ptp(unsigned *ptr)__attribute__((weak));
    if(target_atag_ptp)
    {
        ptr = (char *)target_atag_ptp((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "atag,ptp", buf, ptr - buf);
        if (ret) 
        	return FALSE;
        else
        	 dprintf(CRITICAL,"Create PTP DT OK\n");
    }
    else
    {
        dprintf(CRITICAL,"PTP_INFO Only support in MT6795\n");
    }

    extern unsigned int *target_atag_tee(unsigned *ptr)__attribute__((weak));
    if(target_atag_tee)
    {
        ptr = (char *)target_atag_tee((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "tee_reserved_mem", buf, ptr - buf);
        if (ret) return FALSE;
    }
    else
    {
        dprintf(CRITICAL,"tee_reserved_mem not supported\n");
    }

    extern unsigned int *target_atag_isram(unsigned *ptr)__attribute__((weak));
    if(target_atag_isram)
    {
        ptr = (char *)target_atag_isram((unsigned *)buf);
        ret = fdt_setprop(fdt, offset, "non_secure_sram", buf, ptr - buf);
        if (ret) return FALSE;
    }
    else
    {
        dprintf(CRITICAL,"non_secure_sram not supported\n");
    }


    if (!has_set_p2u) {
#ifdef USER_BUILD
        sprintf(cmdline,"%s%s",cmdline," printk.disable_uart=1");
#else
        sprintf(cmdline,"%s%s",cmdline," printk.disable_uart=0 ddebug_query=\"file *mediatek* +p ; file *gpu* =_\"");
#endif

        /*Append pre-loader boot time to kernel command line*/
        pl_t = g_boot_arg->boot_time;
        sprintf(cmdline, "%s%s%d", cmdline, " bootprof.pl_t=", pl_t);
        /*Append lk boot time to kernel command line*/
        lk_t = ((unsigned int)get_timer(boot_time));
        sprintf(cmdline, "%s%s%d", cmdline, " bootprof.lk_t=", lk_t);

#ifdef LK_PROFILING
        dprintf(CRITICAL,"[PROFILE] ------- boot_time takes %d ms -------- \n", lk_t);
#endif
    }
    /*Append pre-loader boot reason to kernel command line*/
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
    if (g_boot_reason_change) {
        boot_reason = 4;
    }
    else
#endif
    {
        boot_reason = g_boot_arg->boot_reason;
    }
    sprintf(cmdline, "%s%s%d", cmdline, " boot_reason=", boot_reason);

    /* Append androidboot.serialno=xxxxyyyyzzzz in cmdline */
    sprintf(cmdline, "%s%s%s", cmdline, " androidboot.serialno=", sn_buf);
    sprintf(cmdline, "%s%s%s", cmdline, " androidboot.bootreason=", g_boot_reason[boot_reason]);

    extern unsigned int *target_commandline_force_gpt(char *cmd)__attribute__((weak));  
    if(target_commandline_force_gpt)                                                    
    {                                                                            
        target_commandline_force_gpt(cmdline);                                                                                      
    }                                                                            

    check_hibernation(cmdline);

    ptr = (char *)target_atag_commmandline((unsigned *)buf, cmdline);
    ret = fdt_setprop(fdt, offset, "atag,cmdline", buf, ptr - buf);
    if (ret) return FALSE;

    ret = fdt_setprop_string(fdt, offset, "bootargs", cmdline);
    if (ret) return FALSE;

    ret = fdt_pack(fdt);
    if (ret) return FALSE;

    dprintf(CRITICAL,"booting linux @ %p, ramdisk @ %p (%d)\n",
            kernel, ramdisk, ramdisk_size);

    if(strcmp(&buf[FDT_BUFF_SIZE], FDT_BUFF_PATTERN) != 0)
    {
        dprintf(CRITICAL,"ERROR: fdt buff overflow\n");
        return FALSE;
    }

    enter_critical_section();
    /* do any platform specific cleanup before kernel entry */
    platform_uninit();
#ifdef HAVE_CACHE_PL310
    l2_disable();
#endif

    arch_disable_cache(UCACHE);
    arch_disable_mmu();


#ifdef MTK_KERNEL_POWER_OFF_CHARGING
	/*Prevent the system jumps to Kernel if we unplugged Charger/USB before*/
	if(kernel_charging_boot() == -1)
	{
		dprintf(CRITICAL,"[%s] Unplugged Usb/Charger in Kernel Charging Mode Before Jumping to Kernel, Power Off\n", __func__);
#ifndef NO_POWER_OFF
		mt6575_power_off();
#endif

	}
	if(kernel_charging_boot() == 1)
	{
		if(pmic_detect_powerkey())
		{
			dprintf(CRITICAL,"[%s] PowerKey Pressed in Kernel Charging Mode Before Jumping to Kernel, Reboot Os\n", __func__);
			//mt65xx_backlight_off();
			//mt_disp_power(0);
			mtk_arch_reset(1);
		}
	}
#endif
    dprintf(CRITICAL,"DRAM Rank :%d\n", g_nr_bank);
    for(i = 0; i < g_nr_bank; i++) {
#ifndef MTK_LM_MODE
        dprintf(CRITICAL,"DRAM Rank[%d] Start = 0x%x, Size = 0x%x\n", i, (unsigned int)bi_dram[i].start, (unsigned int)bi_dram[i].size);
#else
        dprintf(CRITICAL,"DRAM Rank[%d] Start = 0x%llx, Size = 0x%llx\n", i, bi_dram[i].start, bi_dram[i].size);
#endif
    }
    dprintf(CRITICAL,"cmdline: %s\n", cmdline);
    dprintf(CRITICAL,"lk boot time = %d ms\n", lk_t);
    dprintf(CRITICAL,"lk boot mode = %d\n", g_boot_mode);
    dprintf(CRITICAL,"lk boot reason = %s\n", g_boot_reason[boot_reason]);
    dprintf(CRITICAL,"lk finished --> jump to linux kernel %s\n\n", g_is_64bit_kernel ? "64Bit" : "32Bit");

    if(g_is_64bit_kernel)
    {
        lk_jump64((u32)entry, (u32)tags, 0);
        while(1);
    }
    else
    {
        entry(0, machtype, tags);
    }
    return 0;
}



#endif // DEVICE_TREE_SUPPORT

void boot_linux(void *kernel, unsigned *tags,
                char *cmdline, unsigned machtype,
                void *ramdisk, unsigned ramdisk_size)
{
    int i;
    unsigned *ptr = tags;
    void (*entry)(unsigned,unsigned,unsigned*) = kernel;
    unsigned int lk_t = 0;
    unsigned int pl_t = 0;
    unsigned int boot_reason = 0;

#ifdef DEVICE_TREE_SUPPORT
    boot_linux_fdt((void *)kernel, (unsigned *)tags,
                   (char *)cmdline, machtype,
                   (void *)ramdisk, ramdisk_size);

    while(1) ;
#endif

	/* CORE */
    *ptr++ = 2;
    *ptr++ = 0x54410001;

    ptr = target_atag_boot(ptr);
    ptr = target_atag_mem(ptr);
	
#if 0//#ifndef MTK_EMMC_SUPPORT
    if(target_atag_partition_data)
	{
		ptr = target_atag_partition_data(ptr);
	}
	
	if(target_atag_nand_data)
	{
		ptr = target_atag_nand_data(ptr);
	}
#endif
    //some platform might not have this function, use weak reference for
    extern unsigned *target_atag_dfo(unsigned *ptr)__attribute__((weak));
    if(target_atag_dfo)
    {
        ptr = target_atag_dfo(ptr);
    }

    if(g_boot_mode == META_BOOT || g_boot_mode == ADVMETA_BOOT || g_boot_mode == ATE_FACTORY_BOOT || g_boot_mode == FACTORY_BOOT)
    {
        ptr = target_atag_meta(ptr);
#ifdef MTK_MULTI_INIT_SUPPORT
        sprintf(cmdline, "%s%s", cmdline, " rdinit=sbin/multi_init");
#endif
        unsigned int meta_com_id = g_boot_arg->meta_com_id;
		if ((meta_com_id & 0x0001) != 0)
		{
		    sprintf(cmdline, "%s%s", cmdline, " androidboot.usbconfig=1");
		}
		else
		{
		    sprintf(cmdline, "%s%s", cmdline, " androidboot.usbconfig=0");
		}
		if(g_boot_mode == META_BOOT)
		{
		    if((meta_com_id & 0x0002) != 0)
		    {
		        sprintf(cmdline, "%s%s", cmdline, " androidboot.mblogenable=0");
		    }
		    else
		    {
		        sprintf(cmdline, "%s%s", cmdline, " androidboot.mblogenable=1");
		    }
		}
    }

    ptr = target_atag_devinfo_data(ptr);

    /*Append pre-loader boot time to kernel command line*/
    pl_t = g_boot_arg->boot_time;
    sprintf(cmdline, "%s%s%d", cmdline, " bootprof.pl_t=", pl_t);

    /*Append lk boot time to kernel command line*/
    lk_t = ((unsigned int)get_timer(boot_time));
    sprintf(cmdline, "%s%s%d", cmdline, " bootprof.lk_t=", lk_t);
#ifdef LK_PROFILING
    printf("[PROFILE] ------- boot_time takes %d ms -------- \n", lk_t);
#endif
    if (!has_set_p2u) {
#ifdef USER_BUILD
        sprintf(cmdline,"%s%s",cmdline," printk.disable_uart=1");
#else
        sprintf(cmdline,"%s%s",cmdline," printk.disable_uart=0 ddebug_query=\"file *mediatek* +p ; file *gpu* =_\"");
#endif
    }
	/*Append pre-loader boot reason to kernel command line*/
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
    if (g_boot_reason_change) {
        boot_reason = 4;
    }
    else
#endif
	{
		boot_reason = g_boot_arg->boot_reason;
	}
    sprintf(cmdline, "%s%s%d", cmdline, " boot_reason=", boot_reason);

    /* Append androidboot.serialno=xxxxyyyyzzzz in cmdline */
    sprintf(cmdline, "%s%s%s", cmdline, " androidboot.serialno=", sn_buf);
    sprintf(cmdline, "%s%s%s", cmdline, " androidboot.bootreason=", g_boot_reason[boot_reason]);

    check_hibernation(cmdline);
    ptr = target_atag_commmandline(ptr, cmdline);
    ptr = target_atag_initrd(ptr,(unsigned long) ramdisk, ramdisk_size);
    ptr = target_atag_videolfb(ptr);

    extern unsigned int *target_atag_mdinfo(unsigned *ptr)__attribute__((weak));
    if(target_atag_mdinfo)
    {
        ptr = target_atag_mdinfo(ptr);
    }
    else
    {
        dprintf(CRITICAL,"DFO_MODEN_INFO Only support in MT6582/MT6592\n");
    }

	/* END */
    *ptr++ = 0;
    *ptr++ = 0;

#if 0
    dprintf(CRITICAL,"atag start:0x%08X, end:0x%08X\n", tags, ptr);
    for(unsigned int*scan=tags, i=0; scan!=ptr; scan++, i++)
    {
        dprintf(CRITICAL,"0x%08X%c", *scan, (i%4==0)?'\n':'\t');
    }
    dprintf(CRITICAL,"\n");
#endif

    dprintf(CRITICAL,"booting linux @ %p, ramdisk @ %p (%d)\n",
            kernel, ramdisk, ramdisk_size);

    enter_critical_section();
    /* do any platform specific cleanup before kernel entry */
    platform_uninit();
#ifdef HAVE_CACHE_PL310
    l2_disable();
#endif

    arch_disable_cache(UCACHE);
    arch_disable_mmu();
    arch_uninit();

#ifdef MTK_KERNEL_POWER_OFF_CHARGING
	/*Prevent the system jumps to Kernel if we unplugged Charger/USB before*/
	if(kernel_charging_boot() == -1)
	{
		dprintf(CRITICAL,"[%s] Unplugged Usb/Charger in Kernel Charging Mode Before Jumping to Kernel, Power Off\n", __func__);
#ifndef NO_POWER_OFF
		mt6575_power_off();
#endif

	}
	if(kernel_charging_boot() == 1)
	{
		if(pmic_detect_powerkey())
		{
			dprintf(CRITICAL,"[%s] PowerKey Pressed in Kernel Charging Mode Before Jumping to Kernel, Reboot Os\n", __func__);
			//mt65xx_backlight_off();
			//mt_disp_power(0);
			mtk_arch_reset(1);
		}
	}
#endif
    dprintf(CRITICAL,"DRAM Rank :%d\n", g_nr_bank);
    for(i = 0; i < g_nr_bank; i++) {
#ifndef MTK_LM_MODE
        dprintf(CRITICAL,"DRAM Rank[%d] Start = 0x%x, Size = 0x%x\n", i, (unsigned int)bi_dram[i].start, (unsigned int)bi_dram[i].size);
#else
        dprintf(CRITICAL,"DRAM Rank[%d] Start = 0x%llx, Size = 0x%llx\n", i, (unsigned int)bi_dram[i].start, (unsigned int)bi_dram[i].size);
#endif
    }
    dprintf(CRITICAL,"cmdline: %s\n", cmdline);
    dprintf(CRITICAL,"lk boot time = %d ms\n", lk_t);
    dprintf(CRITICAL,"lk boot mode = %d\n", g_boot_mode);
    dprintf(CRITICAL,"lk boot reason = %s\n", g_boot_reason[boot_reason]);
    dprintf(CRITICAL,"lk finished --> jump to linux kernel\n\n");
    entry(0, machtype, tags);
}

int boot_linux_from_storage(void)
{
    int ret=0;
    char *commanline = g_CMDLINE;
    int strlen=0;
    unsigned int kimg_load_addr;

#ifdef LK_PROFILING
    unsigned int time_load_recovery=0;
    unsigned int time_load_bootimg=0;
    unsigned int time_load_factory=0;
    time_load_recovery = get_timer(0);
    time_load_bootimg = get_timer(0);
#endif

#if 1

    switch(g_boot_mode)
    {
    case NORMAL_BOOT:
    case META_BOOT:
    case ADVMETA_BOOT:
    case SW_REBOOT:
    case ALARM_BOOT:
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
    case KERNEL_POWER_OFF_CHARGING_BOOT:
    case LOW_POWER_OFF_CHARGING_BOOT:
#endif
#if defined(CFG_NAND_BOOT)
        strlen += sprintf(commandline, "%s%s%x%s%x",
                          commandline, NAND_MANF_CMDLINE, nand_flash_man_code, NAND_DEV_CMDLINE, nand_flash_dev_id);
#endif
#ifdef MTK_GPT_SCHEME_SUPPORT
        ret = mboot_android_load_bootimg_hdr("boot", CFG_BOOTIMG_LOAD_ADDR);
#else
        ret = mboot_android_load_bootimg_hdr(PART_BOOTIMG, CFG_BOOTIMG_LOAD_ADDR);
#endif
        if (ret < 0) {
            msg_header_error("Android Boot Image");
        }

        if(g_is_64bit_kernel)
        {
            kimg_load_addr = (unsigned int)target_get_scratch_address();
        }
        else
        {
            kimg_load_addr = (g_boot_hdr!=NULL) ? g_boot_hdr->kernel_addr : CFG_BOOTIMG_LOAD_ADDR;
        }

#ifdef MTK_GPT_SCHEME_SUPPORT
        ret = mboot_android_load_bootimg("boot", kimg_load_addr);
#else
        ret = mboot_android_load_bootimg(PART_BOOTIMG, kimg_load_addr);
#endif

        if (ret < 0) {
            msg_img_error("Android Boot Image");
        }
#ifdef LK_PROFILING
        dprintf(CRITICAL,"[PROFILE] ------- load boot.img takes %d ms -------- \n", (int)get_timer(time_load_bootimg));
#endif
        break;

    case RECOVERY_BOOT:
#ifdef MTK_GPT_SCHEME_SUPPORT
        ret = mboot_android_load_recoveryimg_hdr("recovery", CFG_BOOTIMG_LOAD_ADDR);
#else
        ret = mboot_android_load_recoveryimg_hdr(PART_RECOVERY, CFG_BOOTIMG_LOAD_ADDR);
#endif
        if (ret < 0) {
            msg_header_error("Android Recovery Image");
        }

        if(g_is_64bit_kernel)
        {
            kimg_load_addr =  (unsigned int)target_get_scratch_address();
        }
        else
        {
            kimg_load_addr = (g_boot_hdr!=NULL) ? g_boot_hdr->kernel_addr : CFG_BOOTIMG_LOAD_ADDR;
        }

#ifdef MTK_GPT_SCHEME_SUPPORT
        ret = mboot_android_load_recoveryimg("recovery", kimg_load_addr);
#else
        ret = mboot_android_load_recoveryimg(PART_RECOVERY, kimg_load_addr);
#endif
        if (ret < 0) {
            msg_img_error("Android Recovery Image");
        }
#ifdef LK_PROFILING
        dprintf(CRITICAL,"[PROFILE] ------- load recovery.img takes %d ms -------- \n", (int)get_timer(time_load_recovery));
#endif
        break;

    case FACTORY_BOOT:
    case ATE_FACTORY_BOOT:
#if defined(CFG_NAND_BOOT)
        strlen += sprintf(commandline, "%s%s%x%s%x",
                          commandline, NAND_MANF_CMDLINE, nand_flash_man_code, NAND_DEV_CMDLINE, nand_flash_dev_id);
#endif
        ret = mboot_android_load_factoryimg_hdr(CFG_FACTORY_NAME, CFG_BOOTIMG_LOAD_ADDR);
        if (ret < 0) {
            dprintf(CRITICAL,"factory image doesn't exist in SD card\n");

#ifdef MTK_GPT_SCHEME_SUPPORT
            ret = mboot_android_load_bootimg_hdr("boot", CFG_BOOTIMG_LOAD_ADDR);
#else
            ret = mboot_android_load_bootimg_hdr(PART_BOOTIMG, CFG_BOOTIMG_LOAD_ADDR);
#endif
            if (ret < 0) {
                msg_header_error("Android Boot Image");
            }


            if(g_is_64bit_kernel)
            {
                kimg_load_addr =  (unsigned int)target_get_scratch_address();
            }
            else
            {
                kimg_load_addr = (g_boot_hdr!=NULL) ? g_boot_hdr->kernel_addr : CFG_BOOTIMG_LOAD_ADDR;
            }

#ifdef MTK_GPT_SCHEME_SUPPORT
            ret = mboot_android_load_bootimg("boot", kimg_load_addr);
#else
            ret = mboot_android_load_bootimg(PART_BOOTIMG, kimg_load_addr);
#endif
            if (ret < 0) {
                msg_img_error("Android Boot Image");
	        }
        } else {
            ret = mboot_android_load_factoryimg(CFG_FACTORY_NAME, (g_boot_hdr!=NULL) ? g_boot_hdr->kernel_addr : CFG_BOOTIMG_LOAD_ADDR);
            if (ret < 0) {
                msg_img_error("Android Factory Image");
            }
        }
#ifdef LK_PROFILING
        dprintf(CRITICAL,"[PROFILE] ------- load factory.img takes %d ms -------- \n", (int)get_timer(time_load_factory));
#endif
        break;

    case FASTBOOT:
    case DOWNLOAD_BOOT:
    case UNKNOWN_BOOT:
        break;

    }

    if (g_rimg_sz == 0)
        g_rimg_sz = g_boot_hdr->ramdisk_size;

    /* relocate rootfs (ignore rootfs header) */
    memcpy((g_boot_hdr!=NULL) ? (char *)g_boot_hdr->ramdisk_addr : (char *)CFG_RAMDISK_LOAD_ADDR, (char *)(g_rmem_off), g_rimg_sz);
    g_rmem_off = (g_boot_hdr!=NULL) ? g_boot_hdr->ramdisk_addr : CFG_RAMDISK_LOAD_ADDR;

#endif

    // 2 weak function for mt6572 memory preserved mode
    platform_mem_preserved_load_img();
    platform_mem_preserved_dump_mem();

    custom_port_in_kernel(g_boot_mode, commanline);

    if(g_boot_hdr != NULL)
        strlen += sprintf(commanline, "%s %s", commanline, g_boot_hdr->cmdline);

#ifndef MACH_FPGA_NO_DISPLAY
//FIXME, Waiting LCM Driver owner to fix it
    strlen += sprintf(commanline, "%s lcm=%1d-%s", commanline, DISP_IsLcmFound(), mt_disp_get_lcm_id());
    strlen += sprintf(commanline, "%s fps=%1d", commanline, mt_disp_get_lcd_time());
    strlen += sprintf(commanline, "%s vram=%1d", commanline, DISP_GetVRamSize());
#endif

#ifdef SELINUX_STATUS
#if SELINUX_STATUS == 1
    sprintf(commanline, "%s androidboot.selinux=disabled", commanline);
#elif SELINUX_STATUS == 2
    sprintf(commanline, "%s androidboot.selinux=permissive", commanline);
#endif
#endif

    if(g_boot_hdr != NULL) {
        boot_linux((void *)g_boot_hdr->kernel_addr, (unsigned *)g_boot_hdr->tags_addr,
                   (char *)commanline, board_machtype(), (void *)g_boot_hdr->ramdisk_addr, g_rimg_sz);
    } else {
        boot_linux((void *)CFG_BOOTIMG_LOAD_ADDR, (unsigned *)CFG_BOOTARGS_ADDR,
                   (char *)commanline, board_machtype(), (void *)CFG_RAMDISK_LOAD_ADDR, g_rimg_sz);
    }

    while(1) ;

    return 0;
}

#if defined(CONFIG_MTK_USB_UNIQUE_SERIAL) || (defined(MTK_SECURITY_SW_SUPPORT) && defined(MTK_SEC_FASTBOOT_UNLOCK_SUPPORT))
static char udc_chr[32] = {"ABCDEFGHIJKLMNOPQRSTUVWSYZ456789"};

int get_serial(u64 hwkey, u32 chipid, char ser[SERIALNO_LEN])
{
	u16 hashkey[4];
	u32 idx, ser_idx;
	u32 digit, id;
	u64 tmp = hwkey;

	memset(ser, 0x00, SERIALNO_LEN);

	/* split to 4 key with 16-bit width each */
	tmp = hwkey;
	for (idx = 0; idx < ARRAY_SIZE(hashkey); idx++) {
		hashkey[idx] = (u16)(tmp & 0xffff);
		tmp >>= 16;
	}

	/* hash the key with chip id */
	id = chipid;
	for (idx = 0; idx < ARRAY_SIZE(hashkey); idx++) {
		digit = (id % 10);
		hashkey[idx] = (hashkey[idx] >> digit) | (hashkey[idx] << (16-digit));
		id = (id / 10);
	}

	/* generate serail using hashkey */
	ser_idx = 0;
	for (idx = 0; idx < ARRAY_SIZE(hashkey); idx++) {
		ser[ser_idx++] = (hashkey[idx] & 0x001f);
		ser[ser_idx++] = (hashkey[idx] & 0x00f8) >> 3;
		ser[ser_idx++] = (hashkey[idx] & 0x1f00) >> 8;
		ser[ser_idx++] = (hashkey[idx] & 0xf800) >> 11;
	}
	for (idx = 0; idx < ser_idx; idx++)
		ser[idx] = udc_chr[(int)ser[idx]];
	ser[ser_idx] = 0x00;
	return 0;
}
#endif /* CONFIG_MTK_USB_UNIQUE_SERIAL */

#ifdef SERIAL_NUM_FROM_BARCODE
static inline int read_product_info(char *buf)
{
	int tmp = 0;

	if(!buf) return 0;

	mboot_recovery_load_raw_part("proinfo", buf, SN_BUF_LEN);

	for( ; tmp < SN_BUF_LEN; tmp++) {
		if( (buf[tmp] == 0 || buf[tmp] == 0x20) && tmp > 0) {
			break;
		} else if( !isalpha(buf[tmp]) && !isdigit(buf[tmp]))
			return 0;
	}
	return tmp;
}
#endif

void mt_boot_init(const struct app_descriptor *app)
{
	unsigned usb_init = 0;
	unsigned sz = 0;
        int sec_ret = 0;
#ifdef SERIAL_NUM_FROM_BARCODE
	char tmp[SN_BUF_LEN+1] = {0};
	unsigned ser_len = 0;
#endif
#ifdef CONFIG_MTK_USB_UNIQUE_SERIAL
	u64 key;
	u32 chip_code;
#endif
	char serial_num[SERIALNO_LEN];


#ifdef CONFIG_MTK_USB_UNIQUE_SERIAL
	/* Please enable EFUSE clock in platform.c before reading sn key */

	/* serial string adding */

	key = readl(SERIAL_KEY_HI);
	key = (key << 32) | readl(SERIAL_KEY_LO);
	chip_code = DRV_Reg32(APHW_CODE);

	if (key != 0)
		get_serial(key, chip_code, serial_num);
	else
		memcpy(serial_num, DEFAULT_SERIAL_NUM, SN_BUF_LEN);
	/* copy serial from serial_num to sn_buf */
	memcpy(sn_buf, serial_num, SN_BUF_LEN);
			dprintf(CRITICAL,"serial number %s\n",serial_num);
#else
			memcpy(sn_buf, DEFAULT_SERIAL_NUM, strlen(DEFAULT_SERIAL_NUM));
#endif

#ifdef SERIAL_NUM_FROM_BARCODE
	ser_len = read_product_info(tmp);
	if(ser_len == 0) {
		ser_len = strlen(DEFAULT_SERIAL_NUM);
		strncpy(tmp, DEFAULT_SERIAL_NUM, ser_len);
	}
	memset( sn_buf, 0, sizeof(sn_buf));
	strncpy( sn_buf, tmp, ser_len);
#endif
	sn_buf[SN_BUF_LEN] = '\0';
	surf_udc_device.serialno = sn_buf;



	if (g_boot_mode == FASTBOOT)
		goto fastboot;

#ifdef MTK_SECURITY_SW_SUPPORT
    /* Do not block fastboot if check failed */
    if(0 != sec_boot_check(0))
    {
        dprintf(CRITICAL,"<ASSERT> %s:line %d\n",__FILE__,__LINE__);
        while(1);
    }
#endif

	/* Will not return */
	boot_linux_from_storage();

fastboot:
	target_fastboot_init();
	if(!usb_init)
		/*Hong-Rong: wait for porting*/
		udc_init(&surf_udc_device);

	mt_part_dump();
/*test*/
#if 0

    {
        char buf[2048];
        char buf_t[2048];
        int i;
        part_t *part;
        part_dev_t *dev = mt_part_get_device();
        u64 start_addr;
        bool ret = true;
        memset(buf,0x00,2048);
        memset(buf_t,0x00,2048);

        part = mt_part_get_partition(PART_LOGO);
        start_addr = (u64)part->startblk*BLK_SIZE;
        dprintf(CRITICAL,"---partition test -----%s %llx\n",PART_LOGO,start_addr);


        dev->write(dev,buf,start_addr,2048);
        for(i=0;i<2048;i++){
            buf[i]= i;
        }
        dev->write(dev,buf,start_addr+4,1024);
        dev->read(dev,start_addr+4,buf_t,1024);

        for(i=0;i<1024;i++){
            if(buf[i]!=buf_t[i]){
                dprintf(CRITICAL,"compare error. s=%x,d=%x\n",buf[i],buf_t[i]);
                ret = false;
            }

        }
        if(ret == true){
            dprintf(CRITICAL,"---partition test sucess\n-----");
        }


    }
#endif
/*test*/
	sz = target_get_max_flash_size();
	fastboot_init(target_get_scratch_address(), sz);
	udc_start();

}


APP_START(mt_boot)
.init = mt_boot_init,
    APP_END
