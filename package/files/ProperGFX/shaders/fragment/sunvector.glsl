// [SunRays] [LensFlare] [SunVector] - everything that follows the game's sun (SunVector = CTimeCycle::m_VectorToSun).
// uSun = (sun screen x, y, visibility, ray decay)    uSV = (glow intensity, glow size, golden hour, flare intensity)
// uBloom = the 1/8-res bright-pass texture (only bright pixels: sky around the sun, lights)
uniform vec4 uSun;
uniform vec4 uSV;

// god rays: march from the pixel toward the sun through the bright pass
vec3 sunShafts(vec2 uv){
  vec2 stepv = (uSun.xy - uv) * (0.9/12.0);
  vec2 p = uv; float w = 1.0; vec3 acc = vec3(0.0);
  for (int i = 0; i < 12; i++) { p += stepv; acc += texture2D(uBloom, p).rgb * w; w *= uSun.w; }
  float fall = clamp(1.4 - length(uSun.xy - uv), 0.0, 1.0);
  return acc * (1.0/12.0) * uSun.z * fall;
}

// sun colour from its height: white-yellow at noon, orange at sunset / sunrise
vec3 sunColor(){
  float e = clamp(SunVector.z, 0.0, 1.0);
  return mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.93, 0.80), smoothstep(0.0, 0.45, e));
}

// soft glow disc around the sun (aspect-correct)
vec3 sunGlow(vec2 uv, vec2 res){
  vec2 d = (uv - uSun.xy) * vec2(res.x / res.y, 1.0);
  float r = length(d) / max(uSV.y, 0.02);
  float g = 1.0 / (1.0 + r*r*6.0) - 0.02;
  return sunColor() * max(g, 0.0) * uSun.z * uSV.x;
}

// golden hour: warm the whole picture while the sun is low (but above the horizon). Night = nothing.
vec3 goldenHour(vec3 col){
  float e = SunVector.z;
  float k = smoothstep(-0.06, 0.04, e) * (1.0 - smoothstep(0.10, 0.38, e)) * uSV.z;
  vec3 warm = col * vec3(1.10, 0.98, 0.84) + vec3(0.025, 0.010, 0.0);
  return mix(col, warm, clamp(k, 0.0, 1.0));
}

// lens flare ghosts + halo (screen-space, from the bright pass)
vec3 flare(vec2 uv){
  vec2 gv = (vec2(0.5) - uv) * 0.45;
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    vec2 o = fract(uv + gv * float(i + 1));
    float wgt = pow(clamp(1.0 - length(vec2(0.5) - o)/0.7071, 0.0, 1.0), 6.0);
    vec3 tintc = vec3(1.0 - 0.2*float(i), 0.75 + 0.1*float(i), 0.6 + 0.2*float(i));
    acc += texture2D(uBloom, o).rgb * wgt * tintc;
  }
  vec2 hv = normalize(gv + vec2(0.0001)) * 0.38;
  float hw = pow(clamp(1.0 - length(vec2(0.5) - (uv + hv))/0.7071, 0.0, 1.0), 4.0);
  acc += texture2D(uBloom, uv + hv).rgb * hw;
  return acc;
}
