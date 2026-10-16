#include "ps_scene.h"
#include "ps_log.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <vector>

// GLES 3.0 enums (the module only needs the ES2 entry points)
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_RENDERBUFFER_SAMPLES
#define GL_RENDERBUFFER_SAMPLES 0x8CAB
#endif

namespace {

// ---------------------------------------------------------------- render queue (GTA SA 2.10 arm64 layout, as used by libZyZyGFX64)
// RenderQueue: +0x000 command table (50 x void(*)(char**)), +0x3E8 bool threaded, +0x3E9 bool use mutex, +0x3F0 mutex,
//              +0x400 buffer end, +0x418 committed end (read by the render thread, atomic), +0x420 write pointer, +0x428 last command id.
// A command in the stream = int32 id + payload; the handler gets char** pointing past the id and advances it over its payload.
const int      kSlots = 50;
const uint64_t kMagic = 0x3358464750474650ull;         // "PFGPGFX3": marks OUR commands in the rqDebugMarker slot
const size_t   kCmdSize = 4 + 8 + 8;                     // id + magic + payload

typedef void (*CmdFn)(char**);
void**  g_rqVar = nullptr;                               // &renderQueue (a RenderQueue*)
void   (*g_mutexObtain)(void*) = nullptr;
void   (*g_mutexRelease)(void*) = nullptr;
void   (*g_process)(void*) = nullptr;
void*   g_dbgMarker = nullptr;
CmdFn   g_prev = nullptr;                                // what was in the slot before (rqDebugMarker or another mod's handler)
bool    g_regTried = false, g_wanted = false;
PSSceneEventFn g_handler = nullptr;
PSSceneStats S;

void Note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Note(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(S.note, sizeof(S.note), fmt, ap); va_end(ap);
    PS_Log("scene: %s", S.note);
}

void OurCommand(char** pp)
{
    uint64_t m = 0; memcpy(&m, *pp, 8);
    if (m != kMagic) { if (g_prev) g_prev(pp); return; }
    *pp += 8;
    uint64_t pay = 0; memcpy(&pay, *pp, 8); *pp += 8;
    const int ev = (int)(pay & 0xff);
    if (ev >= 0 && ev < 3) ++S.ran[ev];
    if (g_handler) g_handler(ev);
}

bool LayoutOk(char* rq)
{
    const uint8_t thr = (uint8_t)rq[0x3E8], mtx = (uint8_t)rq[0x3E9];
    char* wp = *(char**)(rq + 0x420); char* end = *(char**)(rq + 0x400); char* com = *(char**)(rq + 0x418);
    if (thr > 1 || mtx > 1 || !wp || !end || end < wp || (size_t)(end - wp) > (256u << 20) || !com || com > wp) return false;
    if (mtx && !*(void**)(rq + 0x3F0)) return false;
    return true;
}

bool Register()
{
    if (S.rqReady) return true;
    if (g_regTried || !S.rqSymbols || !g_rqVar) return false;
    char* rq = (char*)*g_rqVar;
    if (!rq) return false;                                  // created a bit later by the game: try again next frame
    g_regTried = true;
    if (!LayoutOk(rq)) { S.rqBadLayout = true; Note("render queue layout not recognised (not GTA SA 2.10 arm64?) - deferred shadows off"); return false; }
    void** table = (void**)rq;
    int slot = -1;
    for (int i = 0; i < kSlots; ++i) if (table[i] == g_dbgMarker) { slot = i; break; }
    if (slot < 0 && table[47]) { slot = 47; S.chained = true; }  // another mod (ZyZyGFX ...) already took the marker slot: chain to it
    if (slot < 0) { Note("rqDebugMarker slot not found - deferred shadows off"); return false; }
    g_prev = (CmdFn)table[slot];
    __atomic_store_n(&table[slot], (void*)&OurCommand, __ATOMIC_SEQ_CST);
    S.rqSlot = slot; S.rqReady = true; S.threaded = rq[0x3E8] != 0;
    PS_Log("scene: render-queue events on (command slot %d%s, queue %s)", slot, S.chained ? ", chained to another mod's handler" : "", S.threaded ? "threaded" : "not threaded");
    return true;
}

// ---------------------------------------------------------------- depth upgrade
struct Target { GLint fbo = 0; GLuint tex = 0, origRb = 0; int w = 0, h = 0, depth = 0, stencil = 0; bool foreign = false, ok = false; };
std::vector<Target> g_t;
int  g_sw = 0, g_sh = 0;
int  g_logSwaps = 0;

Target* Find(GLint fbo) { for (Target& t : g_t) if (t.fbo == fbo) return &t; return nullptr; }

GLuint MakeDepthTex(int w, int h, bool stencil)
{
    GLint prev = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    GLuint t = 0; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    while (glGetError() != GL_NO_ERROR) {}
    if (stencil) glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    else         glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const bool err = glGetError() != GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    if (err) { glDeleteTextures(1, &t); return 0; }
    return t;
}

void AttachTex(GLuint tex, bool stencil)
{
    if (stencil) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
    else         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
}
void AttachRb(GLuint rb, bool stencil)
{
    glFramebufferTexture2D(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
    if (stencil) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
}

// clear the fresh depth (the renderbuffer's content is gone) without changing any state the game tracks
void ClearDepth(bool stencil)
{
    GLboolean dm = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
    GLint sm = 0; glGetIntegerv(GL_STENCIL_WRITEMASK, &sm);
    GLfloat cd = 1.f; glGetFloatv(GL_DEPTH_CLEAR_VALUE, &cd);
    GLint cs = 0; glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &cs);
    const GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); glStencilMask(0xff); glClearDepthf(1.f); glClearStencil(0); glDisable(GL_SCISSOR_TEST);
    glClear(GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0));
    if (sc) glEnable(GL_SCISSOR_TEST);
    glDepthMask(dm); glStencilMask((GLuint)sm); glClearDepthf(cd); glClearStencil(cs);
}

bool BigEnough(int w, int h)
{
    if (w < 64 || h < 64) return false;
    if (g_sw <= 0 || g_sh <= 0) return w >= 256 && h >= 144;
    const int sw = g_sw, sh = g_sh;
    return (w * 2 >= sw && h * 2 >= sh) || (w * 2 >= sh && h * 2 >= sw);     // the scene can be rendered below screen size
}

void Upgrade(GLint fb)
{
    GLint type = 0, name = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type == GL_NONE) { while (glGetError() != GL_NO_ERROR) {} return; }
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    Target* t = Find(fb);
    if (type == GL_TEXTURE)
    {
        if (t && t->ok && (GLuint)name == t->tex) return;                     // ours, still attached
        if (!t) { if (g_t.size() >= 8) return; g_t.emplace_back(); t = &g_t.back(); t->fbo = fb; }
        // another mod already gives this target a depth TEXTURE: read that one (its size comes from the viewport later)
        t->tex = (GLuint)name; t->foreign = true; t->ok = true; t->w = t->h = 0;
        if (!S.foreignDepth) { S.foreignDepth = true; PS_Log("scene: FBO %d already has a depth texture %d from another mod - using it as it is", fb, name); }
        return;
    }
    if (type != GL_RENDERBUFFER || !name) return;
    // the game's depth renderbuffer: size / bits / samples
    GLint prevRb = 0; glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRb);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)name);
    GLint w = 0, h = 0, db = 0, sb = 0, ms = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &db);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &sb);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &ms);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prevRb);
    while (glGetError() != GL_NO_ERROR) {}
    if (!BigEnough(w, h)) return;                                               // ped shadows, reflections, small helpers
    if (ms > 1) { static bool once = false; if (!once) { once = true; Note("scene target %d is multisampled (MSAA x%d) - no readable depth, deferred shadows off", fb, ms); } return; }
    const bool stencil = sb > 0;
    // a texture made for the same renderbuffer (other targets share the scene depth, e.g. the game's own effects pass)
    GLuint tex = 0;
    for (Target& o : g_t) if (o.ok && !o.foreign && o.origRb == (GLuint)name && o.w == w && o.h == h) { tex = o.tex; break; }
    bool fresh = false;
    if (!tex)
    {
        if (t && t->tex && !t->foreign && t->w == w && t->h == h && t->stencil == (stencil ? 1 : 0)) tex = t->tex;   // the game re-created its target: same size
        else { tex = MakeDepthTex(w, h, stencil); fresh = true; }
    }
    if (!tex) { ++S.failures; Note("could not create a %dx%d depth texture - deferred shadows off", w, h); S.disabled = S.failures >= 3; return; }
    AttachTex(tex, stencil);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
    {
        AttachRb((GLuint)name, stencil);
        if (fresh) glDeleteTextures(1, &tex);
        while (glGetError() != GL_NO_ERROR) {}
        ++S.failures; S.disabled = S.failures >= 3;
        Note("FBO %d incomplete with our depth texture (0x%x) - the game's depth is restored%s", fb, st, S.disabled ? ", deferred shadows off" : "");
        return;
    }
    ClearDepth(stencil);
    while (glGetError() != GL_NO_ERROR) {}
    if (!t) { if (g_t.size() >= 8) { AttachRb((GLuint)name, stencil); return; } g_t.emplace_back(); t = &g_t.back(); t->fbo = fb; }
    t->tex = tex; t->origRb = (GLuint)name; t->w = w; t->h = h; t->depth = db; t->stencil = stencil ? 1 : 0; t->ok = true; t->foreign = false;
    ++S.swaps; S.targets = (int)g_t.size();
    if (g_logSwaps++ < 6)
        PS_Log("scene: FBO %d %dx%d - game depth renderbuffer %d (%d-bit depth, %d-bit stencil) -> our depth texture %u", fb, w, h, name, db, sb, tex);
    else if (g_logSwaps == 7) PS_Log("scene: depth re-attached more than 6 times (the game keeps re-creating its target) - further messages muted");
}

} // namespace

bool Scene_Init(PSSymLookupFn h)
{
    if (!h) return false;
    g_rqVar        = (void**)h("renderQueue");
    g_mutexObtain  = (void (*)(void*))h("_Z14OS_MutexObtainPv");
    g_mutexRelease = (void (*)(void*))h("_Z15OS_MutexReleasePv");
    g_dbgMarker    = h("_Z24RQ_Command_rqDebugMarkerRPc");
    g_process      = (void (*)(void*))h("_ZN11RenderQueue7ProcessEv");
    S.rqSymbols = g_rqVar && g_mutexObtain && g_mutexRelease && g_dbgMarker && g_process;
    if (!S.rqSymbols) Note("render-queue symbols missing (renderQueue %d, mutex %d/%d, rqDebugMarker %d, Process %d) - deferred shadows off",
                           g_rqVar != nullptr, g_mutexObtain != nullptr, g_mutexRelease != nullptr, g_dbgMarker != nullptr, g_process != nullptr);
    return S.rqSymbols;
}

void Scene_SetHandler(PSSceneEventFn fn) { g_handler = fn; }
void Scene_SetWanted(bool on) { g_wanted = on; }
bool Scene_Wanted() { return g_wanted && !S.disabled; }
void Scene_SetScreenSize(int w, int h) { g_sw = w; g_sh = h; }

void Scene_PostEvent(int ev)
{
    if (!g_wanted || S.disabled || ev < 0 || ev > 2) return;
    if (!Register()) return;
    char* rq = (char*)*g_rqVar;
    if (!rq) return;
    char* wp = *(char**)(rq + 0x420); char* end = *(char**)(rq + 0x400);
    if (!wp || !end || end < wp || (size_t)(end - wp) < kCmdSize + 64) { ++S.skippedFull; return; }   // never write past the buffer
    const int32_t id = S.rqSlot; const uint64_t pay = (uint64_t)ev;
    *(int32_t*)(rq + 0x428) = id;
    memcpy(wp, &id, 4); memcpy(wp + 4, &kMagic, 8); memcpy(wp + 12, &pay, 8);
    char* np = wp + kCmdSize;
    *(char**)(rq + 0x420) = np;
    const bool mtx = rq[0x3E9] != 0;
    void* m = mtx ? *(void**)(rq + 0x3F0) : nullptr;
    if (m) g_mutexObtain(m);
    int64_t* com = (int64_t*)(rq + 0x418);
    __atomic_fetch_add(com, (int64_t)(np - *(char**)(rq + 0x418)), __ATOMIC_SEQ_CST);   // committed end = write pointer (what the game's RQ macros do)
    if (m) g_mutexRelease(m);
    ++S.posted[ev];
    if (!rq[0x3E8]) g_process(rq);                          // not threaded: the game runs the queue right away, so do we
}

void Scene_OnTargetSelected()
{
    if (!g_wanted || S.disabled) return;
    GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    if (!fb) return;
    Upgrade(fb);
    const Target* t = Find(fb);
    if (t && t->ok && BigEnough(t->w ? t->w : g_sw, t->h ? t->h : g_sh)) { S.sceneFbo = fb; S.w = t->w; S.h = t->h; S.depthTex = t->tex; S.depthBits = t->depth; S.stencilBits = t->stencil ? 8 : 0; }
}

bool Scene_DepthOf(GLint fbo, GLuint& tex, int& w, int& h)
{
    const Target* t = Find(fbo);
    if (!t || !t->ok || !t->tex) return false;
    tex = t->tex; w = t->w; h = t->h;
    return true;
}

void Scene_ContextLost()
{
    g_t.clear();                                            // the textures died with the old context
    S.sceneFbo = 0; S.depthTex = 0; S.w = S.h = 0; S.targets = 0;
}

const PSSceneStats& Scene_Stats() { return S; }

void Scene_RunEventForTest(int ev)
{
    if (ev >= 0 && ev < 3) ++S.ran[ev];
    if (g_handler) g_handler(ev);
}
