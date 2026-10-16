// Proper GFX Mobile 1.4 (64-bit) - GLSL file loader.
// Every shader of the 64-bit build is a .glsl file in files/ProperGFX/shaders/{vertex,fragment}/ (same layout as SAShaderL's files/shaders).
// The files are written on the first launch (like the .ini); edit them and press "Reload shaders" in the menu (or enable Hot reload).
// A copy of every file is built into the .so: a missing file uses the built-in copy, a file that does not compile is reported and the
// built-in copy is used instead, so an edit can never break the game.
// "#pragma include "name.glsl"" pastes another file of the same stage folder (each file at most once per shader).
#pragma once
#include <string>
#include <vector>

struct PSGlslFile
{
    std::string path;          // "fragment/final.glsl"
    bool  onDisk = false;      // the user's file exists (otherwise the built-in copy is used)
    bool  failed = false;      // the user's file did not compile/link: built-in copy in use
    long long mtime = 0, size = 0;
    std::string error;         // first line(s) of the driver error for this file
};

void GLSL_Init(const char* shaderDir);                 // files/ProperGFX/shaders; writes every missing default file
const char* GLSL_Dir();
// Full source of a stage file with includes resolved. builtinOnly = ignore the user's files (fallback after a compile error).
bool GLSL_Source(const char* rel, std::string& out, bool builtinOnly = false);
// Raw section of a file ("//@@name" ... next "//@@"), used for the snippets injected into the game's own shaders.
bool GLSL_Section(const char* rel, const char* section, std::string& out, bool builtinOnly = false);
void GLSL_MarkFailed(const char* rel, const char* error);   // a program built from rel failed -> remembered for the menu
void GLSL_ClearFailed();
int  GLSL_WriteDefaults(bool overwrite);               // returns the number of files written
bool GLSL_PollChanges(float dt);                       // hot reload: true when a file changed on disk (checked every ~1.5 s)
void GLSL_RequestReload();
unsigned GLSL_Generation();                            // bumped on every reload -> post-process programs are rebuilt
const std::vector<PSGlslFile>& GLSL_Files();           // for the menu
