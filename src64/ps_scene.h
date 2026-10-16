// Proper GFX Mobile 1.4.3 (64-bit) - the game's scene target + render-queue events (deferred shadows).
//
// Ported from how libZyZyGFX64 does it (GLSL-Samples-source-mod: src/by_module/rq.c, scene.c, frame.c):
//  * Render-queue events: GTA SA 2.10 records every GL command into RenderQueue and replays it on the render thread. A hook on
//    RenderScene() (game thread) appends one of OUR commands before and after it. The command id is the slot of rqDebugMarker in
//    the queue's command table (taken over, any other marker is passed on), the payload starts with a magic number. So the event
//    runs on the render thread exactly between the game's own commands: event 2 = "the 3D world is in the scene target, post
//    effects and HUD are not drawn yet" - the moment the shadow mask is multiplied in.
//  * Scene depth: the game renders the world into its own framebuffer with a depth RENDERBUFFER (not readable). After every
//    rqTargetSelect the bound framebuffer is checked; a big target (>= half the screen) gets OUR depth texture of the same size
//    (24-bit, + 8-bit stencil if the game had stencil). Other targets sharing that renderbuffer get the texture too.
#pragma once
#include <GLES2/gl2.h>
#include <stdint.h>

enum PSSceneEvent { PS_EV_BEFORE_SCENE = 1, PS_EV_AFTER_SCENE = 2 };
typedef void (*PSSceneEventFn)(int ev);

struct PSSceneStats
{
    bool  rqSymbols = false, rqReady = false, rqBadLayout = false, chained = false, threaded = false;
    int   rqSlot = -1;
    uint32_t posted[3] = {0, 0, 0}, ran[3] = {0, 0, 0}, skippedFull = 0;
    // depth upgrade
    int   sceneFbo = 0, w = 0, h = 0, depthBits = 0, stencilBits = 0, swaps = 0, failures = 0, targets = 0;
    GLuint depthTex = 0;
    bool  foreignDepth = false;          // another mod already put a depth texture on the scene target: used as it is
    bool  disabled = false;
    char  note[160] = "";
};

bool  Scene_Init(void* hGame);                  // game thread at load: resolves renderQueue, OS_Mutex*, rqDebugMarker, RenderQueue::Process
void  Scene_SetHandler(PSSceneEventFn fn);      // called on the render thread for every event
void  Scene_SetWanted(bool on);                 // depth upgrade + events only while deferred shadows are on
bool  Scene_Wanted();
void  Scene_PostEvent(int ev);                  // game thread (RenderScene hook)
void  Scene_SetScreenSize(int w, int h);        // EGL surface size (render thread, every frame)
void  Scene_OnTargetSelected();                 // render thread, right after the game's rqTargetSelect
bool  Scene_DepthOf(GLint fbo, GLuint& tex, int& w, int& h);   // our (or a foreign) depth texture on that framebuffer
void  Scene_ContextLost();
const PSSceneStats& Scene_Stats();

// host tests: run an event as if the render queue delivered it
void  Scene_RunEventForTest(int ev);
