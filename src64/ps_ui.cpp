#include "ps_ui.h"
#include "ps_features.h"
#include "ps_guide.h"
#include "ps_log.h"
#include "ps_fx.h"
#include "ps_world.h"
#include "ps_lut.h"
#include "ps_glsl.h"
#include "ps_scene.h"
#include "ps_shadow.h"
#include <atomic>
#include <strings.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <GLES2/gl2.h>
#include <float.h>
#include <mutex>
#include <chrono>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace {

// ---------------------------------------------------------------- touch plumbing
struct TouchEv { int type, finger, x, y; bool primary; };
std::mutex g_mx;
std::vector<TouchEv> g_queue;
struct Rect { float x0, y0, x1, y1; bool valid; } g_rects[2] = {};   // [0] floating button, [1] menu window
bool g_captured[16] = {};
bool g_hiddenReset = false;            // 1.4.6: the UI was hidden (not in gameplay)
int  g_primary = -1;

bool InRects(float x, float y)
{
    for (const Rect& r : g_rects) if (r.valid && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return true;
    return false;
}

// ---------------------------------------------------------------- ui state
ImGuiContext* g_ctx = nullptr;
bool  g_menuOpen = false;
bool  g_dirty = false;               // settings changed -> save when interaction ends
char  g_ini[512] = "";
float g_fontPx = 0.f;
int   g_screenW = 0, g_screenH = 0;
int   g_preset = 0;
int   g_testTab = -1;
// floating button interaction
bool  g_pressing = false, g_dragging = false;
float g_idle = 0.f;                       // seconds since the floating button was last touched
long long g_lastEvt = 0;                  // ms of the last captured touch event (watchdog)
long long NowMs() { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
ImVec2 g_pressPos, g_grabOffset;

const char* kFontFiles[] = {
    "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf",
    "/system/fonts/SamsungOne-400.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr };

void ApplyStyle(float sc)
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 6; st.FrameRounding = 4; st.GrabRounding = 4; st.ScrollbarRounding = 4; st.TabRounding = 4; st.ChildRounding = 4;
    st.WindowBorderSize = 1; st.FrameBorderSize = 0; st.PopupBorderSize = 0;
    st.WindowPadding = ImVec2(12, 10); st.FramePadding = ImVec2(8, 6); st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6); st.GrabMinSize = 22; st.ScrollbarSize = 30; st.IndentSpacing = 16;
    st.SeparatorTextBorderSize = 2; st.SeparatorTextPadding = ImVec2(18, 4); st.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    st.ScaleAllSizes(sc);
    // Proper Shaders identity: deep plum + coral/orange (sampled from the PROPER SHADERS logo)
    ImVec4* c = st.Colors;
    const ImVec4 orange(0.93f, 0.50f, 0.27f, 1.0f), orangeHi(1.0f, 0.62f, 0.38f, 1.0f);
    c[ImGuiCol_WindowBg]        = ImVec4(0.070f, 0.040f, 0.090f, 0.965f);
    c[ImGuiCol_ChildBg]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]         = ImVec4(0.10f, 0.055f, 0.12f, 0.98f);
    c[ImGuiCol_Border]          = ImVec4(0.93f, 0.50f, 0.27f, 0.35f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.12f, 0.05f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.19f, 0.07f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.085f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.24f, 0.12f, 0.23f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.30f, 0.15f, 0.27f, 1.00f);
    c[ImGuiCol_SliderGrab]      = orange;
    c[ImGuiCol_SliderGrabActive]= orangeHi;
    c[ImGuiCol_CheckMark]       = orangeHi;
    c[ImGuiCol_Button]          = ImVec4(0.36f, 0.14f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.50f, 0.20f, 0.38f, 1.00f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.93f, 0.50f, 0.27f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.30f, 0.12f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.42f, 0.17f, 0.33f, 1.00f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.52f, 0.22f, 0.38f, 1.00f);
    c[ImGuiCol_Separator]       = ImVec4(0.93f, 0.50f, 0.27f, 0.45f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.04f, 0.02f, 0.06f, 0.8f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.45f, 0.19f, 0.36f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.26f, 0.44f, 1.0f);
    c[ImGuiCol_ResizeGrip]      = ImVec4(0.93f, 0.50f, 0.27f, 0.35f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.58f, 0.50f, 0.60f, 1.0f);
}

void BuildFonts(float px)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    ImFont* f = nullptr;
    for (int i = 0; kFontFiles[i] && !f; ++i)
    {
        FILE* t = fopen(kFontFiles[i], "rb");
        if (!t) continue;
        fclose(t);
        f = io.Fonts->AddFontFromFileTTF(kFontFiles[i], px);
    }
    if (!f) { ImFontConfig fc; fc.SizePixels = px; f = io.Fonts->AddFontDefault(&fc); }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_fontPx = px;
}

// ---------------------------------------------------------------- widget helpers
int g_testMode = 0; float g_testScroll = 0.f;
bool g_worldTouched = false, g_userEdited = false, g_presetPressed = false, g_featDisabled = false;
#define TOUCHED(world) do { if (ImGui::IsItemEdited()) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; } } while (0)

bool SliderF(const char* l, float* v, float a, float b, const char* fmt = "%.2f", bool world = false)
{
    bool ch = ImGui::SliderFloat(l, v, a, b, fmt, ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
    TOUCHED(world);
    return ch;
}
// Day / Night pair (the PC mod keeps separate values for day and night, blended by the game clock)
void SliderDN(const char* l, DN* d, float a, float b, const char* fmt = "%.2f", bool world = false)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  (day)", l);   SliderF(buf, &d->day, a, b, fmt, world);
    snprintf(buf, sizeof(buf), "%s  (night)", l); SliderF(buf, &d->night, a, b, fmt, world);
}
bool Check(const char* l, bool* v, bool world = false)
{
    bool ch = ImGui::Checkbox(l, v);
    TOUCHED(world);
    return ch;
}
bool Combo(const char* l, int* v, const char* const* items, int n, bool world = false)
{
    bool ch = ImGui::Combo(l, v, items, n);
    if (ch) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; }
    return ch;
}

// One feature = one ini section: [Enabled] checkbox + collapsible header. Parameters are greyed out while disabled.
bool FeatureBegin(const char* title, bool* en, bool world = false)
{
    ImGui::PushID(title);
    if (ImGui::Checkbox("##en", en)) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; }
    ImGui::SameLine();
    if (g_testMode) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    bool open = ImGui::CollapsingHeader(title);
    if (!open) { ImGui::PopID(); return false; }
    g_featDisabled = !*en;
    if (g_featDisabled) ImGui::BeginDisabled();
    ImGui::Indent();
    return true;
}
void FeatureEnd()
{
    ImGui::Unindent();
    if (g_featDisabled) ImGui::EndDisabled();
    g_featDisabled = false;
    ImGui::PopID();
}
void Note(const char* t) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", t); ImGui::PopStyleColor(); }

void DrawFeatures(PSSettings& s, const PSUIStatus& st)
{
    // ---------------------------------------------------------------- presets (Low / Medium / High, like the PC mod)
    ImGui::SeparatorText("PRESET");
    {
        float avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x, bw = (avail - sp * 2.f) / 3.f;
        for (int i = 0; i < 3; ++i)
        {
            bool sel = s.quality == i;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(PS_QualityNames[i], ImVec2(bw, 0))) { PS_ApplyQuality(s, i); if (i >= 1) s.shadowRetry = true; g_presetPressed = true; g_dirty = true; g_worldTouched = true; }   // Medium / High also clear shadow safe mode
            if (sel) ImGui::PopStyleColor();
            if (i < 2) ImGui::SameLine();
        }
        Note("Low = fast and smooth: no shadows, bloom or sun rays. Medium / High = shadows, bloom and sun rays.");
        if (s.quality == Q_CUSTOM) Note("Custom: you changed something after picking a preset.");
    }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.58f);
    { int l = s.look; if (Combo("Look", &l, PS_LookNames, PS_LookCount)) { PS_ApplyLook(s, l); g_presetPressed = true; g_worldTouched = true; } }
    Combo("Time of day", &s.timeOfDay, PS_TimeNames, PS_TimeCount);
    Check("Adaptive quality", &s.adaptive);
    { static const char* fps[] = { "30 FPS", "60 FPS" }; Combo("Target FPS", &s.targetFps, fps, 2); Note("Set this to the game's FPS limit."); }
    if (st.fx && st.fx->adaptiveLevel > 0) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Adaptive level %d active (heavy effects reduced)", st.fx->adaptiveLevel);

    // ---------------------------------------------------------------- lighting & atmosphere
    ImGui::SeparatorText("LIGHTING & ATMOSPHERE");
    if (FeatureBegin("SimulateHDR - HDR & tone mapping", &s.hdr))
    {
        Combo("Tonemapper", &s.tonemap, PS_TonemapNames, PS_TonemapCount);
        SliderDN("HDR amount", &s.hdrAmount, 0.f, 1.f);
        SliderDN("Exposure (EV)", &s.exposure, -3.f, 3.f);
        FeatureEnd();
    }
    if (FeatureBegin("AutoExposure - self-exposure", &s.autoExp))
    {
        SliderF("Bias (EV)", &s.autoBias, -2.f, 2.f);
        SliderF("Range (EV)", &s.autoRange, 0.f, 3.f);
        SliderF("Speed", &s.autoSpeed, 0.1f, 5.f);
        FeatureEnd();
    }
    if (FeatureBegin("HorizonFog - new horizon fog", &s.fogOn, true))
    {
        SliderDN("Density", &s.fogDensity, 0.f, 3.f, "%.2f", true);
        SliderF("Softness", &s.fogSoftness, -0.5f, 1.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("WorldRainfall - wet surfaces", &s.wetWorld, true))
    {
        SliderF("Strength", &s.wetStrength, 0.f, 2.f, "%.2f", true);
        Note("Darkens the lit world while it rains (CWeather::WetRoads).");
        FeatureEnd();
    }
    const bool shPrev = s.shadows;
    const bool shOpen = FeatureBegin("DynamicShadows - sun shadows (game clock + timecyc.dat)", &s.shadows);
    if (!shPrev && s.shadows) s.shadowRetry = true;      // switching shadows on also clears safe mode
    if (shOpen)
    {
        s.shadowMethod = 0;                                 // 1.4.6: always Deferred (like ZyZGFX64), the 1.4.2 in-shader method was removed
        Check("Half-resolution mask (faster)", &s.shadowMaskHalf);
        SliderF("Strength", &s.shadowStrength, 0.f, 1.f);
        { static const char* res[] = { "512", "1024", "2048" }; Combo("Resolution", &s.shadowRes, res, 3); }
        SliderF("Distance (m)", &s.shadowDist, 25.f, 160.f, "%.0f");
        { int e = s.shadowEvery; if (ImGui::SliderInt("Update every N frames", &e, 1, 10, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp)) { s.shadowEvery = e; g_userEdited = true; g_dirty = true; } }
        SliderF("Softness", &s.shadowSoft, 0.3f, 3.f);
        SliderF("Bias", &s.shadowBias, 0.3f, 3.f);
        SliderF("Fade below sun elevation", &s.shadowMinSun, 0.f, 0.4f);
        SliderF("Sun smoothing", &s.sunSmoothing, 0.005f, 1.f, "%.3f");
        Check("Show shadow map (debug)", &s.shadowDebug);
        Combo("Sun source", &s.sunSource, PS_SunSourceNames, PS_SunSourceCount);
        Check("Strength follows timecyc.dat (hour / weather)", &s.shadowTimecyc);
        Check("Crash guard (safe mode after a failed launch)", &s.shadowGuard);
        SliderF("Shadow tint amount", &s.shadowTint, 0.f, 1.f);
        SliderF("Tint red", &s.shadowTintR, 0.f, 1.f); SliderF("Tint green", &s.shadowTintG, 0.f, 1.f); SliderF("Tint blue", &s.shadowTintB, 0.f, 1.f);
        Note("Deferred (like ZyZGFX64): casters -> 2 cascades (shadow_depth.glsl), then shadow_mask.glsl rebuilds every pixel's position from the scene depth and darkens it (shadow_apply.glsl). Game shaders are not touched.");
        Note("Trees cast their leaves (cut out with the game's alpha test), LOD stand-ins do not cast near you, and only static buildings are remembered out of view - moved / streamed-out objects leave no ghost shadow.");
        Note("Sun source Auto = the game's time-cycle sun vector (follows the game clock). Strength can follow the Shdw column of timecyc.dat.");
        Note("Buildings, roads, terrain, trees and vehicles cast; everything on screen receives (pedestrians too). Applies right away, no restart.");
        if (st.shadow)
        {
            const PSShadowStats& sh = *st.shadow;
            ImGui::TextWrapped("Status: %s", sh.note[0] ? sh.note : (sh.ready ? "ok" : "waiting for the first frame"));
            ImGui::Text("Method: Deferred   shadows %s%s", sh.enabled ? "on" : "OFF",
                        (st.fx && st.fx->adaptiveLevel >= 2) ? "   (adaptive quality: slower updates, half-res mask)" : "");
            ImGui::Text("Casters: %d captured, %d rendered, %d skipped", sh.captured, sh.replayed, sh.rejected);
            ImGui::Text("Remembered: %d casters (%d kept although out of view), %d draws into other targets ignored", sh.cacheSize, sh.cacheKept, sh.otherTargets);
            {
                static const char* kCam[4] = { "none", "CameraPosition", "view matrix + TheCamera", "last frame (reused)" };
                ImGui::Text("Camera: %s, %d other views rejected (sky / mirror / reflections)", kCam[sh.camSource & 3], sh.camRejected);
            }
            if (st.scene)
            {
                const PSSceneStats& sc = *st.scene;
                if (sc.rqReady) ImGui::Text("Render queue: command slot %d%s, events posted %u / ran %u", sc.rqSlot, sc.chained ? " (chained)" : "", sc.posted[2], sc.ran[2]);
                else ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Render queue: %s", sc.rqSymbols ? (sc.rqBadLayout ? "layout not recognised" : "waiting") : "symbols missing");
                if (sc.depthTex) ImGui::Text("Scene target: FBO %d %dx%d, depth texture %u (%s%d-bit), swaps %d", sc.sceneFbo, sc.w, sc.h, sc.depthTex, sc.foreignDepth ? "from another mod, " : "", sc.depthBits, sc.swaps);
                else ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Scene target: no readable depth yet");
                if (sc.note[0]) ImGui::TextWrapped("Scene: %s", sc.note);
            }
            if (st.build)
            {
                const PSBuildStats& b = *st.build;
                ImGui::Text("Shader self-test: %d tested, %d ok, %d simplified, %d without shadow, %d original", b.tested, b.passed, b.simplified, b.noShadow, b.original);
                if (b.tested > b.passed) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Your GPU driver rejected %d patched shaders (they fell back safely, no crash). Details: files/" PS_DIR_NAME "/propergfx_log.txt", b.tested - b.passed);
                if (b.shadowDisabled) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1), "Shadow receivers are off for this session: the driver rejects them. Send propergfx_log.txt.");
            }
            ImGui::Text("Map %d px   sun elevation %.2f   strength %.0f%%   %.1f ms", sh.resolution, sh.sunElevation, sh.fade * 100.f, sh.cpuMs);
            if (sh.timecycValid) ImGui::Text("timecyc.dat Shdw %d / %d  ->  strength x%.2f", sh.timecycRaw, sh.timecycMax, sh.timecycMul); else ImGui::TextDisabled("timecyc.dat strength: not available");
            ImGui::Text("Sun (%.2f %.2f %.2f) from %s", sh.sunDir[0], sh.sunDir[1], sh.sunDir[2], sh.sunSource == 1 ? "time cycle" : (sh.sunSource == 2 ? "light uniform" : "-"));
            ImGui::TextDisabled("skipped: blend %d, no depth write %d, no index buf %d, vertex fmt %d, shader %d, mode %d, far %d, stale %d",
                                sh.rejBlend, sh.rejNoDepthWrite, sh.rejNoIndexBuffer, sh.rejVertexFormat, sh.rejNoShaderInfo, sh.rejMode, sh.rejDistance, sh.rejStale);
            ImGui::TextDisabled("not objects (clouds / water / effects) %d, LOD %d; entity tags %s, buffer tracking %s (%u writes)",
                                sh.rejUntagged, sh.rejLod, sh.tagsOn ? "on" : "MISSING", sh.bufTracked ? "on" : "off", (unsigned)sh.bufWrites);
            if (sh.guardTripped || (sh.note[0] && strstr(sh.note, "SAFE MODE")))
                if (ImGui::Button("Retry shadows (picking Medium / High does this too)")) s.shadowRetry = true;
        }
        FeatureEnd();
    }
    if (FeatureBegin("SurfaceShading - specular & reflections", &s.surfaceOn, true))
    {
        SliderF("Specular", &s.specularMul, 0.f, 3.f, "%.2f", true);
        SliderF("Vehicle reflections", &s.reflectionMul, 0.f, 3.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("WaterSurface", &s.waterOn, true))
    {
        SliderF("Detail", &s.waterDetail, 0.f, 3.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("Underwater", &s.underwater))
    {
        SliderF("Strength", &s.underwaterStrength, 0.f, 1.f);
        Note("Distortion, tint and caustic shimmer.");
        FeatureEnd();
    }

    // ---------------------------------------------------------------- image
    ImGui::SeparatorText("IMAGE");
    if (FeatureBegin("Bloom", &s.bloom))
    {
        SliderDN("Intensity", &s.bloomIntensity, 0.f, 1.5f);
        SliderF("Threshold", &s.bloomThreshold, 0.2f, 1.f);
        SliderF("Radius", &s.bloomRadius, 0.3f, 3.f);
        FeatureEnd();
    }
    if (FeatureBegin("SunRays", &s.sunRays))
    {
        SliderF("Intensity", &s.sunRaysIntensity, 0.f, 1.5f);
        SliderF("Decay", &s.sunRaysDecay, 0.80f, 0.99f);
        if (!st.sunTracked) Note("Sun position not available yet (camera not found).");
        FeatureEnd();
    }
    if (FeatureBegin("SunVector - sun glow & golden hour (sunvector.glsl)", &s.sunVectorOn))
    {
        SliderF("Sun glow", &s.sunGlow, 0.f, 1.5f);
        SliderF("Glow size", &s.sunGlowSize, 0.03f, 0.6f);
        SliderF("Golden hour (low sun warmth)", &s.goldenHour, 0.f, 1.f);
        const PSShaderGlobals& g = FX_Globals();
        ImGui::Text("SunVector (%.2f %.2f %.2f)   clock %02d:%02d", g.sunVector[0], g.sunVector[1], g.sunVector[2], g.gameTimeSeconds / 60, g.gameTimeSeconds % 60);
        Note("Uses the game's sun vector (CTimeCycle::m_VectorToSun): the glow follows the sun disc, golden hour warms the picture at sunrise / sunset.");
        FeatureEnd();
    }
    if (FeatureBegin("LensFlare", &s.lensFlare))
    {
        SliderF("Intensity", &s.lensFlareIntensity, 0.f, 1.5f);
        FeatureEnd();
    }
    if (FeatureBegin("ColorGrading - grade, LUT", &s.grading))
    {
        Combo("Grade", &s.gradePreset, PS_GradeNames, PS_GradeCount);
        SliderF("Grade amount", &s.gradeAmount, 0.f, 1.f);
        SliderDN("Contrast", &s.contrast, 0.5f, 1.6f);
        SliderDN("Saturation", &s.saturation, 0.f, 2.f);
        SliderF("Vibrance", &s.vibrance, -1.f, 1.f);
        SliderF("Temperature", &s.temperature, -1.f, 1.f);
        SliderF("Tint", &s.tint, -1.f, 1.f);
        SliderF("Gamma", &s.gamma, 0.6f, 1.6f);
        SliderF("Lift", &s.lift, -0.2f, 0.2f);
        SliderF("Gain", &s.gain, 0.6f, 1.5f);
        FeatureEnd();
    }
    if (FeatureBegin("LUT - colour lookup texture", &s.lutOn))
    {
        SliderF("Amount", &s.lutAmount, 0.f, 1.f);
        {   // file picker: every image in files/ProperGFX and files/ProperGFX/luts
            static std::vector<std::string> files; static float scan = 99.f; scan += ImGui::GetIO().DeltaTime;
            if (scan > 2.f && st.dir && st.dir[0]) { char ld[700]; PS_LutDir(s, st.dir, ld, sizeof(ld)); LUT_ListFiles(ld, files); scan = 0.f; }
            const bool isAuto = !strcasecmp(s.lutFile, "auto") || !strcasecmp(s.lutFile, "lut") || !s.lutFile[0];
            if (ImGui::BeginCombo("File", isAuto ? "auto (lut.png / .bmp / .tga / .jpg)" : s.lutFile))
            {
                if (ImGui::Selectable("auto (lut.png / .bmp / .tga / .jpg)", isAuto)) { snprintf(s.lutFile, sizeof(s.lutFile), "auto"); g_dirty = true; g_userEdited = true; }
                for (const std::string& f : files)
                    if (ImGui::Selectable(f.c_str(), !strcasecmp(f.c_str(), s.lutFile))) { snprintf(s.lutFile, sizeof(s.lutFile), "%s", f.c_str()); g_dirty = true; g_userEdited = true; }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Reload LUT")) LUT_RequestReload();
        const PSLutInfo& li = LUT_Info();
        if (li.loaded) ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1), "Loaded: %s  %dx%d  (%d^3%s)", li.file, li.w, li.h, li.n, li.flipped ? ", green bottom-up -> turned" : "");
        else ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1), "Not loaded: %s - %s", li.file[0] ? li.file : "lut", li.error[0] ? li.error : "no file");
        { char ld[700]; PS_LutDir(s, st.dir ? st.dir : "", ld, sizeof(ld)); ImGui::TextDisabled("Folder: %s", ld); }
        Note("Put your LUT (.png / .bmp / .tga / .jpg) in the folder set by [LUT] Folders = in TYFMC.ProperGFX.ini (empty = files/" PS_DIR_NAME " and its luts subfolder; a name is relative to files/" PS_DIR_NAME ", or give a full path) and pick it above or write its name with the extension in File = . Sizes: 256x16, 1024x32, 4096x64 strips, 64x64 or 512x512 squares. Changes are picked up live.");
        FeatureEnd();
    }
    if (FeatureBegin("Dithering - banding fix", &s.dither)) { Note("Triangular dither in the final pass (dark gradients, sky)."); FeatureEnd(); }
    if (FeatureBegin("Vignette", &s.vignette))
    {
        SliderDN("Strength", &s.vignetteStrength, 0.f, 1.f);
        SliderF("Size", &s.vignetteSize, 0.2f, 1.2f);
        FeatureEnd();
    }

    // ---------------------------------------------------------------- anti-aliasing & sharpen
    ImGui::SeparatorText("ANTI-ALIASING & SHARPEN");
    if (FeatureBegin("AntiAliasing - FXAA (SMAA is not possible)", &s.fxaa)) { SliderF("Strength", &s.fxaaStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("Sharpen - AMD CAS", &s.cas)) { SliderF("Strength", &s.casStrength, 0.f, 1.f); FeatureEnd(); }

    // ---------------------------------------------------------------- camera
    ImGui::SeparatorText("CAMERA");
    if (FeatureBegin("ImprovedZFighting - near clip", &s.zfix))
    {
        SliderF("Near clip", &s.nearClip, 0.05f, 2.f);
        Note("Raise to reduce z-fighting; lower to avoid first-person clipping.");
        FeatureEnd();
    }
    if (FeatureBegin("DepthOfField - dof.glsl (tilt-shift / center focus, bokeh)", &s.tiltShift))
    {
        Combo("Mode", &s.dofMode, PS_DofModeNames, PS_DofModeCount);
        SliderF("Focus position", &s.tiltFocus, 0.f, 1.f);
        SliderF("Focus width", &s.tiltWidth, 0.05f, 0.6f);
        SliderF("Strength", &s.tiltStrength, 0.f, 1.f);
        SliderF("Bokeh (bright highlights)", &s.dofBokeh, 0.f, 2.f);
        SliderF("Blur radius", &s.dofRadius, 0.3f, 3.f);
        SliderF("Underwater blur", &s.dofUnderwater, 0.f, 1.f);
        Note("The game has no readable depth buffer: focus is a screen band (TiltShift) or a circle (CenterFocus). Underwater blur uses UnderWaterness.");
        FeatureEnd();
    }

    // ---------------------------------------------------------------- artistic
    ImGui::SeparatorText("ARTISTIC");
    if (FeatureBegin("CelShading - anime", &s.cel))
    {
        SliderF("Levels", &s.celLevels, 2.f, 12.f, "%.0f");
        SliderF("Amount", &s.celAmount, 0.f, 1.f);
        Check("Also cel-shade the world (restart)", &s.celWorld);
        SliderF("World levels", &s.celWorldLevels, 2.f, 12.f, "%.0f", true);
        FeatureEnd();
    }
    if (FeatureBegin("Outlines - GTA:CW style", &s.outline))
    {
        SliderF("Strength", &s.outlineStrength, 0.f, 1.f);
        SliderF("Threshold", &s.outlineThreshold, 0.05f, 0.8f);
        FeatureEnd();
    }
    if (FeatureBegin("VHS", &s.vhs)) { SliderF("Strength", &s.vhsStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("FilmGrain", &s.grain)) { SliderF("Strength", &s.grainStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("ChromaticAberration", &s.chromatic)) { SliderF("Strength", &s.chromaticStrength, 0.f, 1.f); FeatureEnd(); }

    // ---------------------------------------------------------------- feature guide
    ImGui::SeparatorText("FEATURE GUIDE");
    if (ImGui::CollapsingHeader("What each effect does (and what this build really has)"))
    {
        for (int i = 0; i < kGuideCount; ++i)
        {
            const PSGuide& g = kGuide[i];
            ImVec4 col = g.status == 0 ? ImVec4(0.45f, 0.95f, 0.5f, 1) : (g.status == 1 ? ImVec4(1.0f, 0.82f, 0.3f, 1) : ImVec4(1.0f, 0.42f, 0.4f, 1));
            ImGui::PushID(i);
            ImGui::TextColored(col, "%s", g.status == 0 ? "[OK]" : (g.status == 1 ? "[~]" : "[X]")); ImGui::SameLine();
            ImGui::TextWrapped("%s", g.name);
            ImGui::Indent();
            ImGui::TextWrapped("%s", g.what);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("In this build: %s", g.here);
            ImGui::PopStyleColor();
            ImGui::Unindent();
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // ---------------------------------------------------------------- not possible on mobile
    ImGui::SeparatorText("NOT AVAILABLE ON MOBILE");
    if (ImGui::CollapsingHeader("Proper Shaders (PC) feature list"))
    {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1), "[OK]"); ImGui::SameLine(); ImGui::TextUnformatted("implemented");
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1), "[~]");  ImGui::SameLine(); ImGui::TextUnformatted("approximated");
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.4f, 1), "[X]");  ImGui::SameLine(); ImGui::TextUnformatted("engine = cannot do in GTA SA Mobile");
        ImGui::Spacing();
        for (int i = 0; i < kFeatureCount; ++i)
        {
            const PSFeature& f = kFeatures[i];
            ImVec4 col = f.status == 0 ? ImVec4(0.45f, 0.95f, 0.5f, 1) : (f.status == 1 ? ImVec4(1.0f, 0.82f, 0.3f, 1) : ImVec4(1.0f, 0.42f, 0.4f, 1));
            ImGui::TextColored(col, "%s", f.status == 0 ? "[OK]" : (f.status == 1 ? "[~]" : "[X]")); ImGui::SameLine();
            ImGui::TextWrapped("%s", f.name);
            if (f.status == 2) ImGui::TextDisabled("    engine = cannot do in GTA SA Mobile");
        }
    }

    // ---------------------------------------------------------------- status & menu
    ImGui::SeparatorText("STATUS");
    if (ImGui::CollapsingHeader("Renderer & performance"))
    {
        ImGui::TextWrapped("Renderer: %s", st.fxStatus);
        ImGui::TextWrapped("Game: %s", st.gameInfo);
        ImGui::Text("World shaders patched: %d", st.patchedShaders);
        ImGui::Text("Sun tracking: %s", st.sunTracked ? "yes" : "no");
        if (st.fx)
        {
            ImGui::Text("Frame time (avg): %.1f ms  (%.0f FPS)", st.fx->avgFrameMs, st.fx->avgFrameMs > 0.1f ? 1000.f / st.fx->avgFrameMs : 0.f);
            ImGui::Text("Adaptive level: %d    GL passes: %d", st.fx->adaptiveLevel, st.fx->passes);
            ImGui::Text("Shader variants cached: %d", st.fx->variants);
            ImGui::Text("Full-res texture fetches/pixel: ~%.0f", st.fx->fullResFetches);
        }
    }
    if (ImGui::CollapsingHeader("GLSL SHADERS (built into libProperGFX64.so)"))
    {
        Check("Custom pass (custom.glsl: rain & night mood, runs last)", &s.glslCustom);
        static const char* pn[8] = { "Wet-road blue tint", "Night darkening", "Contrast", "Warm / cold",
                                     "Param 5 (PGFX_User1.x)", "Param 6 (PGFX_User1.y)", "Param 7 (PGFX_User1.z)", "Param 8 (PGFX_User1.w)" };
        for (int i = 0; i < 8; ++i) SliderF(pn[i], &s.glslParam[i], -1.f, 1.f);
        int bad = 0; for (const PSGlslFile& f : GLSL_Files()) if (f.failed) ++bad;
        if (bad) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1), "%d shader(s) failed to compile on this GPU - details in files/" PS_DIR_NAME "/propergfx_log.txt", bad);
        else ImGui::TextDisabled("All %d built-in .glsl shaders OK", (int)GLSL_Files().size());
        for (const PSGlslFile& f : GLSL_Files())
            if (f.failed) { ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s  FAILED", f.path.c_str()); if (!f.error.empty()) ImGui::TextWrapped("   %s", f.error.c_str()); }
        Note("The .glsl shaders (shaders/fragment + shaders/vertex in the source) are compiled into the .so - nothing is read from the files folder. "
             "Uniforms (same as SAShaderL): ShaderFlags, SunVector, Time, GameTimeSeconds, UnderWaterness, RoadsWetness, FarClipDist, EntityModel.");
    }
    if (ImGui::CollapsingHeader("Menu & floating button"))
    {
        Check("Show floating button", &s.showButton);
        Check("Shrink to a dot when idle", &s.buttonCollapse);
        float px = s.buttonX * 100.f, py = s.buttonY * 100.f;
        if (ImGui::SliderFloat("X position", &px, 0.f, 100.f, "%.1f%%", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp)) { s.buttonX = px / 100.f; g_dirty = true; }
        if (ImGui::SliderFloat("Y position", &py, 0.f, 100.f, "%.1f%%", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp)) { s.buttonY = py / 100.f; g_dirty = true; }
        SliderF("Button size", &s.buttonScale, 0.5f, 2.5f, "%.2fx");
        SliderF("Button opacity", &s.buttonOpacity, PS_BUTTON_MIN_OPACITY, 1.f, s.buttonOpacity < PS_BUTTON_HIDDEN_BELOW ? "0 (hidden)" : "%.2f");
        if (s.buttonOpacity < PS_BUTTON_HIDDEN_BELOW) s.buttonOpacity = 0.f;      // snap: no faint label at "almost 0"
        if (s.buttonOpacity <= 0.001f) Note("Opacity 0: the button is invisible (label and outline too) but still works at its position. Mods > Proper GFX in the SAUtils menu also opens this menu.");
        SliderF("Menu scale", &s.uiScale, 0.6f, 2.0f, "%.2fx");
        Check("World shader patches (restart)", &s.worldPatch);
        Note("Drag the floating button anywhere; tap it to open this menu.");
    }
    ImGui::PopItemWidth();
}

// floating button (drawn on the foreground list, hit-tested by hand so it works with raw touch input)
void DrawButton(PSSettings& s, int W, int H)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float fs = g_fontPx * 0.78f * s.buttonScale;
    g_idle += io.DeltaTime;
    // idle -> shrink to a small dot so the button can never sit on top of another mod's label/button
    bool collapsed = s.buttonCollapse && !g_menuOpen && !g_pressing && g_idle > 4.f;
    const char* label = collapsed ? "GFX" : "Proper GFX Settings";
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
    float padX = fs * 0.9f, padY = fs * 0.55f;
    ImVec2 sz(ts.x + padX * 2.f, ts.y + padY * 2.f);
    if (collapsed) { float d = fmaxf(ts.x, ts.y) + fs * 1.1f; sz = ImVec2(d, d); padX = (sz.x - ts.x) * 0.5f; padY = (sz.y - ts.y) * 0.5f; }
    ImVec2 c(s.buttonX * W, s.buttonY * H);
    ImVec2 mn(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f);
    if (mn.x < 0) mn.x = 0; if (mn.y < 0) mn.y = 0;
    if (mn.x + sz.x > W) mn.x = W - sz.x; if (mn.y + sz.y > H) mn.y = H - sz.y;
    ImVec2 mx(mn.x + sz.x, mn.y + sz.y);

    // keep the menu window "on top": ignore presses that land on it
    bool overMenu = false;
    if (g_menuOpen && g_rects[1].valid)
    {
        std::lock_guard<std::mutex> l(g_mx);
        overMenu = io.MousePos.x >= g_rects[1].x0 && io.MousePos.x <= g_rects[1].x1 && io.MousePos.y >= g_rects[1].y0 && io.MousePos.y <= g_rects[1].y1;
    }
    bool hover = io.MousePos.x >= mn.x && io.MousePos.x <= mx.x && io.MousePos.y >= mn.y && io.MousePos.y <= mx.y && !overMenu;
    if (io.MouseClicked[0] && hover) { g_pressing = true; g_dragging = false; g_idle = 0.f; g_pressPos = io.MousePos; g_grabOffset = ImVec2(c.x - io.MousePos.x, c.y - io.MousePos.y); }
    if (g_pressing && io.MouseDown[0])
    {
        float dx = io.MousePos.x - g_pressPos.x, dy = io.MousePos.y - g_pressPos.y;
        g_idle = 0.f;
        if (!g_dragging && sqrtf(dx * dx + dy * dy) > g_fontPx * 0.45f) g_dragging = true;
        if (g_dragging)
        {
            s.buttonX = (io.MousePos.x + g_grabOffset.x) / W; s.buttonY = (io.MousePos.y + g_grabOffset.y) / H;
            if (s.buttonX < 0.f) s.buttonX = 0.f; if (s.buttonX > 1.f) s.buttonX = 1.f;
            if (s.buttonY < 0.f) s.buttonY = 0.f; if (s.buttonY > 1.f) s.buttonY = 1.f;
        }
    }
    if (g_pressing && io.MouseReleased[0])
    {
        if (!g_dragging) { g_menuOpen = !g_menuOpen; if (!g_menuOpen) g_dirty = true; }
        else g_dirty = true;
        g_pressing = false; g_dragging = false; g_idle = 0.f;
    }
    float a = s.buttonOpacity < PS_BUTTON_HIDDEN_BELOW ? 0.f : s.buttonOpacity;   // 0 (or an old 0.05 from 1.3) = nothing drawn at all
    bool active = g_pressing || g_menuOpen;
    ImU32 bg = ImGui::GetColorU32(ImVec4(0.17f, 0.065f, 0.15f, a * (active ? 0.95f : (collapsed ? 0.55f : 0.78f))));
    ImU32 bd = ImGui::GetColorU32(ImVec4(0.95f, 0.52f, 0.28f, a * (g_dragging ? 1.0f : (collapsed ? 0.65f : 0.85f))));
    // 64-bit: the label scales with the opacity too, so Opacity 0 = label, fill AND outline fully transparent (the button still works where it is)
    ImU32 tx = ImGui::GetColorU32(ImVec4(1, 1, 1, a * (collapsed ? 0.85f : 1.0f)));
    if (a <= 0.001f) { /* nothing drawn */ }
    else if (collapsed)
    {
        ImVec2 cc((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        dl->AddCircleFilled(cc, sz.x * 0.5f, bg, 24);
        dl->AddCircle(cc, sz.x * 0.5f, bd, 24, fmaxf(1.5f, fs * 0.07f));
    }
    else
    {
        dl->AddRectFilled(mn, mx, bg, sz.y * 0.5f);
        dl->AddRect(mn, mx, bd, sz.y * 0.5f, 0, fmaxf(1.5f, fs * 0.07f));
    }
    if (a > 0.001f) dl->AddText(font, fs, ImVec2(mn.x + padX, mn.y + padY), tx, label);

    // exact hit area (+3 px): nothing outside the drawn button is ever swallowed
    std::lock_guard<std::mutex> l(g_mx);
    g_rects[0] = { mn.x - 3.f, mn.y - 3.f, mx.x + 3.f, mx.y + 3.f, true };
}

} // namespace

bool UI_TestButtonRect(float out[4]) { std::lock_guard<std::mutex> l(g_mx); if (!g_rects[0].valid) return false; out[0] = g_rects[0].x0; out[1] = g_rects[0].y0; out[2] = g_rects[0].x1; out[3] = g_rects[0].y1; return true; }
void UI_TestOpen(bool o, int tab) { g_menuOpen = o; g_testMode = tab >= 1 ? 1 : 0; g_testScroll = tab >= 1 ? (tab - 1) * 560.f : 0.f; }
void UI_SetIniPath(const char* p) { snprintf(g_ini, sizeof(g_ini), "%s", p ? p : ""); }
bool UI_MenuOpen() { return g_menuOpen; }
static std::atomic<bool> g_openRequest{false};
void UI_RequestOpen() { g_openRequest = true; }

bool UI_Init()
{
    if (g_ctx) return true;
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.MouseDrawCursor = false;
    if (!ImGui_ImplOpenGL3_Init("#version 100")) { PS_Log("ImGui GL backend init failed"); return false; }
    return true;
}

void UI_ContextLost()
{
    if (!g_ctx) return;
    ImGui_ImplOpenGL3_Shutdown();       // backend only forgets names; GL objects died with the context
    ImGui::DestroyContext(g_ctx);
    g_ctx = nullptr; g_fontPx = 0.f;
}

void UI_Shutdown()
{
    if (!g_ctx) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(g_ctx);
    g_ctx = nullptr;
}

bool UI_OnTouch(int type, int finger, int x, int y)
{
    if (finger < 0 || finger >= 16) return false;
    std::lock_guard<std::mutex> l(g_mx);
    if (type == 2)      // push: ALWAYS starts a fresh sequence for this finger (a stale capture must never swallow a game touch)
    {
        if (g_captured[finger] && finger == g_primary) g_primary = -1;
        g_captured[finger] = false;
        if (!InRects((float)x, (float)y)) return false;
        g_captured[finger] = true;
        bool primary = (g_primary < 0);
        if (primary) g_primary = finger;
        g_queue.push_back({ type, finger, x, y, primary }); g_lastEvt = NowMs();
        return true;
    }
    if (!g_captured[finger]) return false;
    bool primary = (finger == g_primary);
    if (type == 4) type = 1;            // cancel == release
    g_queue.push_back({ type, finger, x, y, primary }); g_lastEvt = NowMs();
    if (type == 1) { g_captured[finger] = false; if (primary) g_primary = -1; }
    return true;
}

void UI_Render(PSSettings& s, int W, int H, float dt, const PSUIStatus& st, bool gameplay)
{
    if (!gameplay)
    {   // 1.4.6: main menu / pause menu / loading screen: no button, no window, and no touch is taken from the game.
        // A pending "open menu" request (Settings > Mods) waits until gameplay resumes.
        std::lock_guard<std::mutex> l(g_mx);
        g_rects[0].valid = false; g_rects[1].valid = false;
        for (int i = 0; i < 16; ++i) g_captured[i] = false;
        g_primary = -1; g_queue.clear(); g_hiddenReset = true;
        return;
    }
    if (!g_ctx && !UI_Init()) return;
    if (g_hiddenReset)
    {   // back in gameplay: a press that was cut off by the menu must not stay "down" inside ImGui
        g_hiddenReset = false; g_pressing = false; g_dragging = false;
        ImGuiIO& io0 = ImGui::GetIO(); io0.AddMouseButtonEvent(0, false); io0.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    if (g_openRequest.exchange(false)) g_menuOpen = true;
    if (!s.showButton && !g_menuOpen) { std::lock_guard<std::mutex> l(g_mx); g_rects[0].valid = false; g_rects[1].valid = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    g_screenW = W; g_screenH = H;
    float px = floorf(fmaxf(16.f, fminf(56.f, H * 0.03f * s.uiScale)));
    if (fabsf(px - g_fontPx) > 0.5f) { BuildFonts(px); ApplyStyle(px / 18.f); }
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = dt > 0.0001f ? dt : 0.016f;

    std::vector<TouchEv> evs;
    bool watchdog = false;
    {
        std::lock_guard<std::mutex> l(g_mx);
        evs.swap(g_queue);
        // watchdog: a captured finger that has been silent for 2.5 s lost its RELEASE (pointer ids can shift on multi-touch) -> let go,
        // otherwise every later event of that finger id would be swallowed and other mods' buttons would seem dead
        if (g_primary >= 0 && evs.empty() && NowMs() - g_lastEvt > 2500) { g_captured[g_primary] = false; g_primary = -1; watchdog = true; }
    }
    if (watchdog) { g_pressing = false; g_dragging = false; io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
    for (const TouchEv& e : evs)
    {
        if (!e.primary) continue;
        io.AddMousePosEvent((float)e.x, (float)e.y);
        if (e.type == 2) io.AddMouseButtonEvent(0, true);
        else if (e.type == 1) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    g_worldTouched = false; g_userEdited = false; g_presetPressed = false;

    if (g_menuOpen)
    {
        float ww = fminf(W * 0.62f, px * 36.f), wh = H * 0.80f;
        ImGui::SetNextWindowPos(ImVec2(W * 0.02f, H * 0.05f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(px * 16.f, px * 12.f), ImVec2((float)W, (float)H));
        bool open = true;
        if (ImGui::Begin("Proper GFX Settings", &open, ImGuiWindowFlags_NoSavedSettings))
        {
            // branded header
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float fs = px * 1.25f;
                dl->AddText(ImGui::GetFont(), fs, p, ImGui::GetColorU32(ImVec4(0.96f, 0.55f, 0.30f, 1)), "PROPER GFX");
                ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, "PROPER GFX ");
                dl->AddText(ImGui::GetFont(), px * 0.8f, ImVec2(p.x + ts.x, p.y + fs * 0.28f), ImGui::GetColorU32(ImVec4(0.75f, 0.55f, 0.8f, 1)), "MOBILE " PS_VERSION "  -  GTA SA");
                ImGui::Dummy(ImVec2(0, fs + 2.f));
            }
            float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            ImGui::BeginChild("##scroll", ImVec2(0, -footer), false);
            DrawFeatures(s, st);
            if (g_testScroll > 0.f) { ImGui::SetScrollY(g_testScroll); }
            // touch scrolling: drag on anything that is not an active widget
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(0, 8.f))
                ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            ImGui::EndChild();
            if (ImGui::Button("Save")) { if (g_ini[0]) PS_SaveSettings(s, g_ini); g_dirty = false; }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { PS_ResetSettings(s); g_worldTouched = true; g_dirty = true; }
            ImGui::SameLine();
            if (ImGui::Button("Close")) open = false;
            ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
            std::lock_guard<std::mutex> l(g_mx);
            g_rects[1] = { p.x, p.y, p.x + z.x, p.y + z.y, true };
        }
        else { std::lock_guard<std::mutex> l(g_mx); g_rects[1].valid = false; }
        ImGui::End();
        if (!open) { g_menuOpen = false; if (g_ini[0]) PS_SaveSettings(s, g_ini); g_dirty = false; }
    }
    else { std::lock_guard<std::mutex> l(g_mx); g_rects[1].valid = false; }

    if (s.shadowDebug && st.shadowTex)
    {   // shadow map preview (packed depth colours) so problems can be seen on screenshots
        ImDrawList* dl = ImGui::GetForegroundDrawList(); float sz = H * 0.26f; ImVec2 p0(W - sz - 8.f, 8.f), p1(W - 8.f, 8.f + sz);
        dl->AddRectFilled(ImVec2(p0.x - 3, p0.y - 3), ImVec2(p1.x + 3, p1.y + 3), IM_COL32(0, 0, 0, 200));
        dl->AddImage((ImTextureID)(intptr_t)st.shadowTex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));
        dl->AddText(ImVec2(p0.x + 4, p0.y + 2), IM_COL32(255, 220, 120, 255), s.shadowMethod == 0 ? "shadow mask" : "shadow map");
    }
    if (s.showButton) DrawButton(s, W, H);
    else { std::lock_guard<std::mutex> l(g_mx); g_rects[0].valid = false; }

    if (g_userEdited && !g_presetPressed && s.quality != Q_CUSTOM) s.quality = Q_CUSTOM;
    if (g_worldTouched) ++g_worldGen;
    // autosave once the finger is lifted
    if (g_dirty && !io.MouseDown[0] && g_ini[0]) { PS_SaveSettings(s, g_ini); g_dirty = false; }

    ImGui::Render();
    glViewport(0, 0, W, H);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
