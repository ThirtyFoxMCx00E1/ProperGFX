#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string>
#include "../src64/ps_world.h"
#include "../src64/ps_glsl.h"
static bool compile(const std::string& src, const char* tag)
{
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER); const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); printf("  FAIL %s: %s\n", tag, log); }
    return ok;
}
int main()
{
    GLSL_Init(""); World_LoadSnippets();     // 1.4: receiver text comes from the built-in shadows.glsl
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr); EGLint a, b; eglInitialize(d, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE }; EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE }; EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa); eglMakeCurrent(d, s, s, x);

    // Shapes assembled from the literal fragments found in libGTASA's .rodata (see FEATURES / README)
    const std::string hdr = "precision mediump float;\nuniform sampler2D Diffuse;\nuniform sampler2D EnvMap;\nuniform lowp vec3 FogColor;\nuniform mediump vec2 DetailTiling;\n"
        "varying mediump vec2 Out_Tex0;\nuniform lowp float EnvMapCoefficient;\nvarying mediump vec2 Out_Tex1;\nvarying mediump vec3 Out_Refl;\nvarying mediump float Out_FogAmt;\n"
        "varying lowp vec4 Out_Color;\nvarying lowp vec3 Out_Spec;\nuniform lowp float AlphaModulate;\nvarying mediump vec2 Out_WaterDetail;\nvarying mediump vec2 Out_WaterDetail2;\nvarying mediump float Out_WaterAlphaBlend;\n";
    struct V { const char* name; unsigned flags; std::string body; } vs[] = {
      { "lit+tex+fog+atest", PS_FLAG_FOG | PS_FLAG_LIGHTING,
        "void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0);\nfcolor = diffuseColor;\nfcolor *= Out_Color;\n"
        "fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\ngl_FragColor = fcolor;\n/*ATBEGIN*/\nif (diffuseColor.a < 0.5) { discard; }\n/*ATEND*/\ngl_FragColor.a *= AlphaModulate;\n}\n" },
      { "vehicle envmap+spec+fog", PS_FLAG_FOG | PS_FLAG_ENVMAP,
        "void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0, -0.5);\nfcolor = diffuseColor;\nfcolor *= Out_Color;\n"
        "vec2 ReflPos = normalize(Out_Refl.xy) * (Out_Refl.z * 0.5 + 0.5);\nReflPos = (ReflPos * vec2(0.5,0.5)) + vec2(0.5,0.5);\nlowp vec4 ReflTexture =  texture2D(EnvMap, ReflPos);\n"
        "fcolor.xyz = mix(fcolor.xyz,ReflTexture.xyz, EnvMapCoefficient);\nfcolor.w += ReflTexture.b * 0.125;\nfcolor.xyz += Out_Spec;\n"
        "fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\nfcolor.xyz += fcolor.xyz * 0.5;\ngl_FragColor = fcolor;\n}\n" },
      { "sphere envmap", PS_FLAG_FOG,
        "void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0);\nfcolor = diffuseColor;\nfcolor *= Out_Color;\n"
        "fcolor.xyz = mix(fcolor.xyz, texture2D(EnvMap, Out_Tex1).xyz, EnvMapCoefficient);\nfcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\ngl_FragColor = fcolor;\n}\n" },
      { "water", PS_FLAG_FOG | PS_FLAG_WATER,
        "void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0);\nfcolor = diffuseColor;\n"
        "float waterDetail = texture2D(EnvMap, Out_WaterDetail, -1.0).x + texture2D(EnvMap, Out_WaterDetail2, -1.0).x;\nfcolor *= vec4(Out_Color.xyz * waterDetail * 1.1, Out_Color.w);\n"
        "fcolor.a += Out_WaterAlphaBlend;\nfcolor.xyz += Out_Spec;\nfcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\ngl_FragColor = fcolor;\n}\n" },
      { "2D / HUD (must be untouched)", 0u,
        "void main()\n{\nlowp vec4 fcolor;\nlowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0);\nfcolor = diffuseColor;\nfcolor *= Out_Color;\ngl_FragColor = fcolor;\n}\n" },
    };
    int bad = 0;
    { // shadow receiver rules (vertex side)
      std::string o;
      const std::string base = "precision highp float;\nuniform mat4 ObjMatrix;\nuniform vec3 DirLightDirection;\nvarying mediump vec2 Out_Tex0;\nvoid main() {\nvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\nvec3 WorldNormal = (ObjMatrix * vec4(Normal,0.0)).xyz;\n}\n";
      bool ok1 = World_PatchVertexSource(PS_FLAG_FOG, base.c_str(), o) && o.find("Out_ShadowPos = vec4((PS_LightVP * WorldPos).xyz") != std::string::npos;
      bool ok2 = !World_PatchVertexSource(PS_FLAG_FOG, (std::string("uniform highp vec4 Bones[40];\n") + base).c_str(), o);
      std::string many = base; for (int i = 0; i < 7; ++i) many = "varying mediump vec2 V" + std::to_string(i) + ";\n" + many;
      bool ok3 = !World_PatchVertexSource(PS_FLAG_FOG, many.c_str(), o);
      const std::string unlit = "precision highp float;\nuniform mat4 ObjMatrix;\nvarying mediump vec2 Out_Tex0;\nvoid main() {\nvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\n}\n";
      bool ok4 = World_PatchVertexSource(PS_FLAG_FOG, unlit.c_str(), o) && o.find("Out_ShadowPos.w") == std::string::npos;   // v1.3: unlit prelit shaders are receivers (no N.L)
      bool ok5 = !World_PatchVertexSource(PS_FLAG_FOG | PS_FLAG_SPHERE, unlit.c_str(), o);
      printf("%-30s patch=%d skinned-skipped=%d too-many-varyings-skipped=%d unlit-patched=%d sphere-skipped=%d\n", "vertex shadow rules", ok1, ok2, ok3, ok4, ok5);
      if (!ok1 || !ok2 || !ok3 || !ok4 || !ok5) ++bad; }
    for (auto& v : vs)
    {
        std::string orig = hdr + v.body, patched;
        bool changed = World_PatchPixelSource(v.flags, orig.c_str(), patched, true);
        bool o = compile(orig, "orig"), p = compile(patched, "patched");
        printf("%-30s changed=%d  original compiles=%d  patched compiles=%d\n", v.name, changed, o, p);
        if (!o || !p) ++bad;
        if (v.flags == 0 && changed) { printf("  ERROR: 2D shader was modified\n"); ++bad; }
    }

    // ---------------- driver self-test: a pair the driver rejects must step down, never reach the game ----------------
    {
        World_SetEnabled(true); World_SetCompileShadows(true); World_SetCompileCel(false);
        const std::string fs0 = "precision mediump float;\nvarying mediump float Out_FogAmt;\nuniform lowp vec3 FogColor;\nvarying lowp vec4 Out_Color;\nvoid main() {\nlowp vec4 fcolor;fcolor = Out_Color;\nfcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);\ngl_FragColor = fcolor;\n}\n";
        const std::string vsGood = "precision highp float;\nattribute vec3 Position;\nattribute vec3 Normal;\nuniform mat4 ObjMatrix;\nuniform mat4 ViewMatrix;\nuniform mat4 ProjMatrix;\nvarying mediump float Out_FogAmt;\nvarying lowp vec4 Out_Color;\nvoid main() {\nvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\nvec3 WorldNormal = (ObjMatrix * vec4(Normal,0.0)).xyz;\ngl_Position = ProjMatrix * (ViewMatrix * WorldPos);\nOut_FogAmt = 0.0;\nOut_Color = vec4(WorldNormal, 1.0);\n}\n";
        auto run = [&](const std::string& vs, std::string* pOut, std::string* vOut) {
            const char* pp = fs0.c_str(); const char* vv = vs.c_str();
            World_OnBuildSource(PS_FLAG_FOG, &pp, &vv);                   // what the BuildSource hook does (main thread)
            std::string P = pp, V = vv, p2, v2;                           // the strings the game would hand to ES2Shader::Build
            bool repl = World_OnShaderBuild(P.c_str(), V.c_str(), p2, v2);  // what the Build hook does (render thread)
            if (pOut) *pOut = repl ? p2 : P; if (vOut) *vOut = repl ? v2 : V; return repl;
        };
        int bad2 = 0; std::string P, V;
        bool r1 = run(vsGood, &P, &V);
        bool good = !r1 && V.find("Out_ShadowPos") != std::string::npos;
        printf("%-30s replaced=%d keeps-shadow-patch=%d\n", "self-test: healthy pair", r1, good); bad2 += !good;
        // N.L line the driver would reject: `max(0, 0.0)` has no overload -> the simplified vertex patch must be chosen
        const std::string vsNL = "#define dot(a,b) 0\n" + vsGood;
        bool r2 = run(vsNL, &P, &V);
        bool simp = r2 && V.find("Out_ShadowPos") != std::string::npos && V.find("Out_ShadowPos.w") == std::string::npos;
        printf("%-30s replaced=%d simplified-vertex=%d\n", "self-test: N.L rejected", r2, simp); bad2 += !simp;
        // the whole receiver rejected (duplicate declaration): fall back to the game's own vertex shader, no shadow varying
        const std::string vsDup = "uniform highp mat4 PS_LightVP;\n" + vsGood;
        bool r3 = run(vsDup, &P, &V);
        bool orig = r3 && V.find("Out_ShadowPos") == std::string::npos && P.find("Out_ShadowPos") == std::string::npos;
        printf("%-30s replaced=%d no-shadow=%d\n", "self-test: receiver rejected", r3, orig); bad2 += !orig;
        {   // another mod rewrote the strings after our hook (registry miss) but kept our text, and the driver rejects the receiver: strip it again
            const char* pp = fs0.c_str(); const char* vv = vsDup.c_str();
            World_OnBuildSource(PS_FLAG_FOG, &pp, &vv);
            std::string P2 = std::string(pp) + "\n// rewritten by another mod\n", V2 = std::string(vv) + "\n// rewritten by another mod\n", p3, v3;
            bool r4 = World_OnShaderBuild(P2.c_str(), V2.c_str(), p3, v3);
            bool st = r4 && v3.find("Out_ShadowPos") == std::string::npos && p3.find("Out_ShadowPos") == std::string::npos && p3.find("PS_A") != std::string::npos;
            printf("%-30s replaced=%d stripped=%d (cel/wet part kept)\n", "self-test: unknown pair", r4, st); bad2 += !st;
        }
        const PSBuildStats& bs = World_BuildStats();
        printf("%-30s tested=%d passed=%d simplified=%d noShadow=%d original=%d\n", "self-test counters", bs.tested, bs.passed, bs.simplified, bs.noShadow, bs.original);
        bad2 += !(bs.tested == 4 && bs.passed == 1 && bs.simplified == 1 && bs.noShadow == 2);
        bad += bad2;
    }
    printf(bad ? "RESULT: FAIL\n" : "RESULT: all patched shaders compile; 2D untouched\n");
    return bad;
}
