// [DynamicShadows] caster: writes the sun depth packed into RGB (24 bit). uAlpha > 0.5 = alpha-tested mesh (leaves, fences): cut-out texels cast nothing.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D uTex; uniform float uAlpha; varying vec2 vUV;
void main(){
  if (uAlpha > 0.5) { if (texture2D(uTex, vUV).a < 0.5) discard; }
  vec3 enc = fract(gl_FragCoord.z * vec3(1.0, 255.0, 65025.0));
  enc -= enc.yzz * vec3(1.0/255.0, 1.0/255.0, 0.0);
  gl_FragColor = vec4(enc, 1.0);
}
