#include "ps_fx.h"
#include "ps_glsl.h"
#include <set>
#include "ps_log.h"
#include "ps_lut.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

namespace {

struct Prog
{
    GLuint id = 0;
    std::vector<std::pair<std::string, GLint>> locs;
    GLint u(const char* n) const
    {
        for (auto& p : locs) if (!strcmp(p.first.c_str(), n)) return p.second;
        return -1;
    }
};

struct RT { GLuint tex = 0, fbo = 0; int w = 0, h = 0; };

struct Variant { uint32_t key = 0; Prog p; uint32_t used = 0; };

struct FXState
{
    bool ready = false, failed = false;
    char status[160] = "not initialised";
    Prog pBright, pDown, pBlur, pLum, pAA, pCustom; bool customTried = false;
    unsigned glslGen = 0;
    std::vector<Variant> variants;
    uint32_t tick = 0;
    GLuint sceneTex = 0; int sceneW = 0, sceneH = 0; GLenum sceneFmt = GL_RGBA;
    RT aa, bloomA, bloomB, bloomC, dofA, dofB, dofC, lum[2], post;
    int lumIdx = 0; bool lumReset = true; float lumAccDt = 0.f; int lumCounter = 0;
    GLuint dummyTex = 0;
    float frame = 0.f;
    // adaptive quality governor
    float emaDt = 0.f; int over = 0, under = 0, level = 0;
    PSFxStats stats;
};
FXState G;

const GLfloat kTri[6] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };
std::string g_lastErr;
std::set<std::string> g_builtin;          // .glsl programs that failed from the user's files: built from the built-in copies until the next reload
PSShaderGlobals g_glob;
int g_passes = 0;

GLuint Compile(GLenum type, const std::string& src, const char* tag)
{
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024]; GLsizei n = 0; glGetShaderInfoLog(s, sizeof(log), &n, log);
        PS_Log("shader '%s' failed: %s", tag, log); g_lastErr = log;
        snprintf(G.status, sizeof(G.status), "shader '%s' failed to compile", tag);
        glDeleteShader(s); return 0;
    }
    return s;
}

bool Link(Prog& p, const std::string& vsSrc, const std::string& fsSrc, const char* tag)
{
    GLuint v = Compile(GL_VERTEX_SHADER, vsSrc, tag);
    GLuint f = v ? Compile(GL_FRAGMENT_SHADER, fsSrc, tag) : 0;
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return false; }
    p.id = glCreateProgram();
    glAttachShader(p.id, v); glAttachShader(p.id, f);
    glBindAttribLocation(p.id, 0, "aPos");
    glLinkProgram(p.id);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024]; GLsizei n = 0; glGetProgramInfoLog(p.id, sizeof(log), &n, log);
        PS_Log("program '%s' link failed: %s", tag, log); g_lastErr = log;
        snprintf(G.status, sizeof(G.status), "program '%s' failed to link", tag);
        glDeleteProgram(p.id); p.id = 0; return false;
    }
    GLint cnt = 0; glGetProgramiv(p.id, GL_ACTIVE_UNIFORMS, &cnt);
    for (GLint i = 0; i < cnt; ++i)
    {
        char name[64]; GLint sz; GLenum ty; GLsizei len;
        glGetActiveUniform(p.id, i, sizeof(name), &len, &sz, &ty, name);
        char* br = strchr(name, '['); if (br) *br = 0;
        p.locs.emplace_back(name, glGetUniformLocation(p.id, name));
    }
    return true;
}

// Builds a pass from files/ProperGFX/shaders/<rel> (+ vertex/fullscreen.glsl). If the user's file does not compile on this GPU,
// the error is shown in the menu and the built-in copy is used instead - an edited .glsl can never break the picture.
bool BuildFile(Prog& p, const char* rel, const char* tag, const std::string& defines = std::string())
{
    std::string vs, fs;
    const bool bi = g_builtin.count(rel) != 0;
    if (!GLSL_Source("vertex/fullscreen.glsl", vs, bi) || !GLSL_Source(rel, fs, bi)) { g_lastErr = "file missing"; }
    else if (Link(p, vs, defines + fs, tag)) return true;
    if (bi) return false;
    GLSL_MarkFailed(rel, g_lastErr.c_str());
    g_builtin.insert(rel); ++G.stats.glslErrors;
    p = Prog();
    if (!GLSL_Source("vertex/fullscreen.glsl", vs, true) || !GLSL_Source(rel, fs, true)) return false;
    return Link(p, vs, defines + fs, tag);
}

void DestroyRT(RT& r, bool del)
{
    if (del) { if (r.fbo) glDeleteFramebuffers(1, &r.fbo); if (r.tex) glDeleteTextures(1, &r.tex); }
    r = RT();
}

bool MakeRT(RT& r, int w, int h, bool linear)
{
    if (r.tex && r.w == w && r.h == h) return true;
    DestroyRT(r, true);
    if (w < 1) w = 1; if (h < 1) h = 1;
    glGenTextures(1, &r.tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLint f = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &r.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    r.w = w; r.h = h;
    if (st != GL_FRAMEBUFFER_COMPLETE) { PS_Log("FBO incomplete 0x%x (%dx%d)", st, w, h); return false; }
    return true;
}

void DrawTri()
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (int i = 1; i < 8; ++i) glDisableVertexAttribArray(i);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kTri);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++g_passes;
}

// SAShaderL-compatible + Proper GFX uniforms, set after every glUseProgram (only the ones a .glsl declares are found)
PSFrameInfo g_fi;
void Common(const Prog& p, int pass)
{
    GLint l;
    if ((l = p.u("ShaderFlags")) >= 0) glUniform1i(l, 0x20000000 | pass);
    if ((l = p.u("SunVector")) >= 0) glUniform3fv(l, 1, g_glob.sunVector);
    if ((l = p.u("Time")) >= 0) glUniform1i(l, g_glob.timeMs);
    if ((l = p.u("GameTimeSeconds")) >= 0) glUniform1i(l, g_glob.gameTimeSeconds);
    if ((l = p.u("UnderWaterness")) >= 0) glUniform1f(l, g_glob.underWaterness);
    if ((l = p.u("RoadsWetness")) >= 0) glUniform1f(l, g_glob.roadsWetness);
    if ((l = p.u("FarClipDist")) >= 0) glUniform1f(l, g_glob.farClipDist);
    if ((l = p.u("EntityModel")) >= 0) glUniform1i(l, g_glob.entityModel);
    if ((l = p.u("PGFX_TimeSec")) >= 0) glUniform1f(l, g_glob.timeSec);
    if ((l = p.u("PGFX_Sun")) >= 0) glUniform4f(l, g_fi.sunX, g_fi.sunY, g_fi.sunVis, g_glob.sunVector[2]);
    if ((l = p.u("PGFX_User0")) >= 0) glUniform4fv(l, 1, g_glob.user);
    if ((l = p.u("PGFX_User1")) >= 0) glUniform4fv(l, 1, g_glob.user + 4);
}
void Use(const Prog& p, int pass) { glUseProgram(p.id); Common(p, pass); }

void BindTex(int unit, GLuint tex) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, tex); }
void Target(const RT& r) { glBindFramebuffer(GL_FRAMEBUFFER, r.fbo); glViewport(0, 0, r.w, r.h); }
void Set4(const Prog& p, const char* n, float x, float y, float z, float w) { GLint l = p.u(n); if (l >= 0) glUniform4f(l, x, y, z, w); }
void Set1i(const Prog& p, const char* n, int v) { GLint l = p.u(n); if (l >= 0) glUniform1i(l, v); }

// ---- CPU copy of the tone-mappers (used to normalise the white point) ----
float TmCpu(int mode, float x)
{
    switch (mode)
    {
        case TM_ACES: x *= 0.7232f; return fminf(1.f, fmaxf(0.f, (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)));
        case TM_TIMOTHY: x = fmaxf(x * 0.7203f, 0.0001f); return powf(x, 1.6f) / (powf(x, 1.5632f) * 1.07304f + 0.16742f);
        case TM_REINHARD: x *= 1.1998f; return x * (1.f + x / 16.f) / (1.f + x);
        default: return fminf(1.f, x);
    }
}

struct Grade { float sat, con; float sh[3]; float hi[3]; };
const Grade kGrades[] = {
    { 1.00f, 1.00f, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } },
    { 0.97f, 1.05f, {-0.015f, 0.020f, 0.045f }, { 0.040f, 0.015f,-0.030f } },
    { 1.05f, 1.02f, { 0.020f, 0.000f,-0.020f }, { 0.055f, 0.020f,-0.045f } },
    { 0.95f, 1.02f, { 0.000f, 0.012f, 0.050f }, {-0.020f, 0.000f, 0.045f } },
    { 0.00f, 1.15f, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } },
    { 0.80f, 0.95f, { 0.045f, 0.032f, 0.020f }, { 0.020f, 0.015f,-0.020f } },
    { 1.20f, 1.08f, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f } },
};

// ---- which features does this frame need? -> shader variant key ----
enum { F_AAINLINE = 1 << 0, F_CA = 1 << 1, F_CAS = 1 << 2, F_DOF = 1 << 3, F_OUTLINE = 1 << 4, F_CEL = 1 << 5, F_AUTOEXP = 1 << 6,
       F_BLOOM = 1 << 7, F_RAYS = 1 << 8, F_FLARE = 1 << 9, F_LUT = 1 << 10, F_UNDER = 1 << 11, F_VHS = 1 << 12, F_VIGNETTE = 1 << 13,
       F_GRAIN = 1 << 14, F_DITHER = 1 << 15, F_GRADE = 1 << 16, F_GAMMA = 1 << 17,   // bits 18..19 = tonemap mode
       F_GLOW = 1 << 20, F_GOLDEN = 1 << 21 };

std::string DefinesFor(uint32_t key)
{
    static const struct { uint32_t bit; const char* name; } names[] = {
        { F_AAINLINE, "FX_AA" }, { F_CA, "FX_CA" }, { F_CAS, "FX_CAS" }, { F_DOF, "FX_DOF" }, { F_OUTLINE, "FX_OUTLINE" }, { F_CEL, "FX_CEL" },
        { F_AUTOEXP, "FX_AUTOEXP" }, { F_BLOOM, "FX_BLOOM" }, { F_RAYS, "FX_RAYS" }, { F_FLARE, "FX_FLARE" }, { F_LUT, "FX_LUT" },
        { F_UNDER, "FX_UNDER" }, { F_VHS, "FX_VHS" }, { F_VIGNETTE, "FX_VIGNETTE" }, { F_GRAIN, "FX_GRAIN" }, { F_DITHER, "FX_DITHER" },
        { F_GRADE, "FX_GRADE" }, { F_GAMMA, "FX_GAMMA" }, { F_GLOW, "FX_GLOW" }, { F_GOLDEN, "FX_GOLDEN" } };
    std::string d;
    for (auto& n : names) if (key & n.bit) d += std::string("#define ") + n.name + " 1\n";
    d += "#define TM_MODE " + std::to_string((key >> 18) & 3) + "\n";
    return d;
}

const Prog* GetVariant(uint32_t key)
{
    ++G.tick;
    for (auto& v : G.variants) if (v.key == key) { v.used = G.tick; return &v.p; }
    Variant nv; nv.key = key; nv.used = G.tick;
    if (!BuildFile(nv.p, "fragment/final.glsl", "final", DefinesFor(key))) return nullptr;
    if (G.variants.size() >= 10)      // LRU: keep the cache small
    {
        size_t oldest = 0;
        for (size_t i = 1; i < G.variants.size(); ++i) if (G.variants[i].used < G.variants[oldest].used) oldest = i;
        if (G.variants[oldest].p.id) glDeleteProgram(G.variants[oldest].p.id);
        G.variants.erase(G.variants.begin() + oldest);
    }
    G.variants.push_back(nv);
    return &G.variants.back().p;
}

bool EnsureInit()
{
    if (G.ready) return true;
    if (G.failed) return false;
    bool ok = BuildFile(G.pBright, "fragment/bloom_bright.glsl", "bright") && BuildFile(G.pDown, "fragment/downsample.glsl", "down") &&
              BuildFile(G.pBlur, "fragment/blur_gauss.glsl", "blur") && BuildFile(G.pLum, "fragment/autoexposure.glsl", "lum") &&
              BuildFile(G.pAA, "fragment/fxaa_pass.glsl", "fxaa");
    if (!ok) { G.failed = true; return false; }
    if (G.dummyTex) { G.ready = true; snprintf(G.status, sizeof(G.status), "ready (%s) - shaders reloaded", glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "?"); return true; }
    unsigned char px[4] = { 0, 0, 0, 255 };
    glGenTextures(1, &G.dummyTex);
    BindTex(0, G.dummyTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!MakeRT(G.lum[0], 1, 1, false) || !MakeRT(G.lum[1], 1, 1, false)) { G.failed = true; snprintf(G.status, sizeof(G.status), "FBO creation failed"); return false; }
    G.ready = true;
    snprintf(G.status, sizeof(G.status), "ready (%s)", glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "?");
    return true;
}

bool EnsureSize(int w, int h)
{
    if (G.sceneTex && G.sceneW == w && G.sceneH == h) return true;
    if (G.sceneTex) glDeleteTextures(1, &G.sceneTex);
    GLint ab = 0; glBindFramebuffer(GL_FRAMEBUFFER, 0); glGetIntegerv(GL_ALPHA_BITS, &ab);
    G.sceneFmt = ab > 0 ? GL_RGBA : GL_RGB;
    glGenTextures(1, &G.sceneTex);
    BindTex(0, G.sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, G.sceneFmt, w, h, 0, G.sceneFmt, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    G.sceneW = w; G.sceneH = h;
    DestroyRT(G.aa, true); DestroyRT(G.bloomA, true); DestroyRT(G.bloomB, true); DestroyRT(G.bloomC, true);
    DestroyRT(G.dofA, true); DestroyRT(G.dofB, true); DestroyRT(G.dofC, true); DestroyRT(G.post, true);   // all lazily re-created
    G.lumReset = true;
    return true;
}

} // namespace

bool FX_Failed() { return G.failed && G.glslGen == GLSL_Generation(); }   // a reload gives a failed pipeline another chance
void FX_SetGlobals(const PSShaderGlobals& g) { g_glob = g; }
const PSShaderGlobals& FX_Globals() { return g_glob; }
bool FX_LutLoaded() { return LUT_Info().loaded; }
const char* FX_Status() { return G.status; }
const PSFxStats& FX_Stats() { return G.stats; }

void FX_ContextLost()
{
    G = FXState();
    snprintf(G.status, sizeof(G.status), "context lost - rebuilding");
}

void FX_Shutdown()
{
    if (!G.ready) return;
    Prog* ps[] = { &G.pBright, &G.pDown, &G.pBlur, &G.pLum, &G.pAA, &G.pCustom };
    for (Prog* p : ps) if (p->id) glDeleteProgram(p->id);
    for (auto& v : G.variants) if (v.p.id) glDeleteProgram(v.p.id);
    RT* rts[] = { &G.aa, &G.bloomA, &G.bloomB, &G.bloomC, &G.dofA, &G.dofB, &G.dofC, &G.lum[0], &G.lum[1], &G.post };
    for (RT* r : rts) DestroyRT(*r, true);
    if (G.sceneTex) glDeleteTextures(1, &G.sceneTex);
    if (G.dummyTex) glDeleteTextures(1, &G.dummyTex);
    G = FXState();
}

bool FX_Render(const PSSettings& in, const PSFrameInfo& fi)
{
    if (fi.w < 16 || fi.h < 16) return false;
    g_fi = fi;
    if (G.glslGen != GLSL_Generation())
    {   // .glsl files changed (Reload shaders / hot reload): rebuild every program, keep textures and render targets
        Prog* ps[] = { &G.pBright, &G.pDown, &G.pBlur, &G.pLum, &G.pAA, &G.pCustom };
        for (Prog* p : ps) { if (p->id) glDeleteProgram(p->id); *p = Prog(); }
        for (auto& v : G.variants) if (v.p.id) glDeleteProgram(v.p.id);
        G.variants.clear(); G.customTried = false;
        g_builtin.clear(); G.stats.glslErrors = 0;
        G.glslGen = GLSL_Generation(); G.stats.glslGen = G.glslGen;
        G.ready = false; G.failed = false;
    }
    if (!EnsureInit()) return false;
    if (!EnsureSize(fi.w, fi.h)) { G.failed = true; snprintf(G.status, sizeof(G.status), "render targets failed"); return false; }

    // ---------- adaptive quality governor ----------
    {
        float d = fi.dt < 0.0005f ? 0.0005f : (fi.dt > 0.10f ? 0.10f : fi.dt);
        G.emaDt = G.emaDt <= 0.f ? d : G.emaDt * 0.96f + d * 0.04f;
        const float budget = in.targetFps ? (1.f / 60.f) : (1.f / 30.f);
        if (in.adaptive)
        {
            if (G.emaDt > budget * 1.15f) { ++G.over; G.under = 0; }
            else if (G.emaDt < budget * 0.80f) { ++G.under; G.over = 0; }
            else { G.over = G.under = 0; }
            if (G.over > 90 && G.level < 3) { ++G.level; G.over = 0; G.emaDt = budget; }
            else if (G.under > 600 && G.level > 0) { --G.level; G.under = 0; }
        }
        else G.level = 0;
    }
    PSSettings s = in;                        // local copy: the governor never touches the user's settings
    if (G.level >= 1) { s.lensFlare = false; s.sunRays = false; }
    if (G.level >= 2) { s.bloom = false; s.cas = false; }
    if (G.level >= 3) { s.autoExp = false; s.fxaa = false; s.vignette = false; }

    const int w = fi.w, h = fi.h;
    g_passes = 0;
    const int tm = s.hdr ? s.tonemap : TM_OFF;
    const bool wantRays   = s.sunRays && s.sunRaysIntensity > 0.01f && fi.sunVis > 0.01f;
    const bool wantFlare  = s.lensFlare && s.lensFlareIntensity > 0.01f;
    const bool wantBloomFx = s.bloom && s.bloomIntensity.v > 0.01f;
    const bool wantBloomTex = wantBloomFx || wantRays || wantFlare;
    const bool wantDof    = s.tiltShift && (s.tiltStrength > 0.01f || (s.dofUnderwater > 0.01f && fi.underwater > 0.01f));
    const bool wantGlow   = s.sunVectorOn && s.sunGlow > 0.01f && fi.sunVis > 0.01f;
    const bool wantGolden = s.sunVectorOn && s.goldenHour > 0.01f && g_glob.sunVector[2] > -0.06f && g_glob.sunVector[2] < 0.38f;
    const bool wantCustom = s.glslCustom;
    const bool wantAuto   = s.autoExp;
    const bool wantCA     = (s.chromatic && s.chromaticStrength > 0.01f) || (s.vhs && s.vhsStrength > 0.01f);
    const bool wantCAS    = s.cas && s.casStrength > 0.01f;
    const bool wantOutline = s.outline && s.outlineStrength > 0.01f;
    const bool wantAA     = s.fxaa && s.fxaaStrength > 0.01f;
    const bool aaInline   = wantAA && !wantCAS && !wantOutline && !wantCA;       // folded into the final pass
    const bool aaPass     = wantAA && !aaInline;
    const bool wantUnder  = s.underwater && fi.underwater * s.underwaterStrength > 0.01f;

    const Grade& g = kGrades[s.gradePreset < 0 ? 0 : (s.gradePreset > 6 ? 6 : s.gradePreset)];
    const float ga = s.gradeAmount;
    const float satEff = s.saturation.v * (1.f + (g.sat - 1.f) * ga);
    const float conEff = s.contrast.v * (1.f + (g.con - 1.f) * ga);
    const bool gradeNeutral = fabsf(satEff - 1.f) < 0.005f && fabsf(conEff - 1.f) < 0.005f && fabsf(s.vibrance) < 0.005f && fabsf(s.temperature) < 0.005f &&
                              fabsf(s.tint) < 0.005f && fabsf(s.lift) < 0.002f && fabsf(s.gain - 1.f) < 0.005f &&
                              (s.gradePreset == 0 || ga < 0.01f);
    const bool wantGrade = s.grading && !gradeNeutral;

    uint32_t key = ((uint32_t)tm & 3u) << 18;
    if (aaInline) key |= F_AAINLINE; if (wantCA) key |= F_CA; if (wantCAS) key |= F_CAS; if (wantDof) key |= F_DOF; if (wantOutline) key |= F_OUTLINE;
    if (s.cel && s.celAmount > 0.01f) key |= F_CEL; if (wantAuto) key |= F_AUTOEXP; if (wantBloomFx) key |= F_BLOOM; if (wantRays) key |= F_RAYS;
    if (wantFlare) key |= F_FLARE; if (s.lutOn && LUT_Texture() && s.lutAmount > 0.01f) key |= F_LUT; if (wantUnder) key |= F_UNDER; if (s.vhs && s.vhsStrength > 0.01f) key |= F_VHS;
    if (s.vignette && s.vignetteStrength.v > 0.01f) key |= F_VIGNETTE; if (s.grain && s.grainStrength > 0.01f) key |= F_GRAIN; if (s.dither) key |= F_DITHER;
    if (wantGlow) key |= F_GLOW; if (wantGolden) key |= F_GOLDEN;
    if (wantGrade) key |= F_GRADE; if (wantGrade && fabsf(s.gamma - 1.f) > 0.01f) key |= F_GAMMA;
    const Prog* PF = GetVariant(key);
    if (!PF) { G.failed = true; return false; }

    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DITHER); glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glDisable(GL_SAMPLE_COVERAGE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // 1) grab the finished frame
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    BindTex(0, G.sceneTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    GLuint src = G.sceneTex;
    float fetches = 1.f;                                        // final pass reads the scene once

    // 2) stand-alone FXAA (only when CAS / outline / chromatic aberration need an anti-aliased input)
    if (aaPass)
    {
        if (!MakeRT(G.aa, w, h, true)) { G.failed = true; return false; }
        Target(G.aa);
        Use(G.pAA, 1);
        BindTex(0, src); Set1i(G.pAA, "uTex", 0);
        Set4(G.pAA, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
        Set4(G.pAA, "uP", s.fxaaStrength, 0, 0, 0);
        DrawTri();
        src = G.aa.tex; fetches += 9.f;
    }
    if (aaInline) fetches += 8.f;
    if (wantCAS) fetches += 4.f;
    if (wantOutline) fetches += 8.f;
    if (wantCA) fetches += 2.f;

    // 3) auto exposure: GPU only, 16 taps, refreshed every 3rd frame with the accumulated dt
    if (wantAuto)
    {
        G.lumAccDt += fi.dt; ++G.lumCounter;
        if (G.lumReset || G.lumCounter >= 3)
        {
            RT& cur = G.lum[G.lumIdx ^ 1]; RT& prev = G.lum[G.lumIdx];
            Target(cur);
            Use(G.pLum, 2);
            BindTex(0, src); BindTex(1, prev.tex);
            Set1i(G.pLum, "uTex", 0); Set1i(G.pLum, "uPrev", 1);
            float adapt = 1.f - expf(-G.lumAccDt * s.autoSpeed);
            adapt = adapt < 0.02f ? 0.02f : (adapt > 1.f ? 1.f : adapt);
            Set4(G.pLum, "uL", adapt, G.lumReset ? 1.f : 0.f, 0, 0);
            DrawTri();
            G.lumIdx ^= 1; G.lumReset = false; G.lumAccDt = 0.f; G.lumCounter = 0;
        }
    }
    else { G.lumReset = true; G.lumAccDt = 0.f; G.lumCounter = 0; }

    // 4) bloom: bright pass @1/4 res, then H+V gaussian @1/8 res (one iteration, bilinear-upsampled in the final pass)
    GLuint bloomTex = G.dummyTex;
    if (wantBloomTex)
    {
        int qw = (w + 3) / 4, qh = (h + 3) / 4, ew = (w + 7) / 8, eh = (h + 7) / 8;
        if (!MakeRT(G.bloomA, qw, qh, true) || !MakeRT(G.bloomB, ew, eh, true) || !MakeRT(G.bloomC, ew, eh, true)) { G.failed = true; return false; }
        Target(G.bloomA);
        Use(G.pBright, 3);
        BindTex(0, src); Set1i(G.pBright, "uTex", 0);
        Set4(G.pBright, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
        Set4(G.pBright, "uP", s.bloomThreshold, 0.25f, 0, 0);
        DrawTri();
        Use(G.pBlur, 4); Set1i(G.pBlur, "uTex", 0);
        float r = s.bloomRadius * 1.5f;
        Target(G.bloomB); BindTex(0, G.bloomA.tex); Set4(G.pBlur, "uDir", r / ew, 0, 0, 0); DrawTri();
        Target(G.bloomC); BindTex(0, G.bloomB.tex); Set4(G.pBlur, "uDir", 0, r / eh, 0, 0); DrawTri();
        bloomTex = G.bloomC.tex;
        fetches += 1.f / 16.f * 4.f;                             // bright pass (1/16 of the pixels, 4 taps)
        if (wantBloomFx) fetches += 1.f;
        if (wantRays) fetches += 12.f;
        if (wantFlare) fetches += 5.f;
    }

    // 5) blurred scene copy for the tilt-shift DoF
    GLuint dofTex = G.dummyTex;
    if (wantDof)
    {
        int qw = (w + 3) / 4, qh = (h + 3) / 4, ew = (w + 7) / 8, eh = (h + 7) / 8;
        if (!MakeRT(G.dofA, qw, qh, true) || !MakeRT(G.dofB, ew, eh, true) || !MakeRT(G.dofC, ew, eh, true)) { G.failed = true; return false; }
        Target(G.dofA);
        Use(G.pDown, 5); BindTex(0, src); Set1i(G.pDown, "uTex", 0);
        Set4(G.pDown, "uRes", (float)w, (float)h, 1.f / w, 1.f / h); DrawTri();
        Use(G.pBlur, 4); Set1i(G.pBlur, "uTex", 0);
        Target(G.dofB); BindTex(0, G.dofA.tex); Set4(G.pBlur, "uDir", 3.0f / ew, 0, 0, 0); DrawTri();
        Target(G.dofC); BindTex(0, G.dofB.tex); Set4(G.pBlur, "uDir", 0, 3.0f / eh, 0, 0); DrawTri();
        dofTex = G.dofC.tex; fetches += 1.f;
    }

    // 6) final composite -> back buffer (specialised variant: disabled features cost nothing). With the custom pass: -> post RT, then custom.glsl -> back buffer
    bool customOk = false;
    if (wantCustom)
    {
        if (!G.pCustom.id && !G.customTried) { G.customTried = true; if (!BuildFile(G.pCustom, "fragment/custom.glsl", "custom")) G.pCustom = Prog(); }
        customOk = G.pCustom.id && MakeRT(G.post, w, h, false);
    }
    if (customOk) Target(G.post);
    else { glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h); }
    const Prog& P = *PF;
    Use(P, 6);
    BindTex(0, src);
    BindTex(1, bloomTex);
    BindTex(2, dofTex);
    BindTex(3, wantAuto ? G.lum[G.lumIdx].tex : G.dummyTex);
    BindTex(4, LUT_Texture() ? LUT_Texture() : G.dummyTex);
    Set1i(P, "uScene", 0); Set1i(P, "uBloom", 1); Set1i(P, "uDof", 2); Set1i(P, "uLum", 3); Set1i(P, "uLut", 4);

    const float k = (tm == TM_OFF) ? 0.f : 1.8f * s.hdrAmount.v;
    const float norm = (tm == TM_OFF) ? 1.f : 1.f / fmaxf(TmCpu(tm, 1.f + k), 0.05f);
    G.frame = fmodf(G.frame + 1.f, 64.f);
    Set4(P, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
    Set4(P, "uA", fi.time, s.exposure.v, k, 0.f);
    Set4(P, "uB", conEff, satEff, s.vibrance, s.gamma);
    Set4(P, "uC", s.temperature, s.tint, s.lift, s.gain);
    Set4(P, "uD", wantBloomFx ? s.bloomIntensity.v : 0.f, wantRays ? s.sunRaysIntensity : 0.f, wantFlare ? s.lensFlareIntensity : 0.f, 0.f);
    Set4(P, "uE", s.vignetteStrength.v, s.vignetteSize, s.grainStrength, s.chromaticStrength);
    Set4(P, "uF", s.casStrength, wantDof ? s.tiltStrength : 0.f, wantUnder ? fi.underwater * s.underwaterStrength : 0.f, s.lutAmount);
    Set4(P, "uG", s.celAmount, s.celLevels, s.outlineStrength, s.outlineThreshold);
    Set4(P, "uH", s.vhsStrength, 0.f, 0.f, 0.f);
    Set4(P, "uDofA", (float)s.dofMode, s.tiltFocus, s.tiltWidth, s.tiltStrength);
    Set4(P, "uDofB", s.dofBokeh, s.dofRadius, s.dofUnderwater, 0.f);
    Set4(P, "uSV", s.sunGlow, s.sunGlowSize, s.goldenHour, s.lensFlareIntensity);
    Set4(P, "uSun", fi.sunX, fi.sunY, fi.sunVis, s.sunRaysDecay);
    { const PSLutInfo& li = LUT_Info(); Set4(P, "uLutP", (float)(li.n > 1 ? li.n : 16), (float)(li.tilesPerRow > 0 ? li.tilesPerRow : 16), li.w > 0 ? 1.f / li.w : 1.f / 256.f, li.h > 0 ? 1.f / li.h : 1.f / 16.f); }
    Set4(P, "uK", s.autoBias, s.autoRange, norm, G.frame);
    Set4(P, "uGS", g.sh[0] * ga, g.sh[1] * ga, g.sh[2] * ga, 0.f);
    Set4(P, "uGH", g.hi[0] * ga, g.hi[1] * ga, g.hi[2] * ga, s.fxaaStrength);
    DrawTri();
    if (customOk)
    {   // 7) [GLSL] custom pass: files/ProperGFX/shaders/fragment/custom.glsl on the finished picture
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h);
        Use(G.pCustom, 7);
        BindTex(0, G.post.tex); Set1i(G.pCustom, "uTex", 0);
        Set4(G.pCustom, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
        DrawTri();
        fetches += 1.f;
    }

    G.stats.adaptiveLevel = G.level; G.stats.avgFrameMs = G.emaDt * 1000.f; G.stats.passes = g_passes;
    G.stats.variants = (int)G.variants.size(); G.stats.fullResFetches = fetches;
    return true;
}
