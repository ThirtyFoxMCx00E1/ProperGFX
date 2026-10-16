// FXAA 1.0 (T. Lottes, public domain) - shared by the stand-alone pass and the final composite
float fxLum(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }
vec3 fxaaSample(sampler2D tex, vec2 uv, vec2 t, float strength){
  vec3 rgbM  = texture2D(tex, uv).rgb;
  vec3 rgbNW = texture2D(tex, uv + vec2(-1.0,-1.0)*t).rgb;
  vec3 rgbNE = texture2D(tex, uv + vec2( 1.0,-1.0)*t).rgb;
  vec3 rgbSW = texture2D(tex, uv + vec2(-1.0, 1.0)*t).rgb;
  vec3 rgbSE = texture2D(tex, uv + vec2( 1.0, 1.0)*t).rgb;
  float lNW = fxLum(rgbNW), lNE = fxLum(rgbNE), lSW = fxLum(rgbSW), lSE = fxLum(rgbSE), lM = fxLum(rgbM);
  float lMin = min(lM, min(min(lNW,lNE), min(lSW,lSE)));
  float lMax = max(lM, max(max(lNW,lNE), max(lSW,lSE)));
  if (lMax - lMin < max(0.0312, lMax * 0.125)) return rgbM;     // flat area: no edge, skip the 4 extra fetches
  vec2 dir;
  dir.x = -((lNW + lNE) - (lSW + lSE));
  dir.y =  ((lNW + lSW) - (lNE + lSE));
  float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0/128.0);
  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = min(vec2(8.0), max(vec2(-8.0), dir * rcpDirMin)) * t;
  vec3 rgbA = 0.5 * (texture2D(tex, uv + dir*(1.0/3.0-0.5)).rgb + texture2D(tex, uv + dir*(2.0/3.0-0.5)).rgb);
  vec3 rgbB = rgbA*0.5 + 0.25*(texture2D(tex, uv + dir*-0.5).rgb + texture2D(tex, uv + dir*0.5).rgb);
  float lB = fxLum(rgbB);
  vec3 res = (lB < lMin || lB > lMax) ? rgbA : rgbB;
  return mix(rgbM, res, strength);
}
