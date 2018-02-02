ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_HAS_MTK_CPU BOARD_NEEDS_MTK_GETSIZE

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_SRC_FILES := \
	mmcutils.c

LOCAL_MODULE := libmmcutils
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += $(commands_recovery_local_path)

include $(BUILD_STATIC_LIBRARY)

endif	# !TARGET_SIMULATOR
