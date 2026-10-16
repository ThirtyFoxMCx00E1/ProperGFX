#!/usr/bin/env python3
# Host check of glsl64/: resolves "#pragma include" like ps_glsl.cpp and compiles every pass + many final.glsl variants
# as GLSL ES 1.00 with Khronos glslangValidator.  usage: tools/glsl_validate.py /path/to/glslangValidator
import os, sys, subprocess, itertools, random
GV = sys.argv[1]; ROOT = os.path.join(os.path.dirname(__file__), '..', 'glsl64'); TMP = '/tmp/gt'
os.makedirs(TMP, exist_ok=True)
def expand(rel, seen):
    if rel in seen: return ''
    seen.add(rel); stage = rel.split('/')[0]; out = []
    for line in open(os.path.join(ROOT, rel)).read().split('\n'):
        t = line.strip()
        if t.startswith('#pragma include'):
            inc = t.split('"')[1]; out.append(expand(inc if '/' in inc else stage + '/' + inc, seen)); continue
        out.append(line)
    return '\n'.join(out) + '\n'
def check(name, src, stage):
    p = os.path.join(TMP, name + '.' + stage)
    open(p, 'w').write('#version 100\n' + src)
    r = subprocess.run([GV, p], capture_output=True, text=True)
    ok = r.returncode == 0
    if not ok: print('FAIL', name, r.stdout[-1500:])
    return ok
bad = 0
bad += not check('fullscreen', expand('vertex/fullscreen.glsl', set()), 'vert')
bad += not check('caster_v', expand('vertex/shadow_caster.glsl', set()), 'vert')
bad += not check('caster_f', expand('fragment/shadow_caster.glsl', set()), 'frag')
for f in ['fxaa_pass', 'bloom_bright', 'downsample', 'blur_gauss', 'autoexposure', 'custom']:
    bad += not check(f, expand('fragment/%s.glsl' % f, set()), 'frag')
FX = ['FX_AA','FX_CA','FX_CAS','FX_DOF','FX_OUTLINE','FX_CEL','FX_AUTOEXP','FX_BLOOM','FX_RAYS','FX_FLARE','FX_GLOW','FX_GOLDEN','FX_LUT','FX_UNDER','FX_VHS','FX_VIGNETTE','FX_GRAIN','FX_DITHER','FX_GRADE','FX_GAMMA']
final = expand('fragment/final.glsl', set())
random.seed(1); n = 0
combos = [[], FX] + [[f] for f in FX] + [random.sample(FX, random.randint(2, len(FX))) for _ in range(40)]
for c in combos:
    for tm in range(4):
        d = ''.join('#define %s 1\n' % x for x in c) + '#define TM_MODE %d\n' % tm
        bad += not check('final_%d' % n, d + final, 'frag'); n += 1
# shadow receiver snippets inside a mock game shader
def sec(rel, name):
    s = open(os.path.join(ROOT, rel)).read(); m = '//@@' + name + '\n'; a = s.index(m) + len(m); return s[a:s.index('//@@', a)]
fs = 'precision mediump float;\nvarying mediump float Out_FogAmt;\n' + sec('fragment/shadows.glsl','decl') + 'void main(){ lowp vec4 fcolor = vec4(1.0);\n' + sec('fragment/shadows.glsl','apply') + 'gl_FragColor = fcolor; }\n'
vs = 'attribute vec3 Position; attribute vec3 Normal; uniform mat4 ObjMatrix; varying mediump float Out_FogAmt;\n' + sec('vertex/shadows.glsl','decl') + 'void main(){ vec4 WorldPos = ObjMatrix * vec4(Position,1.0);' + sec('vertex/shadows.glsl','assign') + 'vec3 WorldNormal = mat3(ObjMatrix) * Normal;' + sec('vertex/shadows.glsl','nl') + 'Out_FogAmt = 0.0; gl_Position = WorldPos; }\n'
bad += not check('recv_f', fs, 'frag'); bad += not check('recv_v', vs, 'vert')
print('glsl_validate: %d shaders checked, %d failed' % (n + 11, bad)); sys.exit(1 if bad else 0)
