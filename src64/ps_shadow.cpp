#include "ps_shadow.h"
#include "ps_glsl.h"
#include "ps_scene.h"
#include <string>
#include "ps_log.h"
#include <GLES2/gl2ext.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>
#include <time.h>

namespace {

const int   kMaxDraws = 3200;
const float kDepthRange = 220.f;              // half-extent of the light frustum along the sun direction (metres)

struct PInfo { GLint obj = -1, bones = -1, cam = -1, dir = -1, aPos = -1, aUV = -1, view = -1, proj = -1, fog = -1; };
struct Attr  { GLint size = 0, type = 0, norm = 0, stride = 0; uintptr_t ptr = 0; GLuint vbo = 0; };
struct VAttr { Attr pos, uv; bool ok = false; };
struct Draw  { GLuint ibo = 0, tex = 0; GLenum mode = GL_TRIANGLES; GLsizei count = 0; uintptr_t off = 0; Attr pos, uv; float obj[16]; GLint fbo = 0; };
// A shadow caster remembered across frames. The game only draws what is in view, so a building behind you would stop casting into the view the moment you turn;
// anything seen at the same place in two capture frames is treated as static and keeps casting while it is out of view.
struct CEntry { Draw d; uint64_t key = 0; double seen = 0; int hits = 0, miss = 0; uint32_t seenCapture = 0; GLint vboSize = 0, iboSize = 0; bool dead = false; };
struct FboInfo { GLint id = 0; int n = 0; float cam[3] = {0, 0, 0}; float view[16], proj[16]; bool haveCam = false, haveVP = false; };

struct Prog { GLuint id = 0; GLint uLVP = -1, uObj = -1, uTex = -1, uAlpha = -1, uUvScale = -1; };

// ---- 1.4.3 deferred (screen-space) shadows, the libZyZyGFX64 way: see shaders/fragment/shadow_mask.glsl and ps_scene.h ----
struct FrameCam { bool ok = false; GLint fbo = 0; float view[16], proj[16], cam[3] = {0, 0, 0}, fog[3] = {0, 0, 0}; bool haveFog = false; };
struct MaskProg { GLuint id = 0; GLint uDepth = -1, uMap0 = -1, uMap1 = -1, uIVP = -1, uL0 = -1, uL1 = -1, uP = -1, uSun = -1, uTex = -1, uFog = -1, uShade = -1, uTint = -1,
                  uFlags = -1, uSunVector = -1, uUnder = -1; };
struct Deferred
{
    bool es3 = false, checked = false, failed = false;
    Prog caster; MaskProg mask; GLuint apply = 0; GLint applyMask = -1;
    GLuint mapTex[2] = {0, 0}, mapFbo[2] = {0, 0}; int mapRes = 0;
    GLuint maskTex = 0, maskFbo = 0; int mw = 0, mh = 0;
    float lvp[2][16]; float R[2] = {0, 0}; bool mapReady[2] = {false, false};
    uint32_t captures = 0, applied = 0, missedCam = 0, missedDepth = 0;
    FrameCam cam;
    bool sceneDone = false;
    float underwater = 0.f, sunVector[3] = {0, 0, 1};
};

struct ShadowState
{
    PSSettings s;                // copy of the user settings
    int level = 0;
    bool enabled = false, capturing = false, failed = false, noHighp = false;
    uint32_t frame = 0, frameId = 1;
    // capture
    std::vector<Draw> draws;
    std::vector<FboInfo> fbos;                         // per render target seen during the capture frame (the main scene = the one with the most world draws)
    std::unordered_map<uint64_t, CEntry> cache; std::vector<CEntry*> list; uint32_t captureId = 0;
    std::unordered_map<GLuint, PInfo> progs;
    std::unordered_map<uint64_t, VAttr> vcache;
    float cam[3] = {0, 0, 0}, dir[3] = {0, 0, 1}; bool haveCam = false, haveDir = false;
    // sun smoothing
    float sun[3] = {0, 0, 1}; bool haveSun = false;
    float hint[3] = {0, 0, 1}; bool hintValid = false;      // game's CTimeCycle sun vector (set by main.cpp every frame)
    float tcMul = 1.f; bool tcValid = false;
    int   logTick = 0;
    // GPU objects
    Prog rp; GLuint fbo = 0, colorTex = 0, depthRb = 0; int res = 0;
    // result for the receivers
    bool ready = false; float lvp[16] = {0}; float biasNorm = 0.f, texel = 0.f, fade = 0.f;
    int glErrors = 0;
    Deferred def;
    PSShadowStats stats;
};
ShadowState G;

static const char* kRVS =
"attribute vec3 aPos; attribute vec4 aUV;\n"
"uniform mat4 uLightVP; uniform mat4 uObj; uniform float uUvScale;\n"
"varying vec2 vUV;\n"
"void main(){ vUV = aUV.xy * uUvScale; gl_Position = uLightVP * (uObj * vec4(aPos, 1.0)); }\n";

static const char* kRFS =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
"uniform sampler2D uTex; uniform float uAlpha; varying vec2 vUV;\n"
"void main(){\n"
"  if (uAlpha > 0.5) { if (texture2D(uTex, vUV).a < 0.5) discard; }\n"
"  vec3 enc = fract(gl_FragCoord.z * vec3(1.0, 255.0, 65025.0));\n"
"  enc -= enc.yzz * vec3(1.0/255.0, 1.0/255.0, 0.0);\n"
"  gl_FragColor = vec4(enc, 1.0);\n"
"}\n";

double NowMs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
void Note(const char* t) { snprintf(G.stats.note, sizeof(G.stats.note), "%s", t); }

bool Compile(GLuint& out, GLenum type, const char* src)
{
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); PS_Log("shadow shader failed: %s", log); glDeleteShader(s); return false; }
    out = s; return true;
}

bool EnsureProgram()
{
    if (G.rp.id) return true;
    // the shadow lookup needs highp floats in fragment shaders; if the GPU has none, shadows stay off
    GLint range[2] = {0, 0}, prec = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &prec);
    if (prec == 0) { G.noHighp = true; Note("GPU has no highp in fragment shaders - shadows disabled"); return false; }
    GLuint v = 0, f = 0;
    // shaders/{vertex,fragment}/shadow_caster.glsl (built into the .so)
    std::string vs, fs;
    if (!GLSL_Source("vertex/shadow_caster.glsl", vs) || !GLSL_Source("fragment/shadow_caster.glsl", fs)) { vs = kRVS; fs = kRFS; }
    if (!Compile(v, GL_VERTEX_SHADER, vs.c_str()) || !Compile(f, GL_FRAGMENT_SHADER, fs.c_str()))
    {
        if (v) glDeleteShader(v);
        GLSL_MarkFailed("fragment/shadow_caster.glsl", "shadow caster did not compile - see propergfx_log.txt");
        G.failed = true; Note("shadow shader compile failed"); return false;
    }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "aPos"); glBindAttribLocation(p, 1, "aUV");
    glLinkProgram(p); glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); G.failed = true; Note("shadow program link failed"); return false; }
    G.rp.id = p; G.rp.uLVP = glGetUniformLocation(p, "uLightVP"); G.rp.uObj = glGetUniformLocation(p, "uObj");
    G.rp.uTex = glGetUniformLocation(p, "uTex"); G.rp.uAlpha = glGetUniformLocation(p, "uAlpha"); G.rp.uUvScale = glGetUniformLocation(p, "uUvScale");
    return true;
}

bool EnsureTarget(int res)
{
    if (G.fbo && G.res == res) return true;
    if (G.fbo) glDeleteFramebuffers(1, &G.fbo);
    if (G.colorTex) glDeleteTextures(1, &G.colorTex);
    if (G.depthRb) glDeleteRenderbuffers(1, &G.depthRb);
    G.fbo = G.colorTex = G.depthRb = 0;
    glGenTextures(1, &G.colorTex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, G.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &G.depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, G.depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, res, res);
    glGenFramebuffers(1, &G.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G.colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, G.depthRb);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    G.res = res;
    if (st != GL_FRAMEBUFFER_COMPLETE) { PS_Log("shadow FBO incomplete 0x%x", st); G.failed = true; Note("shadow render target unsupported"); return false; }
    return true;
}

PInfo& GetProg(GLuint prog)
{
    auto it = G.progs.find(prog);
    if (it != G.progs.end()) return it->second;
    PInfo pi;
    pi.obj = glGetUniformLocation(prog, "ObjMatrix");
    pi.bones = glGetUniformLocation(prog, "Bones");
    pi.cam = glGetUniformLocation(prog, "CameraPosition");
    pi.dir = glGetUniformLocation(prog, "DirLightDirection");
    pi.aPos = glGetAttribLocation(prog, "Position");
    pi.aUV = glGetAttribLocation(prog, "TexCoord0");
    pi.view = glGetUniformLocation(prog, "ViewMatrix");
    pi.proj = glGetUniformLocation(prog, "ProjMatrix");
    pi.fog = glGetUniformLocation(prog, "FogDistances");
    return G.progs.emplace(prog, pi).first->second;
}

void QueryAttr(GLint loc, Attr& a)
{
    GLint en = 0, buf = 0; GLvoid* p = nullptr;
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &en);
    if (!en) { a = Attr(); return; }
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.norm);
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
    glGetVertexAttribiv(loc, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buf);
    glGetVertexAttribPointerv(loc, GL_VERTEX_ATTRIB_ARRAY_POINTER, &p);
    a.vbo = (GLuint)buf; a.ptr = (uintptr_t)p;
}

inline float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Norm3(float* v) { float l = sqrtf(Dot3(v, v)); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
inline void Cross3(const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
inline float Smooth(float a, float b, float x) { float t = x < a ? 0.f : (x > b ? 1.f : (x - a) / (b - a)); return t * t * (3.f - 2.f * t); }


uint64_t DrawKey(const Draw& d)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(d.pos.vbo); mix(d.ibo); mix((uint64_t)d.off); mix((uint64_t)d.count); mix((uint64_t)d.mode);
    for (int i : { 12, 13, 14 }) mix((uint64_t)(int64_t)lroundf(d.obj[i] * 50.f));              // 2 cm grid: the same mesh instanced elsewhere is a different caster
    for (int i : { 0, 1, 4, 5 })  mix((uint64_t)(int64_t)lroundf(d.obj[i] * 100.f));
    return h;
}

// probably drawn by the game if it existed: inside the view frustum (generous) and close
bool ProbablyDrawn(const FboInfo& f, const float p[3], const float cam[3])
{
    const float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
    if (dx * dx + dy * dy + dz * dz > 70.f * 70.f) return false;
    float v[4], c[4];
    for (int r = 0; r < 4; ++r) v[r] = f.view[r] * p[0] + f.view[4 + r] * p[1] + f.view[8 + r] * p[2] + f.view[12 + r];
    for (int r = 0; r < 4; ++r) c[r] = f.proj[r] * v[0] + f.proj[4 + r] * v[1] + f.proj[8 + r] * v[2] + f.proj[12 + r] * v[3];
    return c[3] > 0.f && fabsf(c[0]) < 1.25f * c[3] && fabsf(c[1]) < 1.25f * c[3];
}

// Turns this frame's captured draws into the list that gets rendered: main-scene draws + remembered static casters the game did not draw this time.
void Commit()
{
    G.list.clear(); G.haveCam = false;
    FboInfo* main = nullptr;
    for (FboInfo& f : G.fbos) if (!main || f.n > main->n) main = &f;
    if (!main) return;
    G.stats.otherTargets = 0;
    for (const FboInfo& f : G.fbos) if (&f != main) G.stats.otherTargets += f.n;
    if (main->haveCam) { memcpy(G.cam, main->cam, sizeof(G.cam)); G.haveCam = true; }
    if (G.cache.size() > 9000) G.cache.clear();                                 // safety valve
    ++G.captureId;
    const double now = NowMs();
    for (const Draw& d : G.draws)
    {
        if (d.fbo != main->id) continue;
        const uint64_t k = DrawKey(d);
        auto it = G.cache.find(k);
        if (it == G.cache.end()) { it = G.cache.emplace(k, CEntry()).first; it->second.key = k; }
        CEntry& e = it->second;
        if (e.seenCapture != G.captureId) ++e.hits;                              // twice in ONE frame (multi-pass) is not "stable"
        e.d = d; e.seen = now; e.miss = 0; e.seenCapture = G.captureId; e.dead = false;
    }
    int kept = 0;
    for (auto it = G.cache.begin(); it != G.cache.end();)
    {
        CEntry& e = it->second;
        bool erase = false;
        if (e.seenCapture != G.captureId)
        {
            if (e.hits < 2) erase = true;                                        // seen once and gone: a moving object
            else if (now - e.seen > 180000.0) erase = true;                      // not seen for 3 minutes
            else if (main->haveVP && main->haveCam && ProbablyDrawn(*main, &e.d.obj[12], main->cam) && ++e.miss >= 3) erase = true;   // should be in view but the game stopped drawing it: moved away / removed
            else ++kept;
        }
        if (erase) { it = G.cache.erase(it); continue; }
        G.list.push_back(&e);
        ++it;
    }
    G.stats.cacheSize = (int)G.cache.size(); G.stats.cacheKept = kept;
}

// sun direction (toward the sun), low-passed so the clock-mod steps never pop; false = no shadows this time (night, interior ...)
// Preferred source: the game's own CTimeCycle sun vector (the one the sun disc / coronas use). Fallback: the directional-light uniform.
bool UpdateSun()
{
    float L[3]; int srcUsed = 0;
    {
        const bool hintOk = G.hintValid && Dot3(G.hint, G.hint) > 0.7f && Dot3(G.hint, G.hint) < 1.35f;
        const bool lightOk = G.haveDir && Dot3(G.dir, G.dir) > 0.25f;
        const int want = G.s.sunSource;                      // 0 auto, 1 time-cycle, 2 light uniform
        if ((want == 0 || want == 1) && hintOk)      { memcpy(L, G.hint, sizeof(L)); srcUsed = 1; }
        else if ((want == 0 || want == 2) && lightOk) { memcpy(L, G.dir, sizeof(L)); srcUsed = 2; }
        else if (hintOk)                              { memcpy(L, G.hint, sizeof(L)); srcUsed = 1; }
        else if (lightOk)                             { memcpy(L, G.dir, sizeof(L)); srcUsed = 2; }
        else { G.fade = 0.f; G.stats.fade = 0.f; Note("no sun direction available (night / interior / symbols missing)"); return false; }
    }
    G.stats.sunSource = srcUsed;
    float len = sqrtf(Dot3(L, L));
    if (len < 0.5f) { G.fade = 0.f; G.stats.fade = 0.f; Note("no directional light (night / interior)"); return false; }
    Norm3(L);
    if (!G.haveSun) { memcpy(G.sun, L, sizeof(L)); G.haveSun = true; }
    else
    {
        float k = G.s.sunSmoothing;
        for (int i = 0; i < 3; ++i) G.sun[i] += (L[i] - G.sun[i]) * k;
        Norm3(G.sun);
    }
    const float elev = G.sun[2];
    G.stats.sunElevation = elev;
    memcpy(G.stats.sunDir, G.sun, sizeof(G.sun));
    G.fade = Smooth(G.s.shadowMinSun, G.s.shadowMinSun + 0.10f, elev);
    G.stats.fade = G.fade;
    if (G.fade <= 0.001f) { Note("sun below the horizon - shadows off"); return false; }
    return true;
}

// Draws G.list (this frame's casters + remembered ones) with program p and light matrix vp; culls by distance from the cascade centre.
// The caller has bound the target, viewport, depth state, program and the light matrix.
int DrawList(const Prog& p, const float center[3], float reach, int maxDraws, int& rej, int& fromCache, int& errs)
{
    GLuint curVbo = 0xffffffffu, curIbo = 0xffffffffu, curTex = 0xffffffffu;
    bool uvOn = false;
    std::unordered_map<GLuint, GLint> iboSize, vboSize;
    int drawn = 0;
    glActiveTexture(GL_TEXTURE0);
    for (CEntry* ep : G.list)
    {
        CEntry& e = *ep; const Draw& d = e.d;
        if (e.dead) continue;
        if (drawn >= maxDraws) break;
        const bool fresh = e.seenCapture == G.captureId;           // drawn by the game this frame (otherwise: remembered)
        const float dx = d.obj[12] - center[0], dy = d.obj[13] - center[1], dz = d.obj[14] - center[2];
        if (dx * dx + dy * dy + dz * dz > reach * reach) { ++rej; ++G.stats.rejDistance; continue; }
        if (!glIsBuffer(d.pos.vbo) || !glIsBuffer(d.ibo)) { ++rej; ++G.stats.rejStale; e.dead = true; continue; }
        if (d.ibo != curIbo) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d.ibo); curIbo = d.ibo; }
        auto it = iboSize.find(d.ibo);
        if (it == iboSize.end()) { GLint sz = 0; glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz); it = iboSize.emplace(d.ibo, sz).first; }
        if ((long long)d.off + (long long)d.count * 2 > it->second) { ++rej; ++G.stats.rejStale; e.dead = true; continue; }      // stale / re-uploaded buffer: never read out of range
        if (d.pos.vbo != curVbo) { glBindBuffer(GL_ARRAY_BUFFER, d.pos.vbo); curVbo = d.pos.vbo; }
        auto vt = vboSize.find(d.pos.vbo);
        if (vt == vboSize.end()) { GLint sz = 0; glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz); vt = vboSize.emplace(d.pos.vbo, sz).first; }
        // a remembered caster whose buffers changed size was freed / re-used by the game for something else: forget it
        if (fresh) { e.vboSize = vt->second; e.iboSize = it->second; }
        else if (e.vboSize != vt->second || e.iboSize != it->second) { ++rej; ++G.stats.rejStale; e.dead = true; continue; }
        glVertexAttribPointer(0, d.pos.size, d.pos.type, d.pos.norm != 0, d.pos.stride, (const void*)d.pos.ptr);
        glEnableVertexAttribArray(0);
        float alpha = 0.f, uvScale = 1.f;
        if (d.uv.size > 0 && d.tex && d.uv.vbo && (fresh || glIsTexture(d.tex)))
        {
            if (d.uv.vbo != curVbo) { glBindBuffer(GL_ARRAY_BUFFER, d.uv.vbo); curVbo = d.uv.vbo; }
            glVertexAttribPointer(1, d.uv.size, d.uv.type, d.uv.norm != 0, d.uv.stride, (const void*)d.uv.ptr);
            glEnableVertexAttribArray(1); uvOn = true;
            if ((d.uv.type == GL_SHORT || d.uv.type == GL_UNSIGNED_SHORT) && !d.uv.norm) uvScale = 1.f / 512.f;   // the game divides by 512.0
            if (d.tex != curTex) { glBindTexture(GL_TEXTURE_2D, d.tex); curTex = d.tex; }
            alpha = 1.f;
        }
        else if (uvOn) { glDisableVertexAttribArray(1); uvOn = false; }
        glUniform1f(p.uAlpha, alpha);
        glUniform1f(p.uUvScale, uvScale);
        glUniformMatrix4fv(p.uObj, 1, GL_FALSE, d.obj);
        glDrawElements(d.mode, d.count, GL_UNSIGNED_SHORT, (const void*)d.off);
        ++drawn; if (!fresh) ++fromCache;
        if ((drawn & 255) == 0) { if (glGetError() != GL_NO_ERROR) ++errs; }
    }
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    while (glGetError() != GL_NO_ERROR) ++errs;
    return drawn;
}

void FinishList()
{
    for (CEntry* ep : G.list) if (ep->dead) G.cache.erase(ep->key);      // after drawing: the list holds pointers into the map
    G.list.clear();
    G.stats.cacheSize = (int)G.cache.size();
}

// 1.4.2 and older ("In-shader" method): one map, RGBA-packed depth, sampled by the patched game shaders
void Replay()
{
    G.stats.captured = (int)G.draws.size();
    if (G.draws.empty()) { Note("no world draws captured (hooks not active?)"); return; }
    if (!G.haveCam) { Note("camera uniform not found"); return; }
    if (!UpdateSun()) return;
    const float elev = G.sun[2];

    if (!EnsureProgram()) return;
    static const int kRes[3] = { 512, 1024, 2048 };
    const int res = kRes[G.s.shadowRes < 0 ? 0 : (G.s.shadowRes > 2 ? 2 : G.s.shadowRes)];
    if (!EnsureTarget(res)) return;

    const double t0 = NowMs();
    const float R = G.s.shadowDist;
    float vp[16];
    Shadow_BuildLightMatrix(G.sun, G.cam, R, kDepthRange, res, vp);

    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
    glViewport(0, 0, res, res);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DITHER);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(1.f, 1.f, 1.f, 1.f); glClearDepthf(1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(G.rp.id);
    glUniformMatrix4fv(G.rp.uLVP, 1, GL_FALSE, vp);
    glUniform1i(G.rp.uTex, 0);

    int rej = 0, errs = 0, fromCache = 0;
    const int drawn = DrawList(G.rp, G.cam, R * 1.5f + 160.f, 5200, rej, fromCache, errs);
    FinishList();
    G.stats.cacheKept = fromCache;
    if (errs > 8) { if (++G.glErrors > 3) { G.failed = true; Note("GL errors while rendering the shadow map - shadows disabled"); } }

    // ---- publish for the receivers ----
    memcpy(G.lvp, vp, sizeof(vp));
    const float texelWorld = 2.f * R / (float)res;
    // receivers on unlit (prelit) world shaders have no normal, so the bias must cover FLAT ground at the current sun height:
    // required offset ~ texel * cot(elevation). Clamped so low sun does not detach shadows completely.
    const float e = elev < 0.15f ? 0.15f : (elev > 1.f ? 1.f : elev);
    float cot = sqrtf(1.f - e * e) / e; if (cot > 4.5f) cot = 4.5f;
    const float biasMeters = (0.12f + texelWorld * (1.3f + 0.85f * cot)) * G.s.shadowBias;
    G.biasNorm = biasMeters * (0.5f / kDepthRange);
    G.texel = G.s.shadowSoft / (float)res;
    G.ready = true;
    ++G.frameId;
    G.stats.replayed = drawn; G.stats.rejected += rej; G.stats.resolution = res; G.stats.ready = true;
    G.stats.cpuMs = (float)(NowMs() - t0);
    Note("ok");
    if (G.logTick++ % 120 == 0)
        PS_Log("shadows: captured=%d replayed=%d cache=%d(kept %d) othertargets=%d (rej: blend=%d nodepthwrite=%d noibo=%d vformat=%d noshader=%d mode=%d dist=%d stale=%d) res=%d sun=(%.2f %.2f %.2f) src=%s %.1fms",
               G.stats.captured, drawn, G.stats.cacheSize, G.stats.cacheKept, G.stats.otherTargets, G.stats.rejBlend, G.stats.rejNoDepthWrite, G.stats.rejNoIndexBuffer, G.stats.rejVertexFormat, G.stats.rejNoShaderInfo, G.stats.rejMode,
               G.stats.rejDistance, G.stats.rejStale, res, G.sun[0], G.sun[1], G.sun[2], G.stats.sunSource == 1 ? "timecycle" : "light", G.stats.cpuMs);
}

// =====================================================================================================================
// 1.4.3 deferred method (default)
// =====================================================================================================================
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif
const float kDefDepth = 260.f;                  // half-extent of the cascades along the sun direction (m)

bool DefBuild(GLuint& out, const char* vsRel, const char* fsRel)
{
    std::string vs, fs;
    if (!GLSL_Source(vsRel, vs) || !GLSL_Source(fsRel, fs)) { PS_Log("deferred shadows: %s / %s are not built into the mod", vsRel, fsRel); return false; }
    GLuint v = 0, f = 0;
    if (!Compile(v, GL_VERTEX_SHADER, vs.c_str())) { GLSL_MarkFailed(vsRel, "did not compile - see propergfx_log.txt"); return false; }
    if (!Compile(f, GL_FRAGMENT_SHADER, fs.c_str())) { glDeleteShader(v); GLSL_MarkFailed(fsRel, "did not compile - see propergfx_log.txt"); return false; }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p); glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512] = ""; glGetProgramInfoLog(p, sizeof(log), nullptr, log); PS_Log("deferred shadows: %s + %s link failed: %s", vsRel, fsRel, log); glDeleteProgram(p); GLSL_MarkFailed(fsRel, "link failed - see propergfx_log.txt"); return false; }
    out = p; return true;
}

bool DefEnsurePrograms()
{
    Deferred& D = G.def;
    if (D.failed) return false;
    if (!D.checked)
    {
        D.checked = true;
        const char* ver = (const char*)glGetString(GL_VERSION);
        D.es3 = ver && strstr(ver, "OpenGL ES 3") != nullptr;
        if (!D.es3) { D.failed = true; Note("deferred shadows need OpenGL ES 3.0 - pick Method = In-shader"); return false; }
    }
    if (D.caster.id && D.mask.id && D.apply) return true;
    if (!D.caster.id)
    {
        if (!DefBuild(D.caster.id, "vertex/shadow_depth.glsl", "fragment/shadow_depth.glsl")) { D.failed = true; Note("shadow_depth.glsl failed - see propergfx_log.txt"); return false; }
        GLuint p = D.caster.id;
        D.caster.uLVP = glGetUniformLocation(p, "uLightVP"); D.caster.uObj = glGetUniformLocation(p, "uObj"); D.caster.uTex = glGetUniformLocation(p, "uTex");
        D.caster.uAlpha = glGetUniformLocation(p, "uAlpha"); D.caster.uUvScale = glGetUniformLocation(p, "uUvScale");
    }
    if (!D.mask.id)
    {
        if (!DefBuild(D.mask.id, "vertex/shadow_screen.glsl", "fragment/shadow_mask.glsl")) { D.failed = true; Note("shadow_mask.glsl failed - see propergfx_log.txt"); return false; }
        MaskProg& m = D.mask; GLuint p = m.id;
        m.uDepth = glGetUniformLocation(p, "uDepth"); m.uMap0 = glGetUniformLocation(p, "uMap0"); m.uMap1 = glGetUniformLocation(p, "uMap1");
        m.uIVP = glGetUniformLocation(p, "uIVP"); m.uL0 = glGetUniformLocation(p, "uL0"); m.uL1 = glGetUniformLocation(p, "uL1");
        m.uP = glGetUniformLocation(p, "uP"); m.uSun = glGetUniformLocation(p, "uSun"); m.uTex = glGetUniformLocation(p, "uTex");
        m.uFog = glGetUniformLocation(p, "uFog"); m.uShade = glGetUniformLocation(p, "uShade"); m.uTint = glGetUniformLocation(p, "uTint");
        m.uFlags = glGetUniformLocation(p, "ShaderFlags"); m.uSunVector = glGetUniformLocation(p, "SunVector"); m.uUnder = glGetUniformLocation(p, "UnderWaterness");
    }
    if (!D.apply)
    {
        if (!DefBuild(D.apply, "vertex/shadow_screen.glsl", "fragment/shadow_apply.glsl")) { D.failed = true; Note("shadow_apply.glsl failed - see propergfx_log.txt"); return false; }
        D.applyMask = glGetUniformLocation(D.apply, "uMask");
    }
    return true;
}

bool DefEnsureMaps(int res)
{
    Deferred& D = G.def;
    if (D.mapFbo[0] && D.mapRes == res) return true;
    for (int i = 0; i < 2; ++i)
    {
        if (D.mapFbo[i]) glDeleteFramebuffers(1, &D.mapFbo[i]);
        if (D.mapTex[i]) glDeleteTextures(1, &D.mapTex[i]);
        D.mapFbo[i] = D.mapTex[i] = 0; D.mapReady[i] = false;
    }
    D.mapRes = res;
    for (int i = 0; i < 2; ++i)
    {
        glGenTextures(1, &D.mapTex[i]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, D.mapTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, res, res, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);         // + compare mode = hardware 2x2 PCF
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &D.mapFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, D.mapFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, D.mapTex[i], 0);
        const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) { PS_Log("deferred shadows: %d^2 depth target incomplete 0x%x", res, st); D.failed = true; Note("shadow depth target unsupported"); return false; }
    }
    return true;
}

bool DefEnsureMask(int w, int h)
{
    Deferred& D = G.def;
    if (D.maskFbo && D.mw == w && D.mh == h) return true;
    if (D.maskFbo) glDeleteFramebuffers(1, &D.maskFbo);
    if (D.maskTex) glDeleteTextures(1, &D.maskTex);
    D.maskFbo = D.maskTex = 0; D.mw = w; D.mh = h;
    glGenTextures(1, &D.maskTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, D.maskTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &D.maskFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, D.maskFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, D.maskTex, 0);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) { PS_Log("deferred shadows: mask target %dx%d incomplete 0x%x", w, h, st); D.failed = true; Note("shadow mask target incomplete - off"); return false; }
    return true;
}

// double-precision 4x4 helpers (column-major like GL)
void MulD(const double* a, const double* b, double* o) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { double s = 0; for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; } }
bool InvD(const double* m, double* inv)
{
    double t[16];
    t[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    t[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    t[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    t[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    t[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    t[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    t[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    t[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    t[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    t[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    t[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    t[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    t[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    double det = m[0] * t[0] + m[1] * t[4] + m[2] * t[8] + m[3] * t[12];
    if (fabs(det) < 1e-30) return false;
    det = 1.0 / det;
    for (int i = 0; i < 16; ++i) inv[i] = t[i] * det;
    return true;
}
// M * translate(t): the matrix then takes (world - camera) instead of world positions
void RelToCam(const float* m, const float t[3], float* out)
{
    for (int i = 0; i < 12; ++i) out[i] = m[i];
    for (int r = 0; r < 4; ++r) out[12 + r] = (float)((double)m[r] * t[0] + (double)m[4 + r] * t[1] + (double)m[8 + r] * t[2] + (double)m[12 + r]);
}

// casters -> the two cascades (near every capture, far every second one)
void DefRenderCascades()
{
    Deferred& D = G.def;
    G.stats.captured = (int)G.draws.size();
    if (G.draws.empty()) { Note("no world draws captured (the draw hook is not active?)"); return; }
    if (!G.haveCam) { Note("camera uniform not found"); return; }
    static const int kRes[3] = { 512, 1024, 2048 };
    const int res = kRes[G.s.shadowRes < 0 ? 0 : (G.s.shadowRes > 2 ? 2 : G.s.shadowRes)];
    if (!DefEnsureMaps(res)) return;
    const double t0 = NowMs();
    const float far = G.s.shadowDist, nearR = fminf(fmaxf(far * 0.28f, 14.f), 40.f);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DITHER);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glClearDepthf(1.f);
    glUseProgram(D.caster.id);
    glUniform1i(D.caster.uTex, 0);
    int drawn = 0, rej = 0, errs = 0, fromCache = 0;
    for (int c = 0; c < 2; ++c)
    {
        if (c == 1 && D.mapReady[1] && (D.captures & 1)) continue;                  // far cascade: every second capture
        const float R = c == 0 ? nearR : far;
        float vp[16];
        Shadow_BuildLightMatrix(G.sun, G.cam, R, kDefDepth, res, vp);
        glBindFramebuffer(GL_FRAMEBUFFER, D.mapFbo[c]);
        glViewport(0, 0, res, res);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(D.caster.uLVP, 1, GL_FALSE, vp);
        drawn += DrawList(D.caster, G.cam, c == 0 ? R * 1.5f + 60.f : R * 1.5f + 160.f, c == 0 ? 4000 : 5200, rej, fromCache, errs);
        memcpy(D.lvp[c], vp, sizeof(vp)); D.R[c] = R; D.mapReady[c] = true;
    }
    FinishList();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    ++D.captures;
    if (errs > 8) { if (++G.glErrors > 3) { G.failed = true; Note("GL errors while rendering the shadow cascades - shadows disabled"); } }
    G.stats.replayed = drawn; G.stats.rejected += rej; G.stats.resolution = res; G.stats.cacheKept = fromCache;
    G.stats.cpuMs = (float)(NowMs() - t0);
    if (G.logTick++ % 120 == 0)
        PS_Log("shadows (deferred): captured=%d rendered=%d (2 cascades %d^2, near %.0f m / far %.0f m) cache=%d kept=%d othertargets=%d rej: blend=%d nodepthwrite=%d noibo=%d vformat=%d noshader=%d mode=%d dist=%d stale=%d sun=(%.2f %.2f %.2f) %.1fms",
               G.stats.captured, drawn, res, nearR, far, G.stats.cacheSize, fromCache, G.stats.otherTargets, G.stats.rejBlend, G.stats.rejNoDepthWrite, G.stats.rejNoIndexBuffer,
               G.stats.rejVertexFormat, G.stats.rejNoShaderInfo, G.stats.rejMode, G.stats.rejDistance, G.stats.rejStale, G.sun[0], G.sun[1], G.sun[2], G.stats.cpuMs);
}

// mask (scene depth + cascades -> multiply factor) and apply (scene colour *= mask). The scene target is bound when this runs.
void DefMaskAndApply(GLint sceneFbo, GLuint depthTex, int w, int h)
{
    Deferred& D = G.def;
    if (!D.mapReady[0] || !D.mapReady[1]) return;
    const FrameCam& fc = D.cam;
    // inverse view-projection relative to the camera: ndc -> world - camera (double precision: world coordinates reach 3000 m)
    double P[16], V[16], PV[16], T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}, PVT[16], I[16];
    for (int i = 0; i < 16; ++i) { P[i] = fc.proj[i]; V[i] = fc.view[i]; }
    T[12] = fc.cam[0]; T[13] = fc.cam[1]; T[14] = fc.cam[2];
    MulD(P, V, PV); MulD(PV, T, PVT);
    if (!InvD(PVT, I)) { Note("camera matrix not invertible"); return; }
    float ivp[16]; for (int i = 0; i < 16; ++i) ivp[i] = (float)I[i];
    float L0[16], L1[16]; RelToCam(D.lvp[0], fc.cam, L0); RelToCam(D.lvp[1], fc.cam, L1);

    const int scale = G.s.shadowMaskHalf ? 2 : 1;
    const int mw = (w + scale - 1) / scale, mh = (h + scale - 1) / scale;
    if (!DefEnsureMask(mw, mh)) return;
    const float res = (float)D.mapRes;
    const float strength = G.s.shadowStrength * G.fade * ((G.s.shadowTimecyc && G.tcValid) ? G.tcMul : 1.f);

    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER); glDisable(GL_POLYGON_OFFSET_FILL); glDepthMask(GL_FALSE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for (int i = 0; i < 8; ++i) glDisableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // ---- mask
    glBindFramebuffer(GL_FRAMEBUFFER, D.maskFbo);
    glViewport(0, 0, mw, mh);
    const MaskProg& m = D.mask;
    glUseProgram(m.id);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, D.mapTex[0]);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, D.mapTex[1]);
    glUniform1i(m.uDepth, 0); glUniform1i(m.uMap0, 1); glUniform1i(m.uMap1, 2);
    glUniformMatrix4fv(m.uIVP, 1, GL_FALSE, ivp);
    glUniformMatrix4fv(m.uL0, 1, GL_FALSE, L0);
    glUniformMatrix4fv(m.uL1, 1, GL_FALSE, L1);
    glUniform4f(m.uP, (float)scale, fc.cam[2] > 0.5f ? fc.cam[2] : 0.f, G.s.shadowSoft, G.s.shadowBias);
    glUniform4f(m.uSun, G.sun[0], G.sun[1], G.sun[2], 0.5f / kDefDepth);
    glUniform4f(m.uTex, 2.f * D.R[0] / res, 2.f * D.R[1] / res, 1.f / res, 1.f / res);
    if (fc.haveFog && fc.fog[2] > 0.f) glUniform2f(m.uFog, fc.fog[0], fc.fog[2]); else glUniform2f(m.uFog, 0.f, 0.f);
    glUniform4f(m.uShade, strength, G.s.shadowTint, 0.6f, 0.f);
    glUniform3f(m.uTint, G.s.shadowTintR, G.s.shadowTintG, G.s.shadowTintB);
    if (m.uFlags >= 0) glUniform1i(m.uFlags, 0x20000000 | 0x100);
    if (m.uSunVector >= 0) glUniform3f(m.uSunVector, D.sunVector[0], D.sunVector[1], D.sunVector[2]);
    if (m.uUnder >= 0) glUniform1f(m.uUnder, D.underwater);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // ---- apply: scene colour *= mask (alpha untouched)
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)sceneFbo);
    glViewport(0, 0, w, h);
    glUseProgram(D.apply);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, D.maskTex);
    glUniform1i(D.applyMask, 0);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    ++D.applied;
    G.ready = true; G.stats.ready = true;
    Note("ok (deferred)");
}

} // namespace

void Shadow_BuildLightMatrix(const float sunDir[3], const float center[3], float R, float D, int res, float out[16])
{
    float f[3] = { -sunDir[0], -sunDir[1], -sunDir[2] };            // light looks from the sun toward the scene
    Norm3(f);
    float up[3] = { 0.f, 0.f, 1.f };
    if (fabsf(f[2]) > 0.98f) { up[0] = 0.f; up[1] = 1.f; up[2] = 0.f; }
    float right[3]; Cross3(f, up, right); Norm3(right);
    float u[3];     Cross3(right, f, u);  Norm3(u);
    // snap the centre to whole texels in light space -> shadows do not shimmer when the camera moves
    const float ts = 2.f * R / (float)res;
    float cx = Dot3(right, center), cy = Dot3(u, center);
    float sx = floorf(cx / ts + 0.5f) * ts, sy = floorf(cy / ts + 0.5f) * ts;
    float C[3] = { center[0] + right[0] * (sx - cx) + u[0] * (sy - cy), center[1] + right[1] * (sx - cx) + u[1] * (sy - cy),
                   center[2] + right[2] * (sx - cx) + u[2] * (sy - cy) };
    const float r0[4] = { right[0] / R, right[1] / R, right[2] / R, -Dot3(right, C) / R };
    const float r1[4] = { u[0] / R,     u[1] / R,     u[2] / R,     -Dot3(u, C) / R };
    const float r2[4] = { f[0] / D,     f[1] / D,     f[2] / D,     -Dot3(f, C) / D };
    const float r3[4] = { 0.f, 0.f, 0.f, 1.f };
    const float* rows[4] = { r0, r1, r2, r3 };
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) out[c * 4 + r] = rows[r][c];     // column-major for glUniformMatrix4fv
}

void Shadow_Configure(const PSSettings& s, int level)
{
    G.s = s; G.level = level;
    G.enabled = s.enabled && s.shadows && level < 3 && !G.failed && !G.noHighp;
    G.stats.enabled = G.enabled;
    G.stats.deferred = s.shadowMethod == 0;
    Scene_SetWanted(G.enabled && s.shadowMethod == 0 && !G.def.failed);
}

bool Shadow_Deferred() { return G.s.shadowMethod == 0; }

void Shadow_SetFrameGlobals(float underwater, const float sunVector[3])
{
    G.def.underwater = underwater;
    memcpy(G.def.sunVector, sunVector, sizeof(G.def.sunVector));
}

bool Shadow_WantCamera() { return G.enabled && G.s.shadowMethod == 0 && !G.def.cam.ok && Scene_Wanted(); }

// deferred: the camera of THIS frame (the mask is rebuilt every frame, the casters only every N frames)
void Shadow_RecordCamera()
{
    if (G.def.cam.ok) return;
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (!prog) return;
    PInfo& pi = GetProg((GLuint)prog);
    if (pi.obj < 0 || pi.view < 0 || pi.proj < 0 || pi.cam < 0) return;               // 2D / HUD shaders
    GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    GLuint dt = 0; int w = 0, h = 0;
    if (!Scene_DepthOf(fb, dt, w, h)) return;                                           // not the scene target (ped shadows, reflections)
    FrameCam& c = G.def.cam;
    glGetUniformfv((GLuint)prog, pi.view, c.view); glGetUniformfv((GLuint)prog, pi.proj, c.proj); glGetUniformfv((GLuint)prog, pi.cam, c.cam);
    c.haveFog = pi.fog >= 0; if (c.haveFog) glGetUniformfv((GLuint)prog, pi.fog, c.fog);
    c.fbo = fb; c.ok = true;
}

void Shadow_BeginScene()
{
    G.def.cam.ok = false; G.def.sceneDone = false;
}

// render thread, right after the game's RenderScene (event 2): the scene target holds the 3D world, nothing 2D yet
bool Shadow_AfterSceneNeeded() { return G.enabled && G.s.shadowMethod == 0 && !G.def.sceneDone && !G.def.failed; }

void Shadow_AfterScene()
{
    Deferred& D = G.def;
    if (!Shadow_AfterSceneNeeded()) return;
    D.sceneDone = true;
    GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    if (!DefEnsurePrograms()) { G.capturing = false; return; }
    if (G.capturing)
    {   // this frame's world draws are complete: casters -> cascades
        G.capturing = false;
        Commit();
        if (UpdateSun()) DefRenderCascades();
        G.draws.clear(); G.vcache.clear(); G.fbos.clear(); G.list.clear();
    }
    else if (!UpdateSun()) return;                       // sun under the horizon: no mask
    if (G.fade <= 0.001f) return;
    if (!D.cam.ok) { ++D.missedCam; if (D.missedCam % 300 == 1) Note("no camera seen in the scene target this frame"); return; }
    GLuint dt = 0; int w = 0, h = 0;
    GLint target = D.cam.fbo;
    if (!Scene_DepthOf(target, dt, w, h)) { ++D.missedDepth; Note("the scene target has no readable depth"); return; }
    if (w <= 0 || h <= 0) { GLint vp[4] = {0, 0, 0, 0}; glGetIntegerv(GL_VIEWPORT, vp); w = vp[2]; h = vp[3]; }   // foreign depth texture: viewport size
    if (w < 16 || h < 16) return;
    DefMaskAndApply(target, dt, w, h);
    (void)fb;
}

void Shadow_SetTimecyc(float mul, int raw, int rawMax, bool valid)
{
    G.tcValid = valid; G.tcMul = valid ? mul : 1.f;
    G.stats.timecycValid = valid; G.stats.timecycMul = G.tcMul; G.stats.timecycRaw = raw; G.stats.timecycMax = rawMax;
}

void Shadow_SetSunHint(const float v[3], bool valid)
{
    G.hintValid = valid;
    if (valid) { G.hint[0] = v[0]; G.hint[1] = v[1]; G.hint[2] = v[2]; }
}

bool Shadow_WantCapture() { return G.capturing; }

void Shadow_RecordDraw(int mode, int count, uintptr_t off)
{
    if (!G.capturing) return;
    if (G.s.shadowMethod == 0 && !G.def.cam.ok) Shadow_RecordCamera();
    if ((int)G.draws.size() >= kMaxDraws) return;
    if ((mode != 0 && mode != 1) || count < 3) { ++G.stats.rejected; ++G.stats.rejMode; return; }   // 0 = triangle list, 1 = strip (game's own primitive ids)
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (!prog) return;
    PInfo& pi = GetProg((GLuint)prog);
    if (pi.obj < 0 || pi.bones >= 0 || pi.aPos < 0) { ++G.stats.rejected; ++G.stats.rejNoShaderInfo; return; }        // 2D / HUD / skinned: not a shadow caster
    if (glIsEnabled(GL_BLEND)) { ++G.stats.rejected; ++G.stats.rejBlend; return; }                              // translucent: particles, glass, water ...
    GLboolean dm = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
    if (!dm) { ++G.stats.rejected; ++G.stats.rejNoDepthWrite; return; }                                               // sky dome etc. do not write depth
    GLint ibo = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ibo);
    if (!ibo) { ++G.stats.rejected; ++G.stats.rejNoIndexBuffer; return; }                                              // client-side indices: not replayable
    GLint vbo = 0; glGetVertexAttribiv(pi.aPos, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vbo);
    if (!vbo) { ++G.stats.rejected; ++G.stats.rejVertexFormat; return; }

    const uint64_t key = ((uint64_t)(GLuint)prog << 32) | (uint32_t)vbo;
    auto it = G.vcache.find(key);
    if (it == G.vcache.end())
    {
        VAttr va;
        QueryAttr(pi.aPos, va.pos);
        if (pi.aUV >= 0) QueryAttr(pi.aUV, va.uv);
        va.ok = va.pos.vbo && va.pos.size >= 3 && va.pos.type == GL_FLOAT;
        it = G.vcache.emplace(key, va).first;
    }
    if (!it->second.ok) { ++G.stats.rejected; ++G.stats.rejVertexFormat; return; }
    if (!G.haveDir && pi.dir >= 0) { glGetUniformfv((GLuint)prog, pi.dir, G.dir); G.haveDir = true; }

    // which render target is this draw going into? (the game also renders its own ped-shadow / reflection passes with the same shaders)
    GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    FboInfo* fi = nullptr;
    for (FboInfo& f : G.fbos) if (f.id == fb) { fi = &f; break; }
    if (!fi) { if (G.fbos.size() >= 8) return; G.fbos.emplace_back(); fi = &G.fbos.back(); fi->id = fb; }
    ++fi->n;
    if (!fi->haveCam && pi.cam >= 0) { glGetUniformfv((GLuint)prog, pi.cam, fi->cam); fi->haveCam = true; }
    if (!fi->haveVP && pi.view >= 0 && pi.proj >= 0) { glGetUniformfv((GLuint)prog, pi.view, fi->view); glGetUniformfv((GLuint)prog, pi.proj, fi->proj); fi->haveVP = true; }

    Draw d;
    d.fbo = fb;
    d.ibo = (GLuint)ibo; d.mode = mode == 1 ? GL_TRIANGLE_STRIP : GL_TRIANGLES; d.count = count; d.off = off;
    d.pos = it->second.pos; d.uv = it->second.uv;
    glGetUniformfv((GLuint)prog, pi.obj, d.obj);
    if (d.uv.size > 0)
    {   // diffuse texture (unit 0) for alpha-tested foliage / fences; the active unit is restored so the game's own tracker stays valid
        GLint prev = 0, t = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prev);
        if (prev != GL_TEXTURE0) glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
        if (prev != GL_TEXTURE0) glActiveTexture((GLenum)prev);
        d.tex = (GLuint)t;
    }
    G.draws.push_back(d);
}

void Shadow_EndFrame()
{
    const bool deferred = G.s.shadowMethod == 0;
    if (G.capturing)
    {   // deferred: the capture ends at the after-scene event; still capturing here = that event never came this frame
        G.capturing = false;
        if (G.enabled && !deferred) { Commit(); Replay(); }
        else if (G.enabled && deferred && !G.def.failed)
        {
            const PSSceneStats& ss = Scene_Stats();
            if (!ss.rqReady) Note(ss.note[0] ? ss.note : "waiting for the render queue (after-scene event not delivered yet)");
        }
        G.draws.clear(); G.vcache.clear(); G.fbos.clear(); G.list.clear();
    }
    if (deferred) { G.def.cam.ok = false; G.def.sceneDone = false; }
    ++G.frame;
    if (!G.enabled) { if (G.ready && G.s.shadows == false) { /* keep texture, strength goes to 0 via Shadow_Uniforms */ } return; }
    int every = G.s.shadowEvery * (G.level >= 2 ? 2 : 1);
    if (every < 1) every = 1;
    G.capturing = (G.frame % (uint32_t)every) == 0;
    if (G.capturing) { G.draws.clear(); G.vcache.clear(); G.fbos.clear(); G.haveCam = G.haveDir = false; G.stats.rejected = 0; G.stats.rejBlend = G.stats.rejNoDepthWrite = G.stats.rejNoIndexBuffer = G.stats.rejVertexFormat = G.stats.rejNoShaderInfo = G.stats.rejMode = G.stats.rejDistance = G.stats.rejStale = 0; }
}

void Shadow_Uniforms(float lightVP[16], float params[4])
{
    if (!G.ready || !G.enabled || G.s.shadowMethod == 0) { memset(lightVP, 0, 16 * sizeof(float)); memset(params, 0, 4 * sizeof(float)); return; }
    memcpy(lightVP, G.lvp, 16 * sizeof(float));
    params[0] = G.s.shadowStrength * G.fade * ((G.s.shadowTimecyc && G.tcValid) ? G.tcMul : 1.f);
    params[1] = G.biasNorm; params[2] = G.texel; params[3] = 0.f;
}

GLuint Shadow_Texture() { return G.s.shadowMethod == 0 ? G.def.maskTex : G.colorTex; }
uint32_t Shadow_FrameId() { return G.frameId + G.frame * 0u; }
bool Shadow_SetGuardFailed(bool failed)
{
    G.failed = failed; G.stats.guardTripped = failed;
    if (failed) Note("SAFE MODE: the previous launch did not finish, shadows are off. Tap 'Retry shadows' in the menu and restart the game.");
    else Note("retry requested - restart the game to compile the shadow receivers");
    return G.failed;
}
void Shadow_ClearProgramCache() { G.progs.clear(); G.vcache.clear(); }
const PSShadowStats& Shadow_Stats() { return G.stats; }

void Shadow_ContextLost()
{
    bool failed = G.failed; PSSettings s = G.s;
    G = ShadowState(); G.failed = failed; G.s = s;
    Note("context lost - rebuilding");
}

void Shadow_Shutdown()
{
    if (G.rp.id) glDeleteProgram(G.rp.id);
    if (G.fbo) glDeleteFramebuffers(1, &G.fbo);
    if (G.colorTex) glDeleteTextures(1, &G.colorTex);
    if (G.depthRb) glDeleteRenderbuffers(1, &G.depthRb);
    Deferred& D = G.def;
    if (D.caster.id) glDeleteProgram(D.caster.id);
    if (D.mask.id) glDeleteProgram(D.mask.id);
    if (D.apply) glDeleteProgram(D.apply);
    for (int i = 0; i < 2; ++i) { if (D.mapFbo[i]) glDeleteFramebuffers(1, &D.mapFbo[i]); if (D.mapTex[i]) glDeleteTextures(1, &D.mapTex[i]); }
    if (D.maskFbo) glDeleteFramebuffers(1, &D.maskFbo);
    if (D.maskTex) glDeleteTextures(1, &D.maskTex);
    G = ShadowState();
}
