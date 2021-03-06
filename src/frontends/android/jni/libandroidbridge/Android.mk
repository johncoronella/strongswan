LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# copy-n-paste from Makefile.am
LOCAL_SRC_FILES := \
android_jni.c \
backend/android_attr.c \
backend/android_creds.c \
backend/android_private_key.c \
backend/android_service.c \
charonservice.c \
kernel/android_ipsec.c \
kernel/android_net.c \
kernel/network_manager.c \
vpnservice_builder.c

# build libandroidbridge -------------------------------------------------------

LOCAL_C_INCLUDES += \
	$(libvstr_PATH) \
	$(strongswan_PATH)/src/libipsec \
	$(strongswan_PATH)/src/libhydra \
	$(strongswan_PATH)/src/libcharon \
	$(strongswan_PATH)/src/libstrongswan

LOCAL_CFLAGS := $(strongswan_CFLAGS) \
	-DPLUGINS='"$(strongswan_CHARON_PLUGINS)"'

LOCAL_MODULE := libandroidbridge

LOCAL_MODULE_TAGS := optional

LOCAL_ARM_MODE := arm

LOCAL_PRELINK_MODULE := false

LOCAL_LDLIBS := -llog

LOCAL_SHARED_LIBRARIES := libstrongswan libhydra libipsec libcharon

include $(BUILD_SHARED_LIBRARY)


