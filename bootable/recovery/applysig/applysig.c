#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include "mtdutils/mtdutils.h"
#include "common.h"
#include "bootimg.h"

#define PART_NAME_LEN 128
#define MTK_BOOT_SIG_LEN  256

static int partition_info(const char *partition_name, char *part_dev_path, int *storage_type){
  int result = 0;
  if (!strcmp(partition_name, "logo") || !strcmp(partition_name, "preloader") || !strcmp(partition_name, "uboot") || 
        !strcmp(partition_name, "dsp_bl") || !strcmp(partition_name, "bootimg") || !strcmp(partition_name, "boot") || !strcmp(partition_name, "tee1") || 
        !strcmp(partition_name, "recovery")) {

        bool is_gpt = support_gpt();

        if (is_gpt) {
            /* Must be EMMC */
            if (!strcmp(partition_name, "preloader")) {
                strcpy(part_dev_path, PRELOADER_PART);
            } else if (!strcmp(partition_name, "bootimg")) {
                strcpy(part_dev_path, BOOT_PART);
            } else if (!strcmp(partition_name, "logo")) {
                strcpy(part_dev_path, LOGO_PART);
            } else if (!strcmp(partition_name, "uboot")) {
                strcpy(part_dev_path, LK_PART);
            } else if (!strcmp(partition_name, "tee1")) {
                strcpy(part_dev_path, TEE1_PART);
            } else if (!strcmp(partition_name, "recovery")) {
                strcpy(part_dev_path, RECOVERY_PART);
            }
            *storage_type = 2;
        } else {

            FILE *fp = fopen("/proc/dumchar_info", "r");
            if (fp) {
                char buf[512], p_name[32], p_size[32], p_addr[32], p_actname[64];
                int p_type = 0;
                if (fgets(buf, sizeof(buf), fp) == NULL) {
                    fclose(fp);
                    result = -1;
                }
                else{
                  while (fgets(buf, sizeof(buf), fp)) {
                      if (sscanf(buf, "%s %s %s %d %s", p_name, p_size, p_addr, &p_type, p_actname) == 5) {
                          if (!strcmp(p_name, "bmtpool")) {
                              break;
                          }
                          *storage_type = p_type;
                          /* EMMC */
                          if(p_type == 2){
                            snprintf(part_dev_path, PART_NAME_LEN, "/dev/%s", partition_name);
                            break;
                          }
                          /* NAND */
                          else if(p_type == 1){
                            if(!strcmp(partition_name, "boot") || !strcmp(partition_name, "bootimg"))
                              strcpy(part_dev_path, "boot");
                            else
                              strcpy(part_dev_path, partition_name);
                            break;
                          }
                          else
                            result = -2;
                      }
                  }
                  fclose(fp);
                }
            } else {
                result = -1;
            }
        }
    }
    else{
      result = -2;
    }
    return result;
}
static inline int get_boot_img_phy_size(boot_img_hdr *header){
  int kernel_page_cnt;
  int ramdisk_page_cnt;
  int second_page_cnt;
  off64_t partion_end_offset;
  
  kernel_page_cnt = (header->kernel_size+header->page_size-1)/header->page_size;
  ramdisk_page_cnt = (header->ramdisk_size+header->page_size-1)/header->page_size;
  second_page_cnt = (header->second_size+header->page_size-1)/header->page_size;
  /* plus 1 page count for boot image header itself */
  partion_end_offset = (off64_t)(kernel_page_cnt+ramdisk_page_cnt+second_page_cnt+1)*header->page_size;
  
  printf("kernel_page_cnt:%d ramdisk_page_cnt:%d second_page_cnt:%d partion_end_offset:0x%llx\n", 
    kernel_page_cnt, ramdisk_page_cnt, second_page_cnt, partion_end_offset);
  
  return partion_end_offset;
}

int applysignature_buf(char *sigfile_buf, int sigfile_buf_size, const char *partition_name){
  int result = 0;
  char part_dev_path[PART_NAME_LEN];
  char *dev_path;
  int storage_type;
  bool success;
  struct boot_img_hdr hdr;
  int partion_end_offset;
  
  if(0 == partition_info(partition_name, part_dev_path, &storage_type)){
    dev_path = part_dev_path;
    if(2 == storage_type){
      /* EMMC */
      int fd = open(part_dev_path, O_RDWR | O_SYNC);
      if (fd != -1) {
          while(read(fd, &hdr, sizeof(boot_img_hdr)) != sizeof(boot_img_hdr)){
            lseek64(fd, 0, SEEK_SET);
          }
          partion_end_offset = get_boot_img_phy_size(&hdr);
          
          /* move FD pointer to bootimg partition tail */
          lseek64(fd, partion_end_offset, SEEK_SET);
          if (write(fd, sigfile_buf, sigfile_buf_size) == -1) {
              printf("fail to write %s\n", part_dev_path);
              result = -1;
          }
          close(fd);
          sync();
      } else {
          printf("open %s fail\n", part_dev_path);
          result = -1;
      }
    }
    else if(1 == storage_type){
      /* NAND */
      const MtdPartition* mtd;
      MtdReadContext *r_ctx;
      int read = sigfile_buf_size;
      
      mtd_scan_partitions();
      mtd = mtd_find_partition_by_name(part_dev_path);
      if (mtd == NULL) {
          printf("%s: no mtd partition named \"%s\"\n", __func__, part_dev_path);
          result = -1;
          goto done;
      }
      /* Read boot.img header */
      r_ctx = mtd_read_partition(mtd);
      if(mtd_read_data(r_ctx, (char *)&hdr, sizeof(boot_img_hdr)) != sizeof(boot_img_hdr)){
        printf("%s: fail to read %s\n", __func__, part_dev_path);
        result = -1;
        mtd_read_close(r_ctx);
        goto done;
      }
      partion_end_offset = get_boot_img_phy_size(&hdr);
      mtd_read_close(r_ctx);
      
      MtdWriteContext* ctx = mtd_write_partition(mtd);
      if (ctx == NULL) {
          printf("%s: can't write mtd partition \"%s\"\n",
                  __func__, part_dev_path);
          result = -1;
          goto done;
      }
      
      success = true;
      
      int wrote = mtd_write_data_ex(ctx, sigfile_buf, read, partion_end_offset);
      success = success && (wrote == read);
      
      if (!success) {
          printf("mtd_write_data to %s failed: %s\n",
                  part_dev_path, strerror(errno));
      }
      
      if (mtd_erase_blocks(ctx, -1) == (off64_t)-1) {
          printf("%s: error erasing blocks of %s\n", __func__, part_dev_path);
      }
      if (mtd_write_close(ctx) != 0) {
          printf("%s: error closing write of %s\n", __func__, part_dev_path);
      }
      
      printf("%s %s partition\n",
             success ? "wrote" : "failed to write", part_dev_path);
      if(success)
        sync();
      result = success ? 0 : -1;
    }
  }
  else{
    result = -2;
  }
done:
  return result;
}
  
int applysignature(const char *filename, const char *partition_name){
  char *rbuf;
  int result = 0;
  int in_fd;
  struct stat f_buf;
  
  in_fd = open(filename, O_RDONLY);
  fstat(in_fd, &f_buf);
  rbuf = malloc(f_buf.st_size);
  if (in_fd != -1) {
    while(read(in_fd, rbuf, f_buf.st_size) != f_buf.st_size){
      lseek64(in_fd, 0, SEEK_SET);
    }
    result = applysignature_buf(rbuf, f_buf.st_size, partition_name);
  }
  else{
    printf("Fail to open %s, %s", filename, strerror(errno));
    result = -1;
  }
  
  close(in_fd);
  free(rbuf);
  return result;
}
