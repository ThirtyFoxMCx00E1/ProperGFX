// [LUT] generic colour lookup: strip (256x16, 1024x32, 4096x64) or square atlas (64x64, 512x512).
// uLutP = (cube size N, tiles per row, 1/width, 1/height). The loader stores green growing with v inside every tile
// and the blue tiles left-to-right, top-to-bottom - ReShade / Unity / PC Proper Shaders LUTs work as they are.
uniform sampler2D uLut;
uniform HP vec4 uLutP;
vec3 applyLut(vec3 c){
  HP vec3 cc = clamp(c, 0.0, 1.0);
  HP float n = uLutP.x; HP float tpr = uLutP.y;
  HP float b = cc.b * (n - 1.0); HP float s0 = floor(b); HP float s1 = min(s0 + 1.0, n - 1.0); HP float f = b - s0;
  HP vec2 inTile = vec2(cc.r * (n - 1.0) + 0.5, cc.g * (n - 1.0) + 0.5);
  HP float r0 = floor((s0 + 0.5) / tpr); HP float r1 = floor((s1 + 0.5) / tpr);
  HP vec2 t0 = vec2(s0 - r0 * tpr, r0) * n; HP vec2 t1 = vec2(s1 - r1 * tpr, r1) * n;
  return mix(texture2D(uLut, (t0 + inTile) * uLutP.zw).rgb, texture2D(uLut, (t1 + inTile) * uLutP.zw).rgb, f);
}
