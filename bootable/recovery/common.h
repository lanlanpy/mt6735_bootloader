/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RECOVERY_COMMON_H
#define RECOVERY_COMMON_H

#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO: restore ui_print for LOGE
#define LOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define LOGW(...) fprintf(stdout, "W:" __VA_ARGS__)
#define LOGI(...) fprintf(stdout, "I:" __VA_ARGS__)

#if 0
#define LOGV(...) fprintf(stdout, "V:" __VA_ARGS__)
#define LOGD(...) fprintf(stdout, "D:" __VA_ARGS__)
#else
#define LOGV(...) do {} while (0)
#define LOGD(...) do {} while (0)
#endif

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

typedef struct {
    const char* mount_point;  // eg. "/cache".  must live in the root directory.

    const char* fs_type;      // "yaffs2" or "ext4" or "vfat"

    const char* device;       // MTD partition name if fs_type == "yaffs"
                              // block device if fs_type == "ext4" or "vfat"

    const char* device2;      // alternative device to try if fs_type
                              // == "ext4" or "vfat" and mounting
                              // 'device' fails

    long long length;         // (ext4 partition only) when
                              // formatting, size to use for the
                              // partition.  0 or negative number
                              // means to format all but the last
                              // (that much).
} Volume;

//typedef struct fstab_rec Volume;

// fopen a file, mounting volumes and making parent dirs as necessary.
FILE* fopen_path(const char *path, const char *mode);

void ui_print(const char* format, ...);


bool is_support_gpt(void);

#define PRELOADER_PART "/dev/block/mmcblk0boot0"
#define BOOT_PART      "/dev/block/platform/mtk-msdc.0/by-name/boot"
#define CACHE_PART     "/dev/block/platform/mtk-msdc.0/by-name/cache"
#define FAT_PART       "/dev/block/platform/mtk-msdc.0/by-name/intsd"
#define SYSTEM_PART    "/dev/block/platform/mtk-msdc.0/by-name/system"
#define DATA_PART      "/dev/block/platform/mtk-msdc.0/by-name/userdata"
#define MISC_PART      "/dev/block/platform/mtk-msdc.0/by-name/para"
#define RECOVERY_PART  "/dev/block/platform/mtk-msdc.0/by-name/recovery"
#define CUSTOM_PART    "/dev/block/platform/mtk-msdc.0/by-name/custom"
#define VENDOR_PART    "/dev/block/platform/mtk-msdc.0/by-name/vendor"
#define LOGO_PART      "/dev/block/platform/mtk-msdc.0/by-name/logo"
#define LK_PART        "/dev/block/platform/mtk-msdc.0/by-name/lk"
#define TEE1_PART      "/dev/block/platform/mtk-msdc.0/by-name/tee1"
#define TEE2_PART      "/dev/block/platform/mtk-msdc.0/by-name/tee2"
#define PERSIST_PART   "/dev/block/platform/mtk-msdc.0/by-name/persist"
#define NVDATA_PART    "/dev/block/platform/mtk-msdc.0/by-name/nvdata"

static inline bool support_gpt(void) {\
    int fd = open(MISC_PART, O_RDONLY);\
    if (fd == -1) {\
        return false;\
    } else {\
        close(fd);\
        return true;\
    }\
}

#ifdef __cplusplus
}
#endif

#endif  // RECOVERY_COMMON_H
