/*root_check.h for root_check*/

#ifndef __ROOT_CHECK__
#define __ROOT_CHECK__
#define MTK_ROOT_NORMAL_CHECK	1
#define MTK_ROOT_ADVANCE_CHECK 	1
enum
{
    CHECK_PASS,
	CHECK_FAIL,
	CHECK_NO_KEY,
	CHECK_OPEN_FILE_ERR,
	CHECK_MOUNT_ERR,
	CHECK_SYSTEM_FILE_NUM_ERR,
	CHECK_FILE_NOT_MATCH,
	CHECK_LOST_FILE,
	CHECK_ADD_NEW_FILE,
	CHECK_IMAGE_ERR,
};

#define MAX_FILES_IN_SYSTEM 10000
#define INT_BY_BIT 32
#define MASK 0x1f
#define SHIFT 5
#define MAX_ROOT_TO_CHECK 20

#define MD5_LENGTH 16
#define DEVNAME_LENGTH 64
typedef struct{
	unsigned int size; //length of xxx.img
	//union{
	unsigned int crc32;//crc32 checksum
	unsigned char md5[MD5_LENGTH]; //md5 checksum		
	//};
}img_checksum_t;

typedef struct{
	char img_devname[DEVNAME_LENGTH];
	char img_printname[DEVNAME_LENGTH];
}img_name_t;

enum{
#ifdef MTK_ROOT_PRELOADER_CHECK
	PRELOADER,
#endif
	UBOOT,
	BOOTIMG,
	RECOVERYIMG,
	LOGO,
	PART_MAX,
};

#define hextoi(c) (((c)-'a'+1)>0?((c)-'a'+10):((c)-'0'))
static void hextoi_md5(unsigned char p_crc[MD5_LENGTH*2]){
	int i = 0;
	for(;i<MD5_LENGTH;i++)
	{
		p_crc[i]=hextoi(p_crc[i*2])*16 + hextoi(p_crc[i*2+1]);
	}
}

int root_check();

#endif

