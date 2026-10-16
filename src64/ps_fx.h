// Proper GFX Mobile (64-bit) - screen-space post-processing pipeline (GLES2). Every pass is a .glsl file (see ps_glsl.h).
#pragma once
#include "ps_settings.h"

struct PSFrameInfo
{
    int   w = 0, h = 0;
    float time = 0.f, dt = 0.016f;
    float underwater = 0.f;               // 0..1 (CWeather::UnderWaterness)
    float sunX = 0.5f, sunY = 0.5f;       // screen UV of the sun
    float sunVis = 0.f;                   // 0 = off screen / below horizon
};

// Values for the SAShaderL-compatible uniforms (ShaderFlags is set per pass) + Proper GFX extras, filled by main.cpp every frame
struct PSShaderGlobals
{
    float sunVector[3] = { 0.f, 0.f, 1.f };   // SunVector
    int   timeMs = 0;                        // Time
    int   gameTimeSeconds = 0;               // GameTimeSeconds (hours*60 + minutes: 01:02 AM = 62)
    float underWaterness = 0.f;              // UnderWaterness
    float roadsWetness = 0.f;                // RoadsWetness
    float farClipDist = 800.f;               // FarClipDist
    int   entityModel = -1;                  // EntityModel
    float timeSec = 0.f;                     // PGFX_TimeSec
    float user[8] = { 0 };                   // PGFX_User0 / PGFX_User1
};
void FX_SetGlobals(const PSShaderGlobals& g);
const PSShaderGlobals& FX_Globals();

struct PSFxStats
{
    int   adaptiveLevel = 0;              // 0 = full quality .. 3 = minimal
    float avgFrameMs = 0.f;
    int   passes = 0;                     // GL passes issued last frame
    int   variants = 0;                   // compiled final-shader variants in cache
    float fullResFetches = 0.f;           // estimated full-resolution texture fetches per pixel (all passes)
    int   glslErrors = 0;                 // .glsl files that failed and fell back to the built-in copy
    unsigned glslGen = 0;                 // shader generation the programs were built from
};

// All FX_* functions must be called from the GL thread, with the game's context current and the caller
// having saved/restored GL state (see PSGLState). `s` must already contain the live (day/night) values.
bool        FX_Render(const PSSettings& s, const PSFrameInfo& fi);
void        FX_ContextLost();             // forget GL names (context was destroyed)
void        FX_Shutdown();                // delete GL objects (context still current)
bool        FX_LutLoaded();
const char* FX_Status();
bool        FX_Failed();
const PSFxStats& FX_Stats();
