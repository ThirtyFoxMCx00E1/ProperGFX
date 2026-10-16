#!/bin/sh
# Builds libProperGFX32.so (GTA SA 2.00, armeabi-v7a) and libProperGFX64.so (GTA SA 2.10, arm64-v8a)
# usage: ./build.sh /path/to/android-ndk-r30   (any recent NDK works)
set -e
NDK="${1:-$ANDROID_NDK_HOME}"
[ -d "$NDK" ] || { echo "usage: $0 /path/to/android-ndk"; exit 1; }
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/dist"; mkdir -p "$OUT" "$HERE/obj"
IMG="$HERE/third_party/imgui"
SRCS="$HERE/src/main.cpp $HERE/src/ps_settings.cpp $HERE/src/ps_glstate.cpp $HERE/src/ps_fx.cpp $HERE/src/ps_ui.cpp $HERE/src/ps_world.cpp $HERE/src/ps_shadow.cpp \
 $HERE/third_party/aml/mod/logger.cpp $HERE/third_party/aml/mod/config.cpp \
 $IMG/imgui.cpp $IMG/imgui_draw.cpp $IMG/imgui_tables.cpp $IMG/imgui_widgets.cpp $IMG/imgui_impl_opengl3.cpp"
COMMON="-std=c++17 -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fno-rtti -ffunction-sections -fdata-sections \
 -DIMGUI_IMPL_OPENGL_ES2 -I$HERE/third_party -I$HERE/third_party/aml -I$IMG -I$HERE/src -Wno-writable-strings -Wno-nontrivial-memcall -Wno-invalid-offsetof -Wno-deprecated-declarations"
LINK="-shared -static-libstdc++ -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384 -llog -lEGL -lGLESv2 -ldl -lm"
build() { # triple name extra
  echo "== $2"
  "$TC/clang++" --target=$1 $COMMON $3 $SRCS $LINK -o "$OUT/$2"
  "$TC/llvm-strip" --strip-unneeded "$OUT/$2"
  ls -la "$OUT/$2"
}
build armv7a-linux-androideabi21 libProperGFX32.so "-mfloat-abi=softfp -mfpu=neon"
build aarch64-linux-android21    libProperGFX64.so ""
