// Proper GFX Mobile - plain-language feature guide (menu > FEATURE GUIDE, mirrored in README.md).
// status: 0 = implemented, 1 = partly (the useful half exists), 2 = not possible / not in this build
#pragma once
struct PSGuide { const char* name; int status; const char* what; const char* here; };
static const PSGuide kGuide[] = {
 { "FSR (FidelityFX Super Resolution)", 2,
   "An image upscaling (and frame-generation) technology. The game is rendered at a lower resolution to reduce the workload, then smart scaling algorithms rebuild the picture so it looks sharp and very close to a native high-resolution image. Result: higher FPS and smoother gameplay without losing too much visual quality.",
   "NOT included. FSR needs the game to render into a smaller buffer first, and GTA SA Mobile draws straight into the screen buffer, so a mod loader cannot lower the internal resolution safely. The closest thing here is [Sharpen] (AMD FidelityFX CAS), which sharpens the full-resolution image." },
 { "Bloom", 0,
   "A shader effect that creates a soft glow or fringe of light extending from the edges of very bright areas in an image.",
   "Implemented: [Bloom] (soft-knee bright pass, blurred at 1/8 resolution; separate day and night intensity)." },
 { "TAA & FXAA", 1,
   "TAA or FXAA is used to smooth out jagged lines and reduce shimmering pixels.",
   "FXAA 1.0 is implemented ([AntiAliasing]). TAA is NOT: it needs per-pixel motion vectors / a depth buffer to reproject the previous frame, which the game does not expose." },
 { "Auto Exposure", 0,
   "Dynamically adjusts the overall brightness of a 3D scene or image in real time.",
   "Implemented: [AutoExposure] (GPU-only log-average luminance, no read-back stalls; Bias / Range / Speed)." },
 { "Ambient Occlusion / AO", 2,
   "Darkens corners, crevices and areas where objects meet to simulate how ambient light is blocked by nearby surfaces.",
   "NOT possible: SSAO / HBAO need a depth buffer and normals, and the game renders without an accessible depth texture. The map's baked vertex lighting already contains ambient occlusion, and [DynamicShadows] adds the sun-blocking part (shade behind buildings, trees, vehicles)." },
 { "Shadows", 0,
   "Most obvious from the name: structures and the overall environment now cast a shadow in real time. Houses, walls, trees, fences, roads and vehicles block the sun; the ground and the surfaces behind them fall into shadow, and the shadows slide across the world as the game clock moves. The sun position comes from the game's time cycle (it is computed from the clock; mods that change the clock, e.g. TimeSync, move the shadows too) and the shadow darkness follows the 'Shdw' column of timecyc.dat by hour and weather.\n"
   "How it works: every few frames the opaque world draws the game just made are captured (mesh + world matrix) and re-rendered depth-only from the sun into one shadow map around the camera. The game's own object shaders are patched so every pixel asks that map 'is something between me and the sun?' (4-tap soft PCF) and darkens itself by [Strength] when the answer is yes. The sun direction is the game's own time-cycle sun vector, shadows fade out when the sun is near or below the horizon, and the map snaps to whole texels so edges do not crawl while you move.",
   "Implemented as [DynamicShadows] (v1.3 rewrite: it now also covers the unlit prelit shaders that buildings, roads and terrain use, which v1.2 skipped). Casters the game stops drawing when you turn away (a building behind you) are remembered for up to 3 minutes, so their shadows keep falling into view; draws into the game's other render targets are ignored. Limits: ONE map (25-160 m around you, no cascades), pedestrians neither cast nor receive, water and translucent surfaces neither cast nor receive, the map refreshes every 2-6 frames. Only on Medium / High (Low = no shadows, bloom or sun rays, for speed). Tune with Strength / Resolution / Distance / Bias / Softness; Show shadow map (debug) overlays the map and the menu lists what was captured and why anything was skipped." },
 { "SSR - Screen Space Reflections", 2,
   "Another obvious feature by name: it adds realistic reflections on wet floors and puddles.",
   "NOT possible: SSR ray-marches the depth buffer and needs normals, neither of which the game exposes. What exists: [WorldRainfall] darkens and adds sheen to the world while it rains, and [SurfaceShading] scales the vehicle environment-map reflections." },
 { "Volumetric Headlights (experimental)", 2,
   "Visible 3D beams or cones of light emitted from a vehicle. Instead of just illuminating a flat surface on the road, the light source itself has a visible form, like a glowing cone.",
   "NOT in this build. A believable cone needs scene depth to fade it where it meets geometry; without that it would just be a flat sprite floating in the air. The game's own headlight coronas and ground pools remain." },
 { "Contact Shadows", 2,
   "Uses screen-space depth buffers inside light shaders to cast sharp, localized shadows that fix light leaking and separation artifacts where objects meet.",
   "NOT possible as screen-space (no depth buffer). The same goal - no gap between an object and its shadow - is handled inside [DynamicShadows] with a bias that adapts to the sun height and a soft PCF filter; lower Bias to tighten contact, raise it if you see striping." },
 { "LUT", 0,
   "A digital file containing a mathematical formula that translates specific input color values from an image or video into new output color values. To install a LUT, grab your own.",
   "Implemented: 7 built-in grades plus your own LUT - put lut.png / .bmp / .tga / .jpg (256x16, 1024x32, 4096x64, 64x64 or 512x512) in files/ProperGFX or pick a file in [LUT]; it is reloaded live when you replace it." },
};
static const int kGuideCount = (int)(sizeof(kGuide) / sizeof(kGuide[0]));
