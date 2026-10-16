# Proper GFX Mobile 1.4.0 - ndk-build alternative to build.sh (run tools/glsl2h.py glsl64 src64/ps_glsl_embedded.h first).
#   armeabi-v7a -> libProperGFX32.so from src32/ (C++/H only)      arm64-v8a -> libProperGFX64.so from src64/ (C++/H + GLSL)
LOCAL_PATH := $(call my-dir)/..
include $(CLEAR_VARS)
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
PGFX_SRC := src32
LOCAL_MODULE := ProperGFX32
else
PGFX_SRC := src64
LOCAL_MODULE := ProperGFX64
endif
LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/$(PGFX_SRC)/*.cpp)) \
                   third_party/aml/mod/logger.cpp third_party/aml/mod/config.cpp \
                   third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
                   third_party/imgui/imgui_widgets.cpp third_party/imgui/imgui_impl_opengl3.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party $(LOCAL_PATH)/third_party/aml $(LOCAL_PATH)/third_party/imgui $(LOCAL_PATH)/$(PGFX_SRC)
LOCAL_CPPFLAGS  += -std=c++17 -O2 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -fno-rtti \
                   -ffunction-sections -fdata-sections -DIMGUI_IMPL_OPENGL_ES2 \
                   -Wno-writable-strings -Wno-nontrivial-memcall -Wno-invalid-offsetof -Wno-deprecated-declarations
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CPPFLAGS  += -mfloat-abi=softfp -mfpu=neon
endif
LOCAL_LDFLAGS   += -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384
LOCAL_LDLIBS    += -llog -lEGL -lGLESv2 -ldl -lm
include $(BUILD_SHARED_LIBRARY)
