// Proper GFX Mobile 1.4.1 (64-bit) - GLSL shaders BUILT INTO libProperGFX64.so.
// The shader sources are the .glsl files of the source tree (shaders/fragment, shaders/vertex - same layout as SAShaderL's GTASA/files/shaders);
// tools/glsl2h.py embeds them into ps_glsl_embedded.h at build time. Nothing is read from or written to the phone's files folder.
// "#pragma include "name.glsl"" pastes another file of the same stage folder (each file at most once per shader).
#pragma once
#include <string>
#include <vector>

struct PSGlslFile
{
    std::string path;          // "fragment/final.glsl"
    bool  failed = false;      // a program built from it did not compile/link on this GPU
    std::string error;         // first part of the driver error
};

void GLSL_Init(const char* filesDir);                  // removes the files/ProperGFX/shaders folder 1.4.0 wrote (not used any more)
bool GLSL_Source(const char* rel, std::string& out);   // full source with includes resolved
bool GLSL_Section(const char* rel, const char* section, std::string& out);   // "//@@name" ... next "//@@" (snippets injected into game shaders)
void GLSL_MarkFailed(const char* rel, const char* error);
void GLSL_ClearFailed();
const std::vector<PSGlslFile>& GLSL_Files();           // for the menu
