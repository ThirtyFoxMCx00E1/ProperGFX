#version 300 es
// Proper GFX 1.4.6 deferred shadows - CASTER pass (fragment). Depth only (no colour attachment).
// Alpha-tested casters (tree leaves, fences, grass cards) are cut out with the GAME'S OWN alpha-test threshold (uAlpha > 0), sampled
// at mip 0 like libZyZGFX64, so a tree casts its leaves and not the box of its leaf cards. uAlpha = 0: solid caster.
precision mediump float;
uniform lowp sampler2D uTex;     // the draw's diffuse texture (unit 0)
uniform float uAlpha;            // alpha-test reference (0 = solid caster)
in highp vec2 vUV;
void main(){ if (uAlpha > 0.0 && textureLod(uTex, vUV, 0.0).a < uAlpha) discard; }
