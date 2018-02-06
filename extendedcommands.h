/*
 * Copyright (C) 2014 The CyanogenMod Project
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

#ifndef __EXTENDEDCOMMANDS_H
#define __EXTENDEDCOMMANDS_H


int signature_check_enabled;
int md5_check_enabled;
int script_assert_enabled;
int vibration_enabled;
int ui_get_rainbow_mode;

void write_recovery_version();
void toggle_md5_check();
void toggle_vibration();
void show_power_menu();
int get_vibration_state();

int do_nandroid_backup(const char* backup_name);
int do_nandroid_restore();
void show_nandroid_restore_menu(const char* path);

void show_advanced_menu();
void show_nandroid_menu();
void show_partition_menu();
int __system(const char *command);

void wipe_preflash(int confirm);
void wipe_data(int confirm);
#ifdef USE_ADOPTED_STORAGE
void wipe_adopted(int confirm);
#endif
void wipe_cache(int confirm);
void wipe_dalvik_cache(int confirm);
void wipe_battery_stats(int confirm);
void show_wipe_menu();
void show_carliv_menu();
void toggle_rainbow();
void show_multi_flash_menu();

void format_sdcard(const char* volume);
int has_datadata();
void handle_failure();
void process_volumes();
int extendedcommand_file_exists();
void show_install_update_menu();
int confirm_selection(const char* title, const char* confirm);

int verify_root_and_recovery();

#ifdef RECOVERY_EXTEND_NANDROID_MENU
void extend_nandroid_menu(char** items, int item_count, int max_items);
void handle_nandroid_menu(int item_count, int selected);
#endif

#define RECOVERY_NO_CONFIRM_FILE    "clockworkmod/.no_confirm"
#define RECOVERY_MANY_CONFIRM_FILE  "clockworkmod/.many_confirm"
#define RECOVERY_VERSION_FILE       "clockworkmod/.recovery_version"
#define RECOVERY_LAST_INSTALL_FILE  "clockworkmod/.last_install_path"

#endif  // __EXTENDEDCOMMANDS_H
