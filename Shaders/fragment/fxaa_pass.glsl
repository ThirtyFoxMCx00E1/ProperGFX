// stand-alone FXAA pass (used when Sharpen / Outlines / Chromatic aberration need an anti-aliased input)
#pragma include "common.glsl"
#pragma include "fxaa.glsl"
uniform sampler2D uTex; uniform vec4 uRes; uniform vec4 uP;   // uP.x = [AntiAliasing] Strength
void main(){ gl_FragColor = vec4(fxaaSample(uTex, vUV, uRes.zw, uP.x), 1.0); }
