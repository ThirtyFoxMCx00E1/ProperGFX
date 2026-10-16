// [AutoExposure] GPU-only: 16-tap log-average luma, smoothed over time in a 1x1 RGBA8 target (16-bit packed in RG)
#pragma include "common.glsl"
uniform sampler2D uTex; uniform sampler2D uPrev; uniform vec4 uL; // x adapt, y reset
void main(){
  float s = 0.0;
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 4; i++) {
      vec2 p = vec2(0.1 + 0.8*(float(i)+0.5)/4.0, 0.1 + 0.8*(float(j)+0.5)/4.0);
      vec3 c = texture2D(uTex, p).rgb;
      s += log(dot(c*c, vec3(0.2126,0.7152,0.0722)) + 0.002);
    }
  }
  float cur = exp(s*(1.0/16.0));
  vec4 pk = texture2D(uPrev, vec2(0.5));
  float prev = dot(pk.rg, vec2(1.0, 1.0/255.0));
  float v = mix(prev, cur, uL.x);
  if (uL.y > 0.5) v = cur;
  v = clamp(v, 0.0, 0.9999);
  vec2 enc = vec2(v, fract(v*255.0));
  enc.x -= enc.y/255.0;
  gl_FragColor = vec4(enc, 0.0, 1.0);
}
