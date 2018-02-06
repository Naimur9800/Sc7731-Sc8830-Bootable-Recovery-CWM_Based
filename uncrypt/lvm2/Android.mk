LOCAL_PATH := $(call my-dir)

# Limit libdevmapper source files to minimum required by external/cryptsetup
libdm_src_files := libdm/libdm-common.c \
                   libdm/libdm-config.c \
                   libdm/libdm-deptree.c \
                   libdm/libdm-file.c \
                   libdm/libdm-report.c \
                   libdm/libdm-stats.c \
                   libdm/libdm-string.c \
                   libdm/libdm-timestamp.c \
                   libdm/datastruct/bitset.c \
                   libdm/datastruct/list.c \
                   libdm/datastruct/hash.c \
                   libdm/ioctl/libdm-iface.c \
                   libdm/mm/dbg_malloc.c \
                   libdm/mm/pool.c \
                   libdm/regex/matcher.c \
                   libdm/regex/parse_rx.c \
                   libdm/regex/ttree.c

libdm_c_includes := $(LOCAL_PATH) \
					$(LOCAL_PATH)/include \
                    $(LOCAL_PATH)/libdm \
                    $(LOCAL_PATH)/libdm/ioctl \
                    $(LOCAL_PATH)/libdm/misc \
                    $(LOCAL_PATH)/libdm/regex \
                    $(LOCAL_PATH)/lib/log \
                    $(LOCAL_PATH)/lib/misc

libdm_cflags := -Drindex=strrchr

include $(CLEAR_VARS)
LOCAL_MODULE := libdevmapper
LOCAL_CFLAGS := -O2 -g -std=gnu11 $(libdm_cflags) -W -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers

LOCAL_SRC_FILES := $(libdm_src_files)

LOCAL_C_INCLUDES := $(libdm_c_includes) \
					bionic/libc/bionic \
					bionic/libc/include \
					bionic/libm/include

LOCAL_MODULE_TAGS := optional
LOCAL_WHOLE_STATIC_LIBRARIES := libm
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := liblvm
LOCAL_CFLAGS := -O2 -g $(libdm_cflags) -W -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -DLVMETAD_SUPPORT -DLVMPOLLD_SUPPORT -DLVMLOCKD_SUPPORT

LOCAL_SRC_FILES := \
			lib/activate/activate.c \
			lib/activate/dev_manager.c \
			lib/activate/fs.c \
			lib/cache/lvmcache.c \
			lib/cache/lvmetad.c \
			lib/cache_segtype/cache.c \
			lib/commands/toolcontext.c \
			lib/config/config.c \
			lib/datastruct/btree.c \
			lib/datastruct/str_list.c \
			lib/device/dev-cache.c \
			lib/device/dev-ext.c \
			lib/device/dev-io.c \
			lib/device/dev-md.c \
			lib/device/dev-swap.c \
			lib/device/dev-type.c \
			lib/device/dev-luks.c \
			lib/display/display.c \
			lib/error/errseg.c \
			lib/unknown/unknown.c \
			lib/filters/filter-composite.c \
			lib/filters/filter-persistent.c \
			lib/filters/filter-regex.c \
			lib/filters/filter-sysfs.c \
			lib/filters/filter-md.c \
			lib/filters/filter-fwraid.c \
			lib/filters/filter-mpath.c \
			lib/filters/filter-partitioned.c \
			lib/filters/filter-type.c \
			lib/filters/filter-usable.c \
			lib/format1/disk-rep.c \
			lib/format1/format1.c \
			lib/format1/import-export.c \
			lib/format1/import-extents.c \
			lib/format1/layout.c \
			lib/format1/lvm1-label.c \
			lib/format1/vg_number.c \
			lib/format_pool/disk_rep.c \
			lib/format_pool/format_pool.c \
			lib/format_pool/import_export.c \
			lib/format_pool/pool_label.c \
			lib/format_text/archive.c \
			lib/format_text/archiver.c \
			lib/format_text/export.c \
			lib/format_text/flags.c \
			lib/format_text/format-text.c \
			lib/format_text/import.c \
			lib/format_text/import_vsn1.c \
			lib/format_text/text_label.c \
			lib/freeseg/freeseg.c \
			lib/label/label.c \
			lib/lvmpolld/lvmpolld-client.c \
			lib/locking/cluster_locking.c \
			lib/locking/file_locking.c \
			lib/locking/locking.c \
			lib/locking/lvmlockd.c \
			lib/locking/no_locking.c \
			lib/locking/external_locking.c \
			lib/log/log.c \
			lib/metadata/cache_manip.c \
			lib/metadata/lv.c \
			lib/metadata/lv_manip.c \
			lib/metadata/merge.c \
			lib/metadata/metadata.c \
			lib/metadata/mirror.c \
			lib/metadata/pool_manip.c \
			lib/metadata/pv.c \
			lib/metadata/pv_manip.c \
			lib/metadata/pv_map.c \
			lib/metadata/raid_manip.c \
			lib/metadata/replicator_manip.c \
			lib/metadata/segtype.c \
			lib/metadata/snapshot_manip.c \
			lib/metadata/thin_manip.c \
			lib/metadata/vg.c \
			lib/mirror/mirrored.c \
			lib/misc/crc.c \
			lib/misc/lvm-exec.c \
			lib/misc/lvm-file.c \
			lib/misc/lvm-flock.c \
			lib/misc/lvm-globals.c \
			lib/misc/lvm-signal.c \
			lib/misc/lvm-string.c \
			lib/misc/lvm-wrappers.c \
			lib/misc/lvm-percent.c \
			lib/misc/sharedlib.c \
			lib/mm/memlock.c \
			lib/properties/prop_common.c \
			lib/raid/raid.c \
			lib/replicator/replicator.c \
			lib/report/properties.c \
			lib/report/report.c \
			lib/snapshot/snapshot.c \
			lib/striped/striped.c \
			lib/thin/thin.c \
			lib/uuid/uuid.c \
			lib/zero/zero.c

LOCAL_C_INCLUDES := \
		$(LOCAL_PATH)/lib \
		$(LOCAL_PATH)/include \
		$(LOCAL_PATH)/man \
		$(LOCAL_PATH)/conf \
		$(LOCAL_PATH)/daemons \
		$(LOCAL_PATH)/daemons/clvmd \
		$(LOCAL_PATH)/daemons/lvmlockd \
		$(LOCAL_PATH)/daemons/lvmpolld \
		$(LOCAL_PATH)/daemons/lvmetad \
		$(LOCAL_PATH)/tools \
		$(LOCAL_PATH)/liblvm \
		$(LOCAL_PATH)/libdm \
		$(LOCAL_PATH)/libdm/datastruct \
		$(LOCAL_PATH)/libdm/ioctl \
		$(LOCAL_PATH)/libdm/misc \
		$(LOCAL_PATH)/libdaemon/client \
		$(LOCAL_PATH)/libdaemon/server \
		$(LOCAL_PATH)/lib/activate \
		$(LOCAL_PATH)/lib/format1 \
		$(LOCAL_PATH)/lib/format_text \
		$(LOCAL_PATH)/lib/format_pool \
		$(LOCAL_PATH)/lib/cache \
		$(LOCAL_PATH)/lib/locking \
		$(LOCAL_PATH)/lib/config \
		$(LOCAL_PATH)/lib/device \
		$(LOCAL_PATH)/lib/display \
		$(LOCAL_PATH)/lib/metadata \
		$(LOCAL_PATH)/lib/locking \
		$(LOCAL_PATH)/lib/datastruct \
		$(LOCAL_PATH)/lib/commands \
		$(LOCAL_PATH)/lib/filters \
		$(LOCAL_PATH)/lib/label \
		$(LOCAL_PATH)/lib/lvmpolld \
		$(LOCAL_PATH)/lib/misc \
		$(LOCAL_PATH)/lib/mm \
		$(LOCAL_PATH)/lib/properties \
		$(LOCAL_PATH)/lib/log \
		$(LOCAL_PATH)/lib/report \
		$(LOCAL_PATH)/lib/uuid

LOCAL_STATIC_LIBRARIES := libdevmapper
LOCAL_WHOLE_STATIC_LIBRARIES := libm
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

ifeq ($(BOARD_USE_ADOPTED_STORAGE), true)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
			tools/vgextend.c \
			tools/vgimport.c \
			tools/pvscan.c \
			tools/lvmcmdlib.c \
			tools/pvmove_poll.c \
			tools/tags.c \
			tools/vgexport.c \
			tools/formats.c \
			tools/vgmerge.c \
			tools/lvextend.c \
			tools/dumpconfig.c \
			tools/lvrename.c \
			tools/lvcreate.c \
			tools/vgcreate.c \
			tools/lvmdiskscan.c \
			tools/lvconvert_poll.c \
			tools/vgcfgbackup.c \
			tools/vgremove.c \
			tools/lvconvert.c \
			tools/pvmove.c \
			tools/pvresize.c \
			tools/vgsplit.c \
			tools/lvm2cmd.c \
			tools/vgck.c \
			tools/lvchange.c \
			tools/lvremove.c \
			tools/pvdisplay.c \
			tools/lvpoll.c \
			tools/vgcfgrestore.c \
			tools/polldaemon.c \
			tools/pvcreate.c \
			tools/lvmchange.c \
			tools/lvscan.c \
			tools/reporter.c \
			tools/segtypes.c \
			tools/lvdisplay.c \
			tools/langinfo.c \
			tools/dmsetup.c \
			tools/vgscan.c \
			tools/vgdisplay.c \
			tools/lvreduce.c \
			tools/pvremove.c \
			tools/vgreduce.c \
			tools/pvck.c \
			tools/lvmcmdline.c \
			tools/lvresize.c \
			tools/vgrename.c \
			tools/vgconvert.c \
			tools/vgchange.c \
			tools/vgmknodes.c \
			tools/toollib.c \
			tools/pvchange.c

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(libdm_c_includes)
LOCAL_C_INCLUDES += \
		$(LOCAL_PATH)/lib/activate \
		$(LOCAL_PATH)/lib/format_text \
		$(LOCAL_PATH)/lib/cache \
		$(LOCAL_PATH)/lib/locking \
		$(LOCAL_PATH)/lib/config \
		$(LOCAL_PATH)/lib/device \
		$(LOCAL_PATH)/lib/display \
		$(LOCAL_PATH)/lib/metadata \
		$(LOCAL_PATH)/lib/locking \
		$(LOCAL_PATH)/lib/datastruct \
		$(LOCAL_PATH)/lib/commands \
		$(LOCAL_PATH)/lib/filters \
		$(LOCAL_PATH)/lib/label \
		$(LOCAL_PATH)/lib/lvmpolld \
		$(LOCAL_PATH)/lib/mm \
		$(LOCAL_PATH)/lib/log \
		$(LOCAL_PATH)/lib/report \
		$(LOCAL_PATH)/lib/uuid \
		$(LOCAL_PATH)/libdaemon/client \
		$(LOCAL_PATH)/libdaemon/server \
		$(LOCAL_PATH)/man \
		$(LOCAL_PATH)/conf \
		$(LOCAL_PATH)/daemons \
		$(LOCAL_PATH)/daemons/lvmpolld \
		$(LOCAL_PATH)/tools \
		$(LOCAL_PATH)/liblvm \
		bionic/libc/bionic \
		bionic/libc/include

LOCAL_CFLAGS := -std=gnu99 -D_GNU_SOURCE -W -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers
LOCAL_MODULE := dms
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := eng optional
LOCAL_STATIC_LIBRARIES := liblvm libdevmapper
LOCAL_STATIC_LIBRARIES += libcrypto_static libc libutils libcutils libext2_uuid
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)
endif
