// Proper GFX final composite. Compiled as a VARIANT per enabled-feature set: the mod puts "#define FX_xxx 1" lines and
// "#define TM_MODE n" in front of this file, so every effect that is switched off is removed at compile time and costs nothing.
// FX_AA FX_CA FX_CAS FX_DOF FX_OUTLINE FX_CEL FX_AUTOEXP FX_BLOOM FX_RAYS FX_FLARE FX_GLOW FX_GOLDEN FX_LUT FX_UNDER FX_VHS FX_VIGNETTE FX_GRAIN FX_DITHER FX_GRADE FX_GAMMA
#pragma include "common.glsl"

uniform sampler2D uScene; uniform sampler2D uBloom; uniform sampler2D uLum;
uniform vec4 uRes;
uniform HP vec4 uA;   // time, ev, hdrK, (unused)
uniform vec4 uB;      // contrast, saturation, vibrance, gamma
uniform vec4 uC;      // temperature, tint, lift, gain
uniform vec4 uD;      // bloom, rays, flare, (unused)
uniform vec4 uE;      // vignette, vigSize, grain, chromatic
uniform vec4 uF;      // cas, (unused), underwater, lut amount
uniform vec4 uG;      // celAmt, celLevels, outline, outlineThr
uniform vec4 uH;      // vhs, (unused), (unused), (unused)
uniform vec4 uK;      // autoBias, autoRange, tonemapNorm, frame
uniform vec4 uGS;     // grade shadow tint
uniform vec4 uGH;     // grade highlight tint, w = fxaa strength

vec3 toLin(vec3 c){ return c*c; }                 // gamma 2.0 approximation: 1 MUL instead of pow()
vec3 toGam(vec3 c){ return sqrt(max(c, vec3(0.0))); }
HP float ign(HP vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
HP float hash21(HP vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }

#pragma include "tonemap.glsl"
#ifdef FX_AA
#pragma include "fxaa.glsl"
#endif
#ifdef FX_LUT
#pragma include "lut.glsl"
#endif
#if defined(FX_RAYS) || defined(FX_FLARE) || defined(FX_GLOW) || defined(FX_GOLDEN)
#pragma include "sunvector.glsl"
#endif
#ifdef FX_DOF
#pragma include "dof.glsl"
#endif

void main(){
  vec2 uv = vUV;
  vec2 px = uRes.zw;
  vec2 suv = uv;
#if defined(FX_UNDER) || defined(FX_VHS)
  HP float t = uA.x;
#endif
#ifdef FX_UNDER
  suv += vec2(sin(uv.y*16.0 + t*1.9), cos(uv.x*12.0 + t*1.4)) * 0.0045 * uF.z;
#endif
#ifdef FX_VHS
  suv.x += sin(uv.y*uRes.y*0.35 + t*14.0) * 0.0006 * uH.x;
  float bandY = fract(t*0.11);
  float inBand = step(abs(uv.y - bandY), 0.035);
  suv.x += inBand * (hash21(vec2(floor(uv.y*uRes.y*0.5), floor(t*24.0))) - 0.5) * 0.02 * uH.x;
#endif

  vec3 col;
#ifdef FX_CA
  {
    float ca = uE.w * 0.012;
#ifdef FX_VHS
    ca += uH.x * 0.0035;
#endif
    vec2 d = (suv - 0.5) * ca;
    col = vec3(texture2D(uScene, suv + d).r, texture2D(uScene, suv).g, texture2D(uScene, suv - d).b);
  }
#elif defined(FX_AA)
  col = fxaaSample(uScene, suv, px, uGH.w);
#else
  col = texture2D(uScene, suv).rgb;
#endif

#ifdef FX_CAS   // AMD FidelityFX CAS, 5-tap cross variant
  {
    vec3 b = texture2D(uScene, suv + px*vec2( 0.0,-1.0)).rgb;
    vec3 d = texture2D(uScene, suv + px*vec2(-1.0, 0.0)).rgb;
    vec3 e = col;
    vec3 f = texture2D(uScene, suv + px*vec2( 1.0, 0.0)).rgb;
    vec3 h = texture2D(uScene, suv + px*vec2( 0.0, 1.0)).rgb;
    vec3 mn = min(min(min(d,e),min(f,b)),h);
    vec3 mx = max(max(max(d,e),max(f,b)),h);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, clamp(uF.x, 0.0, 1.0));
    vec3 w = amp * peak;
    col = clamp(((b + d + f + h) * w + e) / (1.0 + 4.0*w), 0.0, 1.0);
  }
#endif

#ifdef FX_DOF
  col = applyDof(col, suv, px, uRes.xy);
#endif

#ifdef FX_OUTLINE   // screen-space outlines (Sobel on luma)
  {
    float tl = lum3(texture2D(uScene, suv + px*vec2(-1.0,-1.0)).rgb);
    float tt = lum3(texture2D(uScene, suv + px*vec2( 0.0,-1.0)).rgb);
    float tr = lum3(texture2D(uScene, suv + px*vec2( 1.0,-1.0)).rgb);
    float ml = lum3(texture2D(uScene, suv + px*vec2(-1.0, 0.0)).rgb);
    float mr = lum3(texture2D(uScene, suv + px*vec2( 1.0, 0.0)).rgb);
    float bl = lum3(texture2D(uScene, suv + px*vec2(-1.0, 1.0)).rgb);
    float bb = lum3(texture2D(uScene, suv + px*vec2( 0.0, 1.0)).rgb);
    float br = lum3(texture2D(uScene, suv + px*vec2( 1.0, 1.0)).rgb);
    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tt - tr + bl + 2.0*bb + br;
    float edge = smoothstep(uG.w, uG.w + 0.18, length(vec2(gx, gy)));
    col = mix(col, vec3(0.02,0.015,0.03), edge * uG.z);
  }
#endif

#ifdef FX_CEL
  {
    float l = lum3(col);
    float q = floor(l*uG.y + 0.5) / uG.y;
    col = mix(col, clamp(col * ((q + 0.04) / (l + 0.04)), 0.0, 1.0), uG.x);
  }
#endif

  // ---- pseudo-HDR, exposure, bloom, sun, tone mapping ----
  vec3 lin = toLin(col);
#if TM_MODE > 0
  lin *= 1.0 + uA.z * lin * lin;
#endif
  float ev = uA.y;
#ifdef FX_AUTOEXP
  {
    float avg = dot(texture2D(uLum, vec2(0.5)).rg, vec2(1.0, 1.0/255.0));
    ev += clamp(log2(0.18 / max(avg, 0.004)) * 0.6, -uK.y, uK.y) + uK.x;
  }
#endif
  lin *= exp2(ev);
#ifdef FX_BLOOM
  lin += toLin(texture2D(uBloom, uv).rgb) * (uD.x * 1.6);
#endif
#ifdef FX_RAYS
  lin += toLin(sunShafts(uv)) * (uD.y * 2.2);
#endif
#ifdef FX_GLOW
  lin += toLin(sunGlow(uv, uRes.xy));
#endif
#ifdef FX_FLARE
  lin += toLin(flare(uv)) * (uD.z * 1.4);
#endif
#if TM_MODE > 0
  lin = tonemap(lin) * uK.z;
#else
  lin = clamp(lin, 0.0, 1.0);
#endif
  col = toGam(clamp(lin, 0.0, 1.0));
#ifdef FX_GOLDEN
  col = goldenHour(col);
#endif

  // ---- grading ----
#ifdef FX_GRADE
  {
    float l0 = lum3(col);
    col += uGS.rgb * (1.0 - l0)*(1.0 - l0) + uGH.rgb * l0*l0;
    col *= vec3(1.0 + 0.10*uC.x, 1.0 - 0.08*uC.y, 1.0 - 0.10*uC.x);
    float l1 = lum3(col);
    col = mix(vec3(l1), col, uB.y);
    float mxc = max(col.r, max(col.g, col.b)); float mnc = min(col.r, min(col.g, col.b));
    col = mix(vec3(l1), col, 1.0 + uB.z * (1.0 - (mxc - mnc)));
    col = (col - 0.5) * uB.x + 0.5;
    col = col * uC.w + uC.z;
#ifdef FX_GAMMA
    col = pow(max(col, vec3(0.0)), vec3(1.0/uB.w));
#endif
  }
#endif
#ifdef FX_LUT
  col = mix(col, applyLut(col), uF.w);
#endif

#ifdef FX_UNDER   // underwater tint + caustic shimmer
  {
    vec2 cp = uv*vec2(uRes.x/uRes.y, 1.0)*7.0;
    float cs = sin(cp.x + t*0.9 + sin(cp.y*1.3 + t*0.7)) * sin(cp.y*1.1 - t*0.8 + sin(cp.x*0.9 + t*0.5));
    cs = clamp(cs*0.5 + 0.5, 0.0, 1.0); cs *= cs; cs = cs*cs*cs;
    vec3 wc = col * vec3(0.55, 0.85, 1.0) + vec3(0.0, 0.025, 0.05) + vec3(0.25,0.4,0.45) * cs * 0.18 * lum3(col);
    col = mix(col, wc, clamp(uF.z, 0.0, 1.0));
  }
#endif

#ifdef FX_VHS     // scanlines + desaturated bleed + noise
  {
    float sl = 0.5 + 0.5*sin(uv.y*uRes.y*3.14159);
    col *= 1.0 - 0.16*uH.x*(1.0 - sl);
    col = mix(col, vec3(lum3(col)), 0.2*uH.x);
    col += (hash21(uv*uRes.xy*0.5 + t) - 0.5) * 0.08 * uH.x;
  }
#endif

#ifdef FX_VIGNETTE
  {
    float dv = length((uv - 0.5) * vec2(uRes.x/uRes.y, 1.0));
    col *= 1.0 - smoothstep(uE.y, uE.y + 0.8, dv) * uE.x;
  }
#endif
#ifdef FX_GRAIN
  {
    float gn = hash21(gl_FragCoord.xy + vec2(uK.w*7.13, uK.w*3.71)) - 0.5;
    col += gn * uE.z * 0.12 * (1.0 - abs(lum3(col)*2.0 - 1.0)*0.6);
  }
#endif
#ifdef FX_DITHER   // triangular dither: removes banding in dark gradients and the sky
  {
    HP float n1 = ign(gl_FragCoord.xy + uK.w*vec2(5.588238));
    HP float n2 = ign(gl_FragCoord.yx*1.37 + uK.w*vec2(19.19));
    col += (n1 + n2 - 1.0) * (1.0/255.0);
  }
#endif
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
