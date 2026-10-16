// Proper GFX Mobile - colour lookup texture (LUT) loader.
// Accepts PNG / BMP / TGA / JPG (any common variant: 24/32-bit, RLE TGA, BITFIELDS BMP, palette PNG ...) through stb_image.
// Layouts: horizontal strip N*N x N (256x16, 1024x32, 4096x64) and square tile atlases (64x64 = 16^3, 512x512 = 64^3, ...).
// Orientation is detected from the picture itself (green must grow inside every tile), so ReShade / Unity / PC Proper Shaders LUTs
// and the old Proper Shader 1.x example (green upside down) both work.
#pragma once
#include <GLES2/gl2.h>
#include <string>
#include <vector>

struct PSLutInfo
{
    bool  loaded = false;
    int   w = 0, h = 0, n = 0, tilesPerRow = 0;   // n = cube size (16 / 32 / 64)
    bool  flipped = false;                        // green was stored bottom-up (old Proper Shader example) and was turned around
    int   reloads = 0;
    char  file[160] = "";                         // file name that is loaded (or was tried last)
    char  error[160] = "";                        // why the last load failed (shown in the menu)
};

// All calls on the render thread with the GL context current.
// name: "lut" / "auto" / "" = first lut.png|bmp|tga|jpg found (any upper/lower case), otherwise a file name inside dir (or dir/luts).
bool  LUT_Load(const char* dir, const char* name);
void  LUT_Poll(const char* dir, const char* name, float dt);   // hot reload: file replaced / renamed / dropped in while the game runs
void  LUT_RequestReload();
GLuint LUT_Texture();
const PSLutInfo& LUT_Info();
void  LUT_ContextLost();                                      // GL objects died with the context
int   LUT_ListFiles(const char* dir, std::vector<std::string>& out);   // image files in dir and dir/luts (for the menu)

// pure CPU part (host tests): decode + validate + orient into an RGB buffer whose row r (GL row, v grows with r) has green growing with r
bool  LUT_Decode(const unsigned char* data, size_t size, std::vector<unsigned char>& rgb, PSLutInfo& info);
