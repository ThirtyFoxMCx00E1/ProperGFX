#!/bin/sh
# Proper GFX Mobile 1.4.0 - two separate source trees:
#   src32/ -> libProperGFX32.so  GTA SA 2.00, armeabi-v7a  (C++/H only: AML + SAUtils, shaders built into the .so)
#   src64/ -> libProperGFX64.so  GTA SA 2.10, arm64-v8a    (C++/H + GLSL: shaders are .glsl files in files/ProperGFX/shaders/{vertex,fragment})
# usage: ./build.sh /path/to/android-ndk   (r24 or newer)
set -e
NDK="${1:-$ANDROID_NDK_HOME}"
[ -d "$NDK" ] || { echo "usage: $0 /path/to/android-ndk"; exit 1; }
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/dist"; mkdir -p "$OUT"
IMG="$HERE/third_party/imgui"
# 64-bit: every .glsl in Shaders/{fragment,vertex} is compiled INTO libProperGFX64.so (Shaders/ -> src64/ps_glsl_embedded.h)
python3 "$HERE/tools/glsl2h.py" "$HERE/Shaders" "$HERE/src64/ps_glsl_embedded.h"
COMMONSRC="$HERE/third_party/aml/mod/logger.cpp $HERE/third_party/aml/mod/config.cpp \
 $IMG/imgui.cpp $IMG/imgui_draw.cpp $IMG/imgui_tables.cpp $IMG/imgui_widgets.cpp $IMG/imgui_impl_opengl3.cpp"
FLAGS="-std=c++17 -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fno-rtti -ffunction-sections -fdata-sections \
 -DIMGUI_IMPL_OPENGL_ES2 -I$HERE/third_party -I$HERE/third_party/aml -I$IMG -Wno-writable-strings -Wno-invalid-offsetof -Wno-deprecated-declarations -Wno-format-truncation"
LINK="-shared -static-libstdc++ -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384 -llog -lEGL -lGLESv2 -ldl -lm"
build() { # triple out srcdir extra
  echo "== $2 ($3)"
  SRCS="$(ls "$HERE/$3"/*.cpp | tr '\n' ' ') $COMMONSRC"
  "$TC/clang++" --target=$1 $FLAGS -I"$HERE/$3" $4 $SRCS $LINK -o "$OUT/$2"
  "$TC/llvm-strip" --strip-unneeded "$OUT/$2"
  ls -la "$OUT/$2"
}
build armv7a-linux-androideabi21 libProperGFX32.so src32 "-mfloat-abi=softfp -mfpu=neon"
build aarch64-linux-android21    libProperGFX64.so src64 ""
