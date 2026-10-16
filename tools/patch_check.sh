#!/bin/sh
# usage: tools/patch_check.sh /path/to/glslangValidator   - patches the one-line game shaders (Mali log) for both trees and validates them
set -e
GV="$1"; HERE="$(cd "$(dirname "$0")/.." && pwd)"; T=/tmp/pgfx_patch; rm -rf $T; mkdir -p $T/32 $T/64
python3 "$HERE/tools/glsl2h.py" "$HERE/shaders" "$HERE/src64/ps_glsl_embedded.h" >/dev/null
g++ -std=c++17 -O1 -I"$HERE/src32" -I"$HERE/third_party" "$HERE/tools/host_patch_test.cpp" "$HERE"/src32/ps_world.cpp "$HERE"/src32/ps_shadow.cpp "$HERE"/src32/ps_settings.cpp "$HERE"/src32/ps_glstate.cpp -lEGL -lGLESv2 -o $T/t32
g++ -std=c++17 -O1 -DPGFX64 -I"$HERE/src64" -I"$HERE/third_party" "$HERE/tools/host_patch_test.cpp" "$HERE"/src64/ps_world.cpp "$HERE"/src64/ps_shadow.cpp "$HERE"/src64/ps_scene.cpp "$HERE"/src64/ps_settings.cpp "$HERE"/src64/ps_glstate.cpp "$HERE"/src64/ps_glsl.cpp "$HERE"/src64/ps_fx.cpp "$HERE"/src64/ps_lut.cpp -lEGL -lGLESv2 -o $T/t64
fail=0
for b in 32 64; do $T/t$b $T/$b; for f in $T/$b/*; do if "$GV" "$f" >$f.log 2>&1; then echo "OK   $b $(basename $f)"; else echo "FAIL $b $(basename $f)"; cat $f.log; fail=1; fi; done; done
exit $fail
