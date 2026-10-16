#include "ps_world.h"
#include <mutex>
#include "ps_log.h"
#include "ps_shadow.h"
#include <GLES2/gl2.h>
#include <atomic>
#include <string.h>
#include <unordered_map>

namespace {
std::atomic<int>  g_patched{0};
std::atomic<int>  g_shadowRecv{0};
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_cel{false};
std::atomic<bool> g_shadowCompile{false};
GLuint g_lastProg = 0; unsigned g_lastGen = 0;

// uniform block mirrored on the GL thread. Values are stored as DELTAS from vanilla, so an
// un-initialised (all zero) uniform reproduces the original game look exactly.
float g_A[4] = { 0, 0, 0, 0 };
float g_B[4] = { 0, 0, 0, 0 };
float g_LVP[16] = {0}; float g_SH[4] = {0, 0, 0, 0};     // shadow receiver uniforms (zero = shadows off)
unsigned g_gen = 1, g_frame = 1, g_bindFrame = 0;

struct ProgInfo { GLint locA = -1, locB = -1, locLVP = -1, locSh = -1, locMap = -1; unsigned gen = 0; };
std::unordered_map<GLuint, ProgInfo> g_progs;

bool Replace(std::string& s, const char* from, const std::string& to)
{
    size_t p = s.find(from);
    if (p == std::string::npos) return false;
    s.replace(p, strlen(from), to);
    return true;
}

// ---- shadow receiver code (fragment side). Everything is wrapped in GL_FRAGMENT_PRECISION_HIGH: the 24-bit packed depth needs highp ----
const char* kShadowDecl =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
"varying highp vec4 Out_ShadowPos;\n"
"uniform highp vec4 PS_Shadow;\n"
"uniform sampler2D PS_ShadowMap;\n"
"highp float ps_unpack(highp vec3 c) { return dot(c, vec3(1.0, 1.0/255.0, 1.0/65025.0)); }\n"
"highp float ps_tap(highp vec2 uv, highp float d) { return step(d, ps_unpack(texture2D(PS_ShadowMap, uv).rgb)); }\n"
"#endif\n";
const char* kShadowBlock =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
"if (PS_Shadow.x > 0.001) {\n"
"  highp vec3 ps_sp = Out_ShadowPos.xyz;\n"
"  highp vec2 ps_uv = ps_sp.xy * 0.5 + 0.5;\n"
"  highp float ps_d = ps_sp.z * 0.5 + 0.5 - PS_Shadow.y * (1.0 + (1.0 - Out_ShadowPos.w) * 1.5);\n"
"  highp float ps_lit = 1.0;\n"
"  if (ps_uv.x > 0.0 && ps_uv.x < 1.0 && ps_uv.y > 0.0 && ps_uv.y < 1.0 && ps_d < 1.0) {\n"
"    highp vec2 ps_o = vec2(PS_Shadow.z);\n"
"    ps_lit = (ps_tap(ps_uv + vec2(-ps_o.x, -ps_o.y), ps_d) + ps_tap(ps_uv + vec2(ps_o.x, -ps_o.y), ps_d)\n"
"            + ps_tap(ps_uv + vec2(-ps_o.x, ps_o.y), ps_d) + ps_tap(ps_uv + ps_o, ps_d)) * 0.25;\n"
"    highp float ps_e = max(abs(ps_sp.x), abs(ps_sp.y));\n"
"    ps_lit = mix(ps_lit, 1.0, smoothstep(0.82, 1.0, ps_e));\n"
"  }\n"
"  mediump float ps_amt = PS_Shadow.x * smoothstep(0.0, 0.35, Out_ShadowPos.w) * (1.0 - Out_FogAmt);\n"
"  fcolor.xyz *= 1.0 - ps_amt * (1.0 - ps_lit);\n"
"}\n"
"#endif\n";
} // namespace

static const char* const kVtxDecl   = "varying highp vec4 Out_ShadowPos;\nuniform highp mat4 PS_LightVP;\n";
static const char* const kVtxAssign = "\nOut_ShadowPos = vec4((PS_LightVP * WorldPos).xyz, 1.0);\n";
static const char* const kVtxNL     = "\nOut_ShadowPos.w = max(dot(-normalize(vec3(PS_LightVP[0][2], PS_LightVP[1][2], PS_LightVP[2][2])), normalize(WorldNormal)), 0.0);\n";

static bool EraseOnce(std::string& s, const char* what) { size_t p = s.find(what); if (p == std::string::npos) return false; s.erase(p, strlen(what)); return true; }

// Removes exactly the text World_Patch*Source inserted for the shadow receiver (used when a pair reaches the game by a path the registry does not know,
// e.g. another shader mod hooked BuildSource after us and rewrote the strings, but kept our text).
bool World_StripShadowPatch(std::string& pxl, std::string& vtx)
{
    bool any = false;
    any |= EraseOnce(vtx, kVtxNL); any |= EraseOnce(vtx, kVtxAssign); any |= EraseOnce(vtx, kVtxDecl);
    any |= EraseOnce(pxl, kShadowBlock); any |= EraseOnce(pxl, kShadowDecl);
    return any;
}

bool World_PatchVertexSource(unsigned flags, const char* src, std::string& out, bool withNL)
{
    out.assign(src);
    if (flags & (PS_FLAG_WATER | PS_FLAG_SPHERE)) return false;                  // water is translucent; the sphere pass renders the vehicle reflection map
    // v1.3: the receiver only needs the world-space position. Most GTA SA map geometry (buildings, roads, terrain) uses UNLIT prelit shaders with no
    // normals and no directional light - v1.2 required both and therefore skipped nearly everything, so almost nothing could receive a shadow.
    static const char* kWP = "vec4 WorldPos = ObjMatrix * vec4(Position,1.0);";
    size_t wp = out.find(kWP);
    if (wp == std::string::npos) return false;                                  // skinned variant (ObjMatrix * (vec4(Position,1.0) * BoneToLocal)) or unknown layout
    // skinned shaders: the game sizes their Bones[%d] uniform array from the GPU's uniform limit, so even one extra mat4 could break the link
    if (out.find("Bones") != std::string::npos || out.find("BoneToLocal") != std::string::npos) return false;
    if (out.find("gl_Position = vec4(ReflPos.xy") != std::string::npos) return false;   // sphere-map pass without the flag
    // GLES2 only guarantees 8 varying vectors and not every driver packs them: leave room for ours
    { int nv = 0; for (size_t p = out.find("varying "); p != std::string::npos; p = out.find("varying ", p + 8)) ++nv; if (nv > 6) return false; }

    // N.L against the SUN (read back from the light matrix: row 2 of PS_LightVP is the light's forward axis, the sun is the opposite way).
    // Only for the real-normal variant; camera-based normals (foliage) and unlit shaders get w = 1 (no N.L fade).
    static const char* kWN = "vec3 WorldNormal = (ObjMatrix * vec4(Normal,0.0)).xyz;";
    size_t wn = out.find(kWN);
    std::string nl;
    if (withNL && wn != std::string::npos && wn > wp) nl = kVtxNL;

    // insert from the back so the earlier offset stays valid
    if (!nl.empty()) out.insert(wn + strlen(kWN), nl);
    out.insert(wp + strlen(kWP), kVtxAssign);
    size_t m = out.find("void main");
    if (m == std::string::npos) return false;
    out.insert(m, kVtxDecl);
    return true;
}

bool World_PatchPixelSource(unsigned flags, const char* src, std::string& out, bool withCel, bool withShadow, bool* shadowApplied)
{
    if (shadowApplied) *shadowApplied = false;
    out.assign(src);
    size_t mainPos = out.find("void main");
    if (mainPos == std::string::npos) return false;
    bool any = false;
    const bool world = (flags & PS_FLAG_FOG) != 0;

    // The game writes its shaders on ONE line ("...fcolor *= Out_Color;fcolor.xyz = mix(...)"): the receiver block starts with #ifdef, and a
    // preprocessor directive must start a line - Mali rejects "...;#ifdef" (P0001 "#endif directive found outside if-section"). Hence the "\n".
    // 1) horizon fog: density + softness. Smoothstep blend instead of pow() -> a few MADs, no transcendental
    bool shadowIn = false;
    if (world && Replace(out, "fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);",
                         (withShadow ? std::string("\n") + kShadowBlock : std::string()) + "mediump float ps_f = Out_FogAmt; ps_f += (ps_f * ps_f * (3.0 - 2.0 * ps_f) - ps_f) * PS_A.y;\n"
                         "fcolor.xyz = mix(fcolor.xyz, FogColor, clamp(ps_f * (1.0 + PS_A.x), 0.0, 1.0));")) { any = true; shadowIn = withShadow; }
    // 2) specular strength
    if (Replace(out, "fcolor.xyz += Out_Spec;", "fcolor.xyz += Out_Spec * (1.0 + PS_A.z);")) any = true;
    // 3) vehicle / env-map reflection strength
    if (Replace(out, "fcolor.xyz = mix(fcolor.xyz,ReflTexture.xyz, EnvMapCoefficient);",
                "fcolor.xyz = mix(fcolor.xyz,ReflTexture.xyz, clamp(EnvMapCoefficient * (1.0 + PS_A.w), 0.0, 1.0));")) any = true;
    if (Replace(out, "fcolor.xyz = mix(fcolor.xyz, texture2D(EnvMap, Out_Tex1).xyz, EnvMapCoefficient);",
                "fcolor.xyz = mix(fcolor.xyz, texture2D(EnvMap, Out_Tex1).xyz, clamp(EnvMapCoefficient * (1.0 + PS_A.w), 0.0, 1.0));")) any = true;
    // 4) water detail contrast
    if (Replace(out, "fcolor *= vec4(Out_Color.xyz * waterDetail * 1.1, Out_Color.w);",
                "fcolor *= vec4(Out_Color.xyz * (1.0 + (waterDetail - 1.0) * (1.0 + PS_B.y)) * 1.1, Out_Color.w);")) any = true;
    // 5) wet world (+ optional cel) right before the final write. 3D / fogged shaders only: never touches HUD/2D.
    //    Wet is a single multiply; the cel branch is compiled in only when it was requested.
    if (world)
    {
        std::string tail = "fcolor.xyz *= (1.0 - PS_B.x * 0.28);\n";
        if (withCel)
            tail += "if (PS_B.z > 0.5) { float pl = dot(fcolor.xyz, vec3(0.299, 0.587, 0.114)); "
                    "float pq = floor(pl * PS_B.z + 0.5) / PS_B.z; fcolor.xyz *= (pq + 0.04) / (pl + 0.04); }\n";
        if (Replace(out, "gl_FragColor = fcolor;", tail + "gl_FragColor = fcolor;")) any = true;
    }

    if (!any) { out.assign(src); return false; }
    // declare the uniforms right before main() - valid regardless of what precedes it
    mainPos = out.find("void main");
    out.insert(mainPos, std::string("uniform mediump vec4 PS_A;\nuniform mediump vec4 PS_B;\n") + (shadowIn ? kShadowDecl : ""));
    if (shadowApplied) *shadowApplied = shadowIn;
    return true;
}

void World_SetEnabled(bool on) { g_enabled = on; }
void World_SetCompileCel(bool on) { g_cel = on; }
void World_SetCompileShadows(bool on) { g_shadowCompile = on; }
int  World_PatchedCount() { return g_patched; }
int  World_ShadowReceiverCount() { return g_shadowRecv; }


// ---------------------------------------------------------------- driver self-test
namespace {
struct BuildEntry { unsigned flags = 0; std::string origPxl, origVtx; bool shadow = false; };
std::mutex g_bmu; std::unordered_map<uint64_t, BuildEntry> g_build;
PSBuildStats g_bs; int g_shadowFail = 0, g_shadowOk = 0, g_failLogs = 0; bool g_glInfoLogged = false;

uint64_t HashStr(const char* s) { uint64_t h = 1469598103934665603ull; for (; *s; ++s) { h ^= (unsigned char)*s; h *= 1099511628211ull; } return h; }
uint64_t PairKey(const char* p, const char* v) { return HashStr(p) * 0x9E3779B97F4A7C15ull ^ HashStr(v); }

bool CompileOne(GLenum type, const char* src, GLuint& sh, char* err, size_t n)
{
    sh = glCreateShader(type); glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { GLsizei len = 0; glGetShaderInfoLog(sh, (GLsizei)n - 1, &len, err); err[len > 0 ? len : 0] = 0; }
    return ok != 0;
}

// compile + link a throw-away program: the same calls the game's ES2Shader::Build makes, minus its attribute bindings
bool TrialBuild(const char* pxl, const char* vtx, char* err, size_t n)
{
    err[0] = 0;
    GLuint vs = 0, fs = 0, pr = 0; bool ok = false;
    char log[500] = "";
    if (!CompileOne(GL_VERTEX_SHADER, vtx, vs, log, sizeof(log)))        snprintf(err, n, "VERTEX: %s", log);
    else if (!CompileOne(GL_FRAGMENT_SHADER, pxl, fs, log, sizeof(log))) snprintf(err, n, "FRAGMENT: %s", log);
    else
    {
        pr = glCreateProgram(); glAttachShader(pr, vs); glAttachShader(pr, fs); glLinkProgram(pr);
        GLint l = 0; glGetProgramiv(pr, GL_LINK_STATUS, &l);
        if (l) ok = true;
        else { GLsizei len = 0; glGetProgramInfoLog(pr, (GLsizei)sizeof(log) - 1, &len, log); log[len > 0 ? len : 0] = 0; snprintf(err, n, "LINK: %s", log); }
    }
    if (pr) { glDetachShader(pr, vs); glDetachShader(pr, fs); glDeleteProgram(pr); }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    while (glGetError() != GL_NO_ERROR) {}
    return ok;
}

void LogFailure(const char* stage, const BuildEntry& e, const char* err, const char* pxl, const char* vtx)
{
    PS_Log("shader self-test: %s rejected by the driver (flags 0x%x): %.200s", stage, e.flags, err);
    if (g_failLogs < 6)
    {   // the first few failures with the full patched sources: this is what is needed to fix the patch for a given GPU
        ++g_failLogs;
        PS_FileLog("---- shader self-test failure #%d: %s, flags 0x%x\nDRIVER: %s\n==== VERTEX ====\n%s\n==== FRAGMENT ====\n%s\n---- end ----", g_failLogs, stage, e.flags, err, vtx, pxl);
    }
}
}   // namespace

const PSBuildStats& World_BuildStats() { return g_bs; }

bool World_OnShaderBuild(const char* pxl, const char* vtx, std::string& outP, std::string& outV)
{
    if (!pxl || !vtx) return false;
    BuildEntry e; bool known = false;
    { std::lock_guard<std::mutex> lk(g_bmu); auto it = g_build.find(PairKey(pxl, vtx)); if (it != g_build.end()) { e = it->second; known = true; } }
    if (!known)
    {   // not registered by us: another mod rewrote the strings after our hook. If our shadow text is still in there it can still be tested and stripped.
        if (!strstr(vtx, "Out_ShadowPos") && !strstr(pxl, "Out_ShadowPos")) return false;
        char err2[600]; ++g_bs.tested;
        if (TrialBuild(pxl, vtx, err2, sizeof(err2))) { ++g_bs.passed; return false; }
        BuildEntry unk; LogFailure("patched shader (rewritten by another mod)", unk, err2, pxl, vtx);
        std::string p2 = pxl, v2 = vtx;
        if (World_StripShadowPatch(p2, v2) && TrialBuild(p2.c_str(), v2.c_str(), err2, sizeof(err2))) { outP = p2; outV = v2; ++g_bs.noShadow; return true; }
        return false;
    }
    if (!g_glInfoLogged) { g_glInfoLogged = true; const char* r = (const char*)glGetString(GL_RENDERER); const char* v = (const char*)glGetString(GL_VERSION);
                           PS_FileLog("== Proper GFX shader self-test ==\nGL_RENDERER: %s\nGL_VERSION: %s", r ? r : "?", v ? v : "?"); }
    ++g_bs.tested;
    char err[600];
    if (TrialBuild(pxl, vtx, err, sizeof(err))) { ++g_bs.passed; if (e.shadow) ++g_shadowOk; return false; }
    LogFailure("patched shader", e, err, pxl, vtx);
    if (e.shadow)
    {
        ++g_shadowFail;
        if (g_shadowFail >= 4 && g_shadowOk == 0 && g_shadowCompile)
        {   // the driver never accepts the receiver: stop spending time on it for the rest of the session
            g_shadowCompile = false; g_bs.shadowDisabled = 1;
            PS_Log("shader self-test: shadow receivers disabled for this session (driver rejects them)");
            PS_FileLog("shadow receivers disabled for this session: %d rejected, 0 accepted", g_shadowFail);
        }
        // B: same shadow lookup, simpler vertex part (no N.L)
        std::string v2;
        if (World_PatchVertexSource(e.flags, e.origVtx.c_str(), v2, false) && TrialBuild(pxl, v2.c_str(), err, sizeof(err))) { outP = pxl; outV = v2; ++g_bs.simplified; return true; }
    }
    // C: the pixel-side patches that are not shadows (cel / wet) on the game's own vertex shader
    std::string p3;
    if (World_PatchPixelSource(e.flags, e.origPxl.c_str(), p3, g_cel, false) && TrialBuild(p3.c_str(), e.origVtx.c_str(), err, sizeof(err))) { outP = p3; outV = e.origVtx; ++g_bs.noShadow; return true; }
    // D: the game's own shader, exactly as shipped
    outP = e.origPxl; outV = e.origVtx; ++g_bs.original;
    return true;
}

void World_OnBuildSource(unsigned flags, const char** pxl, const char** vtx)
{
    if (!g_enabled || !pxl || !*pxl) return;
    thread_local std::string pbuf, vbuf;
    // the shadow varying must exist in BOTH stages or the link fails (and the game aborts), so the vertex patch decides
    bool vOk = g_shadowCompile && vtx && *vtx && (flags & PS_FLAG_FOG) && World_PatchVertexSource(flags, *vtx, vbuf);
    bool applied = false;
    const std::string origP = *pxl, origV = (vtx && *vtx) ? *vtx : "";
    bool changed = false;
    if (World_PatchPixelSource(flags, *pxl, pbuf, g_cel, vOk, &applied)) { *pxl = pbuf.c_str(); ++g_patched; changed = true; }
    if (vOk) { *vtx = vbuf.c_str(); changed = true; }
    if (vOk && applied) ++g_shadowRecv;
    if (changed && vtx && *vtx)
    {   // remember the originals: the render thread test-compiles this exact pair before the game builds it (see World_OnShaderBuild)
        BuildEntry e; e.flags = flags; e.origPxl = origP; e.origVtx = origV; e.shadow = vOk && applied;
        std::lock_guard<std::mutex> lk(g_bmu);
        if (g_build.size() > 6000) g_build.clear();
        g_build[PairKey(*pxl, *vtx)] = std::move(e);
    }
}

void World_BeginFrame(const PSSettings& s, float wetRoads)
{
    float A[4] = { 0, 0, 0, 0 }, B[4] = { 0, 0, 0, 0 };
    if (s.enabled && s.worldPatch)
    {
        if (s.fogOn)     { A[0] = s.fogDensity.v - 1.f; A[1] = s.fogSoftness; }
        if (s.surfaceOn) { A[2] = s.specularMul - 1.f;  A[3] = s.reflectionMul - 1.f; }
        float w = wetRoads < 0.f ? 0.f : (wetRoads > 1.f ? 1.f : wetRoads);
        B[0] = s.wetWorld ? w * s.wetStrength : 0.f;
        B[1] = s.waterOn ? s.waterDetail - 1.f : 0.f;
        B[2] = s.celWorld ? s.celWorldLevels : 0.f;
    }
    float LVP[16], SH[4];
    Shadow_Uniforms(LVP, SH);
    if (!(s.enabled && s.worldPatch)) { memset(LVP, 0, sizeof(LVP)); memset(SH, 0, sizeof(SH)); }
    ++g_frame;
    if (memcmp(A, g_A, sizeof(A)) || memcmp(B, g_B, sizeof(B)) || memcmp(LVP, g_LVP, sizeof(LVP)) || memcmp(SH, g_SH, sizeof(SH)))
    { memcpy(g_A, A, sizeof(A)); memcpy(g_B, B, sizeof(B)); memcpy(g_LVP, LVP, sizeof(LVP)); memcpy(g_SH, SH, sizeof(SH)); ++g_gen; }
}

void World_ClearProgramCache() { g_progs.clear(); g_lastProg = 0; Shadow_ClearProgramCache(); }

void World_OnShaderSelected()
{
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog <= 0) return;
    if (g_SH[0] > 0.f && g_bindFrame != g_frame && Shadow_Texture())
    {   // unit 3 is never touched by the game (it only binds units 0-2); restore the active unit so its tracker stays valid
        GLint prevUnit = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, Shadow_Texture()); glActiveTexture((GLenum)prevUnit);
        g_bindFrame = g_frame;
    }
    if ((GLuint)prog == g_lastProg && g_lastGen == g_gen) return;      // same shader, nothing changed: skip the map lookup
    auto it = g_progs.find((GLuint)prog);
    if (it == g_progs.end())
    {
        ProgInfo pi;
        pi.locA = glGetUniformLocation(prog, "PS_A");
        pi.locB = glGetUniformLocation(prog, "PS_B");
        pi.locLVP = glGetUniformLocation(prog, "PS_LightVP");
        pi.locSh = glGetUniformLocation(prog, "PS_Shadow");
        pi.locMap = glGetUniformLocation(prog, "PS_ShadowMap");
        it = g_progs.emplace((GLuint)prog, pi).first;
    }
    ProgInfo& pi = it->second;
    g_lastProg = (GLuint)prog; g_lastGen = g_gen;
    if (pi.gen == g_gen) return;
    if (pi.locA >= 0) glUniform4fv(pi.locA, 1, g_A);
    if (pi.locB >= 0) glUniform4fv(pi.locB, 1, g_B);
    if (pi.locLVP >= 0) glUniformMatrix4fv(pi.locLVP, 1, GL_FALSE, g_LVP);
    if (pi.locSh >= 0) glUniform4fv(pi.locSh, 1, g_SH);
    if (pi.locMap >= 0) glUniform1i(pi.locMap, 3);
    pi.gen = g_gen;
}
