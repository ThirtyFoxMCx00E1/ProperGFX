// Proper GFX Mobile (formerly Proper Shader Mobile) - settings (files/ProperGFX/TYFMC.ProperGFX.ini).
// Modelled on how the PC mod is configured: one section per feature, each with an Enabled switch,
// separate Day / Night values for the parameters that matter, and Low / Medium / High presets.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PS_VERSION     "1.4.5"
#define PS_DIR_NAME    "ProperGFX"              // files/ProperGFX (1.3 and older: files/ProperShader, moved automatically)
#define PS_INI_NAME    "TYFMC.ProperGFX.ini"    // 1.3 and older: settings.ini (renamed automatically)
#ifndef PS_BUTTON_MIN_OPACITY
#define PS_BUTTON_MIN_OPACITY 0.05f             // 32-bit build keeps the 1.3 range; the 64-bit build allows a fully transparent button (0.0)
#endif

enum PSTonemap { TM_OFF = 0, TM_ACES = 1, TM_TIMOTHY = 2, TM_REINHARD = 3 };
enum PSQuality { Q_LOW = 0, Q_MEDIUM = 1, Q_HIGH = 2, Q_CUSTOM = 3 };

// A value with a Day and a Night setting; `v` is the live (time-of-day blended) value used by the renderer.
struct DN
{
    float day, night, v;
    DN(float d = 0.f, float n = 0.f) : day(d), night(n), v(d) {}
};

struct PSSettings
{
    // ---- [General] ----
    bool  enabled      = true;
    bool  postFx       = true;
    bool  worldPatch   = true;       // object-shader patches (needs a game restart to (un)install)
    int   quality      = Q_MEDIUM;   // Low / Medium / High / Custom
    int   look         = 0;          // see PS_LookNames
    bool  adaptive     = true;       // adaptive quality governor
    int   targetFps    = 0;          // 0 = 30, 1 = 60
    int   timeOfDay    = 0;          // 0 auto (game clock), 1 force day, 2 force night

    // ---- [SimulateHDR] ----
    bool  hdr          = true;
    int   tonemap      = TM_ACES;
    DN    hdrAmount      { 0.55f, 0.45f };
    DN    exposure       { 0.00f, 0.20f };
    // ---- [AutoExposure] ----
    bool  autoExp      = true;
    float autoBias     = 0.f, autoRange = 1.f, autoSpeed = 1.2f;
    // ---- [Bloom] ----
    bool  bloom        = true;
    DN    bloomIntensity { 0.35f, 0.50f };
    float bloomThreshold = 0.62f, bloomRadius = 1.0f;
    // ---- [SunRays] / [LensFlare] ----
    bool  sunRays      = false;
    float sunRaysIntensity = 0.45f, sunRaysDecay = 0.93f;
    bool  lensFlare    = false;
    float lensFlareIntensity = 0.35f;
    // ---- [AntiAliasing] / [Sharpen] ----
    bool  fxaa         = true;
    float fxaaStrength = 0.85f;
    bool  cas          = true;
    float casStrength  = 0.45f;
    // ---- [ColorGrading] ----
    bool  grading      = true;
    int   gradePreset  = 0;
    float gradeAmount  = 1.0f;
    DN    contrast       { 1.05f, 1.05f };
    DN    saturation     { 1.08f, 1.00f };
    float vibrance = 0.20f, temperature = 0.f, tint = 0.f, gamma = 1.f, lift = 0.f, gain = 1.f;
    // ---- [LUT] colour lookup texture: files/ProperGFX/<LUTFile> (png / bmp / tga / jpg; 256x16, 1024x32, 4096x64, 64x64, 512x512) ----
    bool  lutOn        = true;
    float lutAmount    = 1.0f;       // 1.4: default 1 (was 0, so a dropped-in LUT seemed to do nothing)
    char  lutFile[64]  = "auto";     // "auto" = the first lut.png / lut.bmp / lut.tga / lut.jpg (any case) in files/ProperGFX or files/ProperGFX/luts
    // ---- [Dithering] / [Vignette] / [FilmGrain] / [ChromaticAberration] ----
    bool  dither       = true;
    bool  vignette     = true;
    DN    vignetteStrength { 0.30f, 0.40f };
    float vignetteSize = 0.55f;
    bool  grain        = false;
    float grainStrength = 0.25f;
    bool  chromatic    = false;
    float chromaticStrength = 0.35f;
    // ---- [DepthOfField] (tilt-shift, no depth buffer on mobile) ----
    bool  tiltShift    = false;
    float tiltFocus = 0.5f, tiltWidth = 0.22f, tiltStrength = 0.8f;
    // ---- [Underwater] ----
    bool  underwater   = true;
    float underwaterStrength = 1.0f;
    // ---- [HorizonFog] / [SurfaceShading] / [WorldRainfall] / [WaterSurface] ----
    bool  fogOn        = true;
    DN    fogDensity     { 1.0f, 1.0f };
    float fogSoftness  = 0.35f;      // -0.5 harder .. +1 softer (no pow() in the shader)
    bool  surfaceOn    = true;
    float specularMul  = 1.0f, reflectionMul = 1.0f;
    bool  wetWorld     = true;
    float wetStrength  = 1.0f;
    bool  waterOn      = true;
    float waterDetail  = 1.0f;
    // ---- [ImprovedZFighting] ----
    bool  zfix         = false;
    float nearClip     = 0.30f;
    // ---- [DynamicShadows] sun shadow map (buildings, trees, vehicles cast onto the world) ----
    bool  shadows      = true;
    float shadowStrength = 0.55f;
    int   shadowRes    = 1;          // 0 = 512, 1 = 1024, 2 = 2048
    float shadowDist   = 80.f;       // metres around the camera covered by the map
    int   shadowEvery  = 3;          // re-render the map every N frames (world is static, sun moves slowly)
    float shadowSoft   = 1.0f;       // PCF radius multiplier
    float shadowBias   = 1.0f;       // bias multiplier
    float shadowMinSun = 0.05f;      // sun elevation (sin) below which shadows fade out (dusk/night)
    float sunSmoothing = 0.06f;      // low-pass on the sun direction (clock-mod steps)
    bool  shadowDebug  = false;      // overlay the shadow map in a corner
    int   sunSource    = 0;          // 0 = Auto (game's time-cycle sun vector, falls back to the light uniform), 1 = time-cycle sun only, 2 = light uniform only
    bool  shadowTimecyc = true;      // shadow strength follows timecyc.dat (the 'Shdw' column) by hour and weather
    bool  shadowGuard  = true;       // crash guard: shadows switch themselves off after a launch that did not survive
    bool  shadowRetry  = false;      // runtime only (menu button): clear the crash guard
    // ---- artistic ----
    bool  cel          = false;
    float celLevels = 5.f, celAmount = 0.85f;
    bool  celWorld     = false;      // read when a shader is compiled (restart to change)
    float celWorldLevels = 4.f;
    bool  outline      = false;
    float outlineStrength = 0.8f, outlineThreshold = 0.22f;
    bool  vhs          = false;
    float vhsStrength  = 0.6f;
    // ---- [Button] / [Menu] ----
    bool  showButton   = true;
    float buttonX = 0.38f, buttonY = 0.95f, buttonScale = 1.0f, buttonOpacity = 0.55f;
    bool  buttonCollapse = true;     // shrink to a small dot when idle so it never covers other mods' buttons
    float uiScale      = 1.0f;
};

extern uint32_t g_worldGen;                 // bumped whenever an object-shader uniform may have changed
extern const char* const PS_QualityNames[]; extern const int PS_QualityCount;
extern const char* const PS_LookNames[];    extern const int PS_LookCount;
extern const char* const PS_GradeNames[];   extern const int PS_GradeCount;
extern const char* const PS_TonemapNames[]; extern const int PS_TonemapCount;
extern const char* const PS_TimeNames[];    extern const int PS_TimeCount;
extern const char* const PS_SunSourceNames[]; extern const int PS_SunSourceCount;

bool  PS_LoadSettings(PSSettings& s, const char* path);
bool  PS_SaveSettings(const PSSettings& s, const char* path);
void  PS_ResetSettings(PSSettings& s);
void  PS_ApplyQuality(PSSettings& s, int quality);   // Low / Medium / High
void  PS_ApplyLook(PSSettings& s, int look);         // colour/mood looks on top of a quality tier
float PS_NightFactor(int hours, int minutes);        // 0 = full day .. 1 = full night
void  PS_UpdateLive(PSSettings& s, float nightFactor);
