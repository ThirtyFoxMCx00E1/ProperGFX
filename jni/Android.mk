LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE    := ProperGFX
LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := src/main.cpp src/ps_settings.cpp src/ps_glstate.cpp src/ps_fx.cpp src/ps_ui.cpp src/ps_world.cpp src/ps_shadow.cpp \
                   third_party/aml/mod/logger.cpp third_party/aml/mod/config.cpp \
                   third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
                   third_party/imgui/imgui_widgets.cpp third_party/imgui/imgui_impl_opengl3.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party $(LOCAL_PATH)/third_party/aml $(LOCAL_PATH)/third_party/imgui $(LOCAL_PATH)/src
LOCAL_CPPFLAGS  += -std=c++17 -O2 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -fno-rtti \
                   -ffunction-sections -fdata-sections -DIMGUI_IMPL_OPENGL_ES2 \
                   -Wno-writable-strings -Wno-nontrivial-memcall -Wno-invalid-offsetof -Wno-deprecated-declarations
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CPPFLAGS  += -mfloat-abi=softfp -mfpu=neon
endif
LOCAL_LDFLAGS   += -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384
LOCAL_LDLIBS    += -llog -lEGL -lGLESv2 -ldl -lm
include $(BUILD_SHARED_LIBRARY)
