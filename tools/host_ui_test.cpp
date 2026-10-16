// Host harness: renders game frame + FX + the ImGui menu to a PNG-able raw RGBA file
// usage: host_ui_test in.rgb W H out.rgba tab(-1 = closed) [scroll]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../src/ps_fx.h"
#include "../src/ps_glstate.h"
#include "../src/ps_ui.h"
void UI_TestOpen(bool o, int tab);
int main(int argc, char** argv)
{
    int W = atoi(argv[2]), H = atoi(argv[3]); int tab = atoi(argv[5]);
    std::vector<unsigned char> in(W * H * 3);
    FILE* f = fopen(argv[1], "rb"); if (!f || fread(in.data(), 1, in.size(), f) != in.size()) return 2; fclose(f);
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
    PSSettings st; PS_ApplyQuality(st, 2); PSUIStatus us; us.fxStatus = "host test (llvmpipe)"; us.gameInfo = "GTA SA 2.10 (64-bit)"; us.patchedShaders = 37; us.sunTracked = true; us.fx = &FX_Stats(); static PSShadowStats ss; ss.captured = 842; ss.replayed = 611; ss.rejected = 1290; ss.resolution = 1024; ss.sunElevation = 0.71f; ss.fade = 1.f; ss.cpuMs = 3.4f; ss.ready = true; snprintf(ss.note, sizeof(ss.note), "ok"); us.shadow = &ss;
    UI_SetIniPath(""); UI_TestOpen(tab >= 0, tab);
    PSFrameInfo fi; fi.w = W; fi.h = H; fi.time = 3.f; fi.dt = 0.016f; fi.sunX = 0.8f; fi.sunY = 0.75f; fi.sunVis = 1.f;
    for (int i = 0; i < 6; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H); glUseProgram(p); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri); glDrawArrays(GL_TRIANGLES, 0, 3);
        PSGLState gs; gs.Save();
        PS_UpdateLive(st, 0.f); FX_Render(st, fi); st.shadowDebug = false;
        UI_Render(st, W, H, 0.016f, us);
        gs.Restore();
    }
    fprintf(stderr, "glerr=0x%x\n", glGetError());
    std::vector<unsigned char> out(W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    f = fopen(argv[4], "wb"); fwrite(out.data(), 1, out.size(), f); fclose(f);
    return 0;
}
