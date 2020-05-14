LOCAL_PATH := $(call my-dir)/..
include $(CLEAR_VARS)

LOCAL_CFLAGS    := -std=c99 -O2 -W -Wall -pthread -pipe -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0 -DMG_ENABLE_HTTP_STREAMING_MULTIPART $(COPT)
LOCAL_MODULE    := orchestrator
LOCAL_SRC_FILES := orchestrator.c mongoose/mongoose.c cJSON/cJSON.c src/bs_core.c  src/bs_tdr_job.c src/file_utils.c src/bs_eth_installer_job.c

include $(BUILD_EXECUTABLE)
