// [GLSL] CustomPass = 1 -> this pass runs LAST, on the finished Proper GFX picture (menu > GLSL SHADERS > Custom pass).
// Write your own effect here. Everything from common.glsl is available (SunVector, Time, GameTimeSeconds, UnderWaterness,
// RoadsWetness, FarClipDist, EntityModel, PGFX_TimeSec, PGFX_Sun) plus the 8 menu sliders PGFX_User0 / PGFX_User1.
// uTex = the picture, uRes = (width, height, 1/width, 1/height).
//
// Example below = "Rain & night mood": slider 1 = wet-road blue tint, slider 2 = night darkening, slider 3 = contrast, slider 4 = warm/cold.
#pragma include "common.glsl"
uniform sampler2D uTex; uniform vec4 uRes;

void main(){
  vec3 c = texture2D(uTex, vUV).rgb;
  // 1) wet roads: a slightly cooler, darker picture while it rains (RoadsWetness = CWeather::WetRoads)
  c = mix(c, c * vec3(0.90, 0.96, 1.06), clamp(RoadsWetness, 0.0, 1.0) * PGFX_User0.x);
  // 2) night: game clock 22:00 - 05:00 (GameTimeSeconds = hours*60 + minutes)
  float gt = float(GameTimeSeconds);
  float night = 1.0 - smoothstep(300.0, 390.0, gt) * (1.0 - smoothstep(1260.0, 1350.0, gt));
  c *= 1.0 - 0.25 * night * PGFX_User0.y;
  // 3) contrast around mid grey
  c = (c - 0.5) * (1.0 + PGFX_User0.z) + 0.5;
  // 4) temperature: -1 cold .. +1 warm
  c *= vec3(1.0 + 0.08 * PGFX_User0.w, 1.0, 1.0 - 0.08 * PGFX_User0.w);
  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
