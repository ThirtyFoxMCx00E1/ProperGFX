// Proper GFX Mobile (64-bit) - feature guide (menu > FEATURE GUIDE, mirrored in FEATURES.md).
// "what" = the feature description; "here" = what this 64-bit build really does.
// status: 0 = implemented, 1 = partly (the useful half exists), 2 = not possible / not in this build
#pragma once
struct PSGuide { const char* name; int status; const char* what; const char* here; };
static const PSGuide kGuide[] = {
 { "[FSR]", 2,
   "FSR stands for FidelityFX Super Resolution, an image upscaling and frame-generation technology. "
   "It feature renders a video game at a lower resolution to reduce workload! It uses smart scaling algorithms to rebuild the picture so it looks sharp and VERY close to a high-resolution native image! "
   "The process will give you higher FPS and smoother gameplay without losing too much visual quality.",
   "NOT included: FSR needs the game to render into a smaller buffer first, and GTA SA Mobile draws straight into the screen buffer, so a mod cannot lower the internal resolution safely. "
   "Closest: [Sharpen] (AMD FidelityFX CAS) sharpens the full-resolution picture." },
 { "[Bloom]", 0,
   "Bloom is a shader effect that creates a soft glow or fringe of light extending from the edges of very bright areas in an image.",
   "Implemented: [Bloom] - bloom_bright.glsl (soft-knee bright pass) + blur_gauss.glsl at 1/8 resolution, separate day / night intensity." },
 { "[TAA & FXAA]", 1,
   "TAA or FXAA is used smooth out jagged lines and reduce shimmering pixels.",
   "FXAA 1.0 is implemented ([AntiAliasing], fxaa.glsl). TAA is not: it needs motion vectors / a depth buffer to reproject the previous frame, which the game does not expose." },
 { "[Auto Exposure]", 0,
   "It dynamically adjusts the overall brightness of a 3D scene or image in real-time.",
   "Implemented: [AutoExposure] - autoexposure.glsl, GPU-only log-average luminance (Bias / Range / Speed), no read-back stalls." },
 { "[Ambient Occlusion / AO]", 2,
   "It darkens corners, crevices, and areas where objects meet to simulate how ambient light is blocked by nearby surfaces.",
   "NOT possible as SSAO: it needs a depth buffer and normals, and the game renders without an accessible depth texture. The map's baked vertex lighting already contains AO, and [DynamicShadows] adds the sun-blocking part." },
 { "[Shadows]", 0,
   "Most obvious via name, structures and overall environment now cast a shadow in real time.",
   "Implemented: [DynamicShadows] Method = Deferred (1.4.3 default, the libZyZyGFX64 technique): the scene target gets a readable depth texture, the captured world meshes are rendered from the "
   "game's sun into 2 cascades (shadow_depth.glsl), then shadow_mask.glsl rebuilds each pixel's position from the depth and darkens it right after RenderScene, before the HUD. "
   "Every surface receives, the game's shaders are not changed. Method = In-shader keeps the 1.4.2 receivers. Follows the game clock, strength follows timecyc.dat. Medium / High only." },
 { "[SSR - Screen Space Reflections]", 2,
   "Another obvious feature by name, it adds realistic reflections on wet floors, and puddles.",
   "NOT possible: SSR ray-marches the depth buffer and needs normals, neither of which the game exposes. What exists: [WorldRainfall] darkens / adds sheen while it rains (RoadsWetness), [SurfaceShading] scales the vehicle reflections." },
 { "[Volumetric Headlights]", 2,
   "These are visible 3D beams or cones of light emitted from a vehicle. Instead of just illuminating a flat surface on the road, the light source itself has a visible form, like a glowing cone, althrough experimental.",
   "NOT in this build: a believable cone needs scene depth to fade it where it meets geometry; without it the cone would be a flat sprite floating in the air. The game's own headlight coronas and ground pools remain." },
 { "[Contact Shadows]", 2,
   "Uses screen-space depth buffers inside light shaders to cast sharp, localized shadows that fix light leaking and separation artifacts where objects meet.",
   "Partly: with Method = Deferred the shadow mask reads the scene depth and rebuilds normals from it, with a slope-scaled bias so shadows touch the objects; lower Bias to tighten contact." },
 { "[LUT]", 0,
   "A digital file containing a mathematical formula that translates specific input color values from an image or video into new output color values. "
   "To install an LUT, grab your own LUT formula via .png and go through the .ini configs. "
   "Make sure you have the directory set (Folders=) along with the texture's name (include .png) in the File= thingy for the mod to read the formula.",
   "Implemented: [LUT] Enabled / Amount / Folders / File in TYFMC.ProperGFX.ini (lut.glsl). Folders = empty -> files/ProperGFX (+ luts); a name is relative to files/ProperGFX, or a full path. "
   "File = name with extension (.png / .bmp / .tga / .jpg, any upper/lower case) or auto = lut.*. Strips 256x16, 1024x32, 4096x64 or squares 64x64 / 512x512; reloaded live; also pickable in the menu." },
 { "[Surface Based Reflections]", 1,
   "Metal and Glass can now have reflections.",
   "Partly: [SurfaceShading] Reflections scales the game's environment-map reflections (vehicle paint, metal and glass of cars) and Specular the highlights. Building glass has no reflection map in the game, so it stays as it is." },
 { "[HDR]", 0,
   "HDR (High Dynamic Range) processes color and lighting values beyond the standard 0.0 to 1.0 limit. "
   "It makes virtual worlds look real by increasing the contrast between deep blacks and blazing bright lights while showing a wider range of colors.",
   "Implemented as [SimulateHDR] - tonemap.glsl (ACES / Timothy / Reinhard) on an expanded-range picture, with exposure, Auto Exposure and bloom/sun added before tone mapping. The game itself renders 8-bit, so this is simulated HDR." },
 { "[Foliage Backlights]", 2,
   "Lights now shine through vegetation's leaves from behind, illuminating them from the viewer's perspective.",
   "NOT in this build yet: the game draws leaves with the same shaders as other alpha-tested objects, so they cannot be told apart reliably without model lists. The sun glow / golden hour ([SunVector]) brightens the picture toward the sun instead." },
 { "[Physical Based Rendering Reflections]", 1,
   "Vehicle models now has light bouncing off object surfaces based on real-world physics, using roughness and metalness instead of arbitrary shine values.",
   "Partly: [SurfaceShading] Specular / Reflections rescale the game's vehicle highlight and environment-map strength. True PBR needs roughness / metalness textures the game's models do not have." },
};
static const int kGuideCount = (int)(sizeof(kGuide) / sizeof(kGuide[0]));
