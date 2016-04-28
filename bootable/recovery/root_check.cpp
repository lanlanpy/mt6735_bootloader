/*root_check.c
*/
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "common.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "minzip/SysUtil.h"
#include "roots.h"
#include "ui.h"
#include "screen_ui.h"
#include "device.h"

#include "minzip/Zip.h"
#include "root_check.h"
extern "C" {
    #include "cr32.h"
    #include "md5.h"
}

#define FILENAME_MAX 200
unsigned int key=15;
bool checkResult = true;

static const char *SYSTEM_ROOT = "/system/";

static char* file_to_check[]={ "supersu.apk",
                               "superroot.apk",
                               "superuser.apk",
                               "busybox.apk"};

static char* file_to_pass[]={
                             "recovery-from-boot.p",
                              "install-recovery.sh",
                              "recovery_rootcheck",
                              "build.prop",
                              "S_ANDRO_SFL.ini",
                              "recovery.sig"
                    };


static const char *IMAGE_LOAD_PATH ="/tmp/rootcheck/";
static const char *TEMP_FILE_IN_RAM="/tmp/system_dencrypt";
static const char *TEMP_IMAGE_IN_RAM="/tmp/image_dencrypt";
static const char *CRC_COUNT_TMP="/tmp/crc_count";
static const char *FILE_COUNT_TMP="/tmp/file_count";
static const char *DYW_DOUB_TMP="/tmp/doub_check";
static const char *FILE_NEW_TMP="/tmp/list_new_file";

struct last_check_file{
    int n_newfile; 
    int n_lostfile;
    int n_modifyfile;
    int n_rootfile;
    int file_number_to_check;
    int expect_file_number;
    unsigned int file_count_check;
    unsigned int crc_count_check;
};
static struct last_check_file*check_file_result;

int root_to_check[MAX_ROOT_TO_CHECK]={0};

extern RecoveryUI* ui;

img_name_t img_array[PART_MAX] = { 
#ifdef MTK_ROOT_PRELOADER_CHECK
    {"/dev/preloader", "preloader"},
#endif
    {"/dev/uboot", "uboot"},
    {"/dev/bootimg", "bootimg"},
    {"/dev/recovery", "recoveryimg"},
    {"/dev/logo", "logo"},
};

img_name_t img_array_gpt[PART_MAX] = { 
#ifdef MTK_ROOT_PRELOADER_CHECK
    {"/dev/preloader", "preloader"},
#endif
    {"/dev/block/platform/mtk-msdc.0/by-name/lk", "uboot"},
    {"/dev/block/platform/mtk-msdc.0/by-name/boot", "bootimg"},
    {"/dev/block/platform/mtk-msdc.0/by-name/recovery", "recoveryimg"},
    {"/dev/block/platform/mtk-msdc.0/by-name/logo", "logo"},
};

img_checksum_t computed_checksum[PART_MAX];
img_checksum_t expected_checksum[PART_MAX];

int check_map[MAX_FILES_IN_SYSTEM/INT_BY_BIT+1];
int check_modify[MAX_FILES_IN_SYSTEM/INT_BY_BIT+1];

static bool is_support_gpt_c(void) {
    int fd = open("/dev/block/platform/mtk-msdc.0/by-name/para", O_RDONLY);
    if (fd == -1) {
        return false;
    } else {
        close(fd);
        return true;
    }
}
static void set_bit(int x)
{
    check_map[x>>SHIFT]|= 1<<(x&MASK);
}

static void clear_bit(int x)
{
    check_map[x>>SHIFT]&= ~(1<<(x&MASK));
}

static int test_bit(int x)
{
    return check_map[x>>SHIFT]&(1<<(x&MASK));
}

static void set_bit_m(int x)
{
    check_modify[x>>SHIFT]|= 1<<(x&MASK);
}

static void clear_bit_m(int x)
{
    check_modify[x>>SHIFT]&= ~(1<<(x&MASK));
}

static int test_bit_m(int x)
{
    return check_modify[x>>SHIFT]&(1<<(x&MASK));
}

static int file_crc_check( const char* path, unsigned int* nCS, unsigned char *nMd5)
{
    char buf[4*1024];
    FILE *fp = 0;
    int rRead_count = 0;
    int i = 0;
    *nCS = 0;
    
#ifdef MTK_ROOT_ADVANCE_CHECK
    MD5_CTX md5;
    MD5Init(&md5);
    memset(nMd5, 0, MD5_LENGTH);
#endif
    struct stat st;

    memset(&st,0,sizeof(st));
    if ( path == NULL ){
        LOGE("file_crc_check-> %s is null", path);
        return -1;
    }
    if(lstat(path,&st)<0)
    {
        LOGE("\n %s does not exist,lsta fail", path);
        return 1;
    }
    if(S_ISLNK(st.st_mode))
    {
        printf("%s is a link file,just pass\n", path);
        return 0;    
    }

    fp = fopen(path, "r");
    if( fp == NULL )
    {
        LOGE("\nfile_crc_check->path:%s ,fp is null", path);
        LOGE("\nfopen fail reason is %s",strerror(errno));
        return -1;
    } 
    while(!feof(fp))
    {
        memset(buf, 0x0, sizeof(buf));
        rRead_count = fread(buf, 1, sizeof(buf), fp);
        if( rRead_count <=0 )
            break;
        
#ifdef MTK_ROOT_NORMAL_CHECK        
         *nCS += crc32(*nCS, buf, rRead_count);
#endif

#ifdef MTK_ROOT_ADVANCE_CHECK
         MD5Update(&md5,(unsigned char*)buf, rRead_count);
#endif
    }
    
#ifdef MTK_ROOT_ADVANCE_CHECK
    MD5Final(&md5, nMd5);
#endif
    
    fclose(fp);
    return 0;
}

static int clear_selinux_file(char* path)
{
    int found=0;
    int ret=0;
    FILE *fp_info;
    FILE *fp_new;
    char buf[512];
    char p_name[256];
    unsigned char p_md[MD5_LENGTH*2];
    char *p_cmp_name;
    unsigned int p_size;
    int p_number;
    struct stat statbuf;
    
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
    {
        if(fgets(buf, sizeof(buf), fp_info) != NULL)
        {
            while(fgets(buf, sizeof(buf), fp_info))
            {

                if (sscanf(buf, "%d    %s    %u    %s", &p_number,p_name,&p_size,p_md) == 4)
                {
                  
                    //TODO:path[0] will be '\0' sometimes, and it should be '/'
                    path[0] = '/';
                    if(strstr(p_name,path)!=NULL)
                    {
                        p_cmp_name=strstr(p_name,path);
                        if(strcmp(p_cmp_name,path)==0)
                        {
                            found=1;
                            clear_bit(p_number);
                        }
                    }
                }
            }
            
            if(found==0)
            {
                LOGE("found a new file,filename is %s",path);
                check_file_result->n_newfile+=1;
                
                if(access(FILE_NEW_TMP,0)==-1)
                {
                    int fd_new=creat(FILE_NEW_TMP,0755);
                }
                fp_new=fopen(FILE_NEW_TMP, "a");
                if(fp_new)
                {
                    fprintf(fp_new,"%s\n",path);
                }
                else
                {
                    LOGE("open %s error,error reason is %s\n",FILE_NEW_TMP,strerror(errno));
                    return CHECK_ADD_NEW_FILE;
                }        
                checkResult=false;
                fclose(fp_info);
                fclose(fp_new);
                return CHECK_ADD_NEW_FILE;
            }
        }
    }
    else
    {
        LOGE("open %s error,error reason is %s\n",TEMP_FILE_IN_RAM,strerror(errno));
        return CHECK_NO_KEY;
    }
    fclose(fp_info);
    return CHECK_PASS;
}

static int clear_selinux_dir(char const* path)
{
    int ret=0;
    FILE *fp_info;
    FILE *fp_new;
    char buf[512];
    char p_name[256];
    unsigned char p_md[MD5_LENGTH*2];
    char *p_cmp_name;
    unsigned int p_size;
    int p_number;
    struct stat statbuf;
    
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
    {
        if(fgets(buf, sizeof(buf), fp_info) != NULL)
        {
            while(fgets(buf, sizeof(buf), fp_info))
            {
                if (sscanf(buf, "%d    %s    %u    %s", &p_number,p_name,&p_size,p_md) == 4)
                {
                  
                    //TODO:path[0] will be '\0' sometimes, and it should be '/'
                    //path[0] = '/';
                    if(strstr(p_name,path)!=NULL)
                    {
                        p_cmp_name=strstr(p_name,path);
			LOGE("%s file is selinux protected,just pass\n",p_cmp_name);
                        clear_bit(p_number);
                    }
		    else
		    {
			//LOGE("not found %s in orignal file,please check!",path);
			continue;
		    }
                }
            }
        }
    }
    else
    {
        LOGE("open %s error,error reason is %s\n",TEMP_FILE_IN_RAM,strerror(errno));
        return CHECK_NO_KEY;
    }
    fclose(fp_info);
    return CHECK_PASS;
}



static int check_reall_file(char* path, int nCS, char* nMd5)
{
    int found=0;
    int ret=0;
    FILE *fp_info;
    FILE *fp_new;
    char buf[512];
    char p_name[256];
    unsigned char p_md[MD5_LENGTH*2];
    char *p_cmp_name;
    unsigned int p_size;
    int p_number;
    struct stat statbuf;
    
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
    {
        if(fgets(buf, sizeof(buf), fp_info) != NULL)
        {
            //while(fgets(buf, sizeof(buf), fp_info)&&!found)
            while(fgets(buf, sizeof(buf), fp_info))
            {
                memset(p_md, '0', sizeof(p_md));
                if (sscanf(buf, "%d    %s    %u    %s", &p_number,p_name,&p_size,p_md) == 4)
                {
                    //TODO: can not get correct p_name from sscanf(), so use below instead
                    char *p1 = strchr(buf, '\t');
                    char *p2 = strchr(p1+1, '\t');
                    if(p1&&p2)
                        memcpy(p_name, p1+1, p2-p1-1);
                    
                    //TODO:path[0] will be '\0' sometimes, and it should be '/'
                    path[0] = '/';
                    if(strstr(p_name,path)!=NULL)
                    {
                        p_cmp_name=strstr(p_name,path);
                        if(strcmp(p_cmp_name,path)==0)
                        {
                            found=1;
                            int rettmp = 0;
                            clear_bit(p_number);
#ifdef MTK_ROOT_NORMAL_CHECK                                
                            if(p_size==nCS)
                            {
                                //printf("%s crc check pass\n",path);
                            }
                            else
                            {
                                printf("expected crc is %u\n",p_size);
                                printf("computed crc is %u\n",nCS);
                                printf("%s is modifyed\n",path);
                                //fclose(fp_info);                                        
                                rettmp = CHECK_FILE_NOT_MATCH;
                            }
#endif

#ifdef MTK_ROOT_ADVANCE_CHECK
                            if(p_md[1]=='\0')
                                p_md[1]='0';
                            hextoi_md5(p_md);
                            if(memcmp(nMd5, p_md, MD5_LENGTH)==0)
                            {
                                //printf("%s md5 check pass\n",path);
                            }
                            else
                            {
#if 1
                                int i;
                                for(i=0;i<16;i++)
                                    {
                                        printf("%02x",nMd5[i]);
                                    }
                                for(i=0;i<16;i++)
                                    {
                                        printf("%02x", p_md[i]);
                                    }
#endif
                                LOGE("<<ERROR>>\n");
                                //check_file_result->n_modifyfile+=1;
                                LOGE("Error:%s has been modified md5",path);
                                ret=stat(path,&statbuf);
                                if(ret != 0)
                                {
                                    LOGE("Error:%s is not exist\n",path);
                                }
                                time_t modify=statbuf.st_mtime;
                                ui->Print("on %s\n", ctime(&modify));
                                //fclose(fp_info);
                                //return CHECK_FILE_NOT_MATCH;
                                rettmp = CHECK_FILE_NOT_MATCH;
                            }
#endif
                            if(rettmp){
                                check_file_result->n_modifyfile+=1;
                                clear_bit_m(p_number);
                                fclose(fp_info);
                                return rettmp;
                            }
                        }
                    }
                }
            }
            
            if(found==0)
            {
                LOGE("found a new file,filename is %s",path);
                check_file_result->n_newfile+=1;
                
                if(access(FILE_NEW_TMP,0)==-1)
                {
                    int fd_new=creat(FILE_NEW_TMP,0755);
                }
                fp_new=fopen(FILE_NEW_TMP, "a");
                if(fp_new)
                {
                    fprintf(fp_new,"%s\n",path);
                }
                else
                {
                    LOGE("open %s error,error reason is %s\n",FILE_NEW_TMP,strerror(errno));
                    return CHECK_ADD_NEW_FILE;
                }
                
                checkResult=false;
                fclose(fp_info);
                fclose(fp_new);
                return CHECK_ADD_NEW_FILE;
            }
        }
    }
    else
    {
        LOGE("open %s error,error reason is %s\n",TEMP_FILE_IN_RAM,strerror(errno));
        return CHECK_NO_KEY;
    }
    fclose(fp_info);
    return CHECK_PASS;
}

static bool dir_check( char const*dir)
{
    struct dirent *dp;
    DIR *d_fd;
    unsigned int nCS = 0;
    unsigned char nMd5[MD5_LENGTH];
    char newdir[FILENAME_MAX];
    int find_pass=0;

    if ((d_fd = opendir(dir)) == NULL) {
        if(strcmp(dir,"/system/")==0) {
            LOGE("open system dir fail,please check!\n");
            return false;
	} else {
            LOGE("%s is selinux protected,this dir just pass!\n",dir);
	    if(clear_selinux_dir(dir) != 0) {
                LOGE("clear selinux dir fail\n");
            }
             return false;
        }
    }
    while ((dp = readdir(d_fd)) != NULL) {
        find_pass = 0;
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name,"lost+found")==0)
          continue;

        if (dp->d_type == DT_DIR){
                   memset(newdir, 0, FILENAME_MAX);
                   strcpy(newdir, dir);
                   strcat(newdir, dp->d_name);
                   strcat(newdir, "/");
                   if(dir_check(newdir) == false){
                        //closedir(d_fd);
			continue;
                        //return false;
           }
         }else{     
              int idx = 0;
              int idy = 0;
              memset(newdir, 0, FILENAME_MAX);
              strcpy(newdir, dir);
              strcat(newdir, dp->d_name);
              for(; idx < sizeof(file_to_check)/sizeof(char*); idx++){
                    if(strcmp(dp->d_name, file_to_check[idx]) == 0){
                            root_to_check[idx]=1;
                            ui->Print("Dir_check---found a root File:  %s\n",dp->d_name);
               }
           }

             for(; idy < sizeof(file_to_pass)/sizeof(char*); idy++){
               if(strcmp(dp->d_name, file_to_pass[idy]) == 0){
                               printf("Dir_check---found a file to pass:  %s\n",dp->d_name);
                               find_pass=1;
                               break;
               }
             }
           
            if(find_pass==0)
                {
                    ui->Print("scanning **** %s ****\n",dp->d_name);
                    if(0 == file_crc_check(newdir, &nCS, nMd5)){
                        if (check_reall_file(newdir, nCS, (char*)nMd5)!=0)
                            {
                                LOGE("Error:%s check fail\n",newdir);
                                checkResult = false;
                            }
                            check_file_result->file_number_to_check++;
                    }else if(1 == file_crc_check(newdir, &nCS, nMd5)) {
                        LOGE("%s could be selinux protected\n",newdir);
                        //closedir(d_fd)
                        if (clear_selinux_file(newdir)!=0) {
                                LOGE("Error:%s is a selinux file,clear bit fail\n",newdir);
                                checkResult = false;
                        }
                        check_file_result->file_number_to_check++;
                        continue;
                    } else {
                        LOGE("check %s error\n",newdir);
                        closedir(d_fd);
                        return false;
                    }
                }
            }
    }
     closedir(d_fd);
     return true;
}

static int load_zip_file()
{
    const char *FILE_COUNT_ZIP="file_count";
    const char *CRC_COUNT_ZIP="crc_count";
    const char *DOUBLE_DYW_CHECK="doub_check";
    const char *ZIP_FILE_ROOT="/system/data/recovery_rootcheck";
    //const char *ZIP_FILE_ROOT_TEMP="/system/recovery_rootcheck";
    
    ZipArchive zip;
    //struct stat statbuf;
    int ret;
    int err=1;
    MemMapping map;
    if (sysMapFile(ZIP_FILE_ROOT, &map) != 0) {
        LOGE("failed to map file %s\n", ZIP_FILE_ROOT);
        return INSTALL_CORRUPT;
    }

    //ret=stat(ZIP_FILE_ROOT_TEMP,&statbuf);
    printf("load zip file from %s\n",ZIP_FILE_ROOT);
    err = mzOpenZipArchive(map.addr, map.length, &zip);
    if (err != 0) 
    {
        LOGE("Can't open %s\n(%s)\n", ZIP_FILE_ROOT, err != -1 ? strerror(err) : "bad");
        sysReleaseMap(&map);
        return CHECK_NO_KEY;
    }
    const ZipEntry* file_count = mzFindZipEntry(&zip,FILE_COUNT_ZIP);
    if (file_count== NULL) 
    {
            mzCloseZipArchive(&zip);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    unlink(FILE_COUNT_TMP);
    
    int fd_file = creat(FILE_COUNT_TMP, 0755);
    if (fd_file< 0) 
    {
            mzCloseZipArchive(&zip);
            LOGE("Can't make %s:%s\n", FILE_COUNT_TMP, strerror(errno));
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    bool ok_file= mzExtractZipEntryToFile(&zip, file_count, fd_file);
    close(fd_file);
    
    if (!ok_file) 
    {
            LOGE("Can't copy %s\n", FILE_COUNT_ZIP);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    else
    {
            printf("%s is ok\n", FILE_COUNT_TMP);
    }
    
    const ZipEntry* crc_count =
    mzFindZipEntry(&zip,CRC_COUNT_ZIP);
    if (crc_count== NULL) 
    {
            mzCloseZipArchive(&zip);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    unlink(CRC_COUNT_TMP);
    int fd_crc = creat(CRC_COUNT_TMP, 0755);
    if (fd_crc< 0) 
    {
            mzCloseZipArchive(&zip);
            LOGE("Can't make %s\n", CRC_COUNT_TMP);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    bool ok_crc = mzExtractZipEntryToFile(&zip, crc_count, fd_crc);
    close(fd_crc);
    
    if (!ok_crc) 
    {
            LOGE("Can't copy %s\n", CRC_COUNT_ZIP);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    else
    {
            printf("%s is ok\n", CRC_COUNT_TMP);
    }

    const ZipEntry* dcheck_crc =
    mzFindZipEntry(&zip,DOUBLE_DYW_CHECK);
    if (dcheck_crc== NULL) 
    {
            mzCloseZipArchive(&zip);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    
    unlink(DYW_DOUB_TMP);
    int fd_d = creat(DYW_DOUB_TMP, 0755);
    if (fd_d< 0) 
    {
            mzCloseZipArchive(&zip);
            LOGE("Can't make %s\n", DYW_DOUB_TMP);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    
    bool ok_d = mzExtractZipEntryToFile(&zip, dcheck_crc, fd_d);
    close(fd_d);
    if (!ok_d) 
    {
            LOGE("Can't copy %s\n", DOUBLE_DYW_CHECK);
            sysReleaseMap(&map);
            return CHECK_NO_KEY;
    }
    else
    {
        printf("%s is ok\n", DYW_DOUB_TMP);
    }

    mzCloseZipArchive(&zip);
    sysReleaseMap(&map);
    return 0;
}


static char* decrypt_str(char *source,unsigned int key)
{
    char buf[FILENAME_MAX]={0};
    memset(buf, 0, FILENAME_MAX);
    int i;
    int j=0;
    int len=strlen(source);
    if(len%2 != 0)
    {
        printf("Error,sourcr encrypt filename length is odd");
        return NULL;
    }
    int len2=len/2;
    for(i=0;i<len2;i++)
    {
        char c1=source[j];
        char c2=source[j+1];
        j=j+2;
        c1=c1-65;
        c2=c2-65;
        char b2=c2*16+c1;
        char b1=b2^key;
        buf[i]=b1;
    }
    buf[len2]='\0';
    memset(source,0,len);
    strcpy(source,buf);
    return source;    
}
static int load_image_encrypt_file()
{
    FILE *fp_info;
    FILE *fp_tmp;
    char buf[512];
    char p_name[512];
    char p_img_size[128];
    char *p_cmp_name=NULL;
    char p_crc[256];
    char p_md5[256];
    int p_number;
    int p_file_number;
    fp_info = fopen(CRC_COUNT_TMP, "r");
    fp_tmp = fopen(TEMP_IMAGE_IN_RAM,"w+");
    if(fp_tmp)
        {
    if(fp_info)
        {
                while (fgets(buf, sizeof(buf), fp_info)) {
                    memset(p_md5, 0, sizeof(p_md5));
                    if (sscanf(buf,"%s    %s    %s    %s",p_name,p_img_size,p_crc,p_md5) == 4) {
                            char *p_pname=decrypt_str(p_name,key);
                            char *p_pcrc=decrypt_str(p_crc,key);
                            char *p_pmd5=decrypt_str(p_md5,key);
                            char *p_pimgsize=decrypt_str(p_img_size,key);
                            unsigned long crc;
                            
                            crc = strtoul(p_pcrc, (char **)NULL, 10);
                            
                            unsigned long img_size = strtoul(p_pimgsize, (char **)NULL, 10);
                            //printf("p_pname %s,p_img_size %d,rcrc %u, p_pmd5 %s\n",p_pname,img_size,rcrc, p_pmd5);
                            fprintf(fp_tmp,"%s\t%lu\t%lu\t%s\n",p_pname,img_size, crc, p_pmd5);
                        }
                    } 
                
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return -1;
        }
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return CHECK_NO_KEY;

        }
    fclose(fp_info);
    fclose(fp_tmp);
    return 0;    
}

static int load_system_encrypt_file()
{
    FILE *fp_info;
    FILE *fp_tmp;
    char buf[512];
    char p_name[512];
    char *p_cmp_name=NULL;
    char p_crc[256];
    char p_md5[256];
    int p_number;
    int p_file_number;
    fp_info = fopen(FILE_COUNT_TMP, "r");
    fp_tmp = fopen(TEMP_FILE_IN_RAM,"w+");
    if(fp_tmp)
        {
    if(fp_info)
        {
            if (fgets(buf, sizeof(buf), fp_info) != NULL) {
                if (sscanf(buf, "%d    %s %d", &p_number,p_name,&p_file_number) == 3) {
                    fprintf(fp_tmp,"%d\t%s\t%d\n",p_number,p_name,p_file_number);
                    }
                while (fgets(buf, sizeof(buf), fp_info)) {
                    memset(p_md5, 0, sizeof(p_md5));
                    if (sscanf(buf, "%d    %s %s %s", &p_number,p_name,p_crc,p_md5) == 4) {
                            char *p_pname=decrypt_str(p_name,key);
                            char *p_pcrc=decrypt_str(p_crc,key);
                            char *p_pmd5=decrypt_str(p_md5,key);
                            
                            unsigned long crc;
                            crc = strtoul(p_pcrc, (char **)NULL, 10);
                            //printf("p_pname:%s, crc32:%s %lu %d %d %d\n", p_pname, p_pcrc, crc, sizeof(long), sizeof(long long), sizeof(unsigned int));
                            fprintf(fp_tmp,"%d\t%s\t%lu\t%s\n",p_number,p_pname,crc, p_pmd5);
                        }
                    } 
                }
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return -1;
        }
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return CHECK_NO_KEY;

        }
    fclose(fp_info);
    fclose(fp_tmp);
    return 0;    
}

static bool image_crc_check( char const*dir)
{
    struct dirent *dp;
    DIR *d_fd;
    unsigned int nCS = 0;
    unsigned char nMd5[MD5_LENGTH];
    if ((d_fd = opendir(dir)) == NULL) {
        LOGE("dir_check-<<<< %s not dir\n",dir);
        return false;
    }
    while ((dp = readdir(d_fd)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name,"lost+found")==0)
          continue;

        if (dp->d_type == DT_DIR){
           char newdir[FILENAME_MAX]={0};
           memset(newdir, 0, FILENAME_MAX);
           strcpy(newdir, dir);
           strcat(newdir, dp->d_name);
           strcat(newdir, "/");
           if(dir_check(newdir) == false){
               closedir(d_fd);
               return false;
           }
         }else{     
           char newdir[FILENAME_MAX];
           int idx = 0;
           int idy = 0;
           memset(newdir, 0, FILENAME_MAX);
           strcpy(newdir, dir);
           strcat(newdir, dp->d_name);
               if(0 == file_crc_check(newdir, &nCS, nMd5)){
                   #ifdef MTK_ROOT_PRELOADER_CHECK
                       if(strstr(newdir,"preloader")!=NULL)
                         {
                             computed_checksum[PRELOADER].crc32=nCS;
                        memcpy(computed_checksum[PRELOADER].md5, nMd5, MD5_LENGTH);
                         }
                #endif
                     if(strstr(newdir,"bootimg")!=NULL)
                         {
                             computed_checksum[BOOTIMG].crc32=nCS;
                        memcpy(computed_checksum[BOOTIMG].md5, nMd5, MD5_LENGTH);
                         }
                  if(strstr(newdir,"uboot")!=NULL)
                      {
                          computed_checksum[UBOOT].crc32=nCS;
                        memcpy(computed_checksum[UBOOT].md5, nMd5, MD5_LENGTH);
                      }
                  if(strstr(newdir,"recovery")!=NULL)
                      {
                          computed_checksum[RECOVERYIMG].crc32=nCS;
                        memcpy(computed_checksum[RECOVERYIMG].md5, nMd5, MD5_LENGTH);
                      }
                  if(strstr(newdir,"logo")!=NULL)
                      {
                          computed_checksum[LOGO].crc32=nCS;
                        memcpy(computed_checksum[LOGO].md5, nMd5, MD5_LENGTH);
                      }
                      
               }else{
                     printf("%s function fail\n",__func__);
                  closedir(d_fd);
                  return false;
               }
               
        }
    }
     closedir(d_fd);
     return true;
}
static int list_root_file()
{
    int idx=0;
    for(;idx<MAX_ROOT_TO_CHECK;idx++)
        {
            if(root_to_check[idx]==1)
                {
                    check_file_result->n_rootfile+=1;
                    ui->Print("found a root file,%s\n",file_to_check[idx]);
                }
        }
    return 0;
}

static int list_lost_file(int number)
{
    FILE *fp_info;
    char buf[512];
    char p_name[256];
    char p_md[256];
    int found=0;
    char *p_cmp_name=NULL;
    unsigned int p_size;
    int p_number;
    int idy=0;
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
        {
            if (fgets(buf, sizeof(buf), fp_info) != NULL) {
                while (fgets(buf, sizeof(buf), fp_info)) {
                    if (sscanf(buf, "%d    %s %u    %s", &p_number,p_name,&p_size,p_md) == 4) {
                        if(p_number==number)
                            {
                                p_cmp_name=strstr(p_name,"/system");
                                for(; idy < sizeof(file_to_pass)/sizeof(char*); idy++)
                                {
                                    if(strstr(p_cmp_name, file_to_pass[idy]) != NULL)
                                    {
                                        printf("list lost file---found a file to pass:  %s\n",p_cmp_name);
                                        //checkResult=true;
                                        //break;
                                        return 0;
                                    }
                                }
                                if(p_cmp_name != NULL)
                                {
                                    check_file_result->n_lostfile+=1;
                                    ui->Print("Error:%s is lost\n",p_cmp_name);
                                    checkResult=false;
                                }
                                else
                                {
                                    check_file_result->n_lostfile+=1;
                                    ui->Print("Error:%s is lost\n",p_name);
                                    checkResult=false;
                                }
                                found=1;
                                break;
                            }
                        }
                    }
                if(!found)
                    {
                        LOGE("Error:not found a lost file\n");
                        fclose(fp_info);
                        return -1;
                    }
                
                }
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return -1;
        }
    fclose(fp_info);
    return 0;

}

static int image_copy( const char* path,int loop,const char* tem_name)
{
    unsigned int nCS=0;
    char *temp_file;
    char buf[1024];
    int rRead_count = 0;
    int idx = 0;
    int sum=0;
    if (path == NULL ){
        LOGE("image_copy-> %s is null", path);
        return -1;
    }

    if (ensure_path_mounted(IMAGE_LOAD_PATH) != 0) {
      LOGE("Can't mount %s\n", IMAGE_LOAD_PATH);
      return -1;
    }
    
    if (mkdir(IMAGE_LOAD_PATH, 0700) != 0) {
      if (errno != EEXIST) {
        LOGE("Can't mkdir %s (%s)\n", IMAGE_LOAD_PATH, strerror(errno));
        return -1;
      }
    }
    struct stat st;
    if (stat(IMAGE_LOAD_PATH, &st) != 0) {
      LOGE("failed to stat %s (%s)\n", IMAGE_LOAD_PATH, strerror(errno));
      return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
      LOGE("%s isn't a directory\n", IMAGE_LOAD_PATH);
      return -1;
    }
    if ((st.st_mode & 0777) != 0700) {
      LOGE("%s has perms %o\n", IMAGE_LOAD_PATH, st.st_mode);
      return -1;
    }
    if (st.st_uid != 0) {
      LOGE("%s owned by %lu; not root\n", IMAGE_LOAD_PATH, st.st_uid);
      return -1;
    }
    char copy_path[FILENAME_MAX];
    memset(copy_path, 0, FILENAME_MAX);
    strcpy(copy_path, IMAGE_LOAD_PATH);
    strcat(copy_path, "/temp_");
    strcat(copy_path, tem_name);
    char* buffer = (char*)malloc(1024);
    if (buffer == NULL) {
      LOGE("Failed to allocate buffer\n");
      return -1;
    }
    
    size_t read;
    FILE* fin = fopen(path, "rb");
    if (fin == NULL) {
      LOGE("Failed to open %s (%s)\n", path, strerror(errno));
      return -1;
    }
    FILE* fout = fopen(copy_path, "wb");
    if (fout == NULL) {
      LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
      return -1;
    }
    
    while ((read = fread(buffer, 1, 1024, fin)) > 0) {
      sum+=read;
      if(sum<loop)
      {
          if (fwrite(buffer, 1, read, fout) != read) {
            LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
            return -1;
          }
      }
      else
      {
      int read_end=read+loop-sum;
      if (fwrite(buffer, 1, read_end, fout) != read_end) {
         LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
         return -1;
      }
      break;
      }
    }
    
    free(buffer);
    if (fclose(fout) != 0) {
      LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
      return -1;
    }
    
    if (fclose(fin) != 0) {
      LOGE("Failed to close %s (%s)\n", path, strerror(errno));
      return -1;
    }
    return 0;
}

static int list_new_file()
{
    
    FILE *fp_new;
    struct stat statbuf;
    char buf[256];
    if(access(FILE_NEW_TMP,0)==-1)
    {
        printf("%s is not exist\n",FILE_NEW_TMP);
        return 0;
    }
    fp_new= fopen(FILE_NEW_TMP, "r");
    if(fp_new)
    {
        while (fgets(buf, sizeof(buf), fp_new)) {
        ui->Print("Error:%s is new ",buf);
        int ret=stat(buf,&statbuf);
/*
        if(ret != 0)
        {
            LOGE("Error:%s is not exist\n",buf);
        }
*/
        time_t modify=statbuf.st_mtime;
        ui->Print("it is created on %s\n", ctime(&modify));
        }
    }
    else
        {
            LOGE("open %s error,error reason is %s\n",FILE_NEW_TMP,strerror(errno));
            return -1;
        }

    fclose(fp_new);
    return 0;
}

static bool remove_check_file(const char *file_name)
{
    int  ret = 0;
    ret = unlink(file_name);
    if (ret == 0)
        return true;

    if (ret < 0 && errno == ENOENT)
        return true;
    return false;
}

static bool remove_check_dir(const char *dir_name)
{
    struct dirent *dp;
    DIR *d_fd;
    if ((d_fd = opendir(dir_name)) == NULL) {
        LOGE("dir_check-<<<< %s not dir\n",dir_name);
        return false;
    }
    while ((dp = readdir(d_fd)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name,"lost+found")==0)
          continue;

        if (dp->d_type == DT_DIR){
           char newdir[FILENAME_MAX]={0};
           memset(newdir, 0, FILENAME_MAX);
           strcpy(newdir, dir_name);
           strcat(newdir, dp->d_name);
           strcat(newdir, "/");
           if(remove_check_dir(newdir) == false){
               closedir(d_fd);
               return false;
           }
         }else{     
           char newdir[FILENAME_MAX];
           int idx = 0;
           int idy = 0;
           memset(newdir, 0, FILENAME_MAX);
           strcpy(newdir, dir_name);
           strcat(newdir, dp->d_name);
           const char* to_remove_file;
           to_remove_file=newdir;
           if(!remove_check_file(to_remove_file))
               {
                   LOGE("Error:unlink %s fail\n",to_remove_file);
               }
         }
}
return true;
}

static int get_image_info()
{
    FILE *fp_info;
    char buf[512];
    char p_name[32];
    unsigned int p_size;
    unsigned int p_c;
    unsigned char p_crc[MD5_LENGTH*2];
    memset(expected_checksum, 0, sizeof(expected_checksum));
    
    fp_info = fopen(TEMP_IMAGE_IN_RAM, "r");
    if(fp_info)
        {
                while (fgets(buf, sizeof(buf), fp_info)) {
                    memset(p_crc, 0, sizeof(p_crc));
                    //Z_DEBUG("%d %s\n", __LINE__, buf);
                    if (sscanf(buf, "%s %d %u %s", p_name, &p_size, &p_c, p_crc) == 4) {
                        //Z_DEBUG("%d %s\n", __LINE__, p_crc);
                        hextoi_md5(p_crc);
#ifdef MTK_ROOT_PRELOADER_CHECK
                        if (!strcmp(p_name, "preloader.bin")) {
                            expected_checksum[PRELOADER].size = p_size;
                            expected_checksum[PRELOADER].crc32 = p_c;
                            memcpy(expected_checksum[PRELOADER].md5, p_crc, MD5_LENGTH);
                            }
#endif
                        if (!strcmp(p_name, "lk.bin")) {
                            expected_checksum[UBOOT].size = p_size;
                            expected_checksum[UBOOT].crc32 = p_c;
                            memcpy(expected_checksum[UBOOT].md5, p_crc, MD5_LENGTH);
                            }
                        if (!strcmp(p_name, "boot.img")) {
                            expected_checksum[BOOTIMG].size = p_size;
                            expected_checksum[BOOTIMG].crc32 = p_c;
                            memcpy(expected_checksum[BOOTIMG].md5, p_crc, MD5_LENGTH);
                            }
                        if (!strcmp(p_name, "recovery.img")) {
                            expected_checksum[RECOVERYIMG].size = p_size;
                            expected_checksum[RECOVERYIMG].crc32 = p_c;
                            memcpy(expected_checksum[RECOVERYIMG].md5, p_crc, MD5_LENGTH);
                            }
                        if (!strcmp(p_name, "logo.bin")) {
                            expected_checksum[LOGO].size = p_size;
                            expected_checksum[LOGO].crc32 = p_c;
                            memcpy(expected_checksum[LOGO].md5, p_crc, MD5_LENGTH);
                            }

                        }
                    }
                
        }
    else
        {
            printf("%s function fail,open error reason is %s\n",__func__,strerror(errno));
            return -1;
        }
    fclose(fp_info);
    return 0;
}


static int check_file_number_insystem(int file_number)
{
    FILE *fp_info;
    char buf[512];
    char p_name[128];
    int p_number;
    int p_pnumber;
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
        {
            if (fgets(buf, sizeof(buf), fp_info) != NULL) {
                    if (sscanf(buf, "%d    %s %d", &p_pnumber,p_name, &p_number)== 3) {
                        printf("p_name:%s,p_number : %d\n",p_name,p_number);
                        if (!strcmp(p_name, "file_number_in_system_dayu")) {
                            check_file_result->expect_file_number=p_number;
                            //printf("func is %s,line is %d,p_number is %d,file_number is %d,n_modfyfile is %d,check_file_result->n_newfile is %d\n",__func__,__LINE__,p_number,file_number,check_file_result->n_modifyfile,check_file_result->n_newfile);
                            #if 0
                            if((p_number==file_number)&&(check_file_result->n_lostfile==0)&&(check_file_result->n_newfile==0))
                                {
                                    ui->Print("\nSystem Dir File Number Check Pass");
                                    fclose(fp_info);
                                    return 0;
                                }
                            else
                                {
                                    printf("%s %d p_number:%d file_number:%d check_file_result->n_lostfile:%d check_file_result->n_newfile:%d\n", __func__, __LINE__, p_number, file_number, check_file_result->n_lostfile, check_file_result->n_newfile);
                                    ui->Print("\nSystem Dir File Number Check Fail\n");
                                    fclose(fp_info);
                                    return CHECK_SYSTEM_FILE_NUM_ERR;
                                }
                            #endif
                            }
                        }
                    
                }
        }
    else
        {
            ui->Print("fopen error,error reason is %s\n",strerror(errno));
            return 1;
        }
    fclose(fp_info);
    return 0;
}
static void delete_unneed_file()
{
    if(!remove_check_file(TEMP_FILE_IN_RAM))
    {
        LOGE("unlink temp system crc file error\n");
    }
    
    if(!remove_check_file(TEMP_IMAGE_IN_RAM))
    {
        LOGE("unlink temp image crc file error\n");
    }
    
    if(!remove_check_file(FILE_NEW_TMP))
    {
        LOGE("unlink temp new file error\n");
    }
    
    if(!remove_check_file(FILE_COUNT_TMP))
    {
        LOGE("unlink temp new file error\n");
    }
    
    if(!remove_check_file(DYW_DOUB_TMP))
    {
        LOGE("unlink temp new file error\n");
    }

    if(!remove_check_dir(IMAGE_LOAD_PATH))
    {
        LOGE("unlink temp image dir error\n");
    }

}
static int list_modify_file(int number)
{
    FILE *fp_info;
    struct stat statbuf;
    char buf[512];
    char p_name[256];
    char p_md[256];
    int found=0;
    char *p_cmp_name=NULL;
    unsigned int p_size;
    int p_number;
    fp_info = fopen(TEMP_FILE_IN_RAM, "r");
    if(fp_info)
    {
        if (fgets(buf, sizeof(buf), fp_info) != NULL) 
        {
            while (fgets(buf, sizeof(buf), fp_info)) 
            {
                if (sscanf(buf,"%d    %s    %u    %s", &p_number,p_name,&p_size,p_md) == 4) 
                {
                    if(p_number==number)
                    {
                        p_cmp_name=strstr(p_name,"/system");
                        if(p_cmp_name != NULL)
                        {
                            ui->Print("Error:%s has been modified",p_cmp_name);
                            int ret=stat(p_cmp_name,&statbuf);
                            if(ret != 0)
                            {
                                LOGE("Error:%s is not exist\n",p_cmp_name);
                            }
                            time_t modify=statbuf.st_mtime;
                            ui->Print("on %s\n", ctime(&modify));
                         }
                         else
                         {
                             ui->Print("Error:%s is modifyed\n",p_name);
                         }
                         found=1;
                         break;
                                
                      }
        
                  }
              }
              if(!found)
              {
                  LOGE("Error:not found a lost file\n");
                  fclose(fp_info);
                  return -1;
              }
                
          }
      }
    else
    {
        ui->Print("fopen error,error reason is %s\n",strerror(errno));
        return -1;
    }
    fclose(fp_info);
    return 0;    
}

static int encrypt_file_doub_check()
{
    char buf[512];
    FILE *fp_info;
    unsigned int file_count_crc;
    unsigned int crc_count_crc;
    unsigned int nCS = 0;
    unsigned char nMd5[MD5_LENGTH];
    fp_info = fopen(DYW_DOUB_TMP, "r");
    if(fp_info)
    {
         if(fgets(buf, sizeof(buf), fp_info) != NULL)
        {
            if (sscanf(buf,"%u    %u", &file_count_crc,&crc_count_crc) == 2)
            {
                check_file_result->file_count_check=file_count_crc;
                check_file_result->crc_count_check=crc_count_crc;
            }
            else
            {
                ui->Print("double check file is error\n");
                return CHECK_NO_KEY; 
            }
                        
        }
        else
        {
            ui->Print("double check file is null\n");
            return CHECK_NO_KEY; 
        }
    }
    else
    {
        LOGE("open %s error,error reason is %s\n",DYW_DOUB_TMP,strerror(errno));
        return CHECK_NO_KEY;
    }
    
    if(0 == file_crc_check(FILE_COUNT_TMP, &nCS, nMd5))
    {
        if(nCS!=check_file_result->file_count_check)
        {
            ui->Print("file count double check fail\n");
            return CHECK_NO_KEY; 
        }
    }

    if(0 == file_crc_check(CRC_COUNT_TMP, &nCS, nMd5))
    {
        if(nCS != check_file_result->crc_count_check)
        {
            ui->Print("crc count double check fail\n");
            return CHECK_NO_KEY; 
        }
    }
return 0;
}

int root_check(){
    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);
    ui->Print("Now check begins, please wait.....\n");
    int per,cper;
    
#ifdef MTK_ROOT_NORMAL_CHECK
    printf("use normal check\n");
#endif
    
#ifdef MTK_ROOT_ADVANCE_CHECK
    printf("use advance check\n");
#endif

    check_file_result=(struct last_check_file*)malloc(sizeof(last_check_file));

    if(ensure_path_mounted(SYSTEM_ROOT) != 0)
    {
        ui->Print("--mount System fail \n");
    }
    
    memset(check_file_result,0,sizeof(last_check_file));
    memset(check_map,0xff,sizeof(check_map));
    memset(check_modify,0xff,sizeof(check_modify));

    if(load_zip_file())
    {
        ui->Print("load source zip file fail\n");
        return CHECK_NO_KEY;
    }

    if(load_system_encrypt_file())
    {
        ui->Print("load system encrypt file fail\n");
        return CHECK_NO_KEY;
    }
    
    if(load_image_encrypt_file())
    {
        ui->Print("load  partition encrypt file fail\n");
        return CHECK_NO_KEY;
    }
    
    if(encrypt_file_doub_check())
    {
        ui->Print("encrypt file double check fail\n");
        return CHECK_NO_KEY;
    }
   
    if(false == dir_check(SYSTEM_ROOT))
    {
        checkResult = false;
    }

    check_file_result->file_number_to_check+=1;
    ui->Print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
   
    if (check_file_number_insystem(check_file_result->file_number_to_check)!=0)
    {
        checkResult=false;
    }
    if(list_new_file())
    {
        LOGE("list new file error\n");
    }
    if(list_root_file())
    {
        LOGE("list root file error\n");
    }
   
    for(cper=0;cper<check_file_result->file_number_to_check-1;cper++)
    {
        if(test_bit(cper))
        {
            //checkResult=false;
            list_lost_file(cper);
        }
    }
   
    for(cper=0;cper<check_file_result->file_number_to_check-1;cper++)
    {
           
        if(!test_bit_m(cper))
            {
                checkResult=false;
                list_modify_file(cper);    
            }
    }
    
    if(check_file_result->n_newfile)
    {
        ui->Print("Error:found %d new files\n",check_file_result->n_newfile);
    }
    if(check_file_result->n_lostfile)
    {
        ui->Print("Error:found %d lost files\n",check_file_result->n_lostfile);
    }
    if(check_file_result->n_modifyfile)
    {
        ui->Print("Error:found %d modified files\n",check_file_result->n_modifyfile);

    }
    if(check_file_result->n_rootfile)
    {
        ui->Print("Error:found %d root files\n",check_file_result->n_rootfile);

    }
    if(ensure_path_unmounted(SYSTEM_ROOT) != 0)
    {
        LOGE("root_check function--unmount System fail \n");
    }

    if(get_image_info())
    {
        checkResult=false;
    }
    
    int i = 0;

    if(!is_support_gpt_c())
    {
        printf("this is not gpt version");
        for(i=0;i<PART_MAX;i++)
        {
            if(0 == image_copy(img_array[i].img_devname,expected_checksum[i].size,img_array[i].img_printname))
            {
                printf("copy %s done\n", img_array[i].img_printname);
            }
            else
            {
                printf("copy %s error\n", img_array[i].img_printname);
                checkResult = false;
            }
        }
    }
    else
    {
        printf("this is gpt version");
        for(i=0;i<PART_MAX;i++)
        {
            if(0 == image_copy(img_array_gpt[i].img_devname,expected_checksum[i].size,img_array_gpt[i].img_printname))
            {
                printf("copy %s done\n", img_array_gpt[i].img_printname);
            }
            else
            {
                printf("copy %s error\n", img_array_gpt[i].img_printname);
                checkResult = false;
            }
        }
    }

    if(false == image_crc_check(IMAGE_LOAD_PATH))
    {
        checkResult = false;
        return CHECK_IMAGE_ERR;
    }
    
    for(i=0;i<PART_MAX;i++)
    {
        
#ifdef MTK_ROOT_NORMAL_CHECK
        if((expected_checksum[i].crc32==computed_checksum[i].crc32)&&(computed_checksum[i].crc32 != 0))
        {
            printf("\n%s NORMAL check Pass", img_array[i].img_printname);
        }
        else
        {
            if(i==1)
            {  
                checkResult=false;
                ui->Print("Error:%s NORMAL check Fail\n",img_array[i].img_printname);
                printf("except check sum is %u,compute checksum is %u\n",expected_checksum[i].crc32,computed_checksum[i].crc32);
            }
        }
#endif

#ifdef MTK_ROOT_ADVANCE_CHECK
        if(memcmp(expected_checksum[i].md5, computed_checksum[i].md5, MD5_LENGTH)==0)
        {
            printf("\n%s ADVANCE check Pass", img_array[i].img_printname);
        }
        else
        {
            if(i==1)
            {
                checkResult=false;
                ui->Print("Error:%s ADVANCE check Fail\n", img_array[i].img_printname);
#if 1
                char *nMd5 = (char*)expected_checksum[i].md5;
                char *p_md = (char*)computed_checksum[i].md5;
                int i;
                printf("e:");
                for(i=0;i<16;i++)
                {
                    printf("%02x",nMd5[i]);
                }
                printf("\n");
                printf("c:");
                for(i=0;i<16;i++)
                {
                    printf("%02x", p_md[i]);
                }
                printf("\n");
#endif
            }
        }
#endif
    }
    int m_root_check=0;
    for(;m_root_check<MAX_ROOT_TO_CHECK;m_root_check++)
    {
        root_to_check[m_root_check]=0;
    }
    delete_unneed_file();
    free(check_file_result);
    if(checkResult)
    {
        return CHECK_PASS;
    }
    else
    {
        checkResult=true;
        return CHECK_FAIL;
    }
}

