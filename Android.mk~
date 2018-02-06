ifdef project-path-for
    ifeq ($(call my-dir),$(call project-path-for,recovery))
        RECOVERY_PATH_SET := true
        BOARD_SEPOLICY_DIRS += $(call project-path-for,recovery)/sepolicy
    endif
else
    ifeq ($(call my-dir),bootable/recovery)
        RECOVERY_PATH_SET := true
        BOARD_SEPOLICY_DIRS += bootable/recovery/sepolicy
    endif
endif

ifeq ($(RECOVERY_PATH_SET),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := fuse_sideload.c
LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_MODULE := libfusesideload
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
    libcrecovery/toolbox/dynarray.c \
    libcrecovery/toolbox/getprop.c \
    libcrecovery/toolbox/setprop.c \
    edifyscripting.c \
    adb_install.c \
    asn1_decoder.c \
    verifier.c \
    fuse_sdcard_provider.c \
    propsrvc/legacy_property_service.c

LOCAL_SRC_FILES += \
    ../../system/core/toolbox/newfs_msdos.c \
    ../../system/core/toolbox/start.c \
    ../../system/core/toolbox/stop.c \
    
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

RECOVERY_NAME := Carliv Touch Recovery

ifndef RECOVERY_NAME
RECOVERY_NAME := CWM Based Recovery
endif

RECOVERY_VERSION := $(RECOVERY_NAME) v6.6.1
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
	bionic/libc/bionic \
	external/boringssl/include \
	system/core/include \
	external/stlport/stlport

LOCAL_STATIC_LIBRARIES := \
    libext4_utils_static \
    libmake_ext4fs_static \
    libminizip_static \
    liblz4-static \
    libminiunz_static \
    libsparse_static \
    libfsck_msdos \
    libminipigz_static \
    libzopfli \
    libreboot_static \
    libsdcard \
    libminzip \
    libz \
    libminuictr \
    libadf \
    libdrm \
    libpng \
    libpixelflinger_static \
    libedify \
    libbusyboxctr \
    libmkyaffs2image \
    libunyaffs \
    libflashutils \
    liberase_image \
    libdump_image \
    libflash_image \
    libmtdutils \
    libmmcutils \
    libbmlutils \
    libcrecovery \
    libmincrypt \
    libminadbd \
    libfusesideload \
    libfuse_static \
    libf2fs_sparseblock \
    libdiskconfig \
    libsysutils \
    libfs_mgr \
    libsquashfs_utils \
    libbase \
    libcutils \
    libutils \
    liblog \
    liblogwrap \
    libselinux \
    libcrypto_static \
    libscrypt_static \
    libnl \
    libc++_static \
    libm \
    libc \
    libext2_blkid \
	libext2_uuid

LOCAL_STATIC_LIBRARIES += \
	libext2fs \
	libe2fsck_static \
	libmke2fs_static \
	libtune2fs
 
ifeq ($(TARGET_USES_EXFAT),true)
LOCAL_CFLAGS += -DHAVE_EXFAT   	
LOCAL_WHOLE_STATIC_LIBRARIES += \
    libexfat_mount_static \
    libexfat_mkfs_static \
    libexfat_fsck_static \
	libexfat_static
endif
	
ifeq ($(ENABLE_LOKI_RECOVERY),true)
  LOCAL_CFLAGS += -DENABLE_LOKI
  LOCAL_STATIC_LIBRARIES += libloki_static
  LOCAL_SRC_FILES += loki/loki_recovery.c
endif

ifeq ($(BOARD_USES_BML_OVER_MTD),true)
LOCAL_STATIC_LIBRARIES += libbml_over_mtd
endif

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
LOCAL_CFLAGS += -DMINIVOLD
LOCAL_WHOLE_STATIC_LIBRARIES += libminivold_static
LOCAL_C_INCLUDES += system/extras/ext4_utils system/core/fs_mgr/include external/fsck_msdos
LOCAL_C_INCLUDES += system/vold
endif

ifeq ($(BOARD_USE_ADOPTED_STORAGE), true)
LOCAL_CFLAGS += -DUSE_ADOPTED_STORAGE
#LOCAL_STATIC_LIBRARIES += liblvm libdevmapper
endif

ifeq ($(BOARD_CUSTOM_RECOVERY_KEYMAPPING),)
  LOCAL_SRC_FILES += default_recovery_keys.c
else
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_RECOVERY_KEYMAPPING)
endif

LOCAL_C_INCLUDES += system/extras/ext4_utils
LOCAL_C_INCLUDES += external/boringssl/include

RECOVERY_LINKS := bu make_ext4fs edify busyboxctr flash_image dump_image mkyaffs2image unyaffs erase_image nandroid reboot volume setprop getprop start stop minizip setup_adbd fsck_msdos newfs_msdos sdcard pigz e2fsck mke2fs tune2fs

ifeq ($(TARGET_USES_EXFAT),true)
RECOVERY_LINKS += fsck.exfat mkfs.exfat exfat-fuse
endif

ifeq ($(BOARD_INCLUDE_CRYPTO), true)
RECOVERY_LINKS += minivold vdc
endif

# nc is provided by external/netcat
RECOVERY_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(RECOVERY_LINKS))

BUSYBOX_LINKS := $(shell cat $(LOCAL_PATH)/busybox/busybox-minimalctr.links)
exclude := mke2fs
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))

LOCAL_ADDITIONAL_DEPENDENCIES += \
    permissive.sh \
    nandroid-md5.sh \
    parted \
    sdparted    

ifneq (,$(filter $(TARGET_USERIMAGES_USE_F2FS) $(TARGET_USES_NTFS), true))
LOCAL_ADDITIONAL_DEPENDENCIES += ctrfs
endif

LOCAL_ADDITIONAL_DEPENDENCIES += carliv

ifeq ($(BOARD_USE_ADOPTED_STORAGE), true)
LOCAL_ADDITIONAL_DEPENDENCIES += dms
endif

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
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busyboxctr
$(RECOVERY_BUSYBOX_SYMLINKS):
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@ 
	
#permissive.sh
include $(CLEAR_VARS)
LOCAL_MODULE := permissive.sh
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := permissive.sh
include $(BUILD_PREBUILT)

# make_ext4fs
include $(CLEAR_VARS)
LOCAL_MODULE := libmake_ext4fs_static
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Dmain=make_ext4fs_main
LOCAL_SRC_FILES := \
    ../../system/extras/ext4_utils/make_ext4fs_main.c \
    ../../system/extras/ext4_utils/canned_fs_config.c
include $(BUILD_STATIC_LIBRARY)

# Minizip static library
include $(CLEAR_VARS)
LOCAL_MODULE := libminizip_static
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libz
LOCAL_CFLAGS := -Dmain=minizip_main -D__ANDROID__ -DIOAPI_NO_64
LOCAL_C_INCLUDES := external/zlib
LOCAL_SRC_FILES := \
    ../../external/zlib/src/contrib/minizip/ioapi.c \
    ../../external/zlib/src/contrib/minizip/minizip.c \
    ../../external/zlib/src/contrib/minizip/zip.c
include $(BUILD_STATIC_LIBRARY)

# Miniunz static library
include $(CLEAR_VARS)
LOCAL_MODULE := libminiunz_static
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libz
LOCAL_CFLAGS := -Dmain=miniunz_main -D__ANDROID__ -DIOAPI_NO_64
LOCAL_C_INCLUDES := external/zlib
LOCAL_SRC_FILES := \
    ../../external/zlib/src/contrib/minizip/ioapi.c \
    ../../external/zlib/src/contrib/minizip/miniunz.c \
    ../../external/zlib/src/contrib/minizip/unzip.c
include $(BUILD_STATIC_LIBRARY)

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
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    asn1_decoder.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := verifier_test
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS += -DVERIFIER_TEST -Wno-unused-parameter
LOCAL_LDFLAGS += -Wl,--no-fatal-warnings
LOCAL_SRC_FILES := \
	verifier_test.c \
	asn1_decoder.c \
	verifier.c
LOCAL_STATIC_LIBRARIES := \
	libmincrypt \
	libminuictr \
	libminzip \
	libcutils \
	libc++_static \
	libc
LOCAL_C_INCLUDES += \
	system/extras/ext4_utils \
	external/boringssl/include \
	system/core/include \
	system/core/fs_mgr/include \
    system/vold
include $(BUILD_EXECUTABLE)

commands_recovery_local_path := $(LOCAL_PATH)

include $(LOCAL_PATH)/minui/Android.mk \
	$(LOCAL_PATH)/minuictr/Android.mk \
	$(LOCAL_PATH)/minzip/Android.mk \
	$(LOCAL_PATH)/edify/Android.mk \
	$(LOCAL_PATH)/busybox/Android.mk \
	$(LOCAL_PATH)/yaffsc/Android.mk \
	$(LOCAL_PATH)/flashutils/Android.mk \
	$(LOCAL_PATH)/mtdutils/Android.mk \
	$(LOCAL_PATH)/mmcutils/Android.mk \
	$(LOCAL_PATH)/bmlutils/Android.mk \
	$(LOCAL_PATH)/libcrecovery/Android.mk \
	$(LOCAL_PATH)/minadbd/Android.mk \
	$(LOCAL_PATH)/devices/Android.mk \
	$(LOCAL_PATH)/tools/Android.mk \
	$(LOCAL_PATH)/uncrypt/Android.mk \
	$(LOCAL_PATH)/updater/Android.mk \
	$(LOCAL_PATH)/applypatch/Android.mk \
	$(LOCAL_PATH)/fstools/Android.mk \
	$(LOCAL_PATH)/utilities/Android.mk \
	$(LOCAL_PATH)/loki/Android.mk \
	$(LOCAL_PATH)/locpixelflinger/Android.mk

commands_recovery_local_path :=

endif
