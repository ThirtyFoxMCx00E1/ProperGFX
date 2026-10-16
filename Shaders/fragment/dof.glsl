// [DepthOfField] GTA SA Mobile has no readable depth buffer, so focus is a screen region:
//   Mode 0 = Tilt-shift  (sharp horizontal band at Focus position, blur above / below - miniature look)
//   Mode 1 = Center focus (sharp circle around the screen point (0.5, Focus), blur toward the edges - like a lens focused on what you look at)
// Underwater (UnderWaterness > 0) adds an all-over blur ([DepthOfField] UnderwaterBlur).
// uDof = 1/8-res blurred scene. uDofA = (mode, focus position, focus width, strength)  uDofB = (bokeh boost, radius, underwater blur, 0)
uniform sampler2D uDof;
uniform vec4 uDofA;
uniform vec4 uDofB;

float dofAmount(vec2 uv, vec2 res){
  float m;
  if (uDofA.x < 0.5) m = smoothstep(uDofA.z, uDofA.z + 0.28, abs(uv.y - uDofA.y));
  else {
    vec2 d = (uv - vec2(0.5, uDofA.y)) * vec2(res.x / res.y, 1.0);
    m = smoothstep(uDofA.z, uDofA.z + 0.45, length(d));
  }
  m *= uDofA.w;
  m = max(m, clamp(UnderWaterness, 0.0, 1.0) * uDofB.z);
  return clamp(m, 0.0, 1.0);
}

// 7-tap disc gather from the blurred copy; bright taps weigh more -> round "bokeh" highlights
vec3 dofBlur(vec2 uv, vec2 px, float m){
  vec2 r = px * 8.0 * uDofB.y * m;
  vec3 acc = vec3(0.0); float wsum = 0.0;
  for (int i = 0; i < 7; i++) {
    float a = float(i) * 0.8975979;                 // 2*pi/7
    vec2 o = (i == 0) ? vec2(0.0) : vec2(cos(a), sin(a)) * r;
    vec3 c = texture2D(uDof, uv + o).rgb;
    float l = lum3(c);
    float w = 1.0 + uDofB.x * l * l * 4.0;
    acc += c * w; wsum += w;
  }
  return acc / wsum;
}

vec3 applyDof(vec3 col, vec2 uv, vec2 px, vec2 res){
  float m = dofAmount(uv, res);
  if (m < 0.003) return col;
  return mix(col, dofBlur(uv, px, m), m);
}
