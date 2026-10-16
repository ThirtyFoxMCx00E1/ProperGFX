// Host-side test harness: runs the real PostFX pipeline on a software GLES2 context (Mesa llvmpipe)
// usage: host_fx_test in.rgb W H out.rgba [preset] [underwater]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <time.h>
#include "../src/ps_fx.h"
#include "../src/ps_glstate.h"

int main(int argc, char** argv)
{
    if (argc < 5) return 1;
    int W = atoi(argv[2]), H = atoi(argv[3]);
    int preset = argc > 5 ? atoi(argv[5]) : 0;
    float under = argc > 6 ? (float)atof(argv[6]) : 0.f;
    std::vector<unsigned char> in(W * H * 3);
    FILE* f = fopen(argv[1], "rb"); if (!f || fread(in.data(), 1, in.size(), f) != in.size()) { fprintf(stderr, "bad input\n"); return 2; } fclose(f);

    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint maj, min; eglInitialize(d, &maj, &min); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE };
    EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
    eglMakeCurrent(d, s, s, x);
    PSGLState::InitCaps();

    // "game frame": upload the picture and blit it with a trivial shader
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, in.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const char* vs = "attribute vec2 p; varying vec2 uv; void main(){uv=vec2(p.x*0.5+0.5,0.5-p.y*0.5); gl_Position=vec4(p,0.,1.);}";
    const char* fs = "precision mediump float; varying vec2 uv; uniform sampler2D t; void main(){gl_FragColor=texture2D(t,uv);}";
    GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, 0); glCompileShader(v);
    GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fsh, 1, &fs, 0); glCompileShader(fsh);
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, fsh); glBindAttribLocation(p, 0, "p"); glLinkProgram(p);
    float tri[6] = { -1, -1, 3, -1, -1, 3 };
    auto drawGameFrame = [&]() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H); glUseProgram(p); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    PSSettings st; PS_ApplyQuality(st, getenv("PS_Q") ? atoi(getenv("PS_Q")) : 2); PS_ApplyLook(st, preset);
    if (const char* ini = getenv("PS_INI")) PS_LoadSettings(st, ini);
    if (const char* off = getenv("PS_OFF")) {
        if (strstr(off, "all")) { st.autoExp=false; st.bloom=false; st.sunRays=false; st.lensFlare=false; st.fxaa=false; st.cas=false; st.vignette=false; st.dither=false; st.hdr=false; st.grading=false; }
        if (strstr(off, "auto")) st.autoExp=false;
        if (strstr(off, "bloom")) st.bloom=false;
        if (strstr(off, "sun")) st.sunRays=false;
        if (strstr(off, "flare")) st.lensFlare=false;
        if (strstr(off, "fxaa")) st.fxaa=false;
        if (strstr(off, "cas")) st.cas=false;
        if (strstr(off, "tm")) st.hdr=false;
        if (strstr(off, "dither")) st.dither=false;
    }
    PS_UpdateLive(st, getenv("PS_NIGHT") ? (float)atof(getenv("PS_NIGHT")) : 0.f);
    PSFrameInfo fi; fi.w = W; fi.h = H; fi.time = 3.0f; fi.dt = 0.016f; fi.underwater = under;
    fi.sunX = 0.75f; fi.sunY = 0.7f; fi.sunVis = 1.f;
    if (const char* lp = getenv("PS_LUT")) fprintf(stderr, "lut loaded=%d\n", FX_LoadLut(lp));

    if (const char* bn = getenv("PS_BENCH")) {
        int N = atoi(bn); double total = 0;
        for (int i = 0; i < N + 5; ++i) {
            drawGameFrame(); glFinish();
            struct timespec c0; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c0);
            PSGLState gs; gs.Save(); FX_Render(st, fi); gs.Restore(); glFinish();
            struct timespec c1; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c1);
            if (i >= 5) total += (c1.tv_sec - c0.tv_sec) * 1000.0 + (c1.tv_nsec - c0.tv_nsec) / 1e6;
        }
        printf("%.2f\n", total / N);
        return 0;
    }
    bool ok = false;
    for (int i = 0; i < 4; ++i) {                          // several frames so auto-exposure settles
        drawGameFrame();                                    // fresh "game" frame, like the real game does every frame
        PSGLState gs; gs.Save();
        ok = FX_Render(st, fi);
        gs.Restore();
    }
    fprintf(stderr, "FX_Render=%d status=%s glerr=0x%x passes=%d variants=%d fetches/px=%.1f\n", ok, FX_Status(), glGetError(), FX_Stats().passes, FX_Stats().variants, FX_Stats().fullResFetches);
    std::vector<unsigned char> out(W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    f = fopen(argv[4], "wb"); fwrite(out.data(), 1, out.size(), f); fclose(f);
    return ok ? 0 : 3;
}
