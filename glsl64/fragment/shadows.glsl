// [DynamicShadows] receiver - this text is INJECTED into the game's own object fragment shaders when the game builds them
// (buildings, roads, terrain, vehicles). Built into the .so. Sections:
//   //@@decl  -> inserted before main()
//   //@@apply -> inserted right before the game applies its fog ("fcolor" = the lit pixel, Out_FogAmt = the game's fog amount)
// Every patched shader is test-compiled on your GPU first; if this file breaks it, the mod falls back to a simpler patch / the
// game's original shader (no crash) and writes the driver error to files/ProperGFX/propergfx_log.txt.
// Uniforms set by the mod: PS_Shadow = (strength x fade, bias, PCF radius, 0), PS_ShadowMap = sun depth map (unit 3, RGB-packed 24-bit),
// PS_ShadowTint = (tint r, g, b, amount)  <- [DynamicShadows] Tint: colours the shadowed part (sky-blue ambient instead of plain black)
//@@decl
#ifdef GL_FRAGMENT_PRECISION_HIGH
varying highp vec4 Out_ShadowPos;
uniform highp vec4 PS_Shadow;
uniform mediump vec4 PS_ShadowTint;
uniform sampler2D PS_ShadowMap;
highp float ps_unpack(highp vec3 c) { return dot(c, vec3(1.0, 1.0/255.0, 1.0/65025.0)); }
highp float ps_tap(highp vec2 uv, highp float d) { return step(d, ps_unpack(texture2D(PS_ShadowMap, uv).rgb)); }
#endif
//@@apply
#ifdef GL_FRAGMENT_PRECISION_HIGH
if (PS_Shadow.x > 0.001) {
  highp vec3 ps_sp = Out_ShadowPos.xyz;
  highp vec2 ps_uv = ps_sp.xy * 0.5 + 0.5;
  highp float ps_d = ps_sp.z * 0.5 + 0.5 - PS_Shadow.y * (1.0 + (1.0 - Out_ShadowPos.w) * 1.5);
  highp float ps_lit = 1.0;
  if (ps_uv.x > 0.0 && ps_uv.x < 1.0 && ps_uv.y > 0.0 && ps_uv.y < 1.0 && ps_d < 1.0) {
    highp vec2 ps_o = vec2(PS_Shadow.z);
    ps_lit = (ps_tap(ps_uv + vec2(-ps_o.x, -ps_o.y), ps_d) + ps_tap(ps_uv + vec2(ps_o.x, -ps_o.y), ps_d)
            + ps_tap(ps_uv + vec2(-ps_o.x, ps_o.y), ps_d) + ps_tap(ps_uv + ps_o, ps_d)) * 0.25;
    highp float ps_e = max(abs(ps_sp.x), abs(ps_sp.y));
    ps_lit = mix(ps_lit, 1.0, smoothstep(0.82, 1.0, ps_e));
  }
  mediump float ps_amt = PS_Shadow.x * smoothstep(0.0, 0.35, Out_ShadowPos.w) * (1.0 - Out_FogAmt);
  mediump float ps_k = ps_amt * (1.0 - ps_lit);
  fcolor.xyz *= 1.0 - ps_k;
  fcolor.xyz *= mix(vec3(1.0), PS_ShadowTint.rgb, clamp(ps_k * 1.6, 0.0, 1.0) * PS_ShadowTint.w);
}
#endif
//@@end
