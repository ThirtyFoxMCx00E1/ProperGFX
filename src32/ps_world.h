// Proper GFX Mobile - patches for the game's own object shaders (fog, specular, reflections, water, wet, cel)
#pragma once
#include <string>
#include "ps_settings.h"

#define PS_FLAG_LIGHTING 0x02u
#define PS_FLAG_ENVMAP   0x40u
#define PS_FLAG_FOG      0x400u
#define PS_FLAG_WATER    0x80000u
#define PS_FLAG_SPHERE   0x800000u      // vehicle reflection sphere-map pass (never a shadow receiver)

// Pure text transform (no GL): returns true when the source was modified.
bool World_PatchPixelSource(unsigned flags, const char* src, std::string& out, bool withCel = false, bool withShadow = false, bool* shadowApplied = nullptr);
bool World_PatchVertexSource(unsigned flags, const char* src, std::string& out, bool withNL = true);     // adds the Out_ShadowPos varying (needs only the game's WorldPos; uses WorldNormal for N.L when the shader has real normals)

// Called from the BuildSource hook (any thread). Leaves *pxl pointing at a per-thread patched buffer.
void World_OnBuildSource(unsigned flags, const char** pxl, const char** vtx);
int  World_PatchedCount();
int  World_ShadowReceiverCount();
// Driver self-test (render thread, GL context current). Every patched shader pair is compiled and linked once on the real driver BEFORE the game builds it; the game
// aborts the whole process when a shader fails to build, so a pair the driver rejects is stepped down: simpler shadow patch -> no shadow patch -> the game's own shader.
// Returns true and fills outPxl/outVtx when the sources must be replaced.
bool World_OnShaderBuild(const char* pxl, const char* vtx, std::string& outPxl, std::string& outVtx);
bool World_StripShadowPatch(std::string& pxl, std::string& vtx);   // removes exactly the shadow-receiver text we inserted
struct PSBuildStats { int tested = 0, passed = 0, simplified = 0, noShadow = 0, original = 0, shadowDisabled = 0; };
const PSBuildStats& World_BuildStats();           // object shaders that were compiled with the shadow lookup
void World_SetEnabled(bool on);
void World_SetCompileCel(bool on);
void World_SetCompileShadows(bool on);      // shadow receiver code is compiled into shaders only when requested          // cel code is only compiled into shaders when requested (zero cost otherwise)

// Render thread only
void World_BeginFrame(const PSSettings& s, float wetRoads);   // recompute the uniform block (once per frame)
void World_OnShaderSelected();                                 // call right after the game's glUseProgram
void World_ClearProgramCache();
