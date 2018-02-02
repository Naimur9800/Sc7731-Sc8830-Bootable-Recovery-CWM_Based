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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>

#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "mtdutils/mounts.h"
#include "roots.h"
#include "common.h"
#include "make_ext4fs.h"
#include <fcntl.h>
#include <libgen.h>
#include "flashutils/flashutils.h"
#include "recovery_ui.h"
#include "extendedcommands.h"
#include "libcrecovery/common.h"
#include "cutils/properties.h"

extern struct selabel_handle *sehandle;

static int encryption_state = 0;

int num_volumes;
Volume* device_volumes;

int get_num_volumes() {
    return num_volumes;
}

Volume* get_device_volumes() {
    return device_volumes;
}

static int parse_options(char* options, Volume* volume) {
    char* option;
    while ((option = strtok(options, ","))) {
        options = NULL;

        if (strncmp(option, "length=", 7) == 0) {
            volume->length = strtoll(option+7, NULL, 10);
        } else if (strncmp(option, "fstype2=", 8) == 0) {
            volume->fs_type2 = volume->fs_type;
            volume->fs_type = strdup(option + 8);
        } else if (strncmp(option, "fs_options=", 11) == 0) {
            volume->fs_options = strdup(option + 11);
        } else if (strncmp(option, "fs_options2=", 12) == 0) {
            volume->fs_options2 = strdup(option + 12);
        } else if (strncmp(option, "lun=", 4) == 0) {
            volume->lun = strdup(option + 4);
        } else {
            LOGE("bad option \"%s\"\n", option);
            return -1;
        }
    }
    return 0;
}

void load_volume_table() {
    int alloc = 2;
    device_volumes = malloc(alloc * sizeof(Volume));

    // Insert an entry for /tmp, which is the ramdisk and is always mounted.
    device_volumes[0].mount_point = "/tmp";
    device_volumes[0].fs_type = "ramdisk";
    device_volumes[0].device = NULL;
    device_volumes[0].device2 = NULL;
    device_volumes[0].fs_type2 = NULL;
    device_volumes[0].fs_options = NULL;
    device_volumes[0].fs_options2 = NULL;
    device_volumes[0].lun = NULL;
    device_volumes[0].length = 0;
    num_volumes = 1;

    FILE* fstab = fopen("/etc/ctr.fstab", "r");
    if (fstab == NULL) {
        LOGE("failed to open /etc/ctr.fstab (%s)\n", strerror(errno));
        return;
    }

    char buffer[1024];
    int i;
    while (fgets(buffer, sizeof(buffer)-1, fstab)) {
        for (i = 0; buffer[i] && isspace(buffer[i]); ++i);
        if (buffer[i] == '\0' || buffer[i] == '#') continue;

        char* original = strdup(buffer);

        char* mount_point = strtok(buffer+i, " \t\n");
        char* fs_type = strtok(NULL, " \t\n");
        char* device = strtok(NULL, " \t\n");
        // lines may optionally have a second device, to use if
        // mounting the first one fails.
        char* options = NULL;
        char* device2 = strtok(NULL, " \t\n");
        if (device2) {
            if (device2[0] == '/') {
                options = strtok(NULL, " \t\n");
            } else {
                options = device2;
                device2 = NULL;
            }
        }

        if (mount_point && fs_type && device) {
            while (num_volumes >= alloc) {
                alloc *= 2;
                device_volumes = realloc(device_volumes, alloc*sizeof(Volume));
            }
            device_volumes[num_volumes].mount_point = strdup(mount_point);
            device_volumes[num_volumes].fs_type = strdup(fs_type);
            device_volumes[num_volumes].device = strdup(device);
            device_volumes[num_volumes].device2 =
                device2 ? strdup(device2) : NULL;

            device_volumes[num_volumes].length = 0;

            device_volumes[num_volumes].fs_type2 = NULL;
            device_volumes[num_volumes].fs_options = NULL;
            device_volumes[num_volumes].fs_options2 = NULL;
            device_volumes[num_volumes].lun = NULL;

            if (parse_options(options, device_volumes + num_volumes) != 0) {
                LOGE("skipping malformed recovery.fstab line: %s\n", original);
            } else {
                ++num_volumes;
            }
        } else {
            LOGE("skipping malformed recovery.fstab line: %s\n", original);
        }
        free(original);
    }

    fclose(fstab);

    fprintf(stderr, "recovery filesystem table\n");
    fprintf(stderr, "=========================\n");
    for (i = 0; i < num_volumes; ++i) {
        Volume* v = &device_volumes[i];
        fprintf(stderr, "  %d %s %s %s %s %lld\n", i, v->mount_point, v->fs_type,
               v->device, v->device2, v->length);
    }
    fprintf(stderr,"\n");
}

Volume* volume_for_path(const char* path) {
    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* v = get_device_volumes() + i;
        int len = strlen(v->mount_point);
        if (strncmp(path, v->mount_point, len) == 0 &&
            (path[len] == '\0' || path[len] == '/')) {
            return v;
        }
    }
    return NULL;
}

int is_data_media() {
    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* vol = get_device_volumes() + i;
        if (strcmp(vol->fs_type, "datamedia") == 0)
            return 1;
    }
    return 0;
}

int use_migrated_storage() {
	struct stat s;
	return lstat("/data/media/0", &s) == 0;
}

void setup_data_media() {
    if (!is_data_media())
        return;
        
    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* vol = get_device_volumes() + i;
        if (strcmp(vol->fs_type, "datamedia") == 0) {
            char* path = "/data/media";
            rmdir(vol->mount_point);
            mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            if (use_migrated_storage()) {
                path = "/data/media/0";
		        mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
			}

            LOGI("using %s for %s\n", path, vol->mount_point);
            symlink(path, vol->mount_point);
            return;
        }
    }
}

int is_data_media_volume_path(const char* path) {
    Volume* v = volume_for_path(path);
    // prevent segfault on bad call
    if (v == NULL || v->fs_type == NULL)
        return 0;
    return strcmp(v->fs_type, "datamedia") == 0;
}

static char* primary_storage_path = NULL;
char* get_primary_storage_path() {
	if (primary_storage_path == NULL) {
	    if (volume_for_path("/sdcard") != NULL)
	        primary_storage_path = "/sdcard";
	}   
    return primary_storage_path;
}

int get_num_extra_volumes() {
    int num = 0;
    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* v = get_device_volumes() + i;
        if (strcmp("/internal_sd", v->mount_point) == 0 || strcmp("/external_sd", v->mount_point) == 0)
            num++;
    }
    return num;
}

static char* extra_storage_path = NULL;
char* get_extra_storage_path() {
	int num_extra_volumes = get_num_extra_volumes();
	if (num_extra_volumes == 0)
        return NULL;
	if (extra_storage_path == NULL) {
		if (volume_for_path("/internal_sd") != NULL) {
	        extra_storage_path = "/internal_sd";
		} else if (volume_for_path("/external_sd") != NULL) {
	        extra_storage_path = "/external_sd"; 
		}
	}   
    return extra_storage_path;
}

static char* usb_storage_path = NULL;
char* get_usb_storage_path() {
	if (usb_storage_path == NULL) {
		if (volume_for_path("/usb-otg") != NULL) {
	        usb_storage_path = "/usb-otg";
		} else if (volume_for_path("/usbdisk") != NULL) {
	        usb_storage_path = "/usbdisk"; 
		}
	}   
    return usb_storage_path;
}

static char* android_secure_path = NULL;
char* get_android_secure_path() {
    if (android_secure_path == NULL) {
        android_secure_path = malloc(sizeof("/.android_secure") + strlen(get_primary_storage_path()) + 1);
        sprintf(android_secure_path, "%s/.android_secure", primary_storage_path);
    }
    return android_secure_path;
}

int try_mount(const char* device, const char* mount_point, const char* fs_type, const char* fs_options) {
    if (device == NULL || mount_point == NULL || fs_type == NULL)
        return -1;
    int ret = 0;
    if (fs_options == NULL) {
        ret = mount(device, mount_point, fs_type,
                       MS_NOATIME | MS_NODEV | MS_NODIRATIME, "");
    }
    else {
        char mount_cmd[PATH_MAX];
        sprintf(mount_cmd, "mount -t %s -o%s %s %s", fs_type, fs_options, device, mount_point);
        ret = __system(mount_cmd);
    }
    if (ret == 0)
        return 0;
    //LOGW("failed to mount %s (%s)\n", device, strerror(errno));
    return ret;
}

static int exec_cmd(const char* path, char* const argv[]) {
    int status;
    pid_t child;
    if ((child = vfork()) == 0) {
        execv(path, argv);
        _exit(-1);
    }
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        //LOGE("%s failed with status %d\n", path, WEXITSTATUS(status));
    }
    return WEXITSTATUS(status);
}

int ensure_path_mounted(const char* path) {
    return ensure_path_mounted_at_mount_point(path, NULL);
}

int ensure_path_mounted_at_mount_point(const char* path, const char* mount_point) {

    if (is_data_media_volume_path(path)) {
        if (ui_should_log_stdout()) {
            if (use_migrated_storage())
			    LOGI("using /data/media/0 for %s.\n", path);
		    else LOGI("using /data/media for %s.\n", path);
        }
        int ret;
	    if (!is_encrypted_data() && 0 != (ret = ensure_path_mounted("/data")))
            return ret;
        if (!is_encrypted_data()) setup_data_media();
        return 0;
    }
    
    Volume* v = volume_for_path(path);
    if (v == NULL) {
		LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 0;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    if (mount_point == NULL)
        mount_point = v->mount_point;

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(mount_point);
    if (mv) {
        // volume is already mounted
        return 0;
    }

    mkdir(mount_point, 0755);  // in case it doesn't already exist

    if (strcmp(v->fs_type, "yaffs2") == 0) {
        // mount an MTD partition as a YAFFS2 filesystem.
        mtd_scan_partitions();
        const MtdPartition* partition;
        partition = mtd_find_partition_by_name(v->device);
        if (partition == NULL) {
            LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                 v->device, mount_point);
            return -1;
        }
        return mtd_mount_partition(partition, mount_point, v->fs_type, 0);
    } else if (strcmp(v->fs_type, "ext4") == 0 ||
               strcmp(v->fs_type, "ext3") == 0 ||
               strcmp(v->fs_type, "f2fs") == 0 ||
               strcmp(v->fs_type, "rfs") == 0 ||
               strcmp(v->fs_type, "vfat") == 0) {
        if ((result = try_mount(v->device, mount_point, v->fs_type, v->fs_options)) == 0)
            return 0;
        if ((result = try_mount(v->device, mount_point, v->fs_type2, v->fs_options2)) == 0)
            return 0;
        if ((result = try_mount(v->device2, mount_point, v->fs_type2, v->fs_options2)) == 0)
            return 0;
        return result;
    } else {        
        char mount_cmd[PATH_MAX];
        if (strcmp(v->mount_point, mount_point) != 0)
            sprintf(mount_cmd, "mount %s %s", v->device, mount_point);
        else
            sprintf(mount_cmd, "mount %s", v->mount_point);   
		if ((result = __system(mount_cmd)) != 0) {
            if (strcmp(v->fs_type, "auto") == 0) {
                struct stat s;               
                if (stat("/sbin/mount.exfat", &s) == 0) {
					const char *exfat_path = "/sbin/mount.exfat";
					const char* const exfat_argv[] = {"mount.exfat", "-o big_writes,default_permissions,max_read=131072,max_write=131072", v->device, mount_point, NULL};					
					result = exec_cmd(exfat_path, (char* const*)exfat_argv);
                }               
                if (result != 0 && stat("/sbin/mount.ntfs", &s) == 0) {
					sprintf(mount_cmd, "/sbin/mount.ntfs -o rw,umask=0 %s %s", v->device, mount_point);
	                result = __system(mount_cmd);
                }                               
            }
        }        
        return result;
    }

    return -1;
}

int ensure_path_unmounted(const char* path) {

    if (is_data_media_volume_path(path)) {
        if (is_data_media_preserved()) {
	        return 0;
	    } else if (is_encrypted_data()) {
            return 0;
		} else {
            return ensure_path_unmounted("/data");
		}
    }
    
    if (strstr(path, "/data") == path && is_encrypted_data()) {
        return 0;
    }
    
    if (strstr(path, "/data") == path && is_data_media() && is_data_media_preserved()) {
        return 0;
    }

    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }

    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted; you can't unmount it.
        return -1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        // volume is already unmounted
        return 0;
    }

    return unmount_mounted_volume(mv);
}

int rmtree_except(const char* path, const char* except)
{
    char pathbuf[PATH_MAX];
    int rc = 0;
    DIR* dp = opendir(path);
    if (dp == NULL) {
        return -1;
    }
    struct dirent* de;
    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (except && !strcmp(de->d_name, except))
            continue;
        struct stat st;
        snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, de->d_name);
        rc = lstat(pathbuf, &st);
        if (rc != 0) {
            LOGE("Failed to stat %s\n", pathbuf);
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            rc = rmtree_except(pathbuf, NULL);
            if (rc != 0)
                break;
            rc = rmdir(pathbuf);
        }
        else {
            rc = unlink(pathbuf);
        }
        if (rc != 0) {
            LOGI("Failed to remove %s: %s\n", pathbuf, strerror(errno));
            break;
        }
    }
    closedir(dp);
    return rc;
}

int format_volume(const char* volume) {
	if (is_data_media_volume_path(volume)) {
        return format_unknown_device(NULL, volume, NULL);
    }
    
    Volume* v = volume_for_path(volume);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(volume, "/sd-ext") != 0)
            LOGE("unknown volume '%s'\n", volume);
        return -1;
    }
    // silent failure to format non existing sd-ext when defined in recovery.fstab
    if (strcmp(volume, "/sd-ext") == 0) {
        struct stat s;
        if (0 != stat(v->device, &s)) {
            LOGI("Skipping format of sd-ext\n");
            return -1;
        }
    }

    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", volume);
        return -1;
    }
    if (strcmp(v->mount_point, volume) != 0) {
        return format_unknown_device(NULL, volume, NULL);
    }

    // check to see if /data is being formatted, and if it is /data/media
    // Note: the /sdcard check is redundant probably, just being safe.
    if (strcmp(volume, "/data") == 0 && is_data_media() && is_data_media_preserved()) {
        return format_unknown_device(NULL, volume, NULL);
    }

    if (ensure_path_unmounted(volume) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(v->fs_type, "yaffs2") == 0 || strcmp(v->fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(v->device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", v->device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", v->device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", v->device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n", v->device);
            return -1;
        }
        return 0;
    }

	int result;
	ssize_t length = 0;
	if (v->length != 0) length = v->length;
    if (strcmp(v->fs_type, "ext4") == 0) {
        result = make_ext4fs(v->device, length, volume, sehandle);
        if (result != 0) {
            LOGE("format_volume: make_ext4fs failed on %s\n", v->device);
            return -1;
        }
        return 0;
    }
#ifdef USE_F2FS
    if (strcmp(v->fs_type, "f2fs") == 0) {
		if (length < 0) {
			LOGE("format_volume: negative length (%zd) not supported on %s\n", length, v->fs_type);
			return -1;
		}
		char *num_sectors;
		if (asprintf(&num_sectors, "%zd", length / 512) <= 0) {
			LOGE("format_volume: failed to create %s command for %s\n", v->fs_type, v->device);
			return -1;
		}
		const char *f2fs_path = "/sbin/mkfs.f2fs";
		const char* const f2fs_argv[] = {"mkfs.f2fs", "-t", "-d1", v->device, num_sectors, NULL};

		result = exec_cmd(f2fs_path, (char* const*)f2fs_argv);
		free(num_sectors);
        if (result != 0) {
            LOGE("format_volume: make f2fs failed on %s\n", v->device);
            return -1;
        }
        return 0;
    }
#endif
#if 0
    LOGE("format_volume: fs_type \"%s\" unsupported\n", v->fs_type);
    return -1;
#endif
    return format_unknown_device(v->device, volume, v->fs_type);
}

// mount /cache and unmount all other partitions before installing zip file
int setup_install_mounts() {
	device_volumes = get_device_volumes();
    if (device_volumes == NULL) {
        LOGE("can't set up install mounts: no fstab loaded\n");
        return -1;
    }

    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* v = get_device_volumes() + i;

        if (strcmp(v->mount_point, "/tmp") == 0 ||
                strcmp(v->mount_point, "/cache") == 0) {
            if (ensure_path_mounted(v->mount_point) != 0) return -1;

        } else if (is_encrypted_data()) {
            if (strcmp(v->mount_point, "/data") != 0 && ensure_path_unmounted(v->mount_point) != 0) return -1;
        } else {
            if (ensure_path_unmounted(v->mount_point) != 0) return -1;
        }
    }
    return 0;
}

static int data_media_preserved_state = 1;
void preserve_data_media(int val) {
    data_media_preserved_state = val;
}

int is_data_media_preserved() {
    return data_media_preserved_state;
}

int format_device(const char *device, const char *path, const char *fs_type) {
	
    if (is_data_media_volume_path(path)) {
        return format_unknown_device(NULL, path, NULL);
    }    

    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") != 0)
            LOGE("unknown volume '%s'\n", path);
        return -1;
    }
    
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", path);
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
            return -1;
        }
        return 0;
    }
    
    if (strcmp(path, "/data") == 0 && is_data_media() && is_data_media_preserved()) {
        return format_unknown_device(NULL, path, NULL);
    }

    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) - 1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n", device);
            return -1;
        }
        return 0;
    }
    
	int result;
	ssize_t length = 0;
	if (v->length != 0) length = v->length;
    if (strcmp(fs_type, "ext4") == 0) {
        result = make_ext4fs(device, length, v->mount_point, sehandle);
        if (result != 0) {
            LOGE("format_volume: make_ext4fs failed on %s\n", device);
            return -1;
        }
        return 0;
    }
#ifdef USE_F2FS
    if (strcmp(v->fs_type, "f2fs") == 0) {
        if (length < 0) {
			LOGE("format_volume: negative length (%zd) not supported on %s\n", length, v->fs_type);
			return -1;
		}
		char *num_sectors;
		if (asprintf(&num_sectors, "%zd", length / 512) <= 0) {
			LOGE("format_volume: failed to create %s command for %s\n", v->fs_type, v->device);
			return -1;
		}
		const char *f2fs_path = "/sbin/mkfs.f2fs";
		const char* const f2fs_argv[] = {"mkfs.f2fs", "-t", "-d1", v->device, num_sectors, NULL};

		result = exec_cmd(f2fs_path, (char* const*)f2fs_argv);
		free(num_sectors);
        if (result != 0) {
            LOGE("format_volume: make f2fs failed on %s\n", v->device);
            return -1;
        }
        return 0;
    }
#endif
    return format_unknown_device(device, path, fs_type);
}

extern int format_ext2_device(const char *device);
extern int format_ext3_device(const char *device);

int format_unknown_device(const char *device, const char* path, const char *fs_type) {
    LOGI("Formatting unknown device.\n");

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(fs_type, device);

    if (0 == strcmp(path, "/sd-ext")) {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->device, &st)) {
            LOGI("No app2sd partition found. Skipping format of /sd-ext.\n");
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
            LOGI("Formatting ext3 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
            LOGI("Formatting ext2 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext2_device(device);
        }
    }

	int rc;    
    if (strcmp(path, "/data") == 0 && is_data_media() && is_data_media_preserved()) {
            // Preserve .layout_version to avoid "nesting bug"
            LOGI("Preserving layout version\n");
            unsigned char layout_buf[256];
            ssize_t layout_buflen = -1;
            int fd;
            fd = open("/data/.layout_version", O_RDONLY);
            if (fd != -1) {
                layout_buflen = read(fd, layout_buf, sizeof(layout_buf));
                close(fd);
            }

			rc = rmtree_except("/data", "media");
			
            // Restore .layout_version
            if (layout_buflen > 0) {
                LOGI("Restoring layout version\n");
                fd = open("/data/.layout_version", O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (fd != -1) {
                    write(fd, layout_buf, layout_buflen);
                    close(fd);
                }
            }
            return rc;       
    } else {
        rc = rmtree_except(path, NULL);
        ensure_path_unmounted(path);
		return rc;
    }

    ensure_path_unmounted(path);
    return 0;
}

/*******************************/
/* Decrypt data handling code  */
/*      by carliv@xda for      */
/*    Carliv Touch Recovery    */
/*******************************/
/* 
 * Based on this post from xda by Lekensteyn: http://forum.xda-developers.com/showpost.php?s=2153acd5f8ca815af740d79c2d670d5d&p=47807114&postcount=2 
 */

void set_encryption_state(int val) {
    encryption_state = val;
}

int is_encrypted_data() {
    return encryption_state;
}

#ifdef BOARD_INCLUDE_CRYPTO

int mount_encrypted_data() {
	char tmp[PATH_MAX];
	sprintf(tmp, "mount /dev/block/dm-0 /data");
	sleep(1);
	return __system(tmp);
}
	
#define DEFAULT_HEX_PASSWORD "64656661756c745f70617373776f7264"
#define DEFAULT_PASSWORD "default_password"
int setup_encrypted_data() {
	char crypto_state[PROPERTY_VALUE_MAX];
	char crypto_passwd[PROPERTY_VALUE_MAX];
	char cmd[PATH_MAX];
	int ret = -1;	
	
	if (!data_is_decrypted) {
		property_get("ro.crypto.state", crypto_state, "error");
		if (strcmp(crypto_state, "error") == 0) {
				property_set("ro.crypto.state", "encrypted");
				sleep(1);
		}
	
		property_get("ro.ctr.crypto.passwd", crypto_passwd, "error");
		if (strcmp(crypto_passwd, "error") == 0) {
				sprintf(cmd, "vdc cryptfs checkpw %s", DEFAULT_PASSWORD);
				sleep(1);
				if (0 != __system(cmd)) {
					sprintf(cmd, "vdc cryptfs checkpw %s", DEFAULT_HEX_PASSWORD);
					sleep(1);
				}			
		} else {
			sprintf(cmd, "vdc cryptfs checkpw %s", crypto_passwd);
			sleep(1);
		}

		if (0 == __system(cmd)) {
			ret = mount_encrypted_data();		
		}
	} else {
		ret = mount_encrypted_data();
	}
		
	if (ret == 0) {
		ui_print("[*] Data successfuly decrypted and mounted!\n");
		if (!data_is_decrypted) { 
			data_is_decrypted = 1; 
		}
		encrypted_data_mounted = 1;
		return 0;
	}
	
	ui_print("[!] Data couldn't be decrypted and mounted. Please restart recovery from Power Menu.\n");
	return 1;	
}

#endif

/*******************************/
