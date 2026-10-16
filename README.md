# Proper GFX Mobile 1.4.1 (AML mod for GTA San Andreas Android)

| Build | Game | ABI | File | Source tree | Shaders |
|---|---|---|---|---|---|
| 32-bit | GTA SA **2.00** | armeabi-v7a | `libProperGFX32.so` | `src32/` | C++/H only (AML + SAUtils), built into the `.so` |
| 64-bit | GTA SA **2.10** | arm64-v8a | `libProperGFX64.so` | `src64/` + `glsl64/{fragment,vertex}` | C++/H + **GLSL**, compiled into the `.so` |

For **AML 1.4.0** with **FLA 7.5** (no address patches, everything is hooked by exported symbol name). Uses [SAUtils](https://github.com/ThirtyFoxMCx00E1/SAUtils) when installed (optional).
Formerly *Proper Shader Mobile* (`libProperShader32/64.so`).

## Install
1. Copy `libProperGFX32.so` (GTA SA 2.00) **or** `libProperGFX64.so` (GTA SA 2.10) into the AML `mods` folder.
2. Optional: copy the `files/ProperGFX` folder from the zip into `Android/data/com.rockstargames.gtasa/files/` (example LUTs only - the shaders are inside the `.so`).
3. Start the game. Everything lives in `files/ProperGFX/`; settings are `files/ProperGFX/TYFMC.ProperGFX.ini`.

Upgrading from 1.3: the old `files/ProperShader/` folder is renamed to `files/ProperGFX/` and `settings.ini` to `TYFMC.ProperGFX.ini` automatically (your settings are kept).

## What's new in 1.4.1
* **64-bit: all `.glsl` shaders are inside `libProperGFX64.so`.** They live in the source tree (`glsl64/fragment`, `glsl64/vertex`, like SAShaderL's `GTASA/files/shaders` layout) and `build.sh` compiles them into the binary. Nothing is read from or written to the phone's files folder any more; the `files/ProperGFX/shaders` folder 1.4.0 created is removed automatically. (1.4.0 loaded the copies from that folder.)
* **Floating button (64-bit):** Opacity below 0.06 is treated as 0 and nothing is drawn - an old `Opacity = 0.05` from the 32-bit range (which still drew a faint label) now becomes 0, and the slider snaps to "0 (hidden)".
* **Log fix (both):** shader / driver errors from every part of the mod now reach `files/ProperGFX/propergfx_log.txt` (before, only messages from main.cpp did).
* Verified on a PC GLES2 driver (Mesa): the full 64-bit post-process chain with every effect on, and the shadow tests (casting, receiving, unlit buildings/roads, time of day) with the built-in `shadows.glsl`.

## What's new in 1.4.0
* **Two separate source trees.** `src32/` (32-bit, C++/H only) and `src64/` (64-bit, C++/H + GLSL). Changes for one build no longer touch the other.
* **64-bit: GLSL shaders** (same idea and layout as [SAShaderL](https://github.com/ThirtyFoxMCx00E1/SAShaderL)): every post-process pass, the shadow caster and the shadow receiver code are `.glsl` files (1.4.1: compiled into the `.so`).
* **SAShaderL-compatible uniforms** in every `.glsl` (and in game shaders that declare them): `ShaderFlags`, `SunVector`, `Time`, `GameTimeSeconds`, `UnderWaterness`, `RoadsWetness`, `FarClipDist`, `EntityModel`, plus `PGFX_TimeSec`, `PGFX_Sun`, `PGFX_User0/1` (8 menu sliders).
* **New 64-bit GLSL effects**, each with menu + ini settings: `shadows.glsl` (receiver, new shadow **Tint**), `sunvector.glsl` (**sun glow** + **golden hour** from the sun vector, plus the sun rays / lens flare), `dof.glsl` (**Depth of Field** modes TiltShift / CenterFocus, **bokeh**, **underwater blur**), `custom.glsl` (last pass "rain & night mood" with 4 sliders), `tonemap.glsl`, `lut.glsl`, `fxaa.glsl`, `bloom_bright.glsl`, `blur_gauss.glsl`, `autoexposure.glsl`, `final.glsl`.
* **Floating button opacity 0 = fully invisible (64-bit only).** Label, fill and outline all follow the opacity; at 0 nothing is drawn (it still works where it is, and the SAUtils Mods menu entry opens the menu too). The 32-bit build keeps the old minimum of 0.05.
* **Folder / ini renamed:** `files/ProperShader` -> `files/ProperGFX`, `settings.ini` -> `TYFMC.ProperGFX.ini` (both builds, automatic migration).
* **LUT fixed** (both builds). Why it did nothing before: the file name was case-sensitive (`Lut.bmp` was not found), only plain 24-bit BMP and uncompressed TGA loaded (BMP with BITFIELDS / 32-bit / top-down and RLE TGA failed silently), the LUT amount defaulted to 0 and a changed file was never re-read. Now: PNG / BMP / TGA / JPG (stb_image), any size 16^3 / 32^3 / 64^3 as strip or square atlas, upside-down LUTs auto-detected, case-insensitive lookup in `files/ProperGFX/` and `files/ProperGFX/luts/`, `[LUT] Enabled / Amount (1.0) / File (auto = lut.*)`, live reload when the file changes, a LUT picker and status line in the menu. Example LUTs: `luts/warm_sunset.bmp`, `luts/sepia.png`, `luts/cinematic.tga`.
* **SAUtils** (both builds): *Mods > Proper GFX - open menu* and *Proper GFX - floating button On/Off*.

## The 64-bit shaders (glsl64/)
* Edit `glsl64/fragment/*.glsl` / `glsl64/vertex/*.glsl` in the source, then run `./build.sh` - `tools/glsl2h.py` embeds them into `src64/ps_glsl_embedded.h` and they are compiled into `libProperGFX64.so`.
* `fragment/common.glsl` is included by every post-process pass (`#pragma include "common.glsl"`, each file at most once).
* `fragment/final.glsl` is compiled once per set of enabled features (`#define FX_BLOOM 1`, ..., `#define TM_MODE n` in front), so a disabled effect costs nothing.
* `fragment/shadows.glsl` / `vertex/shadows.glsl` are injected into the game's own object shaders (sections `//@@decl`, `//@@apply`, `//@@assign`, `//@@nl`) and test-compiled on the driver first.
* `tools/glsl_validate.py <glslangValidator>` checks every pass and 160 `final.glsl` variants as GLSL ES 1.00.

## Build
`./build.sh /path/to/android-ndk` (r24+) -> `dist/libProperGFX32.so` + `dist/libProperGFX64.so`. It regenerates `src64/ps_glsl_embedded.h` from `glsl64/` first.

## Quality tiers (menu > PRESET)
| Tier | For | Shadows | Bloom | Sun rays | Other |
|---|---|---|---|---|---|
| **Low** | low-end phones - fast and smooth | **off** | off | off | FXAA light, no Auto Exposure / CAS / vignette |
| **Medium** | mid-range phones | **on** (1024 px, 80 m, every 4th frame) | on | on | Auto Exposure, CAS, vignette |
| **High** | flagship phones (Galaxy S Ultra class) | **on** (2048 px, 100 m, every 2nd frame) | on | on | + lens flare, bigger bloom |

Picking Medium or High turns shadows on and clears shadow safe mode; Low turns them off. Shadow receivers are compiled into the game's shaders when the game starts, so if you launch on Low and then pick Medium/High, restart the game once (the menu tells you).

## What's new in 1.3.2 - the crash is fixed (thanks for the crash log)
The AML crash log from a realme / Mali phone showed `SIGILL` in `RQ_Command_rqBuildShader`: the game builds every shader on the render thread and executes a trap instruction when a shader fails to compile or link (it retries once with a precision tweak). A patched shader pair was rejected by the Mali driver, the game died, and the crash guard then (correctly) put shadows in SAFE MODE.
* **Driver self-test.** Every patched shader pair is now compiled and linked once on the real driver *before* the game builds it (hook on `ES2Shader::Build`). If the driver rejects it, the mod steps down: simpler shadow patch (no N.L) -> cel/wet patch without shadows -> the game's own shader. A rejected shader can no longer crash the game. If another shader mod rewrote the strings after ours, our shadow text is recognised and stripped.
* **Log file.** The first failures are written with the driver's error message and the full patched source to `files/ProperGFX/propergfx_log.txt` (also logcat `ProperShader`). The menu shows `Shader self-test: N tested, N ok, N simplified, ...`. If shadows still do not show, **send me that file**: it contains exactly what the driver complained about.
* Old guard files (1.3.0 / 1.3.1) are deleted at start - they were tripped by this crash - and the guard is now `shadow_guard_v132.txt`.

## What's new in 1.3.1 (shadow fix build)
* **Shadow casters are remembered across frames.** The game only draws what is in view, so v1.3.0 lost the shadow of a building the moment you turned away from it (sun behind you = almost no shadows in front of you). A mesh seen at the same place in two capture frames is treated as static and keeps casting for up to 3 minutes while it is out of view. If it should be in view but the game stopped drawing it (moved away, removed) it is forgotten after 3 captures, so there are no ghost shadows; buffers that changed size are dropped.
* **Only the main scene counts.** The game also renders its own passes (ped shadow, reflections) with the same shaders; draws are now grouped by render target and only the one with the most world draws casts. The menu shows how many were ignored.
* Menu / logcat show remembered casters, kept-out-of-view casters and ignored draws.

## What's new in 1.3 - dynamic shadows fixed

**Why 1.2 shadows did not show.** The shadow *map* was fine, but the *receiver* (the code that darkens a pixel that is in shadow) was only added to shaders that have
normals **and** the directional light. Most of GTA SA's map - buildings, roads, terrain - uses **unlit prelit shaders** (vertex colours, no normals, no light), so
those shaders were skipped and nearly nothing could receive a shadow. Only a few lit objects (some props, vehicles) did.

**Fixes in 1.3**
* Shadow receivers now work on **unlit prelit shaders** (they only need the world position), and still use N.L on lit shaders. Water, the vehicle reflection pass and skinned peds are skipped as before.
* **Shadow strength follows `timecyc.dat`** (`TimecycStrength = 1`): the game's own `CTimeCycle::m_nShadowStrength` table (the `Shdw` column) is blended by hour and weather exactly like the game does, bounded to 0.64-1.0x so it can only soften, never remove, shadows. The menu shows the raw value.
* Sun direction now comes from the game's **time-cycle sun vector** (`CTimeCycle::m_VectorToSun`, what the sun disc uses) with the light uniform as fallback (`SunSource = Auto | TimeCycle | Light`). It still follows the game clock / TimeSync.
* **Slope-aware bias** that follows the sun height (flat ground at a low sun no longer stripes) - needed because unlit shaders have no normal to fade by.
* **Safe mode no longer carries over from 1.2.** 1.2 left `shadow_guard.txt` behind on almost every launch and 1.3 initially read the same file, so it could start in SAFE MODE (Casters 0, 'shadows are off'). 1.3 deletes the old file and uses `shadow_guard_v13.txt`.
* **Crash guard fixed.** 1.2 only cleared its guard file after 900 *successful* replays; quitting early, or any setup where nothing was replayed, left the file behind and silently switched shadows off for every later launch. Now it clears after 600 replayed frames (or ~1 minute of rendering), can be disabled (`CrashGuard = 0`), and the menu shows a **Retry shadows** button when safe mode is active.
* New uniforms are pushed into the shader that is already bound at the end of each frame (1.2 could leave the most-used shader on stale values until the game happened to re-select it).
* **Diagnostics:** the menu (DynamicShadows) and `adb logcat -s ProperShader` now show captured / rendered / skipped casters *with the reason*, how many shader variants carry the receiver, the sun vector and where it came from. If shadows ever look wrong, a screenshot of that block tells what is happening.

## Feature guide (what each effect does, and what this build really has)

| Feature | Status | What it is | In Proper GFX 1.3 |
|---|---|---|---|
| **FSR** | not included | FidelityFX Super Resolution: an image upscaling (and frame-generation) technology. The game renders at a lower resolution to reduce the workload and smart scaling rebuilds a sharp picture that looks very close to native - higher FPS and smoother play without losing much quality. | Needs the game to render into a smaller buffer first; GTA SA Mobile draws straight into the screen, so a mod loader cannot lower the internal resolution safely. Closest: `[Sharpen]` (AMD FidelityFX **CAS**). |
| **Bloom** | yes | A soft glow or fringe of light extending from the edges of very bright areas. | `[Bloom]` - soft-knee bright pass, 1/8-res blur, separate day/night intensity. |
| **TAA & FXAA** | FXAA yes, TAA no | Smooth out jagged lines and reduce shimmering pixels. | FXAA 1.0 in `[AntiAliasing]`. TAA needs motion vectors / depth to reproject the previous frame, which the game does not expose. |
| **Auto Exposure** | yes | Dynamically adjusts the overall brightness of the scene in real time. | `[AutoExposure]` - GPU-only log-average luminance, no read-back stalls. |
| **Ambient Occlusion** | not possible | Darkens corners, crevices and contact areas to simulate blocked ambient light. | SSAO/HBAO need a depth buffer + normals. The map's baked vertex lighting already holds AO; `[DynamicShadows]` adds the sun-blocking part. |
| **Shadows** | yes | Structures and the overall environment cast a shadow in real time. | `[DynamicShadows]` - explained below. |
| **SSR** | not possible | Realistic reflections on wet floors and puddles. | Needs depth + normals. `[WorldRainfall]` (wet darkening + sheen) and `[SurfaceShading]` (vehicle env-map strength) are the substitutes. |
| **Volumetric Headlights** (experimental) | not included | Visible 3D beams / cones of light from a vehicle - the light has a visible form instead of just lighting a flat patch of road. | A believable cone must fade where it meets geometry, which needs scene depth; without it it would be a flat sprite. The game's own coronas / ground pools remain. |
| **Contact Shadows** | not possible (screen-space) | Screen-space depth in the light shaders for sharp local shadows that fix light leaking where objects meet. | No depth buffer. The same goal (no gap between an object and its shadow) is handled inside `[DynamicShadows]` by the sun-height-aware bias + PCF. |
| **LUT** | yes | A file holding a colour-translation formula that maps input colours to new output colours. Bring your own. | 7 built-in grades + your own 256x16 strip: put `lut.bmp` / `lut.tga` in `files/ProperShader` (`lut_example_identity.bmp` included) and raise `[ColorGrading] LUT`. |

### Shadows in detail
*What you see:* houses, walls, fences, trees, roads and vehicles block the sun. The ground and the surfaces behind them fall into shadow, sun-facing surfaces stay lit, and the shadows slide
across the world as the game clock moves. The sun position comes from the game's time cycle (computed from the clock, so TimeSync-style clock mods move the shadows too); the shadow *darkness* follows the `Shdw` column of **`timecyc.dat`** (`files/data/timecyc.dat`) by hour and weather, so a modded timecyc.dat or a storm changes it. They fade out as the sun nears the horizon and are off at night.

*How it works:* every few frames the opaque world draws the game just made are captured (mesh + world matrix) and re-rendered **depth-only from the sun** into one shadow map
around the camera. The game's own object shaders are patched so each pixel asks that map "is something between me and the sun?" (4-tap soft PCF) and darkens itself by *Strength* if so.
The map snaps to whole texels so edges do not crawl while you move.

*Limits:* ONE map (25-160 m around you, no cascades); pedestrians neither cast nor receive; water and translucent surfaces neither cast nor receive; the map refreshes every 2-6 frames.

*Tuning (`[DynamicShadows]` / menu):* `Strength`, `Resolution` (512/1024/2048), `Distance`, `UpdateEvery`, `Softness`, `Bias` (lower = tighter contact, raise it if you see striping),
`MinSunElevation`, `SunSmoothing`, `SunSource`, `ShowDebug` (overlays the shadow map). **Restart the game after enabling shadows for the first time** - the receivers are compiled into the game's shaders at start.

## Troubleshooting shadows
1. Menu > DynamicShadows: *Receivers* must be > 0 after you have driven around a bit, *Casters* must show a few hundred captured / rendered. Status line says what is wrong.
2. "SAFE MODE" in the status: a previous launch did not finish. Pick **Medium** or **High** (or tap **Retry shadows**), then restart the game once. (`CrashGuard = 0` in settings.ini turns the guard off.)
3. Casters captured = 0: the draw hook did not run. Casters skipped lists the reasons (blend / no depth write / client-side indices / vertex format ...).
4. `adb logcat -s ProperShader` prints a `shadows:` line every ~2 s with the same numbers.
5. Slow phone: Quality Low (512 px, 55 m, every 6th frame) or `ShadowEvery` higher; the adaptive governor halves the shadow update rate at level 2 and stops shadow rendering at level 3.

## Install
1. Copy the `.so` that matches your game to your AML mods folder (e.g. `/sdcard/Android/data/com.rockstargames.gtasa/mods/`).
2. Start the game. The mod creates / updates `files/ProperShader/settings.ini`.
3. Optional LUT: `files/ProperShader/lut.bmp|tga` (256x16 strip).
4. `files/ProperShader/disable.txt` (empty file) -> mod does nothing.

## Verified here / not verified (1.3)
Verified: both libraries build (NDK r30, `build.sh`); on a software GLES2 driver (Mesa) the **real game shader lines** - lit *and* unlit variants - take the patch, compile and link; the shadow lands behind a house and a tree at 45 / 30 / 15 degrees sun height,
open ground and the sun-facing wall stay lit (no acne), alpha-cut foliage corners cast nothing, the shadow swings with the clock and fades at night; world / settings / UI host tests pass. From the disassembly of **both** `libGTASA.so` files: the draw stream layout
`{mode, count, offset}` (0x10 bytes on 64-bit, 0xC on 32-bit), that `glDrawElements` is only ever called from `rqDrawIndexed`, that the game does not use VAOs, and that `RQShader::BuildSource`'s output pointers are hashed and `strdup`'d by the caller (so swapping them is safe).
**Not verified (no device):** that capture/replay behaves identically inside the running game on your GPU, real-GPU cost, and the look/bias on real map geometry - expect a tuning round on `Bias`/`Strength`. Use the menu numbers + logcat line above to report what you see.

## Build
`./build.sh /path/to/android-ndk-r30` -> `dist/libProperGFX32.so`, `dist/libProperGFX64.so` (or `ndk-build` with `jni/`). Host tests in `tools/`.

## Credits
Dear ImGui (MIT) - AndroidModLoader headers (MIT, RusJJ) - FXAA 1.0 (T. Lottes, public domain) - FidelityFX CAS idea (AMD, MIT) - ACES fit (K. Narkowicz) - Lottes tone-mapper.
Proper Shaders is by Junior_Djjr / Jessica Natalia; this mod is unaffiliated.
