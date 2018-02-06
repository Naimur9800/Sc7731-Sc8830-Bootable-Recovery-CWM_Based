LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := exfat-fuse
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64
LOCAL_SRC_FILES = ../../../external/exfat/fuse/main.c
LOCAL_C_INCLUDES += \
    external/exfat/fuse \
    external/exfat/libexfat \
    external/fuse/include \
    external/fuse/android
LOCAL_STATIC_LIBRARIES += libz libc libfuse_static libexfat_static
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)

ifneq (,$(filter $(TARGET_USERIMAGES_USE_F2FS) $(TARGET_USES_NTFS), true))
include $(CLEAR_VARS)
LOCAL_MODULE := ctrfs
LOCAL_MODULE_TAGS := eng optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := fstools.c
LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_WHOLE_STATIC_LIBRARIES += \
	libfuse_static

ifeq ($(TARGET_USES_NTFS),true)
LOCAL_CFLAGS += -DHAVE_NTFS
LOCAL_WHOLE_STATIC_LIBRARIES += \
	libntfs-3g_static \
	libntfs3g_fsck_static \
	libntfs3g_mkfs_main \
	libntfs3g_mount_static
endif

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
LOCAL_CFLAGS += -DUSE_F2FS
LOCAL_WHOLE_STATIC_LIBRARIES += \
	libf2fs_static \
	libf2fs_fsck_static \
	libf2fs_mkfs_static
endif

LOCAL_STATIC_LIBRARIES := \
	libext2_blkid \
	libext2_uuid \
	libext2_profile \
	libext2_quota \
	libext2_com_err \
	libext2_e2p \
	libc++_static \
	libc \
	libm

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
FSTOOLS_LINKS := mkfs.f2fs fsck.f2fs
endif

ifeq ($(TARGET_USES_NTFS),true)
FSTOOLS_LINKS += fsck.ntfs mkfs.ntfs mount.ntfs
endif

RECOVERY_FSTOOLS_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(FSTOOLS_LINKS))

LOCAL_ADDITIONAL_DEPENDENCIES += $(RECOVERY_FSTOOLS_SYMLINKS)

include $(BUILD_EXECUTABLE)

# Now let's do fstools symlinks
$(RECOVERY_FSTOOLS_SYMLINKS): FSTOOLS_BINARY := ctrfs
$(RECOVERY_FSTOOLS_SYMLINKS):
	@echo "Symlink: $@ -> $(FSTOOLS_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(FSTOOLS_BINARY) $@ 

endif
