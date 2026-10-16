#version 300 es
// Proper GFX 1.4.3 deferred shadows - CASTER pass (vertex).
// The world meshes the game drew this frame (captured from rqDrawIndexed: buffers + ObjMatrix) are drawn again from the sun
// into a depth texture, once per cascade (near / far). Same technique as libZyZyGFX64's caster replay.
layout(location = 0) in vec3 Position;
layout(location = 1) in vec2 TexCoord0;
uniform highp mat4 uLightVP;     // world -> light clip of this cascade
uniform highp mat4 uObj;         // the draw's ObjMatrix (as the game set it)
uniform highp float uUvScale;    // 1/512 for the game's short texture coordinates, else 1
out highp vec2 vUV;
void main(){ vUV = TexCoord0 * uUvScale; gl_Position = uLightVP * (uObj * vec4(Position, 1.0)); }
