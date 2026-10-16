#version 300 es
// Proper GFX 1.4.3 deferred shadows - APPLY pass: multiplies the game's scene colour by the shadow mask, right after the game
// rendered the 3D world (RenderScene) and before its post effects / HUD. Blend = (ZERO, SRC_COLOR): scene *= mask.
precision mediump float;
uniform mediump sampler2D uMask;
in highp vec2 vUV;
out vec4 oColor;
void main(){ oColor = vec4(texture(uMask, vUV).rgb, 1.0); }
