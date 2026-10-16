#include "ps_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

uint32_t g_worldGen = 1;

const char* const PS_QualityNames[] = { "Low", "Medium", "High", "Custom" };   const int PS_QualityCount = 4;
const char* const PS_LookNames[]    = { "Default", "Cinematic", "Vivid", "Soft dream", "Anime", "Noir", "Retro VHS" }; const int PS_LookCount = 7;
const char* const PS_GradeNames[]   = { "Neutral", "Cinematic (teal/orange)", "Warm sunset", "Cold", "Noir", "Vintage", "Vivid" }; const int PS_GradeCount = 7;
const char* const PS_TonemapNames[] = { "Off", "ACES", "Timothy", "Reinhard" }; const int PS_TonemapCount = 4;
const char* const PS_TimeNames[]    = { "Auto", "Day", "Night" }; const int PS_TimeCount = 3;
const char* const PS_SunSourceNames[] = { "Auto", "TimeCycle", "Light" }; const int PS_SunSourceCount = 3;
const char* const PS_DofModeNames[] = { "TiltShift", "CenterFocus" }; const int PS_DofModeCount = 2;

// ------------------------------------------------------------------ field table
enum FT { FT_BOOL, FT_INT, FT_FLOAT, FT_DN, FT_ENUM, FT_STR, FT_FLOAT_LEGACY };   // LEGACY = read for old ini files, never written
struct Field { const char* sec; const char* key; FT t; size_t off; const char* const* names; int n; };

#define O(m) offsetof(PSSettings, m)
#define B(s, k, m)        { s, k, FT_BOOL,  O(m), nullptr, 0 },
#define I(s, k, m)        { s, k, FT_INT,   O(m), nullptr, 0 },
#define F(s, k, m)        { s, k, FT_FLOAT, O(m), nullptr, 0 },
#define D(s, k, m)        { s, k, FT_DN,    O(m), nullptr, 0 },
#define E(s, k, m, nm, c) { s, k, FT_ENUM,  O(m), nm, c },
#define S(s, k, m)        { s, k, FT_STR,   O(m), nullptr, (int)sizeof(((PSSettings*)0)->m) },
#define L(s, k, m)        { s, k, FT_FLOAT_LEGACY, O(m), nullptr, 0 },

static const Field kFields[] = {
    B("General", "Enabled", enabled) B("General", "PostProcessing", postFx) B("General", "WorldShaderPatches", worldPatch)
    E("General", "Quality", quality, PS_QualityNames, 4) E("General", "Look", look, PS_LookNames, 7)
    B("General", "AdaptiveQuality", adaptive) I("General", "TargetFPS", targetFps) E("General", "TimeOfDay", timeOfDay, PS_TimeNames, 3)
    B("SimulateHDR", "Enabled", hdr) E("SimulateHDR", "Tonemapper", tonemap, PS_TonemapNames, 4)
    D("SimulateHDR", "Amount", hdrAmount) D("SimulateHDR", "Exposure", exposure)
    B("AutoExposure", "Enabled", autoExp) F("AutoExposure", "Bias", autoBias) F("AutoExposure", "Range", autoRange) F("AutoExposure", "Speed", autoSpeed)
    B("Bloom", "Enabled", bloom) D("Bloom", "Intensity", bloomIntensity) F("Bloom", "Threshold", bloomThreshold) F("Bloom", "Radius", bloomRadius)
    B("SunRays", "Enabled", sunRays) F("SunRays", "Intensity", sunRaysIntensity) F("SunRays", "Decay", sunRaysDecay)
    B("LensFlare", "Enabled", lensFlare) F("LensFlare", "Intensity", lensFlareIntensity)
    B("AntiAliasing", "Enabled", fxaa) F("AntiAliasing", "Strength", fxaaStrength)
    B("Sharpen", "Enabled", cas) F("Sharpen", "Strength", casStrength)
    B("ColorGrading", "Enabled", grading) E("ColorGrading", "Grade", gradePreset, PS_GradeNames, 7) F("ColorGrading", "Amount", gradeAmount)
    D("ColorGrading", "Contrast", contrast) D("ColorGrading", "Saturation", saturation) F("ColorGrading", "Vibrance", vibrance)
    F("ColorGrading", "Temperature", temperature) F("ColorGrading", "Tint", tint) F("ColorGrading", "Gamma", gamma)
    F("ColorGrading", "Lift", lift) F("ColorGrading", "Gain", gain) L("ColorGrading", "LUT", lutAmount)
    B("LUT", "Enabled", lutOn) F("LUT", "Amount", lutAmount) S("LUT", "Folders", lutFolder) S("LUT", "File", lutFile)
    B("Dithering", "Enabled", dither)
    B("Vignette", "Enabled", vignette) D("Vignette", "Strength", vignetteStrength) F("Vignette", "Size", vignetteSize)
    B("FilmGrain", "Enabled", grain) F("FilmGrain", "Strength", grainStrength)
    B("ChromaticAberration", "Enabled", chromatic) F("ChromaticAberration", "Strength", chromaticStrength)
    B("DepthOfField", "Enabled", tiltShift) F("DepthOfField", "Focus", tiltFocus) F("DepthOfField", "Width", tiltWidth) F("DepthOfField", "Strength", tiltStrength)
    E("DepthOfField", "Mode", dofMode, PS_DofModeNames, 2) F("DepthOfField", "Bokeh", dofBokeh) F("DepthOfField", "Radius", dofRadius) F("DepthOfField", "UnderwaterBlur", dofUnderwater)
    B("SunVector", "Enabled", sunVectorOn) F("SunVector", "Glow", sunGlow) F("SunVector", "GlowSize", sunGlowSize) F("SunVector", "GoldenHour", goldenHour)
    B("Underwater", "Enabled", underwater) F("Underwater", "Strength", underwaterStrength)
    B("HorizonFog", "Enabled", fogOn) D("HorizonFog", "Density", fogDensity) F("HorizonFog", "Softness", fogSoftness)
    B("SurfaceShading", "Enabled", surfaceOn) F("SurfaceShading", "Specular", specularMul) F("SurfaceShading", "Reflections", reflectionMul)
    B("WorldRainfall", "Enabled", wetWorld) F("WorldRainfall", "Strength", wetStrength)
    B("WaterSurface", "Enabled", waterOn) F("WaterSurface", "Detail", waterDetail)
    B("ImprovedZFighting", "Enabled", zfix) F("ImprovedZFighting", "NearClip", nearClip)
    B("DynamicShadows", "Enabled", shadows) F("DynamicShadows", "Strength", shadowStrength) I("DynamicShadows", "Resolution", shadowRes)
    F("DynamicShadows", "Distance", shadowDist) I("DynamicShadows", "UpdateEvery", shadowEvery) F("DynamicShadows", "Softness", shadowSoft)
    F("DynamicShadows", "Bias", shadowBias) F("DynamicShadows", "MinSunElevation", shadowMinSun) F("DynamicShadows", "SunSmoothing", sunSmoothing)
    B("DynamicShadows", "ShowDebug", shadowDebug)
    E("DynamicShadows", "SunSource", sunSource, PS_SunSourceNames, 3) B("DynamicShadows", "TimecycStrength", shadowTimecyc) B("DynamicShadows", "CrashGuard", shadowGuard)
    F("DynamicShadows", "Tint", shadowTint) F("DynamicShadows", "TintR", shadowTintR) F("DynamicShadows", "TintG", shadowTintG) F("DynamicShadows", "TintB", shadowTintB)
    B("CelShading", "Enabled", cel) F("CelShading", "Levels", celLevels) F("CelShading", "Amount", celAmount)
    B("CelShading", "World", celWorld) F("CelShading", "WorldLevels", celWorldLevels)
    B("Outlines", "Enabled", outline) F("Outlines", "Strength", outlineStrength) F("Outlines", "Threshold", outlineThreshold)
    B("VHS", "Enabled", vhs) F("VHS", "Strength", vhsStrength)
    B("GLSL", "CustomPass", glslCustom)
    F("GLSL", "Param0", glslParam[0]) F("GLSL", "Param1", glslParam[1]) F("GLSL", "Param2", glslParam[2]) F("GLSL", "Param3", glslParam[3])
    F("GLSL", "Param4", glslParam[4]) F("GLSL", "Param5", glslParam[5]) F("GLSL", "Param6", glslParam[6]) F("GLSL", "Param7", glslParam[7])
    B("Button", "Show", showButton) F("Button", "X", buttonX) F("Button", "Y", buttonY) F("Button", "Size", buttonScale) F("Button", "Opacity", buttonOpacity) B("Button", "AutoCollapse", buttonCollapse)
    F("Menu", "Scale", uiScale)
};
static const int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline void clampDN(DN& d, float a, float b) { d.day = clampf(d.day, a, b); d.night = clampf(d.night, a, b); d.v = d.day; }

static void Sanitize(PSSettings& s)
{
    s.tonemap = s.tonemap < 0 ? 0 : (s.tonemap > 3 ? 3 : s.tonemap);
    s.quality = s.quality < 0 ? 0 : (s.quality > 3 ? 3 : s.quality);
    s.look = s.look < 0 ? 0 : (s.look >= PS_LookCount ? 0 : s.look);
    s.gradePreset = s.gradePreset < 0 ? 0 : (s.gradePreset >= PS_GradeCount ? 0 : s.gradePreset);
    s.targetFps = s.targetFps ? 1 : 0;
    s.timeOfDay = s.timeOfDay < 0 || s.timeOfDay > 2 ? 0 : s.timeOfDay;
    clampDN(s.hdrAmount, 0.f, 1.f); clampDN(s.exposure, -3.f, 3.f); clampDN(s.bloomIntensity, 0.f, 1.5f);
    clampDN(s.contrast, 0.5f, 1.6f); clampDN(s.saturation, 0.f, 2.f); clampDN(s.vignetteStrength, 0.f, 1.f);
    clampDN(s.fogDensity, 0.f, 3.f);
    s.autoRange = clampf(s.autoRange, 0.f, 3.f); s.autoSpeed = clampf(s.autoSpeed, 0.1f, 5.f);
    s.gamma = clampf(s.gamma, 0.5f, 2.f); s.sunRaysDecay = clampf(s.sunRaysDecay, 0.80f, 0.99f);
    s.bloomRadius = clampf(s.bloomRadius, 0.3f, 3.f); s.celLevels = clampf(s.celLevels, 2.f, 12.f);
    s.celWorldLevels = clampf(s.celWorldLevels, 2.f, 12.f); s.fogSoftness = clampf(s.fogSoftness, -0.5f, 1.f);
    s.nearClip = clampf(s.nearClip, 0.05f, 2.f);
    s.shadowStrength = clampf(s.shadowStrength, 0.f, 1.f); s.shadowRes = s.shadowRes < 0 ? 0 : (s.shadowRes > 2 ? 2 : s.shadowRes);
    s.shadowDist = clampf(s.shadowDist, 25.f, 160.f); s.shadowEvery = s.shadowEvery < 1 ? 1 : (s.shadowEvery > 10 ? 10 : s.shadowEvery);
    s.shadowSoft = clampf(s.shadowSoft, 0.3f, 3.f); s.shadowBias = clampf(s.shadowBias, 0.3f, 3.f);
    s.sunSource = (s.sunSource < 0 || s.sunSource > 2) ? 0 : s.sunSource;
    s.shadowMinSun = clampf(s.shadowMinSun, 0.f, 0.4f); s.sunSmoothing = clampf(s.sunSmoothing, 0.005f, 1.f);
    s.buttonX = clampf(s.buttonX, 0.f, 1.f); s.buttonY = clampf(s.buttonY, 0.f, 1.f);
    s.buttonScale = clampf(s.buttonScale, 0.5f, 2.5f); s.buttonOpacity = clampf(s.buttonOpacity, PS_BUTTON_MIN_OPACITY, 1.f); if (s.buttonOpacity < PS_BUTTON_HIDDEN_BELOW) s.buttonOpacity = 0.f;
    s.lutAmount = clampf(s.lutAmount, 0.f, 1.f);
    s.dofMode = (s.dofMode < 0 || s.dofMode > 1) ? 0 : s.dofMode; s.dofBokeh = clampf(s.dofBokeh, 0.f, 2.f); s.dofRadius = clampf(s.dofRadius, 0.3f, 3.f);
    s.dofUnderwater = clampf(s.dofUnderwater, 0.f, 1.f); s.sunGlow = clampf(s.sunGlow, 0.f, 1.5f); s.sunGlowSize = clampf(s.sunGlowSize, 0.03f, 0.6f);
    s.goldenHour = clampf(s.goldenHour, 0.f, 1.f); s.shadowTint = clampf(s.shadowTint, 0.f, 1.f);
    s.shadowTintR = clampf(s.shadowTintR, 0.f, 1.f); s.shadowTintG = clampf(s.shadowTintG, 0.f, 1.f); s.shadowTintB = clampf(s.shadowTintB, 0.f, 1.f);
    for (float& p : s.glslParam) p = clampf(p, -1.f, 1.f);
    s.uiScale = clampf(s.uiScale, 0.6f, 2.f);
}

static int EnumFind(const Field& f, const char* v)
{
    for (int i = 0; i < f.n; ++i) if (!strcasecmp(f.names[i], v)) return i;
    int n = atoi(v); return (n >= 0 && n < f.n) ? n : 0;
}

bool PS_LoadSettings(PSSettings& s, const char* path)
{
    FILE* fp = fopen(path, "r");
    if (!fp) return false;
    char line[256], sec[64] = "";
    bool any = false;
    while (fgets(line, sizeof(line), fp))
    {
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        char* p = line; while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '#' || *p == ';') continue;
        if (*p == '[') { char* e = strchr(p, ']'); if (e) { *e = 0; snprintf(sec, sizeof(sec), "%s", p + 1); } continue; }
        char* eq = strchr(p, '='); if (!eq) continue;
        *eq = 0; char* key = p; char* val = eq + 1;
        for (char* t = eq - 1; t >= key && (*t == ' ' || *t == '\t'); --t) *t = 0;
        while (*val == ' ' || *val == '\t') ++val;
        bool isDay = false, isNight = false; size_t kl = strlen(key);
        if (kl > 4 && !strcasecmp(key + kl - 4, ".Day")) { isDay = true; key[kl - 4] = 0; }
        else if (kl > 6 && !strcasecmp(key + kl - 6, ".Night")) { isNight = true; key[kl - 6] = 0; }
        for (int i = 0; i < kFieldCount; ++i)
        {
            const Field& f = kFields[i];
            if (strcasecmp(f.sec, sec) || strcasecmp(f.key, key)) continue;
            char* base = (char*)&s + f.off; any = true;
            switch (f.t)
            {
                case FT_BOOL:  *(bool*)base = atoi(val) != 0; break;
                case FT_INT:   *(int*)base = atoi(val); break;
                case FT_FLOAT: case FT_FLOAT_LEGACY: *(float*)base = (float)atof(val); break;
                case FT_STR:   { char* e = val + strlen(val); while (e > val && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; snprintf(base, (size_t)f.n, "%s", val[0] ? val : "auto"); break; }
                case FT_ENUM:  *(int*)base = EnumFind(f, val); break;
                case FT_DN:    { DN* d = (DN*)base; float v = (float)atof(val); if (isNight) d->night = v; else if (isDay) d->day = v; else { d->day = d->night = v; } break; }
            }
            break;
        }
    }
    fclose(fp);
    Sanitize(s);
    return any;
}

bool PS_SaveSettings(const PSSettings& s, const char* path)
{
    FILE* fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "# Proper GFX Mobile " PS_VERSION " - TYFMC.ProperGFX.ini (also edited by the in-game menu)\n"
                "# One section per feature, like the PC mod. 'Enabled = 0' disables a feature.\n"
                "# Values with .Day / .Night are blended by the game clock.\n");
    char cur[64] = "";
    for (int i = 0; i < kFieldCount; ++i)
    {
        const Field& f = kFields[i];
        if (f.t == FT_FLOAT_LEGACY) continue;
        if (strcmp(cur, f.sec)) { fprintf(fp, "\n[%s]\n", f.sec); snprintf(cur, sizeof(cur), "%s", f.sec); }
        const char* base = (const char*)&s + f.off;
        switch (f.t)
        {
            case FT_BOOL:  fprintf(fp, "%s = %d\n", f.key, *(const bool*)base ? 1 : 0); break;
            case FT_INT:   fprintf(fp, "%s = %d\n", f.key, *(const int*)base); break;
            case FT_FLOAT: fprintf(fp, "%s = %.4f\n", f.key, *(const float*)base); break;
            case FT_STR:   fprintf(fp, "%s = %s\n", f.key, base); break;
            case FT_FLOAT_LEGACY: break;
            case FT_ENUM:  { int v = *(const int*)base; fprintf(fp, "%s = %s\n", f.key, (v >= 0 && v < f.n) ? f.names[v] : "0"); break; }
            case FT_DN:    { const DN* d = (const DN*)base; fprintf(fp, "%s.Day = %.4f\n%s.Night = %.4f\n", f.key, d->day, f.key, d->night); break; }
        }
    }
    fclose(fp);
    return true;
}

void PS_ResetSettings(PSSettings& s)
{
    PSSettings d;   // keep the button / menu placement
    d.showButton = s.showButton; d.buttonX = s.buttonX; d.buttonY = s.buttonY; d.buttonScale = s.buttonScale;
    d.buttonOpacity = s.buttonOpacity; d.uiScale = s.uiScale; memcpy(d.lutFile, s.lutFile, sizeof(d.lutFile)); memcpy(d.lutFolder, s.lutFolder, sizeof(d.lutFolder));
    d.glslCustom = s.glslCustom; memcpy(d.glslParam, s.glslParam, sizeof(d.glslParam)); d.worldPatch = s.worldPatch; d.celWorld = s.celWorld;
    s = d; ++g_worldGen;
}

// Low / Medium / High: performance tiers (the PC mod ships the same three presets)
void PS_ApplyQuality(PSSettings& s, int q)
{
    if (q < 0 || q > 2) { s.quality = Q_CUSTOM; return; }
    s.quality = q;
    s.hdr = true; s.dither = true; s.fxaa = true; s.grading = true;
    switch (q)
    {
        case Q_LOW:
            s.autoExp = false; s.bloom = false; s.sunRays = false; s.lensFlare = false; s.cas = false; s.vignette = false;
            s.shadows = false;                      // Low = fast and smooth: no shadows, no bloom, no sun rays
            s.fxaaStrength = 0.7f; s.shadowRes = 0; s.shadowEvery = 6; s.shadowDist = 55.f; break;
        case Q_MEDIUM:
            s.autoExp = true; s.bloom = true; s.sunRays = true; s.lensFlare = false; s.cas = true; s.vignette = true;
            s.shadows = true;                       // Medium: shadows + bloom + sun rays for mid-range phones
            s.casStrength = 0.40f; s.fxaaStrength = 0.85f; s.shadowRes = 1; s.shadowEvery = 4; s.shadowDist = 80.f; break;
        case Q_HIGH:
            s.autoExp = true; s.bloom = true; s.sunRays = true; s.lensFlare = true; s.cas = true; s.vignette = true;
            s.shadows = true;                       // High: everything, bigger shadow map, updated more often
            s.casStrength = 0.50f; s.fxaaStrength = 0.90f; s.bloomRadius = 1.2f; s.shadowRes = 2; s.shadowEvery = 2; s.shadowDist = 100.f; break;
    }
    ++g_worldGen;
}

void PS_ApplyLook(PSSettings& s, int p)
{
    s.look = p;
    // reset the look-related values first (not the quality tier / feature switches)
    PSSettings d;
    s.tonemap = d.tonemap; s.hdrAmount = d.hdrAmount; s.exposure = d.exposure; s.bloomIntensity = d.bloomIntensity; s.bloomThreshold = d.bloomThreshold;
    s.bloomRadius = d.bloomRadius; s.contrast = d.contrast; s.saturation = d.saturation; s.vibrance = d.vibrance; s.gradePreset = 0; s.gradeAmount = 1.f;
    s.vignetteStrength = d.vignetteStrength; s.grain = false; s.chromatic = false; s.vhs = false; s.cel = false; s.outline = false;
    s.celWorld = s.celWorld; s.fogDensity = d.fogDensity; s.fogSoftness = d.fogSoftness; s.wetStrength = 1.f; s.lensFlare = (s.quality == Q_HIGH);
    switch (p)
    {
        default: case 0: break;
        case 1: s.hdrAmount = DN(0.7f, 0.6f); s.gradePreset = 1; s.contrast = DN(1.12f, 1.12f); s.saturation = DN(0.98f, 0.92f);
                s.bloomIntensity = DN(0.42f, 0.55f); s.vignetteStrength = DN(0.42f, 0.5f); s.grain = true; s.grainStrength = 0.2f; s.fogSoftness = 0.5f; break;
        case 2: s.tonemap = TM_TIMOTHY; s.gradePreset = 6; s.saturation = DN(1.25f, 1.15f); s.vibrance = 0.4f; s.contrast = DN(1.10f, 1.10f);
                s.bloomIntensity = DN(0.40f, 0.5f); s.wetStrength = 1.2f; break;
        case 3: s.hdrAmount = DN(0.85f, 0.75f); s.bloomIntensity = DN(0.7f, 0.8f); s.bloomThreshold = 0.45f; s.bloomRadius = 1.6f; s.lensFlare = true;
                s.gradePreset = 2; s.gradeAmount = 0.6f; s.fogDensity = DN(1.2f, 1.2f); s.fogSoftness = 0.7f; break;
        case 4: s.cel = true; s.celLevels = 5.f; s.outline = true; s.saturation = DN(1.25f, 1.15f); s.vibrance = 0.3f; s.bloomIntensity = DN(0.28f, 0.35f);
                s.tonemap = TM_REINHARD; s.hdrAmount = DN(0.3f, 0.3f); break;
        case 5: s.gradePreset = 4; s.contrast = DN(1.25f, 1.25f); s.vignetteStrength = DN(0.55f, 0.6f); s.grain = true; s.grainStrength = 0.4f;
                s.bloomIntensity = DN(0.3f, 0.35f); break;
        case 6: s.vhs = true; s.vhsStrength = 0.7f; s.chromatic = true; s.grain = true; s.saturation = DN(0.9f, 0.85f); s.gradePreset = 5;
                s.gradeAmount = 0.8f; s.vignetteStrength = DN(0.45f, 0.5f); break;
    }
    ++g_worldGen;
}

float PS_NightFactor(int h, int m)
{
    float t = (float)h + (float)m / 60.f;
    auto sm = [](float a, float b, float x) { float k = x < a ? 0.f : (x > b ? 1.f : (x - a) / (b - a)); return k * k * (3.f - 2.f * k); };
    if (t >= 12.f) return sm(18.f, 21.f, t);          // dusk 18:00 -> 21:00
    return 1.f - sm(4.f, 7.f, t);                     // dawn 04:00 -> 07:00
}

void PS_UpdateLive(PSSettings& s, float nf)
{
    if (s.timeOfDay == 1) nf = 0.f; else if (s.timeOfDay == 2) nf = 1.f;
    DN* all[] = { &s.hdrAmount, &s.exposure, &s.bloomIntensity, &s.contrast, &s.saturation, &s.vignetteStrength, &s.fogDensity };
    for (DN* d : all) d->v = d->day + (d->night - d->day) * nf;
}

void PS_LutDir(const PSSettings& s, const char* base, char* out, int n)
{
    const char* f = s.lutFolder;
    while (*f == ' ' || *f == '\t') ++f;
    if (!*f || !strcmp(f, ".")) snprintf(out, n, "%s", base);
    else if (f[0] == '/') snprintf(out, n, "%s", f);
    else snprintf(out, n, "%s/%s", base, f);
    int l = (int)strlen(out); while (l > 1 && out[l - 1] == '/') out[--l] = 0;
}
