// plain 4x downsample (source of the depth-of-field blur)
#pragma include "common.glsl"
uniform sampler2D uTex; uniform vec4 uRes;
void main(){
  vec2 t = uRes.zw;
  vec3 c = texture2D(uTex, vUV + t*vec2(-1.0,-1.0)).rgb + texture2D(uTex, vUV + t*vec2(1.0,-1.0)).rgb
         + texture2D(uTex, vUV + t*vec2(-1.0,1.0)).rgb  + texture2D(uTex, vUV + t*vec2(1.0,1.0)).rgb;
  gl_FragColor = vec4(c*0.25, 1.0);
}
