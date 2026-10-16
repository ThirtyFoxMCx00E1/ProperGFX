// 9-tap gaussian from 5 bilinear fetches (uDir.xy = UV-space step) - bloom and depth of field
#pragma include "common.glsl"
uniform sampler2D uTex; uniform vec4 uDir;
void main(){
  vec3 c = texture2D(uTex, vUV).rgb * 0.2270270270;
  vec2 o1 = uDir.xy * 1.3846153846;
  vec2 o2 = uDir.xy * 3.2307692308;
  c += (texture2D(uTex, vUV + o1).rgb + texture2D(uTex, vUV - o1).rgb) * 0.3162162162;
  c += (texture2D(uTex, vUV + o2).rgb + texture2D(uTex, vUV - o2).rgb) * 0.0702702703;
  gl_FragColor = vec4(c, 1.0);
}
