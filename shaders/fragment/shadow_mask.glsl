#version 300 es
// Proper GFX 1.4.8 deferred (screen-space) sun shadows - MASK pass. Technique of libZyZyGFX64 (see the GLSL-Samples-source-mod repo):
//  * the game's scene target gets OUR depth texture instead of its depth renderbuffer (ps_scene.cpp), so after RenderScene the
//    depth of every pixel on screen is readable;
//  * each pixel's world position is rebuilt from that depth (inverse view-projection, relative to the camera for precision),
//    its normal from the neighbouring depths;
//  * the position is looked up in two sun shadow-map cascades (hardware 2x2 PCF, 3x3 grid) with normal-offset + slope bias;
//  * the result is a multiply factor (1 = lit) that fragment/shadow_apply.glsl multiplies into the scene colour.
// Every surface on screen receives - buildings, roads, terrain, vehicles AND pedestrians - without patching the game's shaders.
precision highp float;
precision highp int;
uniform highp sampler2D uDepth;          // unit 0: scene depth (full size)
uniform highp sampler2DShadow uMap0;     // unit 1: near cascade (compare sampler)
uniform highp sampler2DShadow uMap1;     // unit 2: far cascade
uniform highp mat4 uIVP;                 // ndc -> world - camera
uniform highp mat4 uL0;                  // (world - camera) -> near light clip
uniform highp mat4 uL1;                  // (world - camera) -> far light clip
uniform highp vec4 uP;                   // x mask->depth pixel scale, y camera height above the sea (world z, 0 = unknown), z softness, w bias multiplier
uniform highp vec4 uSun;                 // xyz direction TOWARD the sun, w light depth units per metre
uniform highp vec4 uTex;                 // texel size in metres near, far; 1 / map size near, far
uniform highp vec2 uFog;                 // the game's distance fog: start (m), 1 / (end - start); 0,0 = unknown
uniform mediump vec4 uShade;             // x strength (0..1, sun fade + timecyc applied), y tint amount, z far-cascade fade start (0..1 of its square)
uniform mediump vec3 uTint;              // colour of the shadowed part (Tint R/G/B)
// SAShaderL-compatible globals (set by Proper GFX like for every other .glsl)
uniform int ShaderFlags;                 // 0x20000000 | 0x100 = deferred shadow mask pass (set, not needed by this shader)
uniform vec3 SunVector;
uniform float UnderWaterness;
out vec4 oColor;

ivec2 gHi;
vec2 gInv;
highp vec3 WorldAt(ivec2 q){
  q = clamp(q, ivec2(0), gHi);
  highp float d = texelFetch(uDepth, q, 0).r;
  highp vec4 p = uIVP * vec4((vec2(q) + 0.5) * gInv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
// one cascade: 3x3 grid of hardware PCF taps, centre weighted (= smooth ~4-texel edge, the same every frame - no noise)
float Cascade(highp sampler2DShadow m, highp mat4 L, highp vec3 C, highp vec3 N, float texelM, float inv, float ndl, out float edge){
  float tanA = sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1);                  // slope: low sun on a wall = more bias
  highp vec3 P = C + N * (texelM * 1.2 * uP.w) + uSun.xyz * 0.02;                // normal offset (camera independent)
  float bias = (0.03 + texelM * 1.5 * min(tanA, 10.0)) * uP.w * uSun.w;           // metres -> light depth
  highp vec4 lp = L * vec4(P, 1.0);
  highp vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;
  edge = max(abs(sc.x - 0.5), abs(sc.y - 0.5)) * 2.0;
  if (edge >= 1.0 || sc.z >= 1.0) { edge = 2.0; return 1.0; }
  float r = 1.5 * uP.z * inv, v = 0.0;
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
    v += texture(m, vec3(sc.xy + vec2(float(x), float(y)) * r, sc.z - bias)) * ((x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0));
  return v * (1.0 / 16.0);
}
void main(){
  gHi = textureSize(uDepth, 0) - 1;
  gInv = 1.0 / vec2(gHi + 1);
  ivec2 ip = ivec2(gl_FragCoord.xy * uP.x);
  highp float d = texelFetch(uDepth, clamp(ip, ivec2(0), gHi), 0).r;
  oColor = vec4(1.0);
  if (d >= 0.99999 || uShade.x <= 0.001) return;                                   // sky / off
  highp vec3 C = WorldAt(ip);
  // water writes no depth in SA: below the sea surface the depth is the SEA FLOOR. Move the point up the view ray to the
  // surface (z = 0) so piers / palms do not cast onto the floor through the water; half strength on water.
  bool water = uP.y > 0.0 && C.z + uP.y < -0.5;                                  // > 0.5 m below sea level (beaches / docks at z ~ 0 are land)
  highp vec3 N = vec3(0.0, 0.0, 1.0);
  if (water) C *= uP.y / -C.z;
  else {
    // normal from a 2-pixel baseline, per axis the side with the smaller depth step (no flipped normals on silhouettes)
    highp vec3 R = WorldAt(ip + ivec2(2, 0)), Lf = WorldAt(ip - ivec2(2, 0)), U = WorldAt(ip + ivec2(0, 2)), D = WorldAt(ip - ivec2(0, 2));
    highp float cl = length(C);
    highp vec3 ex = abs(length(R) - cl) < abs(cl - length(Lf)) ? R - C : C - Lf;
    highp vec3 ey = abs(length(U) - cl) < abs(cl - length(D)) ? U - C : C - D;
    N = normalize(cross(ex, ey));
    if (!(dot(N, N) > 0.5)) return;
    if (dot(N, -C) < 0.0) N = -N;
  }
  float ndl = dot(N, uSun.xyz);
  float dist = length(C);
  float e0 = 0.0, e1 = 2.0;
  float v = Cascade(uMap0, uL0, C, N, uTex.x, uTex.z, ndl, e0);
  if (e0 > 0.8) {                                                                   // near border: blend into the far cascade
    float v1 = Cascade(uMap1, uL1, C, N, uTex.y, uTex.w, ndl, e1);
    v1 = mix(v1, 1.0, smoothstep(uShade.z, 1.0, e1));                               // far border: fade out
    v = e0 >= 1.0 ? v1 : mix(v, v1, smoothstep(0.8, 1.0, e0));
  }
  // a surface that faces away from the sun IS in shadow (single-sided facades have no back side in the map),
  // only where the cascades reach; back faces at half strength (the game's lighting already darkened them)
  float cov = e0 < 1.0 ? 1.0 : (e1 >= 1.0 ? 0.0 : 1.0 - smoothstep(uShade.z, 1.0, e1));
  // 1.4.8: only with the sun reasonably high. With a low sun (morning / evening) every gentle slope turned away from it went
  // fully dark - whole hillsides and roads in shadow with no object casting it. Low sun: the map's real shadows only.
  v = mix(v, 0.0, (1.0 - smoothstep(-0.25, 0.0, ndl)) * cov * smoothstep(0.15, 0.35, uSun.z));
  v = mix(1.0, v, mix(0.5, 1.0, smoothstep(-0.25, 0.25, ndl)));
  if (water) v = mix(1.0, v, 0.5);
  if (uFog.y > 0.0) v = mix(v, 1.0, clamp((dist - uFog.x) * uFog.y, 0.0, 1.0)); // the game already fogged this pixel
  v = mix(v, 1.0, clamp(UnderWaterness, 0.0, 1.0));                                // camera under water: no sun shadows
  float shade = (1.0 - v) * uShade.x;
  vec3 shadowCol = uTint * uShade.y;                                                // tint 0 = plain darkening
  oColor = vec4(mix(vec3(1.0), shadowCol, shade), 1.0);
}
