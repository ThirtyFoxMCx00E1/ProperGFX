// Mock "game" on a software GLES2 driver: real game shader lines (ObjMatrix / WorldPos / WorldNormal / DirLightDirection / fog),
// a house, a tree (alpha-tested foliage) and ground, drawn through the same sequence the hooks see.
// Runs capture -> replay -> receiver lookup, checks shadow geometry numerically and writes images for several times of day.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include "../src/ps_shadow.h"
#include "../src/ps_world.h"
#include "../src/ps_glstate.h"

static int fails = 0;
#define CHECK(c, msg) do { bool ok_ = (c); printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) ++fails; } while (0)

static const char* kGameVS =
"precision highp float;\n"
"attribute vec3 Position;\nattribute vec2 TexCoord0;\nattribute vec3 Normal;\n"
"uniform mat4 ProjMatrix;\nuniform mat4 ViewMatrix;\nuniform mat4 ObjMatrix;\n"
"uniform vec3 CameraPosition;\nuniform vec3 FogDistances;\n"
"uniform lowp vec3 AmbientLightColor;\nuniform lowp vec3 DirLightDiffuseColor;\nuniform vec3 DirLightDirection;\n"
"varying mediump vec2 Out_Tex0;\nvarying mediump float Out_FogAmt;\nvarying lowp vec4 Out_Color;\n"
"void main() {\n"
"vec4 WorldPos = ObjMatrix * vec4(Position,1.0);\n"
"vec4 ViewPos = ViewMatrix * WorldPos;\n"
"gl_Position = ProjMatrix * ViewPos;\n"
"vec3 WorldNormal = (ObjMatrix * vec4(Normal,0.0)).xyz;\n"
"Out_FogAmt = clamp((length(WorldPos.xyz - CameraPosition.xyz) - FogDistances.x) * FogDistances.z, 0.0, 0.90);\n"
"Out_Tex0 = TexCoord0;\n"
"vec3 Out_LightingColor = AmbientLightColor;\n"
"Out_LightingColor += max(dot(DirLightDirection, WorldNormal), 0.0) * DirLightDiffuseColor;\n"
"Out_Color = vec4(Out_LightingColor, 1.0);\n"
"}\n";
static const char* kGameFS =
"precision mediump float;\n"
"uniform sampler2D Diffuse;\nuniform lowp vec3 FogColor;\nuniform lowp float AlphaModulate;\n"
"varying mediump vec2 Out_Tex0;\nvarying mediump float Out_FogAmt;\nvarying lowp vec4 Out_Color;\n"
"void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0);\nfcolor = diffuseColor;\nfcolor *= Out_Color;\n"
"fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\ngl_FragColor = fcolor;\n/*ATBEGIN*/\nif (diffuseColor.a < 0.5) { discard; }\n/*ATEND*/\ngl_FragColor.a *= AlphaModulate;\n}\n";

static const char* kGameVSUnlit =
"precision highp float;\n"
"attribute vec3 Position;\nattribute vec2 TexCoord0;\n"
"uniform mat4 ProjMatrix;\nuniform mat4 ViewMatrix;\nuniform mat4 ObjMatrix;\n"
"uniform vec3 CameraPosition;\nuniform vec3 FogDistances;\n"
"varying mediump vec2 Out_Tex0;\nvarying mediump float Out_FogAmt;\nvarying lowp vec4 Out_Color;\n"
"void main() {\n"
"vec4 WorldPos = ObjMatrix * vec4(Position,1.0);\n"
"vec4 ViewPos = ViewMatrix * WorldPos;\n"
"gl_Position = ProjMatrix * ViewPos;\n"
"Out_FogAmt = clamp((length(WorldPos.xyz - CameraPosition.xyz) - FogDistances.x) * FogDistances.z, 0.0, 0.90);\n"
"Out_Tex0 = TexCoord0;\n"
"Out_Color = vec4(0.82, 0.80, 0.76, 1.0);\n"
"}\n";

struct V { float p[3], uv[2], n[3]; };
struct Mesh { GLuint vbo = 0, ibo = 0; int count = 0; GLuint tex = 0; float obj[16]; };

static void Identity(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.f; }
static void Translate(float* m, float x, float y, float z) { Identity(m); m[12] = x; m[13] = y; m[14] = z; }
static Mesh MakeMesh(const std::vector<V>& v, const std::vector<unsigned short>& i, GLuint tex)
{
    Mesh m; m.tex = tex; m.count = (int)i.size();
    glGenBuffers(1, &m.vbo); glBindBuffer(GL_ARRAY_BUFFER, m.vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, i.size() * 2, i.data(), GL_STATIC_DRAW);
    Identity(m.obj); return m;
}
static void Quad(std::vector<V>& v, std::vector<unsigned short>& idx, const float a[3], const float b[3], const float c[3], const float d[3], const float n[3])
{
    unsigned short base = (unsigned short)v.size();
    const float* P[4] = { a, b, c, d }; const float uv[4][2] = { {0,0},{1,0},{1,1},{0,1} };
    for (int k = 0; k < 4; ++k) { V x; memcpy(x.p, P[k], 12); x.uv[0] = uv[k][0]; x.uv[1] = uv[k][1]; memcpy(x.n, n, 12); v.push_back(x); }
    unsigned short t[6] = { 0, 1, 2, 0, 2, 3 }; for (int k = 0; k < 6; ++k) idx.push_back(base + t[k]);
}
static void Box(std::vector<V>& v, std::vector<unsigned short>& i, float x0, float y0, float z0, float x1, float y1, float z1)
{
    float A[3] = {x0,y0,z0}, B[3] = {x1,y0,z0}, C[3] = {x1,y1,z0}, D[3] = {x0,y1,z0}, E[3] = {x0,y0,z1}, F[3] = {x1,y0,z1}, G[3] = {x1,y1,z1}, H[3] = {x0,y1,z1};
    float nz[3] = {0,0,1}, nmy[3] = {0,-1,0}, ny[3] = {0,1,0}, nx[3] = {1,0,0}, nmx[3] = {-1,0,0};
    Quad(v, i, E, F, G, H, nz); Quad(v, i, A, B, F, E, nmy); Quad(v, i, C, D, H, G, ny); Quad(v, i, B, C, G, F, nx); Quad(v, i, D, A, E, H, nmx);
}
static void Perspective(float* m, float fovy, float asp, float n, float f)
{
    memset(m, 0, 64); float t = 1.f / tanf(fovy * 0.5f); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1.f; m[14] = 2.f * f * n / (n - f);
}
static void LookAt(float* m, const float e[3], const float c[3])
{
    float f[3] = { c[0]-e[0], c[1]-e[1], c[2]-e[2] }; float l = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); for (int i = 0; i < 3; ++i) f[i] /= l;
    float up[3] = {0,0,1};
    float s[3] = { f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] }; l = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]); for (int i = 0; i < 3; ++i) s[i] /= l;
    float u[3] = { s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0] };
    Identity(m);
    m[0]=s[0]; m[4]=s[1]; m[8]=s[2]; m[1]=u[0]; m[5]=u[1]; m[9]=u[2]; m[2]=-f[0]; m[6]=-f[1]; m[10]=-f[2];
    m[12] = -(s[0]*e[0]+s[1]*e[1]+s[2]*e[2]); m[13] = -(u[0]*e[0]+u[1]*e[1]+u[2]*e[2]); m[14] = (f[0]*e[0]+f[1]*e[1]+f[2]*e[2]);
}
static void Project(const float* P, const float* Vw, const float p[3], int W, int H, int* px, int* py)
{
    float w[4] = { p[0], p[1], p[2], 1.f }, v[4], c[4];
    for (int r = 0; r < 4; ++r) v[r] = Vw[0*4+r]*w[0] + Vw[1*4+r]*w[1] + Vw[2*4+r]*w[2] + Vw[3*4+r]*w[3];
    for (int r = 0; r < 4; ++r) c[r] = P[0*4+r]*v[0] + P[1*4+r]*v[1] + P[2*4+r]*v[2] + P[3*4+r]*v[3];
    *px = (int)((c[0] / c[3] * 0.5f + 0.5f) * W); *py = (int)((c[1] / c[3] * 0.5f + 0.5f) * H);   // GL origin: bottom-left
}

static GLuint BuildProgram(const std::string& vs, const std::string& fs, const char* tag)
{
    auto comp = [&](GLenum t, const std::string& s) { GLuint o = glCreateShader(t); const char* p = s.c_str(); glShaderSource(o, 1, &p, 0); glCompileShader(o);
        GLint ok = 0; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[1024]; glGetShaderInfoLog(o, 1024, 0, l); printf("  [%s] compile error: %s\n", tag, l); } return o; };
    GLuint v = comp(GL_VERTEX_SHADER, vs), f = comp(GL_FRAGMENT_SHADER, fs), p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "Position"); glBindAttribLocation(p, 1, "TexCoord0"); glBindAttribLocation(p, 2, "Normal");
    glLinkProgram(p); GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[1024]; glGetProgramInfoLog(p, 1024, 0, l); printf("  [%s] link error: %s\n", tag, l); return 0; }
    return p;
}

int main()
{
    const int W = 800, H = 450;
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr); EGLint a, b; eglInitialize(d, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE }; EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE }; EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa); eglMakeCurrent(d, s, s, x);
    PSGLState::InitCaps();

    // ---- the game's shaders, patched exactly like the BuildSource hook does it ----
    std::string pv, pf;
    bool vOk = World_PatchVertexSource(PS_FLAG_FOG | PS_FLAG_LIGHTING, kGameVS, pv);
    bool fOk = World_PatchPixelSource(PS_FLAG_FOG | PS_FLAG_LIGHTING, kGameFS, pf, false, vOk);
    CHECK(vOk && fOk, "vertex + pixel shader patches apply to the game's shader text");
    GLuint prog = BuildProgram(pv, pf, "patched");
    GLuint progPlain = BuildProgram(kGameVS, kGameFS, "vanilla");
    if (prog) printf("      DBG locations: PS_Shadow=%d PS_ShadowMap=%d PS_LightVP=%d PS_A=%d\n", glGetUniformLocation(prog, "PS_Shadow"), glGetUniformLocation(prog, "PS_ShadowMap"), glGetUniformLocation(prog, "PS_LightVP"), glGetUniformLocation(prog, "PS_A"));
    CHECK(prog && progPlain, "patched vertex+pixel pair compiles AND links on a GLES2 driver");
    std::string skinned = "uniform highp vec4 Bones[64];\nvec3 WorldNormal = vec3(0.0, 0.0, 0.0);\nvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\nuniform vec3 DirLightDirection;\nvoid main(){}";
    std::string tmp; CHECK(!World_PatchVertexSource(PS_FLAG_FOG, skinned.c_str(), tmp), "skinned shaders (Bones[]) are never patched");
    CHECK(!World_PatchVertexSource(PS_FLAG_FOG | PS_FLAG_WATER, kGameVS, tmp), "water shaders are never patched");
    if (!prog) return 1;

    // ---- textures ----
    GLuint texGround, texWall, texLeaf;
    unsigned char g[64 * 64 * 4], w[16 * 16 * 4], lf[64 * 64 * 4];
    for (int y = 0; y < 64; ++y) for (int xx = 0; xx < 64; ++xx) { int k = (((xx / 8) + (y / 8)) & 1); unsigned char v = k ? 150 : 135; unsigned char* o = &g[(y*64+xx)*4]; o[0] = v; o[1] = v; o[2] = v - 8; o[3] = 255; }
    for (int i = 0; i < 16 * 16; ++i) { w[i*4+0] = 170; w[i*4+1] = 105; w[i*4+2] = 70; w[i*4+3] = 255; }
    for (int y = 0; y < 64; ++y) for (int xx = 0; xx < 64; ++xx) { float dx = (xx - 31.5f) / 32.f, dy = (y - 31.5f) / 32.f; bool in = dx*dx + dy*dy < 0.85f; unsigned char* o = &lf[(y*64+xx)*4]; o[0] = 50; o[1] = 120; o[2] = 40; o[3] = in ? 255 : 0; }
    auto mkTex = [&](GLuint& t, int sz, unsigned char* px, bool mip) { glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); (void)mip; };
    mkTex(texGround, 64, g, false); mkTex(texWall, 16, w, false); mkTex(texLeaf, 64, lf, false);

    // ---- scene (metres, Z up like GTA SA) ----
    std::vector<Mesh> scene;
    { std::vector<V> v; std::vector<unsigned short> i; float a1[3] = {-45,-45,0}, b1[3] = {45,-45,0}, c1[3] = {45,45,0}, d1[3] = {-45,45,0}, nz[3] = {0,0,1}; Quad(v, i, a1, b1, c1, d1, nz);
      for (auto& q : v) { q.uv[0] *= 18.f; q.uv[1] *= 18.f; } scene.push_back(MakeMesh(v, i, texGround)); }                       // ground
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, -6, -4, 0, 6, 4, 5); Mesh m = MakeMesh(v, i, texWall); scene.push_back(m); }   // house body 12x8x5
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, -12, -3, 0, -9, 3, 3.5f); scene.push_back(MakeMesh(v, i, texWall)); }          // garage-ish block
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, 9.f, 6.f, 0, 9.5f, 6.5f, 4.f); Mesh m = MakeMesh(v, i, texWall); scene.push_back(m); }   // trunk
      { std::vector<V> v; std::vector<unsigned short> i; float nn[3] = {0,1,0};
        float a2[3] = {6.5f,6.25f,3.f}, b2[3] = {12.5f,6.25f,3.f}, c2[3] = {12.5f,6.25f,9.f}, d2[3] = {6.5f,6.25f,9.f}; Quad(v, i, a2, b2, c2, d2, nn);
        float a3[3] = {9.25f,3.25f,3.f}, b3[3] = {9.25f,9.25f,3.f}, c3[3] = {9.25f,9.25f,9.f}, d3[3] = {9.25f,3.25f,9.f}; float nx[3] = {1,0,0}; Quad(v, i, a3, b3, c3, d3, nx);
        scene.push_back(MakeMesh(v, i, texLeaf)); }                                                                                              // crossed foliage quads

    GLuint dummy; glGenTextures(1, &dummy);
    float proj[16], view[16];
    const float eye[3] = { 30.f, -26.f, 20.f }, look[3] = { 4.f, 3.f, 1.5f };
    Perspective(proj, 0.80f, (float)W / H, 0.5f, 400.f); LookAt(view, eye, look);

    int skipMesh = -1; bool otherFbo = false, turnAway = false; GLuint fbo2 = 0, rb2 = 0;
    float viewAway[16]; { const float lookAway[3] = { 30.f + 26.f, -26.f - 29.f, 18.f }; LookAt(viewAway, eye, lookAway); }   // the camera has turned around: the house is outside the view
    auto drawScene = [&](GLuint pr, const float sun[3], bool capture) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        glClearColor(0.55f, 0.7f, 0.95f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);
        for (const Mesh& m : scene)
        {
            if (capture && skipMesh >= 0 && &m == &scene[(size_t)skipMesh]) continue;        // simulate the game culling it this frame
            glUseProgram(pr);
            if (pr != progPlain) World_OnShaderSelected();                       // what the rqSelectShader hook does
            auto U = [&](const char* nme) { return glGetUniformLocation(pr, nme); };
            glUniformMatrix4fv(U("ProjMatrix"), 1, GL_FALSE, proj); glUniformMatrix4fv(U("ViewMatrix"), 1, GL_FALSE, (capture && turnAway) ? viewAway : view); glUniformMatrix4fv(U("ObjMatrix"), 1, GL_FALSE, m.obj);
            glUniform3f(U("CameraPosition"), eye[0], eye[1], eye[2]); glUniform3f(U("FogDistances"), 60.f, 0.f, 1.f / 120.f);
            glUniform3f(U("AmbientLightColor"), 0.42f, 0.44f, 0.5f); glUniform3f(U("DirLightDiffuseColor"), 0.75f, 0.70f, 0.6f);
            glUniform3f(U("DirLightDirection"), sun[0], sun[1], sun[2]); glUniform3f(U("FogColor"), 0.6f, 0.7f, 0.85f); glUniform1f(U("AlphaModulate"), 1.f);
            glUniform1i(U("Diffuse"), 0);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.tex);
            glBindBuffer(GL_ARRAY_BUFFER, m.vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)12); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)20); glEnableVertexAttribArray(2);
            glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_SHORT, (void*)0);
            if (capture && Shadow_WantCapture()) Shadow_RecordDraw(0, m.count, 0);          // what the rqDrawIndexed hook does
        }
        if (capture && otherFbo && Shadow_WantCapture())
        {   // a draw into another render target (the game's own ped-shadow / reflection passes): must be ignored
            if (!fbo2) { glGenRenderbuffers(1, &rb2); glBindRenderbuffer(GL_RENDERBUFFER, rb2); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 16, 16);
                         glGenFramebuffers(1, &fbo2); glBindFramebuffer(GL_FRAMEBUFFER, fbo2); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb2); }
            glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
            Shadow_RecordDraw(0, scene[1].count, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    };
    auto readPx = [&](int px, int py, float* rgb) { unsigned char c4[4]; glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c4); for (int i = 0; i < 3; ++i) rgb[i] = c4[i] / 255.f; };
    auto lum = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };

    PSSettings st; st.shadowEvery = 1; st.shadowStrength = 0.6f; st.shadowRes = 2; st.shadowDist = 60.f; st.sunSmoothing = 1.0f;   // 1.0 = no smoothing for the test
    bool useHint = false; GLuint progActive = prog;
    auto runFrames = [&](const float sun[3], int n) {
        for (int f = 0; f < n; ++f)
        {
            Shadow_Configure(st, 0); Shadow_SetSunHint(sun, useHint);
            drawScene(progActive, sun, true);
            PSGLState gs; gs.Save(); Shadow_EndFrame(); gs.Restore();
            World_BeginFrame(st, 0.f); World_OnShaderSelected();
        }
    };

    // ---------------- numeric test: sun from the south-west, 45 deg ----------------
    float sun[3] = { -0.5f, -0.5f, 0.7071f }; { float l = sqrtf(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]); for (int i = 0; i < 3; ++i) sun[i] /= l; }
    runFrames(sun, 4);
    const PSShadowStats& ss = Shadow_Stats();
    printf("      captured=%d replayed=%d res=%d cpu=%.1fms sunElev=%.2f note=%s\n", ss.captured, ss.replayed, ss.resolution, ss.cpuMs, ss.sunElevation, ss.note);
    CHECK(ss.ready && ss.captured >= 5 && ss.replayed >= 5, "world draws were captured and replayed into the shadow map");


    { // DEBUG: look inside the shadow map and the receiver uniforms
      float lvp[16], pr[4]; Shadow_Uniforms(lvp, pr);
      printf("      DBG uniforms: strength=%.3f bias=%.6f texel=%.6f tex=%u\n", pr[0], pr[1], pr[2], Shadow_Texture());
      GLuint f; glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Shadow_Texture(), 0);
      int R = Shadow_Stats().resolution; std::vector<unsigned char> px((size_t)R * R * 4); glReadPixels(0, 0, R, R, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      long nonWhite = 0; for (size_t i = 0; i < (size_t)R * R; ++i) if (px[i*4] < 250 || px[i*4+1] < 250) ++nonWhite;
      printf("      DBG shadow map %dx%d: non-empty texels = %ld (%.1f%%)  status=0x%x\n", R, R, nonWhite, 100.0 * nonWhite / ((double)R * R), glCheckFramebufferStatus(GL_FRAMEBUFFER));
      FILE* fo = fopen("/tmp/shadowmap.rgba", "wb"); fwrite(px.data(), 1, px.size(), fo); fclose(fo);
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &f); }
    drawScene(prog, sun, false);                                  // frame WITH shadows
    { float v[16] = {0}; glUseProgram(prog); glGetUniformfv(prog, glGetUniformLocation(prog, "PS_Shadow"), v); printf("      DBG GL PS_Shadow = %.4f %.6f %.6f %.1f\n", v[0], v[1], v[2], v[3]);
      glGetUniformfv(prog, glGetUniformLocation(prog, "PS_LightVP"), v); printf("      DBG GL PS_LightVP col0 = %.4f %.4f %.4f %.4f ; col3 = %.4f %.4f %.4f %.4f\n", v[0], v[1], v[2], v[3], v[12], v[13], v[14], v[15]);
      GLint un = 0; glGetUniformiv(prog, glGetUniformLocation(prog, "PS_ShadowMap"), &un); printf("      DBG GL PS_ShadowMap unit = %d\n", un); }
    float lit[3], shd[3], leaf[3], hole[3];
    // ground points (z=0): the house is at x[-6,6] y[-4,4] h=5; sun comes from (-,-): shadow falls toward (+x,+y)
    // shadow tip offset = height / tan(elev) = 5 along (+0.707,+0.707)  ->  a point just behind the house, e.g. (7.5, 5.5, 0), is in shadow
    float pShadow[3] = { 7.0f, 4.8f, 0.f }, pLit[3] = { -8.f, -9.f, 0.f };
    int sx, sy, lx, ly; Project(proj, view, pShadow, W, H, &sx, &sy); Project(proj, view, pLit, W, H, &lx, &ly);
    readPx(sx, sy, shd); readPx(lx, ly, lit);
    { std::vector<unsigned char> fr((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fr.data()); FILE* f1 = fopen("/tmp/with.rgba", "wb"); fwrite(fr.data(), 1, fr.size(), f1); fclose(f1); printf("      DBG test points px: shadow(%d,%d) lit(%d,%d)\n", sx, sy, lx, ly); }
    // reference: identical scene with shadows switched off
    PSSettings off = st; off.shadows = false; Shadow_Configure(off, 0); World_BeginFrame(off, 0.f);
    drawScene(prog, sun, false); float shd0[3], lit0[3]; readPx(sx, sy, shd0); readPx(lx, ly, lit0);
    { std::vector<unsigned char> fr((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fr.data()); FILE* f1 = fopen("/tmp/without.rgba", "wb"); fwrite(fr.data(), 1, fr.size(), f1); fclose(f1); }
    float ratioShadow = lum(shd) / lum(shd0), ratioLit = lum(lit) / lum(lit0);
    printf("      ground behind house: lum ratio with/without shadow = %.2f   ground in the open: %.2f\n", ratioShadow, ratioLit);
    CHECK(ratioShadow < 0.62f, "ground behind the house (away from the sun) is darkened by the shadow");
    CHECK(ratioLit > 0.97f, "ground in the open stays unshadowed");
    // sun-facing wall of the house must NOT be darkened by the shadow (front lit, back dark)
    float pWallFront[3] = { -2.f, -4.f, 2.5f }; int wx, wy; Project(proj, view, pWallFront, W, H, &wx, &wy);
    float wall[3], wall0[3]; Shadow_Configure(st, 0); World_BeginFrame(st, 0.f); drawScene(prog, sun, false); readPx(wx, wy, wall);
    Shadow_Configure(off, 0); World_BeginFrame(off, 0.f); drawScene(prog, sun, false); readPx(wx, wy, wall0);
    printf("      sun-facing wall: ratio %.2f\n", lum(wall) / lum(wall0));
    CHECK(lum(wall) / lum(wall0) > 0.93f, "the wall facing the sun stays lit (no self-shadow acne)");
    // tree: the opaque part of the foliage quad casts a shadow, the alpha-cut corner does not
    // crossed quads centred (9.5, 6.25); shadow offset for z=6 (quad centre) = 6 along (+.707,+.707) -> ~ (13.7, 10.5)
    // opaque centre of the foliage shadow vs a point at the transparent corner of the foliage shadow
    float pLeaf[3] = { 13.7f, 10.3f, 0.f }, pHole[3] = { 18.6f, 12.4f, 0.f };
    Shadow_Configure(st, 0); World_BeginFrame(st, 0.f); drawScene(prog, sun, false);
    int tx, ty, hx, hy; Project(proj, view, pLeaf, W, H, &tx, &ty); Project(proj, view, pHole, W, H, &hx, &hy); readPx(tx, ty, leaf); readPx(hx, hy, hole);
    Shadow_Configure(off, 0); World_BeginFrame(off, 0.f); drawScene(prog, sun, false); float leaf0[3], hole0[3]; readPx(tx, ty, leaf0); readPx(hx, hy, hole0);
    printf("      foliage shadow ratio %.2f   outside-foliage ratio %.2f\n", lum(leaf) / lum(leaf0), lum(hole) / lum(hole0));
    CHECK(lum(leaf) / lum(leaf0) < 0.8f, "alpha-tested foliage casts a shadow");
    CHECK(lum(hole) / lum(hole0) > 0.95f, "area outside the foliage shape is not shadowed");



    // ---------------- v1.3.1: caster memory + render-target filter ----------------
    {
        skipMesh = 1; otherFbo = true; turnAway = true;                  // the camera turned away: the game no longer draws the house, plus a stray draw into another target
        runFrames(sun, 4);
        const PSShadowStats& sc = Shadow_Stats();
        printf("      [cache] captured=%d replayed=%d cache=%d kept=%d otherTargets=%d\n", sc.captured, sc.replayed, sc.cacheSize, sc.cacheKept, sc.otherTargets);
        CHECK(sc.cacheKept >= 1, "casters the game did NOT draw this frame are kept from earlier frames");
        CHECK(sc.otherTargets >= 1, "draws into other render targets are ignored");
        int a1, b1, a2, b2; float shd1[3], shd2[3];
        Project(proj, view, pShadow, W, H, &a1, &b1);
        Shadow_Configure(st, 0); World_BeginFrame(st, 0.f); drawScene(prog, sun, false); readPx(a1, b1, shd1);
        PSSettings offc = st; offc.shadows = false; Shadow_Configure(offc, 0); World_BeginFrame(offc, 0.f); drawScene(prog, sun, false); readPx(a1, b1, shd2);
        printf("      [cache] ground behind the culled house: ratio %.2f\n", lum(shd1) / lum(shd2));
        CHECK(lum(shd1) / lum(shd2) < 0.75f, "the culled house still casts its shadow (remembered)");
        // while the house SHOULD be in view but is not drawn (occluded / moved away) it is forgotten after 3 captures
        turnAway = false; runFrames(sun, 4);
        CHECK(Shadow_Stats().cacheSize >= 1, "in-view casters the game stopped drawing are forgotten (no ghost shadows)");
        skipMesh = -1; otherFbo = false; (void)a2; (void)b2; runFrames(sun, 3);
        Shadow_Configure(st, 0);
    }

    // ---------------- v1.3: UNLIT prelit shaders (buildings / roads / terrain) must receive shadows too ----------------
    {
        std::string uv, uf; bool uvOk = World_PatchVertexSource(PS_FLAG_FOG, kGameVSUnlit, uv);
        bool ufOk = World_PatchPixelSource(PS_FLAG_FOG, kGameFS, uf, false, uvOk);
        CHECK(uvOk && ufOk, "UNLIT shader (no Normal, no DirLightDirection) accepts the shadow receiver patch");
        GLuint progU = BuildProgram(uv, uf, "patched-unlit");
        CHECK(progU != 0, "patched unlit vertex+pixel pair compiles AND links");
        if (progU)
        {
            progActive = progU; useHint = true; st.sunSource = 1;           // sun straight from the game's time-cycle vector
            struct Case { const char* label; float el; } cases[3] = { { "45 deg", 0.7071f }, { "30 deg", 0.5f }, { "15 deg", 0.2588f } };
            for (int c = 0; c < 3; ++c)
            {
                float el = cases[c].el, ce = sqrtf(1.f - el * el);
                float us[3] = { -0.7071f * ce, -0.7071f * ce, el };
                runFrames(us, 4);
                std::string lbl = std::string("unlit @ ") + cases[c].label;
                printf("      [%s] captured=%d replayed=%d src=%d bias-ok\n", lbl.c_str(), Shadow_Stats().captured, Shadow_Stats().replayed, Shadow_Stats().sunSource);
                // shadow point: just behind the house along the shadow direction; lit point: open ground
                float tipLen = 5.f / (el / ce);                        // house height / tan(elev)
                float pS[3] = { 7.0f + 0.f, 4.8f, 0.f }; if (tipLen < 9.f) { pS[0] = 6.5f + 0.5f; pS[1] = 4.8f; }
                (void)tipLen;
                int a1, b1, a2, b2; float shd1[3], lit1[3], shd2[3], lit2[3];
                Project(proj, view, pS, W, H, &a1, &b1); Project(proj, view, pLit, W, H, &a2, &b2);
                Shadow_Configure(st, 0); Shadow_SetSunHint(us, true); World_BeginFrame(st, 0.f); drawScene(progU, us, false); readPx(a1, b1, shd1); readPx(a2, b2, lit1);
                PSSettings off2 = st; off2.shadows = false; Shadow_Configure(off2, 0); World_BeginFrame(off2, 0.f); drawScene(progU, us, false); readPx(a1, b1, shd2); readPx(a2, b2, lit2);
                float rs = lum(shd1) / lum(shd2), rl = lum(lit1) / lum(lit2);
                printf("      [%s] ground behind house ratio %.2f, open ground ratio %.2f\n", lbl.c_str(), rs, rl);
                CHECK(rs < 0.70f, (lbl + ": unlit ground behind the house is shadowed").c_str());
                CHECK(rl > 0.97f, (lbl + ": unlit open ground is NOT striped / self-shadowed (no acne)").c_str());
                // sun-facing wall (south face is y=-4; the sun comes from -x,-y so the -y wall faces it)
                float pW[3] = { -2.f, -4.f, 2.5f }; int wx2, wy2; float w1[3], w2[3]; Project(proj, view, pW, W, H, &wx2, &wy2);
                Shadow_Configure(st, 0); Shadow_SetSunHint(us, true); World_BeginFrame(st, 0.f); drawScene(progU, us, false); readPx(wx2, wy2, w1);
                Shadow_Configure(off2, 0); World_BeginFrame(off2, 0.f); drawScene(progU, us, false); readPx(wx2, wy2, w2);
                printf("      [%s] sun-facing wall ratio %.2f\n", lbl.c_str(), lum(w1) / lum(w2));
                CHECK(lum(w1) / lum(w2) > 0.90f, (lbl + ": unlit sun-facing wall stays lit").c_str());
                if (c == 0) { std::vector<unsigned char> fr((size_t)W * H * 4); Shadow_Configure(st, 0); World_BeginFrame(st, 0.f); drawScene(progU, us, false);
                              glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fr.data()); FILE* f3 = fopen("/tmp/unlit_with.rgba", "wb"); fwrite(fr.data(), 1, fr.size(), f3); fclose(f3); }
            }
            progActive = prog; useHint = false; st.sunSource = 0;
        }
    }

    // ---------------- time of day (what TimeSync drives): shadows swing with the sun and fade at night ----------------
    struct TOD { const char* label; float hours; } tods[4] = { { "09:00", 9.f }, { "12:30", 12.5f }, { "16:30", 16.5f }, { "19:40", 19.67f } };
    std::vector<unsigned char> sheet((size_t)W * H * 4 * 4);
    float prevDir = -9.f; bool swung = true;
    for (int k = 0; k < 4; ++k)
    {
        float t = (tods[k].hours - 6.f) / 12.f;                   // 06:00 sunrise ... 18:00 sunset (game-like arc, simplified)
        float el = 0.17f + 1.15f * sinf(3.14159f * t) ; if (tods[k].hours > 19.f) el = 0.17f;         // 19:40 -> ~10 deg
        float az = 3.14159f * (1.f - t) + 1.2f;                    // east -> west
        float sd[3] = { cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el) };
        Shadow_Configure(st, 0); World_BeginFrame(st, 0.f);
        runFrames(sd, 3);
        drawScene(prog, sd, false);
        std::vector<unsigned char> fr((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fr.data());
        memcpy(&sheet[(size_t)k * W * H * 4], fr.data(), fr.size());
        float ang = atan2f(sd[1], sd[0]); if (prevDir > -8.f && fabsf(ang - prevDir) < 0.05f) swung = false; prevDir = ang;
        printf("      %s  sun elevation sin=%.2f  captured=%d replayed=%d fade=%.2f\n", tods[k].label, Shadow_Stats().sunElevation, Shadow_Stats().captured, Shadow_Stats().replayed, Shadow_Stats().fade);
    }
    CHECK(swung, "the shadow direction changes as the game clock advances");
    // night: sun below the horizon -> no shadow pass, strength 0
    { float sd[3] = { 0.3f, 0.4f, -0.3f }; float l = sqrtf(0.09f + 0.16f + 0.09f); for (int i = 0; i < 3; ++i) sd[i] /= l;
      Shadow_Configure(st, 0); runFrames(sd, 3); float lvp[16], pr[4]; Shadow_Uniforms(lvp, pr);
      CHECK(Shadow_Stats().fade < 0.01f, "sun below the horizon: shadows fade out (night)");
      (void)pr; }
    FILE* fo = fopen("/tmp/shadow_sheet.rgba", "wb"); fwrite(sheet.data(), 1, sheet.size(), fo); fclose(fo);
    while (glGetError() != GL_NO_ERROR) {}
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: shadow pipeline tests passed\n", fails);
    return fails;
}
