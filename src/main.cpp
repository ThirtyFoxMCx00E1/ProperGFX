// Proper GFX Mobile (v1.3.2) - AML mod for GTA San Andreas Android (2.00 armeabi-v7a / 2.10 arm64-v8a)
// Everything is hooked by exported symbol name, so one source tree serves both builds.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <chrono>
#include "ps_settings.h"
#include "ps_glstate.h"
#include "ps_fx.h"
#include "ps_ui.h"
#include "ps_world.h"
#include "ps_shadow.h"
#include "ps_log.h"

#if defined(__LP64__)
  #define PS_BITS 64
  #define PS_GAME_INFO "GTA SA 2.10 (arm64-v8a)"
#else
  #define PS_BITS 32
  #define PS_GAME_INFO "GTA SA 2.00 (armeabi-v7a)"
#endif

MYMOD(net.propergfx.mobile, Proper GFX Mobile, 1.3.2, ProperGFX)
NEEDGAME(com.rockstargames.gtasa)

// ---------------------------------------------------------------- game data (all optional, null-checked)
static float* g_pWetRoads = nullptr;
static float* g_pUnderWater = nullptr;
static float* g_pVectorToSun = nullptr;    // CVector[16]
static int*   g_pSunIndex = nullptr;       // CTimeCycle::m_CurrentStoredValue
static void*  g_pCamera = nullptr;         // &TheCamera
static float* g_pFov = nullptr;            // CDraw::ms_fFOV
static float* g_pAspect = nullptr;         // CDraw::ms_fAspectRatio
static unsigned char* g_pClockH = nullptr; // CClock::ms_nGameClockHours
static unsigned char* g_pClockM = nullptr; // CClock::ms_nGameClockMinutes
static unsigned char* g_pShdwTable = nullptr;  // CTimeCycle::m_nShadowStrength[8 hours][23 weathers] = the 'Shdw' column of timecyc.dat
static void UpdateTimecycStrength();
static short* g_pWeatherOld = nullptr; static short* g_pWeatherNew = nullptr; static float* g_pWeatherInterp = nullptr;

static PSSettings g_cfg;
static char g_dir[512] = "";
static char g_iniPath[600] = "";
static bool g_initDone = false;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static int  g_sunBad = 0;
static bool g_sunTracked = false;
static char g_guardPath[640] = "";
static bool g_guardActive = false, g_guardTripped = false;
static int  g_guardFrames = 0, g_guardTotal = 0;

static bool Readable(const void* p, size_t n)
{
    if (!p) return false;
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)4095, e = ((uintptr_t)p + n + 4095) & ~(uintptr_t)4095;
    unsigned char vec[8];
    for (uintptr_t pg = a; pg < e; pg += 4096) if (mincore((void*)pg, 4096, vec) != 0) return false;
    return true;
}

// Sun direction -> screen UV, using the camera matrix. Returns visibility 0..1.
static float ComputeSun(float& su, float& sv)
{
    g_sunTracked = false;
    if (g_sunBad > 30 || !g_pVectorToSun || !g_pSunIndex || !g_pCamera) return 0.f;
    int idx = *g_pSunIndex; if (idx < 0 || idx > 15) return 0.f;
    const float* sun = g_pVectorToSun + idx * 3;
    // CPlaceable: vtable, CSimpleTransform(pos[3], heading), CMatrix* m_matrix
    const size_t off = sizeof(void*) + 16;
    void* mp = *(void**)((char*)g_pCamera + off);
    if (!Readable(mp, 0x40)) { ++g_sunBad; return 0.f; }
    const float* m = (const float*)mp;       // right@0 top@4 at@8 (16-byte rows)
    const float* R = m; const float* U = m + 4; const float* F = m + 8;
    auto dot = [](const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
    auto len = [&](const float* a) { return sqrtf(dot(a, a)); };
    float lr = len(R), lu = len(U), lf = len(F);
    if (!(lr > 0.9f && lr < 1.1f && lu > 0.9f && lu < 1.1f && lf > 0.9f && lf < 1.1f) || fabsf(dot(R, F)) > 0.2f)
    { ++g_sunBad; return 0.f; }
    g_sunBad = 0;
    float x = dot(sun, R), y = dot(sun, U), z = dot(sun, F);
    float fov = (g_pFov && *g_pFov > 10.f && *g_pFov < 170.f) ? *g_pFov : 70.f;
    float asp = (g_pAspect && *g_pAspect > 0.5f && *g_pAspect < 4.f) ? *g_pAspect : 16.f / 9.f;
    g_sunTracked = true;
    if (z < 0.05f) return 0.f;
    float halfX = tanf(fov * 0.5f * 3.14159265f / 180.f), halfY = halfX / asp;
    su = (x / z) / halfX * 0.5f + 0.5f; sv = (y / z) / halfY * 0.5f + 0.5f;
    float horizon = sun[2] < -0.05f ? 0.f : (sun[2] > 0.15f ? 1.f : (sun[2] + 0.05f) / 0.2f);
    float edge = fmaxf(fabsf(su - 0.5f), fabsf(sv - 0.5f));
    float onscreen = edge < 0.7f ? 1.f : fmaxf(0.f, 1.f - (edge - 0.7f) / 0.8f);
    return horizon * onscreen;
}

// ---------------------------------------------------------------- per-frame work (render thread, before the buffer swap)
static void OnFrame()
{
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return;
    if (ctx != g_ctx)
    {
        if (g_ctx != EGL_NO_CONTEXT) { FX_ContextLost(); UI_ContextLost(); World_ClearProgramCache(); }
        g_ctx = ctx;
        PSGLState::InitCaps();
        g_initDone = false;
    }
    EGLDisplay dpy = eglGetCurrentDisplay(); EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    EGLint w = 0, h = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &w) || !eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) || w < 16 || h < 16) return;

    static auto last = std::chrono::steady_clock::now();
    static double t0 = 0.0, accum = 0.0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count(); last = now;
    if (dt > 0.25f) dt = 0.25f; if (dt < 0.0005f) dt = 0.0005f;
    accum += dt; (void)t0;

    // time of day -> live Day/Night values
    float nf = (g_pClockH && g_pClockM && *g_pClockH < 24 && *g_pClockM < 60) ? PS_NightFactor(*g_pClockH, *g_pClockM) : 0.f;
    PS_UpdateLive(g_cfg, nf);
    float wet = g_pWetRoads ? *g_pWetRoads : 0.f;
    World_SetEnabled(g_cfg.enabled);
    Shadow_Configure(g_cfg, FX_Stats().adaptiveLevel);
    {   // the game's own sun vector (points TOWARD the sun, world space, z up) - the same vector the sun disc / coronas use
        float sv[3] = { 0.f, 0.f, 1.f }; bool ok = false;
        if (g_pVectorToSun && g_pSunIndex) { int idx = *g_pSunIndex; if (idx >= 0 && idx < 16) { const float* p = g_pVectorToSun + idx * 3; sv[0] = p[0]; sv[1] = p[1]; sv[2] = p[2]; ok = isfinite(sv[0]) && isfinite(sv[1]) && isfinite(sv[2]); } }
        Shadow_SetSunHint(sv, ok);
        UpdateTimecycStrength();
    }
    if (g_cfg.shadowRetry)
    {   // menu button "Retry shadows": clear the crash guard (takes effect after a game restart, the receivers are compiled into shaders at load)
        g_cfg.shadowRetry = false;
        if (g_guardPath[0]) remove(g_guardPath);
        g_guardTripped = false; g_guardActive = false; g_guardFrames = g_guardTotal = 0;
        if (g_cfg.shadows && g_cfg.shadowGuard && g_guardPath[0])    // re-arm: if the retry itself crashes, the next launch is in safe mode again
        { FILE* gf = fopen(g_guardPath, "w"); if (gf) { fputs("retry in progress\n", gf); fclose(gf); g_guardActive = true; } }
        Shadow_SetGuardFailed(false);
    }

    if (!g_initDone)
    {
        char lut[640]; snprintf(lut, sizeof(lut), "%s/lut.bmp", g_dir);
        if (!FX_LoadLut(lut)) { snprintf(lut, sizeof(lut), "%s/lut.tga", g_dir); FX_LoadLut(lut); }
        g_initDone = true;
    }

    PSGLState gs; gs.Save();
    PSFrameInfo fi; fi.w = w; fi.h = h; fi.time = (float)fmod(accum, 3600.0); fi.dt = dt;
    float uw = g_pUnderWater ? *g_pUnderWater : 0.f; fi.underwater = uw < 0.f ? 0.f : (uw > 1.f ? 1.f : uw);
    fi.sunVis = ComputeSun(fi.sunX, fi.sunY);
    if (g_cfg.enabled && g_cfg.postFx && !FX_Failed()) FX_Render(g_cfg, fi);

    PSUIStatus st; st.fxStatus = FX_Status(); st.gameInfo = PS_GAME_INFO; st.patchedShaders = World_PatchedCount();
    st.sunTracked = g_sunTracked; st.lutLoaded = FX_LutLoaded(); st.fx = &FX_Stats();
    st.shadow = &Shadow_Stats(); st.shadowTex = Shadow_Texture(); st.shadowReceivers = World_ShadowReceiverCount(); st.build = &World_BuildStats();
    UI_Render(g_cfg, w, h, dt, st);
    Shadow_EndFrame();                  // replays this frame's captured world draws into the shadow map (own FBO)
    gs.Restore();
    World_BeginFrame(g_cfg, wet);       // after the replay: the new light matrix and the new map become active together
    World_OnShaderSelected();           // push the new uniforms into the shader the game keeps bound (it may not re-select it this frame)

    // crash guard: the guard file exists from launch until shadows have run for 120 captured frames (or the game has rendered ~1 minute).
    // v1.2 only counted frames with a successful replay (900 of them), so a quit before that - or a setup where nothing was replayed - left the file behind
    // and silently switched shadows off for every later launch.
    if (g_guardActive)
    {
        ++g_guardTotal;
        if (Shadow_Stats().replayed > 0) ++g_guardFrames;
        if (g_guardFrames >= 600 || g_guardTotal >= 2400) { remove(g_guardPath); g_guardActive = false; }
    }
}

// Shadow strength from timecyc.dat: CTimeCycle::m_nShadowStrength is the "Shdw" column of timecyc.dat, one value per (time slot, weather).
// The game blends those slots by the clock (0:00 5:00 6:00 7:00 12:00 19:00 20:00 22:00) and the two weathers by CWeather::InterpolationValue; this does the same
// and turns it into a gentle multiplier (relative to the strongest value in the file) so a modded timecyc.dat / storms / fog change how dark the shadows are.
// (The sun DIRECTION is not stored in timecyc.dat: the time-cycle code computes it from the clock - that is CTimeCycle::m_VectorToSun, used above.)
static void UpdateTimecycStrength()
{
    if (!g_pShdwTable || !g_pClockH || !g_pClockM || !g_pWeatherOld || !g_pWeatherNew || !g_pWeatherInterp) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    static const int kSlot[9] = { 0, 300, 360, 420, 720, 1140, 1200, 1320, 1440 };
    int t = (int)*g_pClockH * 60 + (int)*g_pClockM; int wo = *g_pWeatherOld, wn = *g_pWeatherNew; float ip = *g_pWeatherInterp;
    if (t < 0 || t >= 1440 || wo < 0 || wo > 22 || wn < 0 || wn > 22 || !isfinite(ip)) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    if (ip < 0.f) ip = 0.f; if (ip > 1.f) ip = 1.f;
    int i = 0; while (i < 7 && t >= kSlot[i + 1]) ++i;
    const float f = (float)(t - kSlot[i]) / (float)(kSlot[i + 1] - kSlot[i]); const int i2 = (i + 1) & 7;
    auto at = [&](int h, int w) { return (float)g_pShdwTable[h * 23 + w]; };
    const float vo = at(i, wo) + (at(i2, wo) - at(i, wo)) * f, vn = at(i, wn) + (at(i2, wn) - at(i, wn)) * f;
    const float cur = vo + (vn - vo) * ip;
    int mx = 0; for (int k = 0; k < 8 * 23; ++k) if (g_pShdwTable[k] > mx) mx = g_pShdwTable[k];
    if (mx <= 0) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    float rel = cur / (float)mx; if (rel > 1.f) rel = 1.f; if (rel < 0.4f) rel = 0.4f;     // bounded: a wrong table layout can only soften shadows, never remove them
    Shadow_SetTimecyc(1.f + 0.6f * (rel - 1.f), (int)(cur + 0.5f), mx, true);
}

// ---------------------------------------------------------------- hooks
// NOTE: every hook forwards the original's return value. RQShader::BuildSource returns 1 (success) and the
// caller checks it; for functions that really return void the extra register value is simply ignored.
DECL_HOOKi(OS_ScreenSwapBuffers)
{
    OnFrame();
    return OS_ScreenSwapBuffers();
}

// The game compiles every shader in ES2Shader::Build (render thread, GL context current) and aborts the process when it fails. Patched pairs are test-built first.
DECL_HOOKi(ES2Shader_Build, void* self, const char* pxl, const char* vtx)
{
    std::string p, v;
    if (World_OnShaderBuild(pxl, vtx, p, v)) return ES2Shader_Build(self, p.c_str(), v.c_str());
    return ES2Shader_Build(self, pxl, vtx);
}

DECL_HOOKi(RQ_Command_rqSelectShader, char** ptr)
{
    int r = RQ_Command_rqSelectShader(ptr);
    World_OnShaderSelected();           // game has just called glUseProgram
    return r;
}

DECL_HOOKi(RQ_Command_rqDeleteShader, char** ptr)
{
    World_ClearProgramCache();          // GL program ids may be recycled
    return RQ_Command_rqDeleteShader(ptr);
}

DECL_HOOKi(RQShader_BuildSource, unsigned flags, const char** pxl, const char** vtx)
{
    int r = RQShader_BuildSource(flags, pxl, vtx);
    if (r) World_OnBuildSource(flags, pxl, vtx);   // swaps *pxl / *vtx for patched copies (the caller hashes + strdup()s them right after)
    return r;
}

// Shadow capture: the stream layout {int mode, int count, uintptr_t offset} was read from the disassembly of rqDrawIndexed (2.00 and 2.10).
// Recording happens AFTER the original ran, so the GL state (program, buffers, attribute pointers, uniforms) is exactly what the draw used.
DECL_HOOKi(RQ_Command_rqDrawIndexed, char** ptr)
{
    if (!Shadow_WantCapture()) return RQ_Command_rqDrawIndexed(ptr);
    const char* p = *ptr;
    const int mode = *(const int*)p, count = *(const int*)(p + 4);
    const uintptr_t off = *(const uintptr_t*)(p + 8);
    int r = RQ_Command_rqDrawIndexed(ptr);
    Shadow_RecordDraw(mode, count, off);
    return r;
}

DECL_HOOKv(AND_TouchEvent, int type, int finger, int x, int y)
{
    if (UI_OnTouch(type, finger, x, y)) return;
    AND_TouchEvent(type, finger, x, y);
}

DECL_HOOKp(RwCameraSetNearClipPlane, void* cam, float nearPlane)
{
    if (g_cfg.enabled && g_cfg.zfix) nearPlane = g_cfg.nearClip;
    return RwCameraSetNearClipPlane(cam, nearPlane);
}

// ---------------------------------------------------------------- entry
extern "C" __attribute__((visibility("default"))) void OnModLoad()
{
    logger->SetTag("ProperShader");
    void* hGame = aml->GetLibHandle("libGTASA.so");
    if (!hGame) { logger->Error("libGTASA.so not found"); return; }
    if (aml->HasMod("net.propershader.mobile")) { logger->Error("Proper Shader Mobile 1.2 (libProperShader32/64.so) is also installed - delete it, both cannot run together"); return; }

    const char* base = aml->GetAndroidDataPath();
    snprintf(g_dir, sizeof(g_dir), "%s%sProperShader", base ? base : "/sdcard/Android/data/com.rockstargames.gtasa/files",
             (base && base[0] && base[strlen(base) - 1] == '/') ? "" : "/");
    mkdir(g_dir, 0777);
    {   // kill-switch: create ProperShader/disable.txt to make the mod do nothing at all
        char kill[640]; snprintf(kill, sizeof(kill), "%s/disable.txt", g_dir);
        if (access(kill, F_OK) == 0) { logger->Info("disable.txt found - Proper GFX Mobile stays inactive"); return; }
    }
    snprintf(g_iniPath, sizeof(g_iniPath), "%s/settings.ini", g_dir);
    if (!PS_LoadSettings(g_cfg, g_iniPath)) PS_SaveSettings(g_cfg, g_iniPath);
    UI_SetIniPath(g_iniPath);
    World_SetEnabled(g_cfg.enabled && g_cfg.worldPatch);
    World_SetCompileCel(g_cfg.celWorld);
    {   // shadow crash guard: the guard file exists from launch until shadows ran fine for a while; if it is still there next launch, shadows stay off (safe mode)
        {   // v1.2 used shadow_guard.txt with the broken 900-replay rule and left it behind on almost every launch: it must not put 1.3 into safe mode
            char legacy[640]; snprintf(legacy, sizeof(legacy), "%s/shadow_guard.txt", g_dir);
            if (access(legacy, F_OK) == 0) { remove(legacy); logger->Info("removed the leftover Proper Shader 1.2 shadow_guard.txt"); }
        }
        {   // v1.3.0 / 1.3.1 guard files: written by builds whose patched shaders could abort the game; that cause is fixed (self-test), so they must not keep shadows off
            char old13[640]; snprintf(old13, sizeof(old13), "%s/shadow_guard_v13.txt", g_dir);
            if (access(old13, F_OK) == 0) { remove(old13); logger->Info("removed the 1.3.0/1.3.1 shadow guard file"); }
        }
        snprintf(g_guardPath, sizeof(g_guardPath), "%s/shadow_guard_v132.txt", g_dir);
        snprintf(PS_LogFilePath(), 640, "%s/propergfx_log.txt", g_dir);
        { FILE* lf = fopen(PS_LogFilePath(), "rb"); if (lf) { fseek(lf, 0, SEEK_END); long sz = ftell(lf); fclose(lf); if (sz > 96 * 1024) remove(PS_LogFilePath()); } }
        PS_FileLog("---- launch: Proper GFX Mobile 1.3.2 (%d-bit) ----", PS_BITS);
        if (g_cfg.shadows && g_cfg.shadowGuard && access(g_guardPath, F_OK) == 0) { g_guardTripped = true; logger->Info("shadow_guard_v132.txt found: shadows are in SAFE MODE (previous launch did not finish). Menu > DynamicShadows > Retry shadows, then restart."); }
        else if (g_cfg.shadows && g_cfg.shadowGuard) { FILE* gf = fopen(g_guardPath, "w"); if (gf) { fputs("exists while the first minute of shadows has not finished; Retry shadows in the menu or CrashGuard = 0 in settings.ini disables this\n", gf); fclose(gf); g_guardActive = true; } }
        if (g_guardTripped) Shadow_SetGuardFailed(true);
        World_SetCompileShadows(g_cfg.shadows && !g_guardTripped);
    }

    SET_TO(g_pWetRoads,    aml->GetSym(hGame, "_ZN8CWeather8WetRoadsE"));
    SET_TO(g_pUnderWater,  aml->GetSym(hGame, "_ZN8CWeather14UnderWaternessE"));
    SET_TO(g_pVectorToSun, aml->GetSym(hGame, "_ZN10CTimeCycle13m_VectorToSunE"));
    SET_TO(g_pSunIndex,    aml->GetSym(hGame, "_ZN10CTimeCycle20m_CurrentStoredValueE"));
    SET_TO(g_pCamera,      aml->GetSym(hGame, "TheCamera"));
    SET_TO(g_pFov,         aml->GetSym(hGame, "_ZN5CDraw7ms_fFOVE"));
    SET_TO(g_pAspect,      aml->GetSym(hGame, "_ZN5CDraw15ms_fAspectRatioE"));
    SET_TO(g_pClockH,      aml->GetSym(hGame, "_ZN6CClock18ms_nGameClockHoursE"));
    SET_TO(g_pClockM,      aml->GetSym(hGame, "_ZN6CClock20ms_nGameClockMinutesE"));
    SET_TO(g_pShdwTable,   aml->GetSym(hGame, "_ZN10CTimeCycle17m_nShadowStrengthE"));
    SET_TO(g_pWeatherOld,  aml->GetSym(hGame, "_ZN8CWeather14OldWeatherTypeE"));
    SET_TO(g_pWeatherNew,  aml->GetSym(hGame, "_ZN8CWeather14NewWeatherTypeE"));
    SET_TO(g_pWeatherInterp, aml->GetSym(hGame, "_ZN8CWeather18InterpolationValueE"));

    HOOKSYM(OS_ScreenSwapBuffers,     hGame, "_Z20OS_ScreenSwapBuffersv");
    HOOKSYM(AND_TouchEvent,           hGame, "_Z14AND_TouchEventiiii");
    HOOKSYM(RwCameraSetNearClipPlane, hGame, "_Z24RwCameraSetNearClipPlaneP8RwCameraf");
    if (g_cfg.worldPatch)
    {
        HOOKSYM(RQ_Command_rqDrawIndexed, hGame, "_Z24RQ_Command_rqDrawIndexedRPc");   // always: free while not capturing, lets Medium/High switch shadows on live
        HOOKSYM(ES2Shader_Build,          hGame, "_ZN9ES2Shader5BuildEPKcS1_");        // installed before the source hook: nothing patched may reach the game untested
        HOOKSYM(RQShader_BuildSource,     hGame, "_ZN8RQShader11BuildSourceEjPPKcS2_");
        HOOKSYM(RQ_Command_rqSelectShader, hGame, "_Z25RQ_Command_rqSelectShaderRPc");
        HOOKSYM(RQ_Command_rqDeleteShader, hGame, "_Z25RQ_Command_rqDeleteShaderRPc");
    }
    logger->Info("Proper GFX Mobile 1.3.2 loaded (%d-bit). Config: %s", PS_BITS, g_iniPath);
}
