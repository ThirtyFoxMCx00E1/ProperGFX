#include "ps_shadow.h"
#include "ps_log.h"
#include <GLES2/gl2ext.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <time.h>

namespace {

const int   kMaxDraws = 6000;
const int   kMaxCache = 8000;
#ifndef GL_GREATER
#define GL_GREATER 0x0204
#endif
const float kDepthRange = 220.f;              // half-extent of the light frustum along the sun direction (metres)

struct PInfo { GLint obj = -1, bones = -1, cam = -1, dir = -1, aPos = -1, aUV = -1, view = -1, proj = -1; bool discard = false, srcKnown = false; };
struct Attr  { GLint size = 0, type = 0, norm = 0, stride = 0; uintptr_t ptr = 0; GLuint vbo = 0; };
struct VAttr { Attr pos, uv; bool ok = false; };
// 1.4.6: alphaRef = alpha-test threshold (0 = solid), *Gen = buffer generation at capture (bumped by the game's glBufferData / SubData / Delete)
struct Draw  { GLuint ibo = 0, tex = 0; GLenum mode = GL_TRIANGLES; GLsizei count = 0; uintptr_t off = 0; Attr pos, uv; float obj[16]; GLint fbo = 0;
               float alphaRef = 0.f; uint32_t vboGen = 0, iboGen = 0, uvGen = 0; bool cacheable = false; };
// A shadow caster remembered across frames. The game only draws what is in view, so a building behind you would stop casting into the view the moment you turn;
// anything seen at the same place in two capture frames is treated as static and keeps casting while it is out of view.
struct CEntry { Draw d; uint64_t key = 0; double seen = 0; int hits = 0, miss = 0; uint32_t seenCapture = 0; GLint vboSize = 0, iboSize = 0; bool dead = false, temp = false; float dist2 = 0.f; };
struct FboInfo { GLint id = 0; int n = 0; float cam[3] = {0, 0, 0}; float view[16], proj[16]; bool haveCam = false, haveVP = false; };

struct Prog { GLuint id = 0; GLint uLVP = -1, uObj = -1, uTex = -1, uAlpha = -1, uUvScale = -1; };

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
    std::vector<CEntry> temp;                          // 1.4.6: this capture's non-cacheable casters (dynamic buffers)
    const int* atFunc = nullptr; const float* atRef = nullptr; bool bufTracking = false;
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
    PSShadowStats stats;
};
ShadowState G;

// 1.4.6 buffer generations (see the 64-bit tree): a remembered caster dies when the game rewrites / frees its buffers = no ghost shadows
struct BufInfo { uint32_t gen = 0, lastFrame = 0; uint16_t hot = 0; };
std::vector<BufInfo> g_buf; std::unordered_map<GLuint, BufInfo> g_bufBig;
BufInfo* Buf(GLuint id, bool create)
{
    if (id < (1u << 18)) { if (id >= g_buf.size()) { if (!create) return nullptr; g_buf.resize(((size_t)id + 4096) & ~(size_t)4095); } return &g_buf[id]; }
    auto it = g_bufBig.find(id);
    if (it != g_bufBig.end()) return &it->second;
    return create ? &g_bufBig[id] : nullptr;
}
uint32_t BufGen(GLuint id) { const BufInfo* b = Buf(id, false); return b ? b->gen : 0u; }
bool BufHot(GLuint id) { const BufInfo* b = Buf(id, false); return b && b->hot >= 3 && G.frame - b->lastFrame < 600; }
void BufTouch(GLuint id)
{
    if (!id) return;
    BufInfo* b = Buf(id, true);
    b->hot = (G.frame - b->lastFrame < 120 && b->gen) ? (uint16_t)(b->hot < 1000 ? b->hot + 1 : b->hot) : 1;
    b->lastFrame = G.frame; ++b->gen;
}

static const char* kRVS =
"attribute vec3 aPos; attribute vec4 aUV;\n"
"uniform mat4 uLightVP; uniform mat4 uObj; uniform float uUvScale;\n"
"varying vec2 vUV;\n"
"void main(){ vUV = aUV.xy * uUvScale; gl_Position = uLightVP * (uObj * vec4(aPos, 1.0)); }\n";

static const char* kRFS =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
"uniform sampler2D uTex; uniform float uAlpha; varying vec2 vUV;\n"
"void main(){\n"
"  if (uAlpha > 0.0) { if (texture2D(uTex, vUV).a < uAlpha) discard; }\n"
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
    if (!Compile(v, GL_VERTEX_SHADER, kRVS) || !Compile(f, GL_FRAGMENT_SHADER, kRFS)) { G.failed = true; Note("shadow shader compile failed"); return false; }
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
    {   // 1.4.6: does the game's fragment shader alpha-test (discard)?
        GLuint sh[4] = {0, 0, 0, 0}; GLsizei n = 0;
        glGetAttachedShaders(prog, 4, &n, sh);
        for (GLsizei i = 0; i < n; ++i)
        {
            GLint type = 0, len = 0; glGetShaderiv(sh[i], GL_SHADER_TYPE, &type);
            if (type != GL_FRAGMENT_SHADER) continue;
            glGetShaderiv(sh[i], GL_SHADER_SOURCE_LENGTH, &len);
            if (len <= 1 || len > (1 << 20)) continue;
            std::string src((size_t)len, '\0'); GLsizei got = 0;
            glGetShaderSource(sh[i], len, &got, &src[0]);
            if (got > 0) { pi.srcKnown = true; pi.discard = src.find("discard") != std::string::npos; }
        }
        while (glGetError() != GL_NO_ERROR) {}
    }
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

// probably drawn by the game if it existed: well inside the view frustum and close
bool ProbablyDrawn(const FboInfo& f, const float p[3], const float cam[3])
{
    const float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
    if (dx * dx + dy * dy + dz * dz > 110.f * 110.f) return false;
    float v[4], c[4];
    for (int r = 0; r < 4; ++r) v[r] = f.view[r] * p[0] + f.view[4 + r] * p[1] + f.view[8 + r] * p[2] + f.view[12 + r];
    for (int r = 0; r < 4; ++r) c[r] = f.proj[r] * v[0] + f.proj[4 + r] * v[1] + f.proj[8 + r] * v[2] + f.proj[12 + r] * v[3];
    return c[3] > 0.f && fabsf(c[0]) < 0.9f * c[3] && fabsf(c[1]) < 0.9f * c[3];
}

// 1.4.6: this capture's draws + remembered static casters -> a SORTED list (this capture first, then nearest first), so the replay cap always
// drops the same far casters (1.4.5 used the hash-map order = random casters popping on / off). Remembered casters die when the game rewrote /
// freed their buffers, when they are far behind, or when they are in front of the camera and the game stopped drawing them.
void Commit()
{
    G.list.clear(); G.temp.clear(); G.haveCam = false;
    FboInfo* main = nullptr;
    for (FboInfo& f : G.fbos) if (!main || f.n > main->n) main = &f;
    if (!main) return;
    G.stats.otherTargets = 0;
    for (const FboInfo& f : G.fbos) if (&f != main) G.stats.otherTargets += f.n;
    if (main->haveCam) { memcpy(G.cam, main->cam, sizeof(G.cam)); G.haveCam = true; }
    ++G.captureId;
    const double now = NowMs();
    G.temp.reserve(G.draws.size());
    for (const Draw& d : G.draws)
    {
        if (d.fbo != main->id) continue;
        if (!d.cacheable)
        {
            G.temp.emplace_back(); CEntry& e = G.temp.back();
            e.d = d; e.seen = now; e.hits = 1; e.seenCapture = G.captureId; e.temp = true;
            continue;
        }
        const uint64_t k = DrawKey(d);
        auto it = G.cache.find(k);
        if (it == G.cache.end()) { it = G.cache.emplace(k, CEntry()).first; it->second.key = k; }
        CEntry& e = it->second;
        if (e.seenCapture != G.captureId) ++e.hits;
        e.d = d; e.seen = now; e.miss = 0; e.seenCapture = G.captureId; e.dead = false;
    }
    const float keepR = G.s.shadowDist * 1.5f + 260.f;
    int kept = 0;
    for (auto it = G.cache.begin(); it != G.cache.end();)
    {
        CEntry& e = it->second;
        bool erase = e.dead;
        if (!erase && e.seenCapture != G.captureId)
        {
            const float dx = e.d.obj[12] - G.cam[0], dy = e.d.obj[13] - G.cam[1], dz = e.d.obj[14] - G.cam[2];
            const uint32_t gu = e.d.uv.vbo ? BufGen(e.d.uv.vbo) : e.d.uvGen;
            if (BufGen(e.d.pos.vbo) != e.d.vboGen || BufGen(e.d.ibo) != e.d.iboGen || gu != e.d.uvGen) erase = true;   // the game rewrote / freed the mesh
            else if (e.hits < 2) erase = true;                                   // seen once and gone: a moving object
            else if (now - e.seen > 20000.0) erase = true;                       // not seen for 20 s
            else if (G.haveCam && dx * dx + dy * dy + dz * dz > keepR * keepR) erase = true;
            else if (main->haveVP && main->haveCam && ProbablyDrawn(*main, &e.d.obj[12], main->cam) && ++e.miss >= 2) erase = true;   // in front of us, not drawn: gone
            else ++kept;
        }
        if (erase) { it = G.cache.erase(it); continue; }
        ++it;
    }
    if ((int)G.cache.size() > kMaxCache)
    {
        std::vector<std::pair<double, uint64_t>> age; age.reserve(G.cache.size());
        for (auto& kv : G.cache) if (kv.second.seenCapture != G.captureId) age.emplace_back(kv.second.seen, kv.first);
        std::sort(age.begin(), age.end());
        const size_t drop = std::min(age.size(), G.cache.size() - (size_t)(kMaxCache * 9 / 10));
        for (size_t i = 0; i < drop; ++i) G.cache.erase(age[i].second);
    }
    G.list.reserve(G.cache.size() + G.temp.size());
    for (CEntry& e : G.temp) G.list.push_back(&e);
    for (auto& kv : G.cache) G.list.push_back(&kv.second);
    for (CEntry* e : G.list)
    {
        const float dx = e->d.obj[12] - G.cam[0], dy = e->d.obj[13] - G.cam[1], dz = e->d.obj[14] - G.cam[2];
        e->dist2 = dx * dx + dy * dy + dz * dz;
    }
    const uint32_t cid = G.captureId;
    std::sort(G.list.begin(), G.list.end(), [cid](const CEntry* a, const CEntry* b) {
        const bool fa = a->seenCapture == cid, fb = b->seenCapture == cid;
        if (fa != fb) return fa;
        if (a->dist2 != b->dist2) return a->dist2 < b->dist2;
        return a->key < b->key;
    });
    G.stats.cacheSize = (int)G.cache.size(); G.stats.cacheKept = kept;
}

void Replay()
{
    G.stats.captured = (int)G.draws.size();
    if (G.draws.empty()) { Note("no world draws captured (hooks not active?)"); return; }
    if (!G.haveCam) { Note("camera uniform not found"); return; }

    // ---- sun direction (toward the sun), low-passed so the clock-mod steps never pop ----
    // Preferred source: the game's own CTimeCycle sun vector (the one the sun disc / coronas use). Fallback: the directional-light uniform of the object shaders.
    float L[3]; int srcUsed = 0;
    {
        const bool hintOk = G.hintValid && Dot3(G.hint, G.hint) > 0.7f && Dot3(G.hint, G.hint) < 1.35f;
        const bool lightOk = G.haveDir && Dot3(G.dir, G.dir) > 0.25f;
        const int want = G.s.sunSource;                      // 0 auto, 1 time-cycle, 2 light uniform
        if ((want == 0 || want == 1) && hintOk)      { memcpy(L, G.hint, sizeof(L)); srcUsed = 1; }
        else if ((want == 0 || want == 2) && lightOk) { memcpy(L, G.dir, sizeof(L)); srcUsed = 2; }
        else if (hintOk)                              { memcpy(L, G.hint, sizeof(L)); srcUsed = 1; }
        else if (lightOk)                             { memcpy(L, G.dir, sizeof(L)); srcUsed = 2; }
        else { G.fade = 0.f; G.stats.fade = 0.f; Note("no sun direction available (night / interior / symbols missing)"); return; }
    }
    G.stats.sunSource = srcUsed;
    float len = sqrtf(Dot3(L, L));
    if (len < 0.5f) { G.fade = 0.f; G.stats.fade = 0.f; Note("no directional light (night / interior)"); return; }
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
    if (G.fade <= 0.001f) { Note("sun below the horizon - shadows off"); return; }

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
    glActiveTexture(GL_TEXTURE0);

    GLuint curVbo = 0xffffffffu, curIbo = 0xffffffffu, curTex = 0xffffffffu, curUvVbo = 0xffffffffu;
    bool uvOn = false;
    std::unordered_map<GLuint, GLint> iboSize, vboSize;
    const float reach = R * 1.5f + 160.f;                      // objects can be large: generous distance cull
    int drawn = 0, rej = 0, errs = 0, fromCache = 0;
    const int kMaxReplay = 5200;
    for (CEntry* ep : G.list)
    {
        CEntry& e = *ep; const Draw& d = e.d;
        if (drawn >= kMaxReplay) break;
        const bool fresh = e.seenCapture == G.captureId;           // drawn by the game this frame (otherwise: remembered)
        const float dx = d.obj[12] - G.cam[0], dy = d.obj[13] - G.cam[1], dz = d.obj[14] - G.cam[2];
        if (dx * dx + dy * dy + dz * dz > reach * reach) { ++rej; ++G.stats.rejDistance; continue; }
        if (G.bufTracking && (BufGen(d.pos.vbo) != d.vboGen || BufGen(d.ibo) != d.iboGen || (d.uv.vbo && BufGen(d.uv.vbo) != d.uvGen)))
        { ++rej; ++G.stats.rejStale; if (!e.temp) e.dead = true; continue; }      // the buffer now holds something else
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
        const bool cut = d.alphaRef > 0.f;
        if (cut && (!d.tex || !d.uv.vbo || d.uv.size <= 0 || (!fresh && !glIsTexture(d.tex)))) { ++rej; ++G.stats.rejStale; if (!fresh) e.dead = true; continue; }
        glVertexAttribPointer(0, d.pos.size, d.pos.type, d.pos.norm != 0, d.pos.stride, (const void*)d.pos.ptr);
        glEnableVertexAttribArray(0);
        float alpha = 0.f, uvScale = 1.f;
        if (cut)
        {
            if (d.uv.vbo != curVbo) { glBindBuffer(GL_ARRAY_BUFFER, d.uv.vbo); curVbo = d.uv.vbo; }
            glVertexAttribPointer(1, d.uv.size, d.uv.type, d.uv.norm != 0, d.uv.stride, (const void*)d.uv.ptr);
            glEnableVertexAttribArray(1); uvOn = true;
            if ((d.uv.type == GL_SHORT || d.uv.type == GL_UNSIGNED_SHORT) && !d.uv.norm) uvScale = 1.f / 512.f;   // the game divides by 512.0
            if (d.tex != curTex) { glBindTexture(GL_TEXTURE_2D, d.tex); curTex = d.tex; }
            alpha = d.alphaRef;
        }
        else if (uvOn) { glDisableVertexAttribArray(1); uvOn = false; }
        glUniform1f(G.rp.uAlpha, alpha);
        glUniform1f(G.rp.uUvScale, uvScale);
        glUniformMatrix4fv(G.rp.uObj, 1, GL_FALSE, d.obj);
        glDrawElements(d.mode, d.count, GL_UNSIGNED_SHORT, (const void*)d.off);
        ++drawn; if (!fresh) ++fromCache;
        if ((drawn & 255) == 0) { if (glGetError() != GL_NO_ERROR) ++errs; }
    }
    for (CEntry* ep : G.list) if (ep->dead && !ep->temp) G.cache.erase(ep->key);      // after the loop: the list holds pointers into the map
    G.list.clear(); G.temp.clear();
    G.stats.cacheKept = fromCache; G.stats.cacheSize = (int)G.cache.size();
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    while (glGetError() != GL_NO_ERROR) ++errs;
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
}

void Shadow_SetAlphaTestState(const int* func, const float* ref) { G.atFunc = func; G.atRef = ref; }
void Shadow_SetBufferTracking(bool on) { G.bufTracking = on; }
void Shadow_OnBufferWrite(GLenum target)
{
    GLenum q = target == GL_ARRAY_BUFFER ? GL_ARRAY_BUFFER_BINDING : (target == GL_ELEMENT_ARRAY_BUFFER ? GL_ELEMENT_ARRAY_BUFFER_BINDING : 0);
    if (!q) return;
    GLint id = 0; glGetIntegerv(q, &id);
    if (id > 0) BufTouch((GLuint)id);
}
void Shadow_OnBufferDelete(GLsizei n, const GLuint* ids)
{
    if (!ids || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) BufTouch(ids[i]);
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
    if (!G.capturing || (int)G.draws.size() >= kMaxDraws) return;
    if ((mode != 0 && mode != 1) || count < 3) { ++G.stats.rejected; ++G.stats.rejMode; return; }   // 0 = triangle list, 1 = strip (game's own primitive ids)
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (!prog) return;
    PInfo& pi = GetProg((GLuint)prog);
    if (pi.obj < 0 || pi.bones >= 0 || pi.aPos < 0) { ++G.stats.rejected; ++G.stats.rejNoShaderInfo; return; }        // 2D / HUD / skinned: not a shadow caster
    GLboolean dm = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
    if (!dm) { ++G.stats.rejected; ++G.stats.rejNoDepthWrite; return; }                                               // sky dome etc. do not write depth
    // 1.4.6: alpha test like the game (RQ_Command_rqSetAlphaTest state or a discarding shader). Tree leaves are blended + alpha-tested:
    // 1.4.5 rejected every blended draw, so trees only cast their trunk / a box. Now they cast their leaves.
    bool at = false; float ref = 0.f;
    if (G.atFunc && G.atRef) { const int f = *G.atFunc; const float r = *G.atRef; if (f >= GL_GREATER && f <= GL_GREATER + 2 && r >= 0.f && r < 1.f) { at = true; ref = r; } }
    const bool blend = glIsEnabled(GL_BLEND) != GL_FALSE;
    float alphaRef = 0.f;
    if (blend)
    {
        if (!(at || pi.discard || (!G.atFunc && !pi.srcKnown))) { ++G.stats.rejected; ++G.stats.rejBlend; return; }     // translucent: glass, water, effects
        alphaRef = fmaxf(at ? ref : 0.f, 0.5f);
    }
    else if (at && ref > 0.004f) alphaRef = fmaxf(ref, 0.01f);
    else if (pi.discard) alphaRef = 0.5f;
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
    if (!fi->haveCam && fi->haveVP)
    {   // no CameraPosition uniform seen yet: camera = inverse of the (rigid) view matrix
        const float* v = fi->view;
        for (int i = 0; i < 3; ++i) fi->cam[i] = -(v[i * 4 + 0] * v[12] + v[i * 4 + 1] * v[13] + v[i * 4 + 2] * v[14]);
        fi->haveCam = isfinite(fi->cam[0]) && isfinite(fi->cam[1]) && isfinite(fi->cam[2]);
    }

    Draw d;
    d.fbo = fb;
    d.ibo = (GLuint)ibo; d.mode = mode == 1 ? GL_TRIANGLE_STRIP : GL_TRIANGLES; d.count = count; d.off = off;
    d.pos = it->second.pos; d.uv = it->second.uv;
    glGetUniformfv((GLuint)prog, pi.obj, d.obj);
    if (alphaRef > 0.f)
    {   // diffuse texture (unit 0) for alpha-tested foliage / fences; the active unit is restored so the game's own tracker stays valid
        if (d.uv.size <= 0 || !d.uv.vbo) { if (blend) { ++G.stats.rejected; ++G.stats.rejVertexFormat; return; } alphaRef = 0.f; }
        else
        {
            GLint prev = 0, t = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prev);
            if (prev != GL_TEXTURE0) glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
            if (prev != GL_TEXTURE0) glActiveTexture((GLenum)prev);
            d.tex = (GLuint)t;
            if (!d.tex) { if (blend) { ++G.stats.rejected; ++G.stats.rejVertexFormat; return; } alphaRef = 0.f; }
        }
    }
    d.alphaRef = alphaRef;
    d.vboGen = BufGen(d.pos.vbo); d.iboGen = BufGen(d.ibo); d.uvGen = d.uv.vbo ? BufGen(d.uv.vbo) : 0;
    d.cacheable = !BufHot(d.pos.vbo) && !BufHot(d.ibo);                 // dynamic / streaming buffers are never remembered
    G.draws.push_back(d);
}

void Shadow_EndFrame()
{
    if (G.capturing)
    {
        G.capturing = false;
        if (G.enabled) { Commit(); Replay(); }
        G.draws.clear(); G.vcache.clear(); G.fbos.clear(); G.list.clear(); G.temp.clear();
    }
    ++G.frame;
    if (!G.enabled) { if (G.ready && G.s.shadows == false) { /* keep texture, strength goes to 0 via Shadow_Uniforms */ } return; }
    int every = G.s.shadowEvery * (G.level >= 2 ? 2 : 1);
    if (every < 1) every = 1;
    G.capturing = (G.frame % (uint32_t)every) == 0;
    if (G.capturing) { G.draws.clear(); G.vcache.clear(); G.fbos.clear(); G.haveCam = G.haveDir = false; G.stats.rejected = 0; G.stats.rejBlend = G.stats.rejNoDepthWrite = G.stats.rejNoIndexBuffer = G.stats.rejVertexFormat = G.stats.rejNoShaderInfo = G.stats.rejMode = G.stats.rejDistance = G.stats.rejStale = 0; }
}

void Shadow_Uniforms(float lightVP[16], float params[4])
{
    if (!G.ready || !G.enabled) { memset(lightVP, 0, 16 * sizeof(float)); memset(params, 0, 4 * sizeof(float)); return; }
    memcpy(lightVP, G.lvp, 16 * sizeof(float));
    params[0] = G.s.shadowStrength * G.fade * ((G.s.shadowTimecyc && G.tcValid) ? G.tcMul : 1.f);
    params[1] = G.biasNorm; params[2] = G.texel; params[3] = 0.f;
}

GLuint Shadow_Texture() { return G.colorTex; }
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
    bool failed = G.failed; PSSettings s = G.s; const int* af = G.atFunc; const float* ar = G.atRef; bool bt = G.bufTracking;
    G = ShadowState(); G.failed = failed; G.s = s; G.atFunc = af; G.atRef = ar; G.bufTracking = bt;
    g_buf.clear(); g_bufBig.clear();
    Note("context lost - rebuilding");
}

void Shadow_Shutdown()
{
    if (G.rp.id) glDeleteProgram(G.rp.id);
    if (G.fbo) glDeleteFramebuffers(1, &G.fbo);
    if (G.colorTex) glDeleteTextures(1, &G.colorTex);
    if (G.depthRb) glDeleteRenderbuffers(1, &G.depthRb);
    G = ShadowState();
}
