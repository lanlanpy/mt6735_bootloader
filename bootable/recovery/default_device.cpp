/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <linux/input.h>

#include "common.h"
#include "device.h"
#include "screen_ui.h"

#if 0 //wschen 2014-09-10
static const char* HEADERS[] = { "Volume up/down to move highlight;",
                                 "enter button to select.",
                                 "",
                                 NULL };
#else
static const char* HEADERS[] = { "Volume up/down to move highlight;",
                                 "enter Power key to select.",
                                 "",
                                 NULL };
#endif

#if 0 //wschen 2012-07-10
static const char* ITEMS[] =  {"reboot system now",
                               "apply update from ADB",
                               "wipe data/factory reset",
                               "wipe cache partition",
                               "reboot to bootloader",
                               "power down",
                               "view recovery logs",
                               NULL };
#else
static const char* ITEMS[] =  {"reboot system now",
                               "apply update from ADB",
                               "apply update from sdcard",
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
                               "apply update from sdcard2",
#endif //SUPPORT_SDCARD2
                               "apply update from cache",
                               "wipe data/factory reset",
                               "wipe cache partition",
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 
                               "backup user data",
                               "restore user data",
#endif
#ifdef ROOT_CHECK
                               "root integrity check",
#endif
                               "reboot to bootloader",
                               "power down",
                               "view recovery logs",
                               NULL };


static const char* FORCE_ITEMS[] =  {"reboot system now",
                                     "apply sdcard:update.zip",
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
                                     "apply sdcard2:update.zip",
#endif //SUPPORT_SDCARD2
                                     NULL };
#endif

class DefaultDevice : public Device {
  public:
    DefaultDevice() :
        ui(new ScreenRecoveryUI) {
    }

    RecoveryUI* GetUI() { return ui; }

    int HandleMenuKey(int key, int visible) {
        if (visible) {
            switch (key) {
              case KEY_DOWN:
              case KEY_VOLUMEDOWN:
                return kHighlightDown;

              case KEY_UP:
              case KEY_VOLUMEUP:
                return kHighlightUp;

              case KEY_ENTER:
              case KEY_POWER:
                return kInvokeItem;
            }
        }

        return kNoAction;
    }

    BuiltinAction InvokeMenuItem(int menu_position) {
        switch (menu_position) {
#if 0 //wschen 2012-07-10
          case 0: return REBOOT;
          case 1: return APPLY_ADB_SIDELOAD;
          case 2: return WIPE_DATA;
          case 3: return WIPE_CACHE;
          case 4: return REBOOT_BOOTLOADER;
          case 5: return SHUTDOWN;
          case 6: return READ_RECOVERY_LASTLOG;
          default: return NO_ACTION;
#else
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
          case 0: return REBOOT;
          case 1: return APPLY_ADB_SIDELOAD;
          case 2: return APPLY_EXT;
          case 3: return APPLY_SDCARD2;
          case 4: return APPLY_CACHE;
          case 5: return WIPE_DATA;
          case 6: return WIPE_CACHE;
#ifdef SUPPORT_DATA_BACKUP_RESTORE
          case 7: return USER_DATA_BACKUP;
          case 8: return USER_DATA_RESTORE;
#ifdef ROOT_CHECK
          case 9: return CHECK_ROOT;
          case 10: return REBOOT_BOOTLOADER;
          case 11: return SHUTDOWN;
          case 12: return READ_RECOVERY_LASTLOG;
#else
          case 9: return REBOOT_BOOTLOADER;
          case 10: return SHUTDOWN;
          case 11: return READ_RECOVERY_LASTLOG;
#endif
#else
#ifdef ROOT_CHECK
          case 7: return CHECK_ROOT;
          case 8: return REBOOT_BOOTLOADER;
          case 9: return SHUTDOWN;
          case 10: return READ_RECOVERY_LASTLOG;
#else
          case 7: return REBOOT_BOOTLOADER;
          case 8: return SHUTDOWN;
          case 9: return READ_RECOVERY_LASTLOG;
#endif
#endif
          default: return NO_ACTION;
#else
          case 0: return REBOOT;
          case 1: return APPLY_ADB_SIDELOAD;
          case 2: return APPLY_EXT;
          case 3: return APPLY_CACHE;
          case 4: return WIPE_DATA;
          case 5: return WIPE_CACHE;
#ifdef SUPPORT_DATA_BACKUP_RESTORE
          case 6: return USER_DATA_BACKUP;
          case 7: return USER_DATA_RESTORE;
#ifdef ROOT_CHECK
          case 8: return CHECK_ROOT;
          case 9: return REBOOT_BOOTLOADER;
          case 10: return SHUTDOWN;
          case 11: return READ_RECOVERY_LASTLOG;
#else
          case 8: return REBOOT_BOOTLOADER;
          case 9: return SHUTDOWN;
          case 10: return READ_RECOVERY_LASTLOG;
#endif
#else
#ifdef ROOT_CHECK
          case 6: return CHECK_ROOT;
          case 7: return REBOOT_BOOTLOADER;
          case 8: return SHUTDOWN;
          case 9: return READ_RECOVERY_LASTLOG;
#else
          case 6: return REBOOT_BOOTLOADER;
          case 7: return SHUTDOWN;
          case 8: return READ_RECOVERY_LASTLOG;
#endif
#endif
          default: return NO_ACTION;
#endif //SUPPORT_SDCARD2
#endif
        }
    }

#if 1 //wschen 2012-07-10
    BuiltinAction InvokeForceMenuItem(int menu_position) {
        switch (menu_position) {
          case 0: return REBOOT;
          case 1: return FORCE_APPLY_SDCARD_SIDELOAD;
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
          case 2: return FORCE_APPLY_SDCARD2_SIDELOAD;
#endif //SUPPORT_SDCARD2
          default: return NO_ACTION;
        }
    }
#endif

    const char* const* GetMenuHeaders() { return HEADERS; }
    const char* const* GetMenuItems() { return ITEMS; }
    const char* const* GetForceMenuItems() { return FORCE_ITEMS; }

  private:
    RecoveryUI* ui;
};

Device* make_device() {
    return new DefaultDevice();
}
