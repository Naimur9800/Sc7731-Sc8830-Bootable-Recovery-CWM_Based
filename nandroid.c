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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "libcrecovery/common.h"

#include "bootloader.h"
#include "common.h"
#include "install.h"
#include "minuictr/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include <sys/vfs.h>
#include "cutils/android_reboot.h"
#include "cutils/properties.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mtdutils/mounts.h"

#include "flashutils/flashutils.h"

typedef void (*file_event_callback)(const char* filename);
typedef int (*nandroid_backup_handler)(const char* backup_path, const char* backup_file_image, int callback);
typedef int (*nandroid_restore_handler)(const char* backup_file_image, const char* backup_path, int callback);

static int nandroid_backup_bitfield = 0;
static unsigned int nandroid_files_total = 0;
static unsigned int nandroid_files_count = 0;
static int nandroid_canceled = 0;

// time using gettimeofday()
// to get time in usec, we call timenow_usec() which will link here if clock_gettime fails
static long long gettime_usec() {
    struct timeval now;
    long long useconds;
    gettimeofday(&now, NULL);
    useconds = (long long)(now.tv_sec) * 1000000LL;
    useconds += (long long)now.tv_usec;
    return useconds;
}

// use clock_gettime for elapsed time
// this is nsec precise + less prone to issues for elapsed time
// unsigned integers cannot be negative (overflow): set error return code to 0 (1.1.1970 00:00)
static unsigned long long gettime_nsec() {
    struct timespec ts;
    static int err = 0;

    if (err) return 0;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        LOGI("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        ++err;
        return 0;
    }

    unsigned long long nseconds = (unsigned long long)(ts.tv_sec) * 1000000000ULL;
    nseconds += (unsigned long long)(ts.tv_nsec);
    return nseconds;
}

static long long timenow_msec() {
    // first try using clock_gettime
    unsigned long long nseconds;
    nseconds = gettime_nsec();
    if (nseconds == 0) {
        // LOGI("dropping to gettimeofday()\n");
        return (gettime_usec() / 1000LL);
    }

    return (long long)(nseconds / 1000000ULL);
}

static long long interval_passed_t_timer = 0;
int is_time_interval_passed(long long msec_interval) {
    long long t = timenow_msec();
    if (msec_interval != 0 && t - interval_passed_t_timer < msec_interval)
        return 0;

    interval_passed_t_timer = t;
    return 1;
}

void nandroid_generate_timestamp_path(char* backup_path) {
	time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    if (tmp == NULL) {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        snprintf(backup_path, PATH_MAX, "%s/clockworkmod/backup/CTR-%ld", get_primary_storage_path(), tp.tv_sec);
    } else {
        char str[PATH_MAX];
        strftime(str, PATH_MAX, "%F-%H-%M-%S", tmp);
        snprintf(backup_path, PATH_MAX, "%s/clockworkmod/backup/CTR-%s", get_primary_storage_path(), str);
    }
}

static void ensure_directory(const char* dir) {
    char tmp[PATH_MAX];
    sprintf(tmp, "mkdir -p %s && chmod 777 %s", dir, dir);
    __system(tmp);
}

static int print_and_error(const char* message, int ret) {
    ui_reset_progress();
    if (nandroid_canceled) {
		ui_set_background(BACKGROUND_ICON_CLOCKWORK);
		nandroid_canceled = 0;
	} else {
		ui_set_background(BACKGROUND_ICON_ERROR);
	}
    if (message != NULL)
        LOGE("%s", message); // Assumes message has line termination

    return ret;
}

static void nandroid_callback(const char* filename) {
    if (filename == NULL)
        return;

    char tmp[PATH_MAX];
    ui_set_log_stdout(0);
    strcpy(tmp, filename);
    if (tmp[strlen(tmp) - 1] == '\n')
        tmp[strlen(tmp) - 1] = '\0';
    LOGI("%s\n", tmp);

    if (nandroid_files_total != 0) {
        nandroid_files_count++;
        float progress_decimal = (float)((double)nandroid_files_count /
                                         (double)nandroid_files_total);
        ui_set_progress(progress_decimal);
    }
    ui_set_log_stdout(1);
}

static void compute_directory_stats(const char* directory) {
    char tmp[PATH_MAX];
    char count_text[100];

    // reset file count if we ever return before setting it
    nandroid_files_count = 0;
    nandroid_files_total = 0;

    sprintf(tmp, "find %s | %s wc -l > /tmp/dircount", directory, strcmp(directory, "/data") == 0 && is_data_media() ? "grep -v /data/media |" : "");
    __system(tmp);

    FILE* f = fopen("/tmp/dircount", "r");
    if (f == NULL)
        return;

    if (fgets(count_text, sizeof(count_text), f) == NULL) {
        fclose(f);
        return;
    }

    size_t len = strlen(count_text);
    if (count_text[len - 1] == '\n')
        count_text[len - 1] = '\0';

    fclose(f);
    nandroid_files_total = atoi(count_text);
    ui_reset_progress();
    ui_show_progress(1, 0);
}

void finish_nandroid_job() {
    ui_print("[*] Finalizing, please wait...\n");
    sync();
        ui_set_background(BACKGROUND_ICON_NONE);

    ui_reset_progress();
}

int user_cancel_nandroid(FILE **fp, const char* backup_file_image, int is_backup, int *nand_starts) {
    if (!is_ui_initialized())
        return 0;

    if (*nand_starts) {
        ui_clear_key_queue();
        ui_print("\n[>] Press Power to cancel.\n \n \n");
        *nand_starts = 0;
    }

    int key_event = key_press_event();
    if (key_event != NO_ACTION) {

        // support cancel nandroid job
        if (key_event == SELECT_ITEM) {
            ui_print("\n[!] Really cancel? (press Power)\n");
            is_time_interval_passed(0);
            ui_clear_key_queue();
            while (!is_time_interval_passed(5000)) {
                key_event = key_press_event();
                if (key_event != NO_ACTION)
                    break;
                continue;
            }

            if (key_event != SELECT_ITEM) {
                return 0;
            }

            ui_print("[*] Cancelling, please wait...\n");
            ui_clear_key_queue();
            __pclose(*fp);
            nandroid_canceled = 1;
            if (is_backup) {
                char cmd[PATH_MAX];
                ui_print("[*] Deleting backup...\n");
                sync(); // before deleting backup folder
                sprintf(cmd, "rm -rf '%s'", dirname(backup_file_image));
                __system(cmd);
            }

            finish_nandroid_job();
            if (!is_backup) {
                ui_print("\n[!] Partition was left corrupted after cancel command!\n");
            }

            return 1;
        }
    }

    return 0;
}

static int mkyaffs2image_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd %s ; mkyaffs2image . %s.img ; exit $?", backup_path, backup_file_image);

    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("Unable to execute mkyaffs2image.\n");
        return -1;
    }

	int nand_starts = 1;
    while (fgets(tmp, PATH_MAX, fp) != NULL) {
		if (user_cancel_nandroid(&fp, backup_file_image, 1, &nand_starts))
            return -1;
        tmp[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(tmp);
    }

    return __pclose(fp);
}

static int do_tar_compress(char* command, int callback, const char* backup_file_image) {
    char buf[PATH_MAX];

    set_perf_mode(1);
    FILE *fp = __popen(command, "r");
    if (fp == NULL) {
        ui_print("Unable to execute tar command!\n");
        set_perf_mode(0);
        return -1;
    }

	int nand_starts = 1;
    while (fgets(buf, PATH_MAX, fp) != NULL) {
		if (user_cancel_nandroid(&fp, backup_file_image, 1, &nand_starts)) {
            set_perf_mode(0);
            return -1;
        }
        buf[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(buf);
    }

    set_perf_mode(0);
    return __pclose(fp);
}

static int tar_compress_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; touch %s.tar ; set -o pipefail ; (tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) | split -a 1 -b 1000000000 /proc/self/fd/0 %s.tar.) 2> /proc/self/fd/1 ; exit $?", backup_path, backup_file_image, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path, backup_file_image);

    return do_tar_compress(tmp, callback, backup_file_image);
}

static int tar_gzip_compress_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; touch %s.tar.gz ; set -o pipefail ; (tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) | pigz -c | split -a 1 -b 1000000000 /proc/self/fd/0 %s.tar.gz.) 2> /proc/self/fd/1 ; exit $?", backup_path, backup_file_image, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path, backup_file_image);

    return do_tar_compress(tmp, callback, backup_file_image);
}

static int tar_dump_wrapper(const char* backup_path, const char* backup_file_image, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s); set -o pipefail ; tar -cpv --exclude=data/data/com.google.android.music/files/* %s $(basename %s) 2> /dev/null | cat", backup_path, strcmp(backup_path, "/data") == 0 && is_data_media() ? "--exclude=data/media" : "", backup_path);

    return __system(tmp);
}

static void build_configuration_path(char *path_buf, const char *file) {
    sprintf(path_buf, "%s/%s", get_primary_storage_path(), file);
}

static nandroid_backup_handler default_backup_handler = tar_compress_wrapper;
static char forced_backup_format[5] = "";
void nandroid_force_backup_format(const char* fmt) {
    strcpy(forced_backup_format, fmt);
}

static void refresh_default_backup_handler() {
    char fmt[5];
    if (strlen(forced_backup_format) > 0) {
        strcpy(fmt, forced_backup_format);
    } else {
        char path[PATH_MAX];

        build_configuration_path(path, NANDROID_BACKUP_FORMAT_FILE);
        ensure_path_mounted(path);
        FILE* f = fopen(path, "r");
        if (NULL == f) {
            default_backup_handler = tar_compress_wrapper;
            return;
        }
        fread(fmt, 1, sizeof(fmt), f);
        fclose(f);
    }
    fmt[3] = '\0';
    if (0 == strcmp(fmt, "tgz"))
        default_backup_handler = tar_gzip_compress_wrapper;
    else
        default_backup_handler = tar_compress_wrapper;
}

unsigned int nandroid_get_default_backup_format() {
    refresh_default_backup_handler();
    if (default_backup_handler == tar_gzip_compress_wrapper) {
        return NANDROID_BACKUP_FORMAT_TGZ;
    } else {
        return NANDROID_BACKUP_FORMAT_TAR;
    }
}

static nandroid_backup_handler get_backup_handler(const char *backup_path) {
    Volume *v = volume_for_path(backup_path);
    if (v == NULL) {
        ui_print("Unable to find volume.\n");
        return NULL;
    }
    const MountedVolume *mv = find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        ui_print("Unable to find mounted volume: %s\n", v->mount_point);
        return NULL;
    }

    if (strcmp(backup_path, "/data") == 0 && is_data_media()) {
        return default_backup_handler;
    }

    if (strlen(forced_backup_format) > 0)
        return default_backup_handler;

    // Disable tar backups of yaffs2 by default
    char prefer_tar[PROPERTY_VALUE_MAX];
    property_get("ro.ctr.prefer_tar", prefer_tar, "false");
    if (strcmp("yaffs2", mv->filesystem) == 0 && strcmp("false", prefer_tar) == 0) {
        return mkyaffs2image_wrapper;
    }

    return default_backup_handler;
}

static int nandroid_backup_partition_extended(const char* backup_path, const char* mount_point, int umount_when_finished) {
    int ret = 0;
    char name[PATH_MAX];
    char tmp[PATH_MAX];
    strcpy(name, basename(mount_point));

    struct stat file_info;
    build_configuration_path(tmp, NANDROID_HIDE_PROGRESS_FILE);
    ensure_path_mounted(tmp);
    int callback = stat(tmp, &file_info) != 0;

    ui_print("\n[*] Backing up %s...\n", name);
    if (0 != (ret = ensure_path_mounted(mount_point) != 0)) {
        ui_print("Can't mount %s!\n", mount_point);
        return ret;
    }
    compute_directory_stats(mount_point);
    scan_mounted_volumes();
    Volume *v = volume_for_path(mount_point);
    const MountedVolume *mv = NULL;
    if (v != NULL)
        mv = find_mounted_volume_by_mount_point(v->mount_point);

    if (strcmp(backup_path, "-") == 0)
        sprintf(tmp, "/proc/self/fd/1");
    else if (mv == NULL || mv->filesystem == NULL)
        sprintf(tmp, "%s/%s.auto", backup_path, name);
    else
        sprintf(tmp, "%s/%s.%s", backup_path, name, mv->filesystem);
    nandroid_backup_handler backup_handler = get_backup_handler(mount_point);

    if (backup_handler == NULL) {
        ui_print("[!] Error finding an appropriate backup handler.\n");
        return -2;
    }
    ret = backup_handler(mount_point, tmp, callback);
    if (umount_when_finished) {
        ensure_path_unmounted(mount_point);
    }
    if (0 != ret) {
        ui_print("[!] Error while making a backup image of %s!\n", mount_point);
        return ret;
    }
    ui_print("[*] Backup of %s completed.\n", name);
    return 0;
}

static int nandroid_backup_partition(const char* backup_path, const char* root) {
    Volume *vol = volume_for_path(root);
    // make sure the volume exists before attempting anything...
    if (vol == NULL || vol->fs_type == NULL)
        return 0;

    // see if we need a raw backup (mtd)
    char tmp[PATH_MAX];
    int ret;
    if (strcmp(vol->fs_type, "mtd") == 0 ||
            strcmp(vol->fs_type, "bml") == 0 ||
            strcmp(vol->fs_type, "emmc") == 0) {
        ui_print("\n[*] Backing up %s...\n", root);
        const char* name = basename(root);
        if (strcmp(backup_path, "-") == 0)
            strcpy(tmp, "/proc/self/fd/1");
        else
            sprintf(tmp, "%s/%s.img", backup_path, name);
        //ui_print("[*] Backing up %s image...\n", name);

        if (0 != (ret = backup_raw_partition(vol->fs_type, vol->device, tmp))) {
            ui_print("[!] Error while backing up %s image!\n", name);
            return ret;
        }

        ui_print("[*] Backup of %s image completed.\n", name);
        return 0;
    }

    return nandroid_backup_partition_extended(backup_path, root, 1);
}

int nandroid_backup(const char* backup_path) {
    nandroid_backup_bitfield = 0;
    refresh_default_backup_handler();

    if (ensure_path_mounted(backup_path) != 0) {
        return print_and_error("Can't mount backup path.\n", NANDROID_ERROR_GENERAL);
    }
    ui_print("-- Start the backup to %s.\n", backup_path);

    Volume* volume;
    if (is_data_media_volume_path(backup_path))
        volume = volume_for_path("/data");
    else
        volume = volume_for_path(backup_path);
    if (NULL == volume)
        return print_and_error("Unable to find volume for backup path.\n", NANDROID_ERROR_GENERAL);
        
    int ret;
    struct statfs sfs;
    struct stat s;
    if (NULL != volume) {
        if (0 != (ret = statfs(volume->mount_point, &sfs)))
            return print_and_error("Unable to stat backup path.\n", NANDROID_ERROR_GENERAL);
        uint64_t bavail = sfs.f_bavail;
        uint64_t bsize = sfs.f_bsize;
        uint64_t sdcard_free = bavail * bsize;
        uint64_t sdcard_free_mb = sdcard_free / (uint64_t)(1024 * 1024);
        ui_print("SD Card space free: %"PRIu64" MB\n", sdcard_free_mb);
        if (sdcard_free_mb < 150)
            ui_print("There may not be enough free space to complete backup... continuing...\n");
    }
	
    char tmp[PATH_MAX];
    ensure_directory(backup_path);
    ui_set_background(BACKGROUND_ICON_INSTALLING);

    if (0 != (ret = nandroid_backup_partition(backup_path, "/boot")))
        return print_and_error(NULL, ret);

    Volume *vol = volume_for_path("/wimax");
    if (vol != NULL && 0 == stat(vol->device, &s)) {
        char serialno[PROPERTY_VALUE_MAX];
        ui_print("[*] Backing up WiMAX...\n");
        serialno[0] = 0;
        property_get("ro.serialno", serialno, "");
        sprintf(tmp, "%s/wimax.%s.img", backup_path, serialno);
        ret = backup_raw_partition(vol->fs_type, vol->device, tmp);
        if (0 != ret)
            return print_and_error("Error while dumping WiMAX image!\n", NANDROID_ERROR_GENERAL);
    }

    if (0 != (ret = nandroid_backup_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);
        
	if (volume_for_path("/custpack") != NULL) {
	    if (0 != (ret = nandroid_backup_partition(backup_path, "/custpack")))
	        return print_and_error(NULL, ret);
	}
	
	if (volume_for_path("/cust") != NULL) {
	    if (0 != (ret = nandroid_backup_partition(backup_path, "/cust")))
	        return print_and_error(NULL, ret);
	}

    if (0 != (ret = nandroid_backup_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (has_datadata()) {
        if (0 != (ret = nandroid_backup_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }

    if (0 == stat(get_android_secure_path(), &s)) {
        if (0 != (ret = nandroid_backup_partition_extended(backup_path, get_android_secure_path(), 0)))
            return print_and_error(NULL, ret);
    }

    if (0 != (ret = nandroid_backup_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    vol = volume_for_path("/sd-ext");
    if (vol == NULL || 0 != stat(vol->device, &s)) {
        LOGI("No sd-ext found. Skipping backup of sd-ext.\n");
    } else {
        if (0 != ensure_path_mounted("/sd-ext"))
            LOGI("Could not mount sd-ext. sd-ext backup may not be supported on this device. Skipping backup of sd-ext.\n");
        else if (0 != (ret = nandroid_backup_partition(backup_path, "/sd-ext")))
            return print_and_error(NULL, ret);
    }

    ui_print("Generating md5 sum...\n");
    sprintf(tmp, "nandroid-md5.sh %s", backup_path);
    if (0 != (ret = __system(tmp))) {
        ui_print("Error while generating md5 sum!\n");
        return ret;
    }

    sprintf(tmp, "cp /tmp/recovery.log %s/recovery.log", backup_path);
    __system(tmp);

    char base_dir[PATH_MAX];
    strcpy(base_dir, backup_path);
    char *d = dirname(base_dir);
    strcpy(base_dir, d);
    d = dirname(base_dir);
    strcpy(base_dir, d);

    sprintf(tmp, "chmod -R 777 %s ; chmod -R u+r,u+w,g+r,g+w,o+r,o+w %s ; chmod u+x,g+x,o+x %s/backup", backup_path, base_dir, base_dir);
    __system(tmp);

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nBackup complete!\n");
    return 0;
}

//=========================================/
//=        Nandroid advanced backup       =/
//=             by carliv@xda             =/
//=========================================/

int nandroid_advanced_backup(const char* backup_path, int boot, int system, int data, int cache) {

    refresh_default_backup_handler();

	if (ensure_path_mounted(backup_path) != 0) {
        return print_and_error("Can't mount backup path.\n", NANDROID_ERROR_GENERAL);
    }

    ui_print("-- Start the advanced backup to %s.\n", backup_path);

    Volume* volume = volume_for_path(backup_path);
    if (NULL == volume)
        return print_and_error("Unable to find volume for backup path.\n", NANDROID_ERROR_GENERAL);
    if (is_data_media_volume_path(volume->mount_point))
        volume = volume_for_path("/data");
    int ret;
    struct statfs sfs;
    struct stat s;
    if (NULL != volume) {
        if (0 != (ret = statfs(volume->mount_point, &sfs)))
            return print_and_error("Unable to stat backup path.\n", NANDROID_ERROR_GENERAL);
        uint64_t bavail = sfs.f_bavail;
        uint64_t bsize = sfs.f_bsize;
        uint64_t sdcard_free = bavail * bsize;
        uint64_t sdcard_free_mb = sdcard_free / (uint64_t)(1024 * 1024);
        ui_print("SD Card space free: %"PRIu64" MB\n", sdcard_free_mb);
        if (sdcard_free_mb < 150)
            ui_print("There may not be enough free space to complete backup... continuing...\n");
    }
    char tmp[PATH_MAX];
    ensure_directory(backup_path);
    ui_set_background(BACKGROUND_ICON_INSTALLING);

    if (boot && 0 != (ret = nandroid_backup_partition(backup_path, "/boot")))
        return print_and_error(NULL, ret);

    if (system && 0 != (ret = nandroid_backup_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);
        
	if (volume_for_path("/custpack") != NULL) {
	    if (system && 0 != (ret = nandroid_backup_partition(backup_path, "/custpack")))
        return print_and_error(NULL, ret);
	}
	
	if (volume_for_path("/cust") != NULL) {
	    if (system && 0 != (ret = nandroid_backup_partition(backup_path, "/cust")))
        return print_and_error(NULL, ret);
	}

    if (data && 0 != (ret = nandroid_backup_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (data && has_datadata()) {
        if (0 != (ret = nandroid_backup_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }
    
    if (data && 0 == stat(get_android_secure_path(), &s)) {
        if (0 != (ret = nandroid_backup_partition_extended(backup_path, get_android_secure_path(), 0)))
            return print_and_error(NULL, ret);
    }

    if (cache && 0 != (ret = nandroid_backup_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    ui_print("Generating md5 sum...\n");
    sprintf(tmp, "nandroid-md5.sh %s", backup_path);
    if (0 != (ret = __system(tmp))) {
        ui_print("Error while generating md5 sum!\n");
        return ret;
    }

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nBackup complete!\n");
    return 0;
}

#ifdef BOARD_HAS_MTK_CPU

//=========================================/
//=           MTK special backup          =/
//=             by carliv@xda             =/
//=========================================/

int nandroid_mtk_backup(const char* backup_path, int uboot, int logo, int nvram, int secro) {

    refresh_default_backup_handler();

	if (ensure_path_mounted(backup_path) != 0) {
        return print_and_error("Can't mount backup path.\n", NANDROID_ERROR_GENERAL);
    }

    ui_print("-- Start mtk partitions backup to %s.\n", backup_path);

    Volume* volume = volume_for_path(backup_path);
    if (NULL == volume)
        return print_and_error("Unable to find volume for backup path.\n", NANDROID_ERROR_GENERAL);
    if (is_data_media_volume_path(volume->mount_point))
        volume = volume_for_path("/data");
    int ret;
    struct statfs sfs;
    if (NULL != volume) {
        if (0 != (ret = statfs(volume->mount_point, &sfs)))
            return print_and_error("Unable to stat backup path.\n", NANDROID_ERROR_GENERAL);
        uint64_t bavail = sfs.f_bavail;
        uint64_t bsize = sfs.f_bsize;
        uint64_t sdcard_free = bavail * bsize;
        uint64_t sdcard_free_mb = sdcard_free / (uint64_t)(1024 * 1024);
        ui_print("SD Card space free: %"PRIu64" MB\n", sdcard_free_mb);
        if (sdcard_free_mb < 150)
            ui_print("There may not be enough free space to complete backup... continuing...\n");
    }
    char tmp[PATH_MAX];
    struct stat sn;
    sprintf(tmp, "%s/%s", get_primary_storage_path(), NANDROID_HIDE_PROGRESS_FILE);
    int callback = stat(tmp, &sn) != 0;
    char backup_file_image[PATH_MAX];
	Volume* vol = volume_for_path("/data");
    sprintf(backup_file_image, "%s/nvdata.%s", backup_path, vol->fs_type == NULL ? "auto" : vol->fs_type);
    ensure_directory(backup_path);
    ui_set_background(BACKGROUND_ICON_INSTALLING);

    if (uboot && 0 != (ret = nandroid_backup_partition(backup_path, "/uboot")))
        return print_and_error(NULL, ret);

    if (logo && 0 != (ret = nandroid_backup_partition(backup_path, "/logo")))
        return print_and_error(NULL, ret);

    if (nvram) {
        if (0 != (ret = nandroid_backup_partition(backup_path, "/nvram")))
	        return print_and_error(NULL, ret);
        if (volume_for_path("/nvdata") != NULL) {
		    if (0 != (ret = nandroid_backup_partition(backup_path, "/nvdata")))
	        return print_and_error(NULL, ret);
		} else {
			char nvd[PATH_MAX];
		    sprintf(nvd, "cd / ; touch %s.tar.gz ; set -o pipefail ; (tar cv data/nvram | pigz -c -1 | split -a 1 -b 1000000000 /proc/self/fd/0 %s.tar.gz.) 2> /proc/self/fd/1 ; exit $?", backup_file_image, backup_file_image);
		    if (0 != (ret = do_tar_compress(nvd, callback, backup_file_image))) 
		    return print_and_error(NULL, ret);
	    }
    }

    if (secro && 0 != (ret = nandroid_backup_partition(backup_path, "/secro")))
        return print_and_error(NULL, ret);

    ui_print("Generating md5 sum...\n");
    sprintf(tmp, "nandroid-md5.sh %s", backup_path);
    if (0 != (ret = __system(tmp))) {
        ui_print("Error while generating md5 sum!\n");
        return ret;
    }

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nBackup complete!\n");
    return 0;
}

#endif

static int nandroid_dump(const char* partition) {
    // silence our ui_print statements and other logging
    ui_set_log_stdout(0);

    nandroid_backup_bitfield = 0;
    refresh_default_backup_handler();

    // override our default to be the basic tar dumper
    default_backup_handler = tar_dump_wrapper;

    if (strcmp(partition, "boot") == 0) {
        Volume *vol = volume_for_path("/boot");
        // make sure the volume exists before attempting anything...
        if (vol == NULL || vol->fs_type == NULL)
            return 1;
        char cmd[PATH_MAX];
        sprintf(cmd, "cat %s", vol->device);
        return __system(cmd);
        // return nandroid_backup_partition("-", "/boot");
    }

    if (strcmp(partition, "recovery") == 0) {
        return __system("set -o pipefail ; dump_image recovery /proc/self/fd/1 | cat");
    }

    if (strcmp(partition, "data") == 0) {
        return nandroid_backup_partition("-", "/data");
    }

    if (strcmp(partition, "system") == 0) {
        return nandroid_backup_partition("-", "/system");
    }

    if (strcmp(partition, "custpack") == 0) {
        return nandroid_backup_partition("-", "/custpack");
    }

    if (strcmp(partition, "cust") == 0) {
        return nandroid_backup_partition("-", "/cust");
    }

    return 1;
}

static int unyaffs_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd %s ; unyaffs %s ; exit $?", backup_path, backup_file_image);
    FILE *fp = __popen(tmp, "r");
    if (fp == NULL) {
        ui_print("Unable to execute unyaffs.\n");
        return -1;
    }

	int nand_starts = 1;
    while (fgets(tmp, PATH_MAX, fp) != NULL) {
		if (user_cancel_nandroid(&fp, NULL, 0, &nand_starts))
            return -1;
        tmp[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(tmp);
    }

    return __pclose(fp);
}

static int do_tar_extract(char* command, const char* backup_file_image, const char* backup_path, int callback) {
    char buf[PATH_MAX];

    set_perf_mode(1);
    FILE *fp = __popen(command, "r");
    if (fp == NULL) {
        ui_print("Unable to execute tar command.\n");
        set_perf_mode(0);
        return -1;
    }

	int nand_starts = 1;
    while (fgets(buf, PATH_MAX, fp) != NULL) {
		if (user_cancel_nandroid(&fp, NULL, 0, &nand_starts)) {
            set_perf_mode(0);
            return -1;
        }
        buf[PATH_MAX - 1] = '\0';
        if (callback)
            nandroid_callback(buf);
    }

    set_perf_mode(0);
    return __pclose(fp);
}

static int tar_gzip_extract_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; set -o pipefail ; cat %s* | pigz -d -c | tar -xpv ; exit $?", backup_path, backup_file_image);

    return do_tar_extract(tmp, backup_file_image, backup_path, callback);
}

static int tar_extract_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; set -o pipefail ; cat %s* | tar -xpv ; exit $?", backup_path, backup_file_image);

    return do_tar_extract(tmp, backup_file_image, backup_path, callback);
}

static int tar_undump_wrapper(const char* backup_file_image, const char* backup_path, int callback) {
    char tmp[PATH_MAX];
    sprintf(tmp, "cd $(dirname %s) ; tar -xpv ", backup_path);

    return __system(tmp);
}

static nandroid_restore_handler get_restore_handler(const char *backup_path) {
    Volume *v = volume_for_path(backup_path);
    if (v == NULL) {
        ui_print("Unable to find volume.\n");
        return NULL;
    }
    scan_mounted_volumes();
    const MountedVolume *mv = find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        ui_print("Unable to find mounted volume: %s\n", v->mount_point);
        return NULL;
    }

    if (strcmp(backup_path, "/data") == 0 && is_data_media()) {
        return tar_extract_wrapper;
    }

    // Disable tar backups of yaffs2 by default
    char prefer_tar[PROPERTY_VALUE_MAX];
    property_get("ro.ctr.prefer_tar", prefer_tar, "false");
    if (strcmp("yaffs2", mv->filesystem) == 0 && strcmp("false", prefer_tar) == 0) {
        return unyaffs_wrapper;
    }

    return tar_extract_wrapper;
}

static int nandroid_restore_partition_extended(const char* backup_path, const char* mount_point, int umount_when_finished) {
    int ret = 0;
    char* name = basename(mount_point);

    nandroid_restore_handler restore_handler = NULL;
    const char *filesystems[] = { "yaffs2", "ext2", "ext3", "ext4", "f2fs", "ntfs", "vfat", "exfat", "rfs", "auto", NULL };
    const char* backup_filesystem = NULL;
    Volume *vol = volume_for_path(mount_point);
    const char *device = NULL;
    if (vol != NULL)
        device = vol->device;

    ui_print("\n[*] Restoring %s...\n", mount_point);
    char tmp[PATH_MAX];
    sprintf(tmp, "%s/%s.img", backup_path, name);
    struct stat file_info;
    if (strcmp(backup_path, "-") == 0) {
        if (vol)
            backup_filesystem = vol->fs_type;
        restore_handler = tar_extract_wrapper;
        strcpy(tmp, "/proc/self/fd/0");
    } else if (0 != (ret = stat(tmp, &file_info))) {
        // can't find the backup, it may be the new backup format?
        // iterate through the backup types
        printf("couldn't find default\n");
        const char *filesystem;
        int i = 0;
        while ((filesystem = filesystems[i]) != NULL) {
            sprintf(tmp, "%s/%s.%s.img", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = unyaffs_wrapper;
                break;
            }
            sprintf(tmp, "%s/%s.%s.tar", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = tar_extract_wrapper;
                break;
            }
            sprintf(tmp, "%s/%s.%s.tar.gz", backup_path, name, filesystem);
            if (0 == (ret = stat(tmp, &file_info))) {
                backup_filesystem = filesystem;
                restore_handler = tar_gzip_extract_wrapper;
                break;
            }
            i++;
        }

        if (backup_filesystem == NULL || restore_handler == NULL) {
            ui_print("%s backup file not found. Skipping restore of %s.\n", name, mount_point);
            return 0;
        } else {
            printf("Found new backup image: %s\n", tmp);
        }
    }


    if (vol == NULL || 0 == strcmp(vol->fs_type, "auto"))
        backup_filesystem = NULL;
    else if (0 == strcmp(vol->mount_point, "/data") && is_data_media())
        backup_filesystem = NULL;

    else if (backup_filesystem != NULL && strcmp(vol->fs_type, backup_filesystem) != 0 &&
                (strcmp(vol->fs_type, "ext4") == 0 || strcmp(vol->fs_type, "f2fs") == 0) &&
                (strcmp(backup_filesystem, "ext4") == 0 || strcmp(backup_filesystem, "f2fs") == 0))
        backup_filesystem = NULL;

    ensure_directory(mount_point);

    char path[PATH_MAX];
    build_configuration_path(path, NANDROID_HIDE_PROGRESS_FILE);
    ensure_path_mounted(path);
    int callback = stat(path, &file_info) != 0;

    ui_print("[*] Restoring %s...\n", name);
    if (backup_filesystem == NULL) {
        if (0 != (ret = format_volume(mount_point))) {
            ui_print("Error while formatting %s!\n", mount_point);
            return ret;
        }
    } else if (0 != (ret = format_device(device, mount_point, backup_filesystem))) {
        ui_print("Error while formatting %s!\n", mount_point);
        return ret;
    }

    if (0 != (ret = ensure_path_mounted(mount_point))) {
        ui_print("Can't mount %s!\n", mount_point);
        return ret;
    }

    if (restore_handler == NULL)
        restore_handler = get_restore_handler(mount_point);

    // override restore handler for undump
    if (strcmp(backup_path, "-") == 0) {
        restore_handler = tar_undump_wrapper;
    }

    if (restore_handler == NULL) {
        ui_print("Error finding an appropriate restore handler.\n");
        return -2;
    }

    if (0 != (ret = restore_handler(tmp, mount_point, callback))) {
        ui_print("Error while restoring %s!\n", mount_point);
        return ret;
    }

    if (umount_when_finished) {
        ensure_path_unmounted(mount_point);
    }

    return 0;
}

static int nandroid_restore_partition(const char* backup_path, const char* root) {
    Volume *vol = volume_for_path(root);
    // make sure the volume exists...
    if (vol == NULL || vol->fs_type == NULL)
        return 0;

    // see if we need a raw restore (mtd)
    char tmp[PATH_MAX];
    if (strcmp(vol->fs_type, "mtd") == 0 ||
            strcmp(vol->fs_type, "bml") == 0 ||
            strcmp(vol->fs_type, "emmc") == 0) {
		ui_print("\n[*] Restoring %s...\nUsing raw mode...\n", root);
        int ret;
        const char* name = basename(root);
        
        struct stat file_check;
        if (strcmp(backup_path, "-") == 0)
            strcpy(tmp, backup_path);
        else
            sprintf(tmp, "%s%s.img", backup_path, root);

        if (0 != strcmp(backup_path, "-") && 0 != stat(tmp, &file_check)) {
            ui_print("%s not found. Skipping restore of %s\n", basename(tmp), root);
            return 0;
        }
        
        ui_print("[*] Erasing %s before restore...\n", name);
        if (0 != (ret = format_volume(root))) {
            ui_print("Error while erasing %s image!", name);
            return ret;
        }

        ui_print("[*] Restoring %s image...\n", name);
        if (0 != (ret = restore_raw_partition(vol->fs_type, vol->device, tmp))) {
            ui_print("Error while flashing %s image!", name);
            return ret;
        }
        return 0;
    }
    return nandroid_restore_partition_extended(backup_path, root, 1);
}

int nandroid_restore(const char* backup_path, int restore_boot, int restore_system, int restore_data, int restore_cache, int restore_sdext, int restore_wimax) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    nandroid_files_total = 0;

    if (ensure_path_mounted(backup_path) != 0)
        return print_and_error("Can't mount backup path\n", NANDROID_ERROR_GENERAL);

    char tmp[PATH_MAX];
    if (md5_check_enabled) {
		ui_print("Checking MD5 sums...\n");
	    sprintf(tmp, "cd %s && md5sum -c nandroid.md5", backup_path);
	    if (0 != __system(tmp))
	        return print_and_error("MD5 mismatch!\n", NANDROID_ERROR_GENERAL);
	}	
    
    int ret;
	struct stat st;
	sprintf(tmp, "%s/boot.img", backup_path);
	
	if (restore_boot && (stat(tmp, &st) == 0)) {
	    if (0 != (ret = nandroid_restore_partition(backup_path, "/boot")))
            return print_and_error(NULL, ret);           
    }

    struct stat s;
    Volume *vol = volume_for_path("/wimax");
    if (restore_wimax && vol != NULL && 0 == stat(vol->device, &s)) {
        char serialno[PROPERTY_VALUE_MAX];

        serialno[0] = 0;
        property_get("ro.serialno", serialno, "");
        sprintf(tmp, "%s/wimax.%s.img", backup_path, serialno);

        struct stat st;
        if (0 != stat(tmp, &st)) {
            ui_print("WARNING: WiMAX partition exists, but nandroid\n");
            ui_print("         backup does not contain WiMAX image.\n");
            ui_print("         You should create a new backup to\n");
            ui_print("         protect your WiMAX keys.\n");
        } else {
            ui_print("[*] Erasing WiMAX before restore...\n");
            if (0 != (ret = format_volume("/wimax")))
                return print_and_error("Error while formatting wimax!\n", NANDROID_ERROR_GENERAL);
            ui_print("[*] Restoring WiMAX image...\n");
            if (0 != (ret = restore_raw_partition(vol->fs_type, vol->device, tmp)))
                return print_and_error(NULL, ret);
        }
    }

    if (restore_system && 0 != (ret = nandroid_restore_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);
        
	if (volume_for_path("/custpack") != NULL) {
	    if (restore_system && 0 != (ret = nandroid_restore_partition(backup_path, "/custpack")))
        return print_and_error(NULL, ret);
	}
	
	if (volume_for_path("/cust") != NULL) {
	    if (restore_system && 0 != (ret = nandroid_restore_partition(backup_path, "/cust")))
        return print_and_error(NULL, ret);
	}

    if (restore_data && 0 != (ret = nandroid_restore_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (has_datadata()) {
        if (restore_data && 0 != (ret = nandroid_restore_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }
    
    if (restore_data && 0 == stat(get_android_secure_path(), &s)) {
        if (0 != (ret = nandroid_restore_partition_extended(backup_path, get_android_secure_path(), 0)))
			return print_and_error(NULL, ret);
    }

    if (restore_cache && 0 != (ret = nandroid_restore_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    if (restore_sdext && 0 != (ret = nandroid_restore_partition(backup_path, "/sd-ext")))
        return print_and_error(NULL, ret);

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nRestore complete!\n");
    return 0;
}

//=========================================/
//=       Nandroid advanced restore       =/
//=             by carliv@xda             =/
//=========================================/

int nandroid_advanced_restore(const char* backup_path, int boot, int system, int data, int cache) {

	ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    nandroid_files_total = 0;

	if (ensure_path_mounted(backup_path) != 0)
        return print_and_error("Can't mount backup path\n", NANDROID_ERROR_GENERAL);

    char tmp[PATH_MAX];
    if (md5_check_enabled) {
		ui_print("Checking MD5 sums...\n");
	    sprintf(tmp, "cd %s && md5sum -c nandroid.md5", backup_path);
	    if (0 != __system(tmp))
	        return print_and_error("MD5 mismatch!\n", NANDROID_ERROR_GENERAL);
	}

    ui_print("-- Start the advanced restore from %s.\n", backup_path);

    int ret;
	struct stat stb;
	struct stat s;
	sprintf(tmp, "%s/boot.img", backup_path);

    if (boot && (stat(tmp, &stb) == 0)) {
	    if (0 != (ret = nandroid_restore_partition(backup_path, "/boot")))
            return print_and_error(NULL, ret);           
    }

    if (system && 0 != (ret = nandroid_restore_partition(backup_path, "/system")))
        return print_and_error(NULL, ret);
        
	if (volume_for_path("/custpack") != NULL) {
	    if (system && 0 != (ret = nandroid_restore_partition(backup_path, "/custpack")))
        return print_and_error(NULL, ret);
	}
	
	if (volume_for_path("/cust") != NULL) {
	    if (system && 0 != (ret = nandroid_restore_partition(backup_path, "/cust")))
        return print_and_error(NULL, ret);
	}

    if (data && 0 != (ret = nandroid_restore_partition(backup_path, "/data")))
        return print_and_error(NULL, ret);

    if (data && has_datadata()) {
        if (0 != (ret = nandroid_restore_partition(backup_path, "/datadata")))
            return print_and_error(NULL, ret);
    }
    
    if (data && 0 == stat(get_android_secure_path(), &s)) {
        if (0 != (ret = nandroid_restore_partition_extended(backup_path, get_android_secure_path(), 0)))
			return print_and_error(NULL, ret);
    }

    if (cache && 0 != (ret = nandroid_restore_partition_extended(backup_path, "/cache", 0)))
        return print_and_error(NULL, ret);

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nRestore complete!\n");
    return 0;
}

#ifdef BOARD_HAS_MTK_CPU

//=========================================/
//=           MTK special restore         =/
//=             by carliv@xda             =/
//=========================================/

int nandroid_mtk_restore(const char* backup_path, int uboot, int logo, int nvram, int secro) {

	ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    nandroid_files_total = 0;

	if (ensure_path_mounted(backup_path) != 0)
        return print_and_error("Can't mount backup path\n", NANDROID_ERROR_GENERAL);

    char tmp[PATH_MAX];
    if (md5_check_enabled) {
		ui_print("Checking MD5 sums...\n");
	    sprintf(tmp, "cd %s && md5sum -c nandroid.md5", backup_path);
	    if (0 != __system(tmp))
	        return print_and_error("MD5 mismatch!\n", NANDROID_ERROR_GENERAL);
	}

    ui_print("-- Start mtk partitions restore from %s.\n", backup_path);

	nandroid_restore_handler restore_handler = NULL;
    struct stat sn;
    sprintf(tmp, "%s/%s", get_primary_storage_path(), NANDROID_HIDE_PROGRESS_FILE);
    int callback = stat(tmp, &sn) != 0;
    char backup_file_image[PATH_MAX];
    const char *filesystems[] = { "yaffs2", "ext2", "ext3", "ext4", "f2fs", "ntfs", "vfat", "exfat", "rfs", "auto", NULL };
    const char *filesystem;

    int ret;
	if (uboot && 0 != (ret = nandroid_restore_partition(backup_path, "/uboot")))
        return print_and_error(NULL, ret);
        
	if (logo && 0 != (ret = nandroid_restore_partition(backup_path, "/logo")))
        return print_and_error(NULL, ret);

    if (nvram) {
        if (0 != (ret = nandroid_restore_partition(backup_path, "/nvram")))
	        return print_and_error(NULL, ret);
        if (volume_for_path("/nvdata") != NULL) {
		    if (0 != (ret = nandroid_restore_partition(backup_path, "/nvdata")))
	        return print_and_error(NULL, ret);
		} else {
			char nvd[PATH_MAX];
			int i = 0;
		    while ((filesystem = filesystems[i]) != NULL) {
		        sprintf(backup_file_image, "%s/nvdata.%s.tar.gz", backup_path, filesystem);
		        if (0 == stat(backup_file_image, &sn)) {
					restore_handler = tar_gzip_extract_wrapper;
		            sprintf(nvd, "cd / ; rm -rf data/nvram ; set -o pipefail ; cat %s* | pigz -d -c | tar xv ; exit $?", backup_file_image);
		            break;
		        }
		        i++;
		    }
		    if (0 != (ret = do_tar_extract(nvd, backup_file_image, "/data", callback))) return print_and_error(NULL, ret);
	    }
    }

    if (secro && 0 != (ret = nandroid_restore_partition(backup_path, "/secro")))
        return print_and_error(NULL, ret);   

    finish_nandroid_job();
    ui_reset_progress();
    ui_print("\nRestore complete!\n");
    return 0;
}

#endif

static int nandroid_undump(const char* partition) {
    nandroid_files_total = 0;

    int ret;

    if (strcmp(partition, "boot") == 0) {
        Volume *vol = volume_for_path("/boot");
        if (vol == NULL || vol->fs_type == NULL)
            return 1;
        char cmd[PATH_MAX];
        sprintf(cmd, "cat /proc/self/fd/0 > %s", vol->device);
        return __system(cmd);
    }

    if (strcmp(partition, "recovery") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/recovery")))
            return ret;
    }

    if (strcmp(partition, "system") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/system")))
            return ret;
    }

    if (strcmp(partition, "custpack") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/custpack")))
            return ret;
    }
    
    if (strcmp(partition, "cust") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/cust")))
            return ret;
    }

    if (strcmp(partition, "data") == 0) {
        if (0 != (ret = nandroid_restore_partition("-", "/data")))
            return ret;
    }

    sync();
    return 0;
}

int nandroid_usage() {
    printf("Usage: nandroid backup\n");
    printf("Usage: nandroid restore <directory>\n");
    printf("Usage: nandroid dump <partition>\n");
    printf("Usage: nandroid undump <partition>\n");
    return 1;
}

static int bu_usage() {
    printf("Usage: bu <fd> backup partition\n");
    printf("Usage: Prior to restore:\n");
    printf("Usage: echo -n <partition> > /tmp/ro.bu.restore\n");
    printf("Usage: bu <fd> restore\n");
    return 1;
}

int bu_main(int argc, char** argv) {
    load_volume_table();
    setup_data_media();

    if (strcmp(argv[2], "backup") == 0) {
        if (argc != 4) {
            return bu_usage();
        }

        int fd = atoi(argv[1]);
        char* partition = argv[3];

        if (fd != STDOUT_FILENO) {
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        int ret = nandroid_dump(partition);
        sleep(10);
        return ret;
    } else if (strcmp(argv[2], "restore") == 0) {
        if (argc != 3) {
            return bu_usage();
        }

        int fd = atoi(argv[1]);
        if (fd != STDIN_FILENO) {
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        char partition[100];
        FILE* f = fopen("/tmp/ro.bu.restore", "r");
        if (f == NULL) {
            printf("cannot open ro.bu.restore\n");
            return bu_usage();
        }

        if (fgets(partition, sizeof(partition), f) == NULL) {
            fclose(f);
            printf("nothing to restore!\n");
            return bu_usage();
        }

        size_t len = strlen(partition);
        if (partition[len - 1] == '\n')
            partition[len - 1] = '\0';

        // fprintf(stderr, "%d %d %s\n", fd, STDIN_FILENO, argv[3]);
        return nandroid_undump(partition);
    }

    return bu_usage();
}

int nandroid_main(int argc, char** argv) {
    load_volume_table();
    char backup_path[PATH_MAX];

    if (argc > 3 || argc < 2)
        return nandroid_usage();

    if (strcmp("backup", argv[1]) == 0) {
        if (argc != 2)
            return nandroid_usage();

        nandroid_generate_timestamp_path(backup_path);
        return nandroid_backup(backup_path);
    }

    if (strcmp("restore", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        return nandroid_restore(argv[2], 1, 1, 1, 1, 1, 0);
    }

    if (strcmp("dump", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        return nandroid_dump(argv[2]);
    }

    if (strcmp("undump", argv[1]) == 0) {
        if (argc != 3)
            return nandroid_usage();
        return nandroid_undump(argv[2]);
    }

    return nandroid_usage();
}
