// 1.4.3 DEFERRED shadows (Method 0, the libZyZyGFX64 way) on a GLES 3 driver. Mock "game": real game shader lines (ObjMatrix / WorldPos / WorldNormal / DirLightDirection / fog),
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
#include "../src64/ps_shadow.h"
#include "../src64/ps_world.h"
#include "../src64/ps_glsl.h"
#include "../src64/ps_glstate.h"
#include "../src64/ps_scene.h"

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


// ---- mock of the game's render queue (GTA SA 2.10 arm64 layout) so Scene_PostEvent's command writing is tested for real ----
#include <dlfcn.h>
static char g_rqMem[0x430 + 64];
static char g_stream[4096];
extern "C" __attribute__((visibility("default"))) void* renderQueue = g_rqMem;
static int g_mutexOps = 0, g_markers = 0;
extern "C" __attribute__((visibility("default"))) void OSMO(void*) __asm__("_Z14OS_MutexObtainPv");
extern "C" void OSMO(void*) { ++g_mutexOps; }
extern "C" __attribute__((visibility("default"))) void OSMR(void*) __asm__("_Z15OS_MutexReleasePv");
extern "C" void OSMR(void*) { ++g_mutexOps; }
extern "C" __attribute__((visibility("default"))) void RQDM(char** p) __asm__("_Z24RQ_Command_rqDebugMarkerRPc");
extern "C" void RQDM(char** p) { ++g_markers; *p += 8; }            // a game marker: 8-byte payload
static char* g_readPos = g_stream;
extern "C" __attribute__((visibility("default"))) void RQP(void* rq) __asm__("_ZN11RenderQueue7ProcessEv");
extern "C" void RQP(void* rq)
{
    char* r = (char*)rq; char* end = *(char**)(r + 0x418);
    while (g_readPos < end) { int id; memcpy(&id, g_readPos, 4); g_readPos += 4; ((void (*)(char**))((void**)r)[id])(&g_readPos); }
}
static void MockQueueInit(bool threaded)
{
    memset(g_rqMem, 0, sizeof(g_rqMem));
    void** table = (void**)g_rqMem;
    for (int i = 0; i < 50; ++i) table[i] = (void*)&OSMO;          // filler entries
    table[12] = (void*)&RQDM;                                       // rqDebugMarker somewhere in the table
    g_rqMem[0x3E8] = threaded ? 1 : 0; g_rqMem[0x3E9] = 1;
    static int dummyMutex; *(void**)(g_rqMem + 0x3F0) = &dummyMutex;
    *(char**)(g_rqMem + 0x400) = g_stream + sizeof(g_stream);
    *(char**)(g_rqMem + 0x418) = g_stream;
    *(char**)(g_rqMem + 0x420) = g_stream;
    g_readPos = g_stream;
}
static void MockMarker()   // the game writes its own rqDebugMarker command (id + 8 bytes) and commits
{
    char* wp = *(char**)(g_rqMem + 0x420); int id = 12; memcpy(wp, &id, 4); memset(wp + 4, 0x11, 8);
    *(char**)(g_rqMem + 0x420) = wp + 12; *(char**)(g_rqMem + 0x418) = wp + 12;
}

int main()
{
    GLSL_Init(""); World_LoadSnippets();
    const int W = 800, H = 450;
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr); EGLint a, b; eglInitialize(d, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x40 /*EGL_OPENGL_ES3_BIT*/, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE }; EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE }; EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa); eglMakeCurrent(d, s, s, x);
    printf("      GL_VERSION %s\n", (const char*)glGetString(GL_VERSION));
    PSGLState::InitCaps();
    GLuint prog = BuildProgram(kGameVS, kGameFS, "vanilla game shader");
    CHECK(prog != 0, "unpatched game shader builds (deferred does not patch it)");
    if (!prog) return 1;

    GLuint texGround, texWall, texLeaf;
    unsigned char g[64 * 64 * 4], w[16 * 16 * 4], lf[64 * 64 * 4];
    for (int y = 0; y < 64; ++y) for (int xx = 0; xx < 64; ++xx) { int k = (((xx / 8) + (y / 8)) & 1); unsigned char v = k ? 150 : 135; unsigned char* o = &g[(y*64+xx)*4]; o[0] = v; o[1] = v; o[2] = v - 8; o[3] = 255; }
    for (int i = 0; i < 16 * 16; ++i) { w[i*4+0] = 170; w[i*4+1] = 105; w[i*4+2] = 70; w[i*4+3] = 255; }
    for (int y = 0; y < 64; ++y) for (int xx = 0; xx < 64; ++xx) { float dx = (xx - 31.5f) / 32.f, dy = (y - 31.5f) / 32.f; bool in = dx*dx + dy*dy < 0.85f; unsigned char* o = &lf[(y*64+xx)*4]; o[0] = 50; o[1] = 120; o[2] = 40; o[3] = in ? 255 : 0; }
    auto mkTex = [&](GLuint& t, int sz, unsigned char* px) { glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); };
    mkTex(texGround, 64, g); mkTex(texWall, 16, w); mkTex(texLeaf, 64, lf);
    std::vector<Mesh> scene;
    { std::vector<V> v; std::vector<unsigned short> i; float a1[3] = {-45,-45,0}, b1[3] = {45,-45,0}, c1[3] = {45,45,0}, d1[3] = {-45,45,0}, nz[3] = {0,0,1}; Quad(v, i, a1, b1, c1, d1, nz);
      for (auto& q : v) { q.uv[0] *= 18.f; q.uv[1] *= 18.f; } scene.push_back(MakeMesh(v, i, texGround)); }
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, -6, -4, 0, 6, 4, 5); scene.push_back(MakeMesh(v, i, texWall)); }
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, 9.f, 6.f, 0, 9.5f, 6.5f, 4.f); scene.push_back(MakeMesh(v, i, texWall)); }
    { std::vector<V> v; std::vector<unsigned short> i; float nn[3] = {0,1,0};
      float a2[3] = {6.5f,6.25f,3.f}, b2[3] = {12.5f,6.25f,3.f}, c2[3] = {12.5f,6.25f,9.f}, d2[3] = {6.5f,6.25f,9.f}; Quad(v, i, a2, b2, c2, d2, nn);
      float a3[3] = {9.25f,3.25f,3.f}, b3[3] = {9.25f,9.25f,3.f}, c3[3] = {9.25f,9.25f,9.f}, d3[3] = {9.25f,3.25f,9.f}; float nx[3] = {1,0,0}; Quad(v, i, a3, b3, c3, d3, nx);
      scene.push_back(MakeMesh(v, i, texLeaf)); }
    // 1.4.6: a kiosk (mesh 4) for the cache / ghost / LOD tests, hidden unless a test shows it
    { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, 14.f, -12.f, 0, 16.f, -10.f, 4.f); scene.push_back(MakeMesh(v, i, texWall)); }
    // per mesh: entity kind (what CEntity::Render tags), drawn this frame, blended + alpha-tested like SA's foliage
    std::vector<int> kind = { PS_KIND_BUILDING, PS_KIND_BUILDING, PS_KIND_TREE, PS_KIND_TREE, PS_KIND_BUILDING };
    std::vector<int> visible = { 1, 1, 1, 1, 0 };
    std::vector<int> foliage = { 0, 0, 0, 1, 0 };
    static int s_atFunc = 0x0207; static float s_atRef = 0.f;          // the game's RQ_Command_rqSetAlphaTest statics
    Shadow_SetAlphaTestState(&s_atFunc, &s_atRef);
    Shadow_SetBufferTracking(true);

    // the game's scene target: colour texture + DEPTH RENDERBUFFER (what GTA SA 2.10 does), the size of the screen
    GLuint sceneTex, sceneRb, sceneFbo;
    glGenTextures(1, &sceneTex); glBindTexture(GL_TEXTURE_2D, sceneTex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &sceneRb); glBindRenderbuffer(GL_RENDERBUFFER, sceneRb); glRenderbufferStorage(GL_RENDERBUFFER, 0x88F0 /*DEPTH24_STENCIL8*/, W, H);
    glGenFramebuffers(1, &sceneFbo); glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneRb); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneRb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "mock game scene target (colour + depth/stencil renderbuffer) is complete");

    float proj[16], view[16];
    const float eye[3] = { 30.f, -26.f, 20.f }, look[3] = { 4.f, 3.f, 1.5f };
    Perspective(proj, 0.80f, (float)W / H, 0.5f, 400.f); LookAt(view, eye, look);
    Scene_SetHandler([](int ev, int arg) {
        if (ev == PS_EV_TAG) { Shadow_SetDrawKind(arg); return; }
        if (ev == PS_EV_BEFORE_SCENE) { Scene_OnTargetSelected(); Shadow_BeginScene(); }
        if (ev == PS_EV_AFTER_SCENE && Shadow_AfterSceneNeeded()) { PSGLState gs; gs.Save(); Shadow_AfterScene(); gs.Restore(); }
    });
    PSSettings st; st.shadowMethod = 0; st.shadowEvery = 1; st.shadowStrength = 0.6f; st.shadowRes = 2; st.shadowDist = 60.f; st.sunSmoothing = 1.0f; st.shadowTint = 0.f;
    bool shadowsOn = true;
    auto frame = [&](const float sun[3]) {
        PSSettings cur = st; cur.shadows = shadowsOn;
        Shadow_Configure(cur, 0); Shadow_SetSunHint(sun, true); Scene_SetScreenSize(W, H);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);                 // rqTargetSelect
        Scene_OnTargetSelected();                                     // our hook right after it
        Scene_RunEventForTest(PS_EV_BEFORE_SCENE);
        glViewport(0, 0, W, H); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glClearColor(0.55f, 0.7f, 0.95f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        for (size_t mi = 0; mi < scene.size(); ++mi)
        {
            const Mesh& m = scene[mi];
            if (!visible[mi]) continue;
            Scene_RunEventForTest(PS_EV_TAG, kind[mi]);                   // CEntity::Render: tag before the entity
            if (foliage[mi]) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); s_atFunc = 0x0204; s_atRef = 0.5f; }
            else { glDisable(GL_BLEND); s_atFunc = 0x0207; s_atRef = 0.f; }
            glUseProgram(prog);
            auto U = [&](const char* nme) { return glGetUniformLocation(prog, nme); };
            glUniformMatrix4fv(U("ProjMatrix"), 1, GL_FALSE, proj); glUniformMatrix4fv(U("ViewMatrix"), 1, GL_FALSE, view); glUniformMatrix4fv(U("ObjMatrix"), 1, GL_FALSE, m.obj);
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
            if (Shadow_WantCapture()) Shadow_RecordDraw(0, m.count, 0);          // the rqDrawIndexed hook
            else if (Shadow_WantCamera()) Shadow_RecordCamera();
            Scene_RunEventForTest(PS_EV_TAG, PS_KIND_NONE);               // ... and the reset after it
        }
        glDisable(GL_BLEND);
        Scene_RunEventForTest(PS_EV_AFTER_SCENE);                      // what the render queue delivers right after RenderScene
        PSGLState gs; gs.Save(); Shadow_EndFrame(); gs.Restore();     // OS_ScreenSwapBuffers
    };
    auto lum = [](const unsigned char* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
    auto grab = [&](std::vector<unsigned char>& fr) { fr.resize((size_t)W * H * 4); glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fr.data()); };
    auto at = [&](std::vector<unsigned char>& fr, const float p[3]) { int px, py; Project(proj, view, p, W, H, &px, &py); return lum(&fr[((size_t)py * W + px) * 4]); };

    float sun[3] = { -0.5f, -0.5f, 0.7071f }; { float l = sqrtf(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]); for (int i = 0; i < 3; ++i) sun[i] /= l; }
    for (int f = 0; f < 4; ++f) frame(sun);
    const PSSceneStats& sc = Scene_Stats(); const PSShadowStats& ss = Shadow_Stats();
    printf("      scene: fbo=%d %dx%d depthTex=%u depth=%d stencil=%d swaps=%d note=%s\n", sc.sceneFbo, sc.w, sc.h, sc.depthTex, sc.depthBits, sc.stencilBits, sc.swaps, sc.note);
    printf("      shadows: captured=%d rendered=%d res=%d ready=%d note=%s\n", ss.captured, ss.replayed, ss.resolution, ss.ready, ss.note);
    CHECK(sc.depthTex != 0 && sc.sceneFbo == (int)sceneFbo && sc.swaps >= 1, "the scene target's depth renderbuffer was replaced by a readable depth texture");
    CHECK(sc.stencilBits == 8, "the game's stencil is kept (depth24 + stencil8 texture)");
    CHECK(ss.ready && ss.replayed >= 4, "casters rendered into the cascades after the scene");
    std::vector<unsigned char> with, without; grab(with);
    { GLuint f; glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Shadow_Texture(), 0);
      float q[3] = { 7.0f, 4.8f, 0.f }, q2[3] = { 13.7f, 10.3f, 0.f }; int px, py; unsigned char m4[4];
      Project(proj, view, q, W, H, &px, &py); glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, m4); printf("      mask behind house = %d %d %d\n", m4[0], m4[1], m4[2]);
      Project(proj, view, q2, W, H, &px, &py); glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, m4); printf("      mask foliage = %d %d %d\n", m4[0], m4[1], m4[2]);
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &f); }
    shadowsOn = false; for (int f = 0; f < 2; ++f) frame(sun); grab(without); shadowsOn = true;
    float pShadow[3] = { 7.0f, 4.8f, 0.f }, pLit[3] = { -8.f, -9.f, 0.f }, pWall[3] = { -2.f, -4.f, 2.5f }, pLeaf[3] = { 13.7f, 10.3f, 0.f }, pHole[3] = { 18.6f, 12.4f, 0.f };
    float rS = at(with, pShadow) / at(without, pShadow), rL = at(with, pLit) / at(without, pLit), rW = at(with, pWall) / at(without, pWall);
    float rLeaf = at(with, pLeaf) / at(without, pLeaf), rHole = at(with, pHole) / at(without, pHole);
    printf("      ratios with/without: behind house %.2f, open ground %.2f, sun-facing wall %.2f, foliage shadow %.2f, outside foliage %.2f\n", rS, rL, rW, rLeaf, rHole);
    CHECK(rS < 0.62f, "ground behind the house is darkened (deferred mask)");
    CHECK(rL > 0.97f, "open ground stays lit (no acne from depth-rebuilt normals)");
    CHECK(rW > 0.93f, "the sun-facing wall stays lit");
    CHECK(rLeaf < 0.8f, "BLENDED + alpha-tested foliage (SA trees) casts its leaf shadow (1.4.5 dropped every blended draw)");
    CHECK(rHole > 0.95f, "outside the foliage shape is not shadowed (cut out with the game's alpha-test reference, no box)");
    { FILE* f1 = fopen("/tmp/def_with.rgba", "wb"); fwrite(with.data(), 1, with.size(), f1); fclose(f1); FILE* f2 = fopen("/tmp/def_without.rgba", "wb"); fwrite(without.data(), 1, without.size(), f2); fclose(f2); }
    // half-resolution mask
    st.shadowMaskHalf = true; for (int f = 0; f < 3; ++f) frame(sun); std::vector<unsigned char> half; grab(half);
    float hS = at(half, pShadow) / at(without, pShadow), hL = at(half, pLit) / at(without, pLit);
    printf("      half-res mask: behind house %.2f, open ground %.2f\n", hS, hL);
    CHECK(hS < 0.65f && hL > 0.96f, "half-resolution mask gives the same result");
    st.shadowMaskHalf = false;
    // 1.4.5: adaptive quality level 3 must not switch shadows off
    { PSSettings cur = st; Shadow_Configure(cur, 3); CHECK(Shadow_Stats().enabled, "adaptive level 3 keeps shadows on (slower updates only)"); Shadow_Configure(cur, 0); }
    { PSSettings off = st; off.shadows = false; Shadow_Configure(off, 0); CHECK(strstr(Shadow_Stats().note, "unchecked") != nullptr, "status says why shadows are off"); Shadow_Configure(st, 0); }
    // the game re-creates its target (resolution change): the depth is re-attached
    glBindRenderbuffer(GL_RENDERBUFFER, sceneRb); glRenderbufferStorage(GL_RENDERBUFFER, 0x88F0, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneRb); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneRb);
    const int swaps0 = Scene_Stats().swaps; for (int f = 0; f < 2; ++f) frame(sun);
    CHECK(Scene_Stats().swaps > swaps0, "a re-attached game renderbuffer is upgraded again");
    // GL state the game tracks is unchanged by the after-scene pass
    { glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo); glUseProgram(prog); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glViewport(0, 0, W, H);
      Shadow_Configure(st, 0); Shadow_BeginScene(); Shadow_RecordCamera();
      Scene_RunEventForTest(PS_EV_AFTER_SCENE);
      GLint p = 0, fb = 0, df = 0, vp[4]; glGetIntegerv(GL_CURRENT_PROGRAM, &p); glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb); glGetIntegerv(GL_DEPTH_FUNC, &df); glGetIntegerv(GL_VIEWPORT, vp);
      CHECK(p == (GLint)prog && fb == (GLint)sceneFbo && df == GL_LEQUAL && vp[2] == W && glIsEnabled(GL_DEPTH_TEST), "program / target / depth func / viewport are restored after the shadow pass"); }
    // night: no mask
    { float nsun[3] = { 0.3f, 0.4f, -0.5f }; for (int f = 0; f < 3; ++f) frame(nsun); std::vector<unsigned char> night; grab(night);
      std::vector<unsigned char> nightOff; shadowsOn = false; for (int f = 0; f < 2; ++f) frame(nsun); grab(nightOff); shadowsOn = true;
      CHECK(fabsf(at(night, pShadow) - at(nightOff, pShadow)) < 2.f, "sun below the horizon: no shadow mask"); }
    // ---------------- 1.4.6: cache / ghost / LOD / stability ----------------
    {
        const float pK[3] = { 17.0f, -9.0f, 0.f };                     // ground in the kiosk's shadow
        { int px, py; Project(proj, view, pK, W, H, &px, &py); CHECK(px > 2 && px < W - 2 && py > 2 && py < H - 2, "kiosk shadow spot is on screen"); }
        std::vector<unsigned char> fr;
        auto kioskShade = [&]() { grab(fr); return at(fr, pK) / at(without, pK); };
        // A: static building, buffers untouched: remembered for a capture after the game stops drawing it, gone once it is clearly not there
        kind[4] = PS_KIND_BUILDING; visible[4] = 1; for (int f = 0; f < 3; ++f) frame(sun);
        const float a0 = kioskShade();
        visible[4] = 0; frame(sun); const float a1 = kioskShade();
        for (int f = 0; f < 3; ++f) frame(sun); const float a2 = kioskShade();
        printf("      kiosk (building): drawn %.2f, 1 frame later %.2f, 4 frames later %.2f\n", a0, a1, a2);
        CHECK(a0 < 0.62f, "a tagged building casts");
        CHECK(a2 > 0.97f, "a building the game stopped drawing in front of the camera leaves no ghost");
        // B: the game re-uses the kiosk's vertex buffer for another mesh (streaming): the remembered caster dies at once
        visible[4] = 1; for (int f = 0; f < 3; ++f) frame(sun);
        visible[4] = 0;
        { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, 300.f, 300.f, 0, 301.f, 301.f, 1.f);
          glBindBuffer(GL_ARRAY_BUFFER, scene[4].vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_STATIC_DRAW);
          Shadow_OnBufferWrite(GL_ARRAY_BUFFER);                          // what the GOT hook on libGTASA's glBufferData reports
          glBindBuffer(GL_ARRAY_BUFFER, 0); }
        frame(sun); const float b1 = kioskShade();
        printf("      kiosk buffer re-used: 1 frame later %.2f\n", b1);
        CHECK(b1 > 0.97f, "buffer re-used by the game -> the remembered caster is dropped immediately (no ghost shadow)");
        { std::vector<V> v; std::vector<unsigned short> i; Box(v, i, 14.f, -12.f, 0, 16.f, -10.f, 4.f);    // restore the kiosk
          glBindBuffer(GL_ARRAY_BUFFER, scene[4].vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V), v.data(), GL_STATIC_DRAW); Shadow_OnBufferWrite(GL_ARRAY_BUFFER); glBindBuffer(GL_ARRAY_BUFFER, 0); }
        // C: something that can move (object / vehicle): casts only while drawn
        kind[4] = PS_KIND_DYNAMIC; visible[4] = 1; for (int f = 0; f < 3; ++f) frame(sun); const float c0 = kioskShade();
        visible[4] = 0; frame(sun); const float c1 = kioskShade();
        printf("      kiosk (dynamic): drawn %.2f, 1 frame later %.2f\n", c0, c1);
        CHECK(c0 < 0.62f && c1 > 0.97f, "objects / vehicles cast while drawn and are never remembered (no ghost where they were)");
        // D: LOD tree stand-in never casts (its crossed boxes are not the tree's shape)
        kind[4] = PS_KIND_LODTREE; visible[4] = 1; for (int f = 0; f < 3; ++f) frame(sun); const float d0 = kioskShade();
        printf("      kiosk (LOD tree): %.2f\n", d0);
        CHECK(d0 > 0.97f, "LOD tree boxes cast no shadow");
        visible[4] = 0; kind[4] = PS_KIND_BUILDING; for (int f = 0; f < 3; ++f) frame(sun);
        // E: stable scene, update every 2nd frame: the mask is identical every frame (no on/off flicker)
        st.shadowEvery = 2;
        float mn = 1e9f, mx = -1e9f, ln = 1e9f, lx = -1e9f;
        for (int f = 0; f < 10; ++f) { frame(sun); grab(fr); const float v = at(fr, pShadow), l = at(fr, pLeaf); mn = fminf(mn, v); mx = fmaxf(mx, v); ln = fminf(ln, l); lx = fmaxf(lx, l); }
        printf("      10 frames (update every 2): behind house %.1f..%.1f, foliage %.1f..%.1f\n", mn, mx, ln, lx);
        CHECK(mx - mn < 1.5f && lx - ln < 1.5f && mx / at(without, pShadow) < 0.62f, "shadows are steady from frame to frame (no flicker)");
        st.shadowEvery = 1;
    }
    // ---- render queue: our command goes through the game's own command table, between the game's commands
    {
        MockQueueInit(false);
        static void* self; self = dlopen(nullptr, RTLD_NOW);
        CHECK(Scene_Init([](const char* n) -> void* { return dlsym(self, n); }), "render-queue symbols resolve (renderQueue, OS_Mutex*, rqDebugMarker, RenderQueue::Process)");
        int seen[3] = {0, 0, 0}; static int* sp; sp = seen;
        Scene_SetHandler([](int ev, int) { if (ev >= 0 && ev < 3) ++sp[ev]; });
        Scene_SetWanted(true);
        MockMarker(); Scene_PostEvent(PS_EV_BEFORE_SCENE); MockMarker(); Scene_PostEvent(PS_EV_AFTER_SCENE); MockMarker();
        RQP(g_rqMem);
        const PSSceneStats& q = Scene_Stats();
        printf("      rq: slot=%d posted=%u/%u ran=%u/%u markers=%d mutexOps=%d stream used=%d bytes\n", q.rqSlot, q.posted[1], q.posted[2], q.ran[1], q.ran[2], g_markers, g_mutexOps, (int)(*(char**)(g_rqMem + 0x420) - g_stream));
        CHECK(q.rqReady && q.rqSlot == 12, "our handler took the rqDebugMarker slot");
        CHECK(seen[1] == 1 && seen[2] == 1, "before/after-scene events are delivered on Process (not threaded: right away)");
        CHECK(g_markers == 3, "the game's own debug markers still reach the original handler");
        {   // 1.4.6: entity tags carry their kind through the queue
            static int lastTag; lastTag = -1;
            Scene_SetHandler([](int ev, int arg) { if (ev == PS_EV_TAG) lastTag = arg; });
            CHECK(Scene_PostEvent(PS_EV_TAG, PS_KIND_LOD), "tag event posted"); RQP(g_rqMem);
            CHECK(lastTag == PS_KIND_LOD, "the tag's kind arrives on the render side");
            Scene_PostEvent(PS_EV_TAG, PS_KIND_NONE); RQP(g_rqMem);
            CHECK(lastTag == PS_KIND_NONE, "the reset tag arrives too");
        }
        CHECK(*(char**)(g_rqMem + 0x418) == *(char**)(g_rqMem + 0x420) && g_readPos == *(char**)(g_rqMem + 0x420), "committed end = write pointer, the stream is consumed exactly");
        // a full buffer is never overrun
        *(char**)(g_rqMem + 0x400) = *(char**)(g_rqMem + 0x420) + 30;
        const uint32_t before = q.posted[2]; Scene_PostEvent(PS_EV_AFTER_SCENE);
        CHECK(q.posted[2] == before && q.skippedFull >= 1, "no command is written when the queue buffer is nearly full");
        Scene_SetWanted(false);
    }
    while (glGetError() != GL_NO_ERROR) {}
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: deferred shadow tests passed\n", fails);
    return fails;
}
