// Proper GFX Mobile - dynamic sun shadows.
// Opaque world draws of a frame are captured (world matrix + buffers), re-rendered depth-only from the sun direction into a shadow map
// and sampled by the game's own object shaders (patched in ps_world.cpp). The sun direction is the game's own DirLightDirection, so it follows
// the time of day (the game clock, and so anything that sets it); strength follows timecyc.dat's Shdw column.
#pragma once
#include "ps_settings.h"
#include <GLES2/gl2.h>
#include <stdint.h>

struct PSShadowStats
{
    bool  enabled = false, ready = false;
    int   captured = 0, replayed = 0, rejected = 0, resolution = 0;
    // why draws were not captured (per capture frame) - shown in the menu / logcat so a failing setup can be diagnosed from one screenshot
    int   rejBlend = 0, rejNoDepthWrite = 0, rejNoIndexBuffer = 0, rejVertexFormat = 0, rejNoShaderInfo = 0, rejMode = 0, rejDistance = 0, rejStale = 0;
    float sunDir[3] = {0, 0, 1};   // direction toward the sun that the map was rendered with
    int   sunSource = 0;           // 1 = game time-cycle sun vector, 2 = directional-light uniform
    int   cacheSize = 0, cacheKept = 0, otherTargets = 0;   // casters remembered from earlier frames / kept this frame although the game culled them / draws into other render targets (ignored)
    bool  guardTripped = false;
    float timecycMul = 1.f; int timecycRaw = -1, timecycMax = 0; bool timecycValid = false;   // timecyc.dat 'Shdw' value for the current hour/weather
    float sunElevation = 0.f;      // sin(elevation): 0 = horizon, 1 = zenith
    float cpuMs = 0.f;             // CPU time of the last replay
    float fade = 0.f;              // 0 = no shadows (night), 1 = full
    char  note[96] = "";
};

void   Shadow_Configure(const PSSettings& s, int perfLevel);   // once per frame (render thread)
void   Shadow_SetTimecyc(float mul, int raw, int rawMax, bool valid);   // strength multiplier derived from CTimeCycle::m_nShadowStrength (timecyc.dat)
void   Shadow_SetSunHint(const float v[3], bool valid);        // CTimeCycle::m_VectorToSun[current] (points TOWARD the sun); once per frame before Shadow_EndFrame
bool   Shadow_WantCapture();                                   // true while this frame's draws are being recorded
void   Shadow_RecordDraw(int mode, int count, uintptr_t offset);   // call right AFTER the game issued the draw
void   Shadow_EndFrame();
// 1.4.6: the game's alpha-test state (RQ_Command_rqSetAlphaTest statics) and buffer writes seen by the GOT hooks on libGTASA's GL imports
void   Shadow_SetAlphaTestState(const int* func, const float* ref);
void   Shadow_SetBufferTracking(bool on);
void   Shadow_OnBufferWrite(GLenum target);
void   Shadow_OnBufferDelete(GLsizei n, const GLuint* ids);                                      // at swap: replays the capture (GL state must be saved by the caller)
void   Shadow_Uniforms(float lightVP[16], float params[4]);    // values for the object shaders (zero = shadows off)
GLuint Shadow_Texture();
uint32_t Shadow_FrameId();
bool   Shadow_SetGuardFailed(bool failed);
void   Shadow_ClearProgramCache();                             // GL program ids may be recycled
void   Shadow_ContextLost();
void   Shadow_Shutdown();
const PSShadowStats& Shadow_Stats();

// exposed for tests
void   Shadow_BuildLightMatrix(const float sunDir[3], const float center[3], float radius, float depthRange, int res, float outColMajor[16]);
