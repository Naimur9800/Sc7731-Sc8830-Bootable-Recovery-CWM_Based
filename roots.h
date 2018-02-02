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

#ifndef RECOVERY_ROOTS_H_
#define RECOVERY_ROOTS_H_

#include "common.h"

// Load and parse volume data from /etc/ctr.fstab.
void load_volume_table();

// Return the Volume* record for this path (or NULL).
Volume* volume_for_path(const char* path);

// Make sure that the volume 'path' is on is mounted.  Returns 0 on
// success (volume is mounted).
int setup_encrypted_data();
int mount_encrypted_data();
void set_encryption_state(int val);
int is_encrypted_data();

int ensure_path_mounted(const char* path);
int ensure_path_mounted_at_mount_point(const char* path, const char* mount_point);
int rmtree_except(const char* path, const char* except);
// Make sure that the volume 'path' is on is mounted.  Returns 0 on
// success (volume is unmounted);
int ensure_path_unmounted(const char* path);

// Reformat the given volume (must be the mount point only, eg
// "/cache"), no paths permitted.  Attempts to unmount the volume if
// it is mounted.
int format_volume(const char* volume);
int use_migrated_storage();
int get_num_extra_volumes();
char* get_primary_storage_path();
char* get_extra_storage_path();
char* get_usb_storage_path();
char* get_android_secure_path();

int get_num_volumes();

Volume* get_device_volumes();

// format device to custom fstype
int format_device(const char *device, const char *path, const char *fs_type);

// support format MTD, MMC, BML, ext2, ext3 and directory rm -rf like if a path is passed
int format_unknown_device(const char *device, const char* path, const char *fs_type);

int is_data_media();
void setup_data_media();
int is_data_media_volume_path(const char* path);
void preserve_data_media(int val);
int is_data_media_preserved();
int setup_install_mounts();
int encrypted_data_mounted;
int data_is_decrypted;

#endif  // RECOVERY_ROOTS_H_
