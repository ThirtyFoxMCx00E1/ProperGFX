#version 300 es
// Proper GFX 1.4.3 deferred shadows - full-screen triangle (no vertex buffer: gl_VertexID 0..2)
out highp vec2 vUV;
void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); vUV = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
