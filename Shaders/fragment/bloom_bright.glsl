// [Bloom] 4x downsample + soft-knee bright pass  (uP.x = Threshold, uP.y = knee)
#pragma include "common.glsl"
uniform sampler2D uTex; uniform vec4 uRes; uniform vec4 uP;
void main(){
  vec2 t = uRes.zw;
  vec3 c = texture2D(uTex, vUV + t*vec2(-1.0,-1.0)).rgb + texture2D(uTex, vUV + t*vec2(1.0,-1.0)).rgb
         + texture2D(uTex, vUV + t*vec2(-1.0,1.0)).rgb  + texture2D(uTex, vUV + t*vec2(1.0,1.0)).rgb;
  c *= 0.25;
  float br = max(c.r, max(c.g, c.b));
  float knee = uP.y + 0.0001;
  float soft = clamp(br - uP.x + knee, 0.0, 2.0*knee);
  soft = soft*soft/(4.0*knee);
  float contrib = max(soft, br - uP.x) / max(br, 0.0001);
  gl_FragColor = vec4(c*contrib, 1.0);
}
