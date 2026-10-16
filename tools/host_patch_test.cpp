// Regression test for the Mali-G57 log of 1.4.1: the game's shaders are ONE line; the shadow receiver must still start its
// #ifdef on a new line. Patches the exact game sources from that log and writes them out; tools/patch_check.sh runs glslang on them.
// build: g++ -std=c++17 -I../srcXX -I../third_party host_patch_test.cpp ../srcXX/ps_world.cpp ../srcXX/ps_shadow.cpp ../srcXX/ps_settings.cpp ../srcXX/ps_glstate.cpp [64: ../src64/ps_glsl.cpp ../src64/ps_fx.cpp ../src64/ps_lut.cpp] -lEGL -lGLESv2
#include <stdio.h>
#include <string>
#include "ps_world.h"
#ifdef PGFX64
#include "ps_glsl.h"
#endif
static const char* kV40a = "#version 100\nprecision highp float;uniform mat4 ProjMatrix;uniform mat4 ViewMatrix;uniform mat4 ObjMatrix;uniform lowp vec3 AmbientLightColor;uniform lowp vec4 MaterialEmissive;uniform lowp vec4 MaterialAmbient;uniform lowp vec4 MaterialDiffuse;attribute vec3 Position;attribute vec3 Normal;attribute vec4 GlobalColor;uniform vec3 CameraPosition;varying mediump float Out_FogAmt;uniform vec3 FogDistances;varying lowp vec4 Out_Color;void main() {\tvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\tvec4 ViewPos = ViewMatrix * WorldPos;\tgl_Position = ProjMatrix * ViewPos;vec3 WorldNormal = (ObjMatrix * vec4(Normal,0.0)).xyz;Out_FogAmt = clamp((length(WorldPos.xyz - CameraPosition.xyz) - FogDistances.x) * FogDistances.z, 0.0, 1.00);vec3 Out_LightingColor;Out_LightingColor = AmbientLightColor * MaterialAmbient.xyz + GlobalColor.xyz;Out_Color = vec4(Out_LightingColor * MaterialDiffuse.xyz, MaterialAmbient.w * GlobalColor.w);Out_Color = clamp(Out_Color, 0.0, 1.0);}";
static const char* kV430 = "#version 100\nprecision highp float;uniform mat4 ProjMatrix;uniform mat4 ViewMatrix;uniform mat4 ObjMatrix;attribute vec3 Position;attribute vec3 Normal;attribute vec2 TexCoord0;attribute vec4 GlobalColor;varying mediump vec2 Out_Tex0;uniform vec3 CameraPosition;varying mediump float Out_FogAmt;uniform vec3 FogDistances;varying lowp vec4 Out_Color;void main() {\tvec4 WorldPos = ObjMatrix * vec4(Position,1.0);\tvec4 ViewPos = ViewMatrix * WorldPos;\tgl_Position = ProjMatrix * ViewPos;Out_FogAmt = clamp((length(WorldPos.xyz - CameraPosition.xyz) - FogDistances.x) * FogDistances.z, 0.0, 1.00);Out_Tex0 = TexCoord0;Out_Color = GlobalColor;}";
static const char* kP40a = "precision mediump float;varying mediump float Out_FogAmt;uniform lowp vec3 FogColor;varying lowp vec4 Out_Color;void main(){lowp vec4 fcolor;fcolor = Out_Color;fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);gl_FragColor = fcolor;}";
static const char* kP431 = "precision mediump float;uniform sampler2D Diffuse;varying mediump vec2 Out_Tex0;varying mediump float Out_FogAmt;uniform lowp vec3 FogColor;varying lowp vec4 Out_Color;void main(){lowp vec4 fcolor;lowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0, -0.5);fcolor = diffuseColor;fcolor *= Out_Color;fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);gl_FragColor = fcolor;/*ATBEGIN*/if (gl_FragColor.a < 0.2) { discard; }/*ATEND*/}";
static const char* kP434 = "precision mediump float;uniform sampler2D Diffuse;varying mediump vec2 Out_Tex0;varying mediump float Out_FogAmt;uniform lowp vec3 FogColor;varying lowp vec4 Out_Color;uniform lowp float AlphaModulate;void main(){lowp vec4 fcolor;lowp vec4 diffuseColor = texture2D(Diffuse, Out_Tex0, -0.5);fcolor = diffuseColor;fcolor *= Out_Color;fcolor.xyz = mix(fcolor.xyz, FogColor, Out_FogAmt);gl_FragColor = fcolor;gl_FragColor.a *= AlphaModulate;}";
static bool dump(const char* path, const std::string& s, bool addVersion) { FILE* f = fopen(path, "w"); if (!f) return false; if (addVersion) fputs("#version 100\n", f); fputs(s.c_str(), f); fclose(f); return true; }
int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : ".";
#ifdef PGFX64
    GLSL_Init(""); World_LoadSnippets();
#endif
    int bad = 0; char p[512]; std::string o;
    struct { unsigned flags; const char* src; const char* name; } px[] = { { 0x40a, kP40a, "p40a" }, { 0x431, kP431, "p431" }, { 0x434, kP434, "p434" } };
    for (auto& t : px)
    {
        bool sh = false;
        if (!World_PatchPixelSource(t.flags, t.src, o, true, true, &sh) || !sh) { printf("FAIL %s not patched with shadows\n", t.name); ++bad; continue; }
        snprintf(p, sizeof(p), "%s/%s.frag", out, t.name); dump(p, o, true);   // the game's pixel shaders have no #version: glslang needs one (line 1), the patch starts later
    }
    struct { unsigned flags; const char* src; const char* name; } vx[] = { { 0x40a, kV40a, "v40a" }, { 0x430, kV430, "v430" } };
    for (auto& t : vx)
    {
        if (!World_PatchVertexSource(t.flags, t.src, o)) { printf("FAIL %s not patched\n", t.name); ++bad; continue; }
        snprintf(p, sizeof(p), "%s/%s.vert", out, t.name); dump(p, o, false);
    }
    printf("host_patch_test: %d failures\n", bad);
    return bad ? 1 : 0;
}
