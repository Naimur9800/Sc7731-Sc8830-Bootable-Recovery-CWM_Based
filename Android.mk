ifdef project-path-for
    ifeq ($(call my-dir),$(call project-path-for,recovery))
        RECOVERY_PATH_SET := true
        BOARD_SEPOLICY_DIRS += $(call project-path-for,recovery)/sepolicy
        BOARD_SEPOLICY_UNION += ctr.te
    endif
else
    ifeq ($(call my-dir),bootable/recovery)
        RECOVERY_PATH_SET := true
        BOARD_SEPOLICY_DIRS += bootable/recovery/sepolicy
        BOARD_SEPOLICY_UNION += ctr.te
    endif
endif

ifeq ($(RECOVERY_PATH_SET),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := fuse_sideload.c
LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_MODULE := libfusesideload
LOCAL_C_INCLUDES += system/core/include
LOCAL_STATIC_LIBRARIES := libcutils libc libmincrypt
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    recovery.c \
    bootloader.c \
    install.c \
    roots.c \
    default_recovery_ui.c \
    ui.c \
    mtdutils/mounts.c \
    extendedcommands.c \
    nandroid.c \
    ../../system/core/toolbox/dynarray.c \
    ../../system/core/toolbox/getprop.c \
    ../../system/core/toolbox/newfs_msdos.c \
    ../../system/core/toolbox/setprop.c \
    ../../system/core/toolbox/start.c \
    ../../system/core/toolbox/stop.c \
    edifyscripting.c \
    adb_install.c \
    asn1_decoder.c \
    verifier.c \
    fuse_sdcard_provider.c \
    propsrvc/legacy_property_service.c

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
LOCAL_CFLAGS += -DBOARD_INCLUDE_CRYPTO
LOCAL_SRC_FILES += \
	../../system/vold/vdc.c
endif

ADDITIONAL_RECOVERY_FILES := $(shell echo $$ADDITIONAL_RECOVERY_FILES)
LOCAL_SRC_FILES += $(ADDITIONAL_RECOVERY_FILES)

LOCAL_MODULE := recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE_TAGS := eng
LOCAL_LDFLAGS := -Wl,--no-fatal-warnings

# We will allways refer and give credits here to  Koushik Dutta who made this possible in 
# the first place!

RECOVERY_NAME := CTR Recovery by Md. Naimur

ifndef RECOVERY_NAME
RECOVERY_NAME := CWM Based Recovery
endif

RECOVERY_VERSION := $(RECOVERY_NAME) v5.7.3
LOCAL_CFLAGS += -DRECOVERY_VERSION="$(RECOVERY_VERSION)"
RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wl,--no-fatal-warnings
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-sign-compare

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

ifeq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
	ifneq ($(DEVICE_RESOLUTION),)
		ifneq ($(filter $(DEVICE_RESOLUTION), 240x240 320x480 480x320 1024x600),)
		    BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_7x16.h\"
		else ifneq ($(filter $(DEVICE_RESOLUTION), 480x800 480x854 540x960 600x1024 1024x768 1280x720 1280x768 1280x800),)
		    BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_10x18.h\"
		else ifneq ($(filter $(DEVICE_RESOLUTION), 720x1280 768x1024 768x1280 800x1200 800x1280),)
		    BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_16x35.h\"
		else ifneq ($(filter $(DEVICE_RESOLUTION), 1080x1920 1200x1920 1920x1200 2560x1600),)
		    BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_23x49.h\"
		else ifneq ($(filter $(DEVICE_RESOLUTION), 1440x2560 1600x2560),)
		    BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_26x55.h\"
		endif
	else
	    BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_15x24.h\"
	endif
endif

BOARD_RECOVERY_CHAR_WIDTH := $(shell echo $(BOARD_USE_CUSTOM_RECOVERY_FONT) | cut -d _  -f 2 | cut -d . -f 1 | cut -d x -f 1)
BOARD_RECOVERY_CHAR_HEIGHT := $(shell echo $(BOARD_USE_CUSTOM_RECOVERY_FONT) | cut -d _  -f 2 | cut -d . -f 1 | cut -d x -f 2)

RECOVERY_BUILD_DATE := $(shell date +"%Y-%m-%d")
RECOVERY_BUILD_USER := $(shell whoami)
RECOVERY_BUILD_HOST := $(shell hostname)
RECOVERY_DEVICE := $(TARGET_DEVICE)
LOCAL_CFLAGS += -DBOARD_RECOVERY_CHAR_WIDTH=$(BOARD_RECOVERY_CHAR_WIDTH) -DBOARD_RECOVERY_CHAR_HEIGHT=$(BOARD_RECOVERY_CHAR_HEIGHT) -DRECOVERY_BUILD_DATE="$(RECOVERY_BUILD_DATE)" -DRECOVERY_BUILD_USER="$(RECOVERY_BUILD_USER)" -DRECOVERY_BUILD_HOST="$(RECOVERY_BUILD_HOST)" -DRECOVERY_DEVICE="$(RECOVERY_DEVICE)"

BOARD_RECOVERY_DEFINES := BOARD_HAS_NO_SELECT_BUTTON BOARD_UMS_LUNFILE BOARD_RECOVERY_ALWAYS_WIPES BOARD_RECOVERY_HANDLES_MOUNT RECOVERY_EXTEND_NANDROID_MENU TARGET_USE_CUSTOM_LUN_FILE_PATH TARGET_DEVICE TARGET_RECOVERY_FSTAB BOARD_HAS_MTK_CPU CUSTOM_BATTERY_FILE CUSTOM_BATTERY_STATS_PATH BOARD_NEEDS_MTK_GETSIZE BOARD_USE_PROTOCOL_TYPE_B RECOVERY_TOUCHSCREEN_FLIP_X RECOVERY_TOUCHSCREEN_FLIP_Y RECOVERY_TOUCHSCREEN_SWAP_XY VIBRATOR_TIMEOUT_FILE

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

ifneq ($(BOARD_RECOVERY_BLDRMSG_OFFSET),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=$(BOARD_RECOVERY_BLDRMSG_OFFSET)
endif

ifneq ($(TOUCH_INPUT_BLACKLIST),)
  LOCAL_CFLAGS += -DTOUCH_INPUT_BLACKLIST=$(TOUCH_INPUT_BLACKLIST)
endif

LOCAL_C_INCLUDES := \
    system/vold \
    system/extras/ext4_utils \
    system/core/adb \
    external/e2fsprogs/lib \
    system/core/libsparse \
    system/core/libcutils \
	bionic/libc/bionic \
	external/openssl/include \
	system/core/include \
	external/stlport/stlport

LOCAL_STATIC_LIBRARIES := \
    libext4_utils_static \
    libmake_ext4fs \
    libminizip \
    libsparse_static \
    libfsck_msdos \
    libminipigz \
    libreboot_static \
    libsdcard \
    libminzip \
    libz \
    liblz4-static \
    libunz \
    libcrecovery \
    libflashutils \
    libmtdutils \
    libmmcutils \
    libbmlutils \
    libmincrypt \
    libminadbd \
    libedify \
    libbusybox \
    libmkyaffs2image \
    libunyaffs \
    liberase_image \
    libdump_image \
    libflash_image \
    libfusesideload \
    libcrypto_static \
    libminuictr \
    libadf \
    libpixelflinger_static \
    libpng \
    libfs_mgr \
    libcutils \
    liblog \
    libselinux \
    libstdc++ \
    libutils \
    libm \
    libc \
    libext2_blkid \
    libext2_uuid

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
	LOCAL_CFLAGS += -DUSE_EXT4
	LOCAL_C_INCLUDES += system/extras/ext4_utils
	LOCAL_STATIC_LIBRARIES += libext4_utils_static libz liblz4-static
endif

ifeq ($(BOARD_USES_BML_OVER_MTD),true)
LOCAL_STATIC_LIBRARIES += libbml_over_mtd
endif

ifeq ($(ENABLE_LOKI_RECOVERY),true)
  LOCAL_CFLAGS += -DENABLE_LOKI
  LOCAL_STATIC_LIBRARIES += libloki_static
  LOCAL_SRC_FILES += loki/loki_recovery.c
endif

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
	LOCAL_CFLAGS += -DUSE_F2FS
	LOCAL_STATIC_LIBRARIES += libf2fs_fmt
endif

ifeq ($(TARGET_USES_NTFS),true)
	LOCAL_CFLAGS += -DHAVE_NTFS
	LOCAL_STATIC_LIBRARIES += libmount.ntfs-3g libntfsfix.recovery libmkntfs.recovery libfuse-lite.recovery libntfs-3g.recovery
endif

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
LOCAL_STATIC_LIBRARIES += libvold
LOCAL_C_INCLUDES += \
	system/extras/ext4_utils \
	system/core/fs_mgr/include \
	external/fsck_msdos \
LOCAL_C_INCLUDES += system/vold 
endif

ifeq ($(TARGET_USES_EXFAT),true)
	LOCAL_CFLAGS += -DHAVE_EXFAT
	LOCAL_WHOLE_STATIC_LIBRARIES += libexfat libfuse libexfat_fsck libexfat_mkfs libexfat_mount
endif

ifeq ($(BOARD_CUSTOM_RECOVERY_KEYMAPPING),)
  LOCAL_SRC_FILES += default_recovery_keys.c
else
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_RECOVERY_KEYMAPPING)
endif

LOCAL_C_INCLUDES += system/extras/ext4_utils
LOCAL_C_INCLUDES += external/openssl/include

RECOVERY_LINKS := bu make_ext4fs edify busybox flash_image dump_image mkyaffs2image unyaffs erase_image nandroid reboot volume setprop getprop start stop minizip setup_adbd fsck_msdos newfs_msdos sdcard pigz

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
	LOCAL_CFLAGS += -DMINIVOLD
	RECOVERY_LINKS += vdc
endif

# nc is provided by external/netcat
RECOVERY_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(RECOVERY_LINKS))

BUSYBOX_LINKS := $(shell cat external/busybox/busybox-minimal.links)
exclude := mke2fs
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
LOCAL_ADDITIONAL_DEPENDENCIES := \
    minivold
endif

LOCAL_ADDITIONAL_DEPENDENCIES += \
    killrecovery.sh \
    nandroid-md5.sh \
    parted \
    sdparted

ifeq ($(TARGET_USES_EXFAT),true)
LOCAL_ADDITIONAL_DEPENDENCIES += \
    mount.exfat_static
endif
    
ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
LOCAL_ADDITIONAL_DEPENDENCIES += \
	mkfs.f2fs \
	fsck.f2fs
endif
    
ifeq ($(TARGET_USES_NTFS),true)
LOCAL_ADDITIONAL_DEPENDENCIES += \
	mkfs.ntfs \
	mount.ntfs \
	fsck.ntfs
endif

LOCAL_ADDITIONAL_DEPENDENCIES += carliv

LOCAL_ADDITIONAL_DEPENDENCIES += $(RECOVERY_SYMLINKS) $(RECOVERY_BUSYBOX_SYMLINKS)

ifneq ($(TARGET_RECOVERY_DEVICE_MODULES),)
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_RECOVERY_DEVICE_MODULES)
endif

include $(BUILD_EXECUTABLE)

$(RECOVERY_SYMLINKS): RECOVERY_BINARY := $(LOCAL_MODULE)
$(RECOVERY_SYMLINKS):
	@echo "Symlink: $@ -> $(RECOVERY_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(RECOVERY_BINARY) $@

# Now let's do recovery symlinks
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busybox
$(RECOVERY_BUSYBOX_SYMLINKS):
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@ 

# Reboot static library
include $(CLEAR_VARS)
LOCAL_MODULE := libreboot_static
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Dmain=reboot_main
LOCAL_SRC_FILES := ../../system/core/reboot/reboot.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := nandroid-md5.sh
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := nandroid-md5.sh
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := killrecovery.sh
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := killrecovery.sh
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := asn1_decoder.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_SRC_FILES := verifier_test.c asn1_decoder.c verifier.c

LOCAL_C_INCLUDES += \
	system/extras/ext4_utils \
	external/openssl/include \
	system/core/include

LOCAL_MODULE := verifier_test

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE_TAGS := tests

LOCAL_LDFLAGS += -Wl,--no-fatal-warnings
LOCAL_STATIC_LIBRARIES := libmincrypt libminuictr libminzip libcutils libstdc++ libc

include $(BUILD_EXECUTABLE)

commands_recovery_local_path := $(LOCAL_PATH)
include $(commands_recovery_local_path)/minui/Android.mk
include $(commands_recovery_local_path)/bmlutils/Android.mk
include $(commands_recovery_local_path)/flashutils/Android.mk
include $(commands_recovery_local_path)/libcrecovery/Android.mk
include $(commands_recovery_local_path)/minuictr/Android.mk
include $(commands_recovery_local_path)/devices/Android.mk
include $(commands_recovery_local_path)/minzip/Android.mk
include $(commands_recovery_local_path)/minadbd/Android.mk
include $(commands_recovery_local_path)/yaffsc/Android.mk
include $(commands_recovery_local_path)/mtdutils/Android.mk
include $(commands_recovery_local_path)/mmcutils/Android.mk
include $(commands_recovery_local_path)/tools/Android.mk
include $(commands_recovery_local_path)/edify/Android.mk
include $(commands_recovery_local_path)/uncrypt/Android.mk
include $(commands_recovery_local_path)/updater/Android.mk
include $(commands_recovery_local_path)/applypatch/Android.mk
include $(commands_recovery_local_path)/utilities/Android.mk
include $(commands_recovery_local_path)/loki/Android.mk
include $(commands_recovery_local_path)/ntfs-3g/Android.mk
commands_recovery_local_path :=

endif
