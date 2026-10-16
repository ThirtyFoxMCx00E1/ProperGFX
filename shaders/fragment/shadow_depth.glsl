#version 300 es
// Proper GFX 1.4.3 deferred shadows - CASTER pass (fragment). Depth only (no colour attachment): foliage / fences are cut out
// with their own texture alpha like the game's alpha test, everything else just writes depth.
precision mediump float;
uniform lowp sampler2D uTex;     // the draw's diffuse texture (unit 0)
uniform float uAlpha;            // 1 = alpha-tested caster
in highp vec2 vUV;
void main(){ if (uAlpha > 0.5 && texture(uTex, vUV).a < 0.5) discard; }
