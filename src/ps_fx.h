// Proper Shader Mobile - screen-space post-processing pipeline (GLES2)
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

struct PSFxStats
{
    int   adaptiveLevel = 0;              // 0 = full quality .. 3 = minimal
    float avgFrameMs = 0.f;
    int   passes = 0;                     // GL passes issued last frame
    int   variants = 0;                   // compiled final-shader variants in cache
    float fullResFetches = 0.f;           // estimated full-resolution texture fetches per pixel (all passes)
};

// All FX_* functions must be called from the GL thread, with the game's context current and the caller
// having saved/restored GL state (see PSGLState). `s` must already contain the live (day/night) values.
bool        FX_Render(const PSSettings& s, const PSFrameInfo& fi);
void        FX_ContextLost();             // forget GL names (context was destroyed)
void        FX_Shutdown();                // delete GL objects (context still current)
bool        FX_LoadLut(const char* path); // 256x16 strip, .bmp or .tga
bool        FX_LutLoaded();
const char* FX_Status();
bool        FX_Failed();
const PSFxStats& FX_Stats();
