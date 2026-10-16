# Changelog

## 1.3.2
* Fix: game crash (SIGILL in `RQ_Command_rqBuildShader`) when the GPU driver (seen: Mali, realme, Android 16) rejects a patched shader. Patched pairs are test-compiled on the real driver first (hook on `ES2Shader::Build`) and step down: simplified shadow patch -> no shadow patch -> original shader. Unknown pairs carrying our shadow text (another mod rewrote the strings) are stripped.
* `files/ProperShader/propergfx_log.txt`: driver error text + full patched sources of the first failures, GL renderer/version.
* Menu: Shader self-test counters and a warning when the driver rejected shaders.
* Old shadow guard files deleted; guard renamed `shadow_guard_v132.txt`, clears after 600 replayed / 2400 rendered frames.
* Host test: self-test chain (healthy / N.L rejected / receiver rejected / unknown pair).

## 1.3.1
* Shadows: casters are remembered across frames (static meshes keep casting while out of view; in-view meshes the game stopped drawing are forgotten after 3 captures; changed buffers are dropped).
* Shadows: only draws into the main scene's render target cast (the game's ped-shadow / reflection passes are ignored).
* Menu + logcat: remembered / kept / ignored counters.
* Host test: caster memory, no ghost shadows, other-target filter.

## 1.3.0
**Dynamic shadows now actually reach the map** (library renamed to `libProperGFX32.so` / `libProperGFX64.so`)
* Root cause of "shadows don't work" in 1.2: the receiver code was only compiled into shaders that have normals AND a directional light. Buildings, roads and terrain use unlit prelit shaders, so they never received a shadow.
  The receiver now needs only the world position (unlit shaders get no N.L fade, lit shaders still use it). Water, the vehicle reflection sphere pass and skinned (ped) shaders are still skipped.
* Sun direction = the game's CTimeCycle sun vector (falls back to the light uniform). New `[DynamicShadows] SunSource = Auto|TimeCycle|Light`.
* Bias follows the sun height (texel * cot(elevation)) so flat ground does not stripe at a low sun and shadows still start at the object's base.
* Quality tiers: **Low** = shadows, bloom and sun rays off (fast and smooth); **Medium** / **High** = shadows, bloom and sun rays on. Picking Medium/High (or switching shadows on) clears shadow safe mode; the capture hook is always installed so this works live.
* Fixed: 1.3 first build read the 1.2 `shadow_guard.txt` and started in SAFE MODE; the old file is now deleted and the guard is `shadow_guard_v13.txt`.
* Shadow strength follows timecyc.dat's `Shdw` column (`TimecycStrength`), blended by hour and weather like the game does.
* Crash guard rewritten: clears after 600 replayed frames (or ~3600 rendered frames) instead of 900 successful replays; `CrashGuard = 0` disables it; menu "Retry shadows" button; hooks/receivers only skipped while safe mode is active.
* Uniforms are pushed into the currently bound shader at the end of every frame (no stale values on the most-used shader).
* Diagnostics: skip reasons per capture frame, receiver count, sun vector + source in the menu and in logcat (`shadows:` line).
* Menu: new FEATURE GUIDE section (FSR, Bloom, TAA & FXAA, Auto Exposure, AO, Shadows, SSR, Volumetric Headlights, Contact Shadows, LUT - what each is and what this build has). Floating button / title renamed to Proper GFX.
* AML id changed to `net.propergfx.mobile`; refuses to run next to 1.2.
* Host tests: unlit-shader receiver cases at 45/30/15 degrees, sphere/skinned/water skip rules.

## 1.2.0
**Dynamic sun shadows (buildings, trees, vehicles) — follows TimeSync / timesync.dat**
* New `[DynamicShadows]` feature (menu + ini). Every frame (or every N frames) the opaque world draws are captured with their world matrices,
  re-rendered depth-only from the sun into a shadow map (512/1024/2048 px, 25–160 m around you) and sampled by the game's own object shaders.
  Surfaces facing the sun stay lit, everything behind a house/tree/wall falls into shadow; alpha-cut foliage casts leaf-shaped shadows.
* The sun direction is the game's own `DirLightDirection`, so shadows move with the game clock. TimeSync / `timesync.dat` only sets the clock, so
  it works with it automatically; `SunSmoothing` removes the once-a-minute steps; shadows fade out when the sun is below `MinSunElevation` (night).
* Safety: shadow receiver code is skipped for skinned shaders (peds) and shaders that are near the varying limit; a **crash guard**
  (`files/ProperShader/shadow_guard.txt`) turns shadows off automatically if a launch did not survive 900 frames; `ShowDebug=1` overlays the shadow map.
* Cost control: Low/Medium/High set map size and update rate; the adaptive governor halves the update rate at level 2 and stops shadow rendering at level 3.

**Fix: Cutscene Controller (and other mods') buttons could stop responding**
* A touch whose RELEASE never arrived stayed "captured", so every later event of that finger id was swallowed — including the tap that reopens another mod's
  window. A new PUSH now always starts a fresh sequence, and a watchdog frees a capture that was silent for 2.5 s.
* The floating button's touch area is exactly the drawn button (+3 px, was +6) and it **collapses to a small dot after 4 s idle** (`[Button] AutoCollapse`),
  so it no longer sits on top of other mods' labels. A tap on the dot still opens the menu in one tap.

## 1.1.0
**Menu & configuration now follow the real Proper Shaders model** (the previous menu borrowed the Cutscene Controller layout):
* One section per feature with an **Enabled** switch (`[SimulateHDR]`, `[AutoExposure]`, `[Bloom]`, `[SunRays]`, `[HorizonFog]`, …) – the same
  structure the PC mod uses in `ProperShaders.ini` (e.g. `[SimulateHDR] Enabled`).
* **Separate Day / Night values** (`Exposure.Day`, `Exposure.Night`, …) blended by the game clock; `TimeOfDay = Auto|Day|Night` to preview.
* **Low / Medium / High** presets (like the PC mod) + a separate *Look* (Default, Cinematic, Vivid, Soft dream, Anime, Noir, Retro VHS).
* New plum/orange theme taken from the Proper Shaders logo. "Not available on mobile" feature list kept in the menu.
* `settings.ini` format changed (old flat file is ignored; defaults are written on first launch).
* Default floating-button position moved so it no longer sits on top of other mods' bottom-centre labels (still draggable).

**Optimisation**
* Final shader is compiled as a *variant per enabled-feature set* (LRU cache of 10): disabled effects cost nothing on the GPU.
* mediump colour math, `pow(x,2.2)` replaced by `x*x` / `sqrt`, highp only where needed (dither, trig).
* FXAA folded into the final pass when no CAS/outline/chromatic-aberration needs it (saves a full-res render target pass); FXAA early-out on flat areas.
* CAS: 5-tap cross variant (4 fetches instead of 8). Sun shafts 20 → 12 taps.
* Bloom blur at 1/8 resolution, one H+V iteration (≈8× less blur work than before).
* Auto-exposure 64 → 16 taps, refreshed every 3rd frame.
* Object-shader patch: fog uses a smoothstep blend instead of `pow()`; cel code is compiled in only when requested; fewer program-cache lookups.
* **Adaptive quality** governor (optional): steps heavy effects down when frame time exceeds the target, back up when there is headroom.

## 1.0.0
First release (post-processing, world-shader patches, draggable button + menu).
