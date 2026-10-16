// Stress: random feature combinations (shader variant cache + LRU), governor behaviour, GL error freedom
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/ps_fx.h"
#include "../src/ps_glstate.h"
static int fails = 0;
#define CHECK(c, msg) do { bool ok_ = (c); printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) ++fails; } while (0)
int main()
{
    const int W = 320, H = 180;
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr); EGLint a, b; eglInitialize(d, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE }; EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE }; EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa); eglMakeCurrent(d, s, s, x);
    PSGLState::InitCaps();
    glClearColor(0.4f, 0.5f, 0.6f, 1); glClear(GL_COLOR_BUFFER_BIT);

    srand(1234);
    int okFrames = 0, errFrames = 0, maxVariants = 0;
    for (int i = 0; i < 60; ++i)
    {
        PSSettings st; st.adaptive = false;
        bool* flags[] = { &st.hdr, &st.autoExp, &st.bloom, &st.sunRays, &st.lensFlare, &st.fxaa, &st.cas, &st.grading, &st.dither, &st.vignette, &st.grain,
                          &st.chromatic, &st.tiltShift, &st.underwater, &st.cel, &st.outline, &st.vhs };
        for (bool* f : flags) *f = (rand() & 1);
        st.tonemap = rand() % 4; st.gradePreset = rand() % 7; st.gamma = (rand() & 1) ? 1.f : 1.2f;
        PS_UpdateLive(st, (rand() % 100) / 100.f);
        PSFrameInfo fi; fi.w = W; fi.h = H; fi.time = i * 0.3f; fi.dt = 0.016f; fi.underwater = (rand() & 1) ? 0.8f : 0.f; fi.sunX = 0.7f; fi.sunY = 0.6f; fi.sunVis = 1.f;
        PSGLState gs; gs.Save();
        bool ok = FX_Render(st, fi);
        GLenum e = glGetError();
        gs.Restore();
        if (ok) ++okFrames;
        if (e != GL_NO_ERROR) ++errFrames;
        if (FX_Stats().variants > maxVariants) maxVariants = FX_Stats().variants;
        if (!ok) { printf("  frame %d failed: %s\n", i, FX_Status()); break; }
    }
    CHECK(okFrames == 60, "60 random feature combinations all compile, link and render");
    CHECK(errFrames == 0, "no GL errors in any combination");
    CHECK(maxVariants <= 10 && maxVariants >= 2, "variant cache stays bounded (LRU)");
    printf("      variants cached: max %d\n", maxVariants);

    // adaptive governor: sustained slow frames -> level rises; sustained fast frames -> level falls back
    PSSettings g; g.adaptive = true; g.targetFps = 0; PS_UpdateLive(g, 0.f);
    PSFrameInfo fi; fi.w = W; fi.h = H;
    for (int i = 0; i < 400; ++i) { fi.dt = 0.050f; PSGLState gs; gs.Save(); FX_Render(g, fi); gs.Restore(); }
    int hi = FX_Stats().adaptiveLevel;
    CHECK(hi == 3, "governor steps down to level 3 when frames are slow (50 ms vs 33 ms budget)");
    for (int i = 0; i < 2500; ++i) { fi.dt = 0.010f; PSGLState gs; gs.Save(); FX_Render(g, fi); gs.Restore(); }
    CHECK(FX_Stats().adaptiveLevel < hi, "governor recovers when frames are fast again");
    printf("      level after recovery: %d\n", FX_Stats().adaptiveLevel);
    PSSettings off = g; off.adaptive = false; { PSGLState gs; gs.Save(); FX_Render(off, fi); gs.Restore(); }
    CHECK(FX_Stats().adaptiveLevel == 0, "adaptive quality off => always level 0");
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: variant/governor tests passed\n", fails);
    return fails;
}
