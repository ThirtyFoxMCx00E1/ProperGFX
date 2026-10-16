#include "ps_lut.h"
#include "ps_log.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <algorithm>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"

namespace {
GLuint    g_tex = 0;
PSLutInfo g_info;
char      g_path[700] = "";        // resolved path of the loaded / last tried file
char      g_name[160] = "";        // setting value the current state was resolved from
long long g_mtime = 0; long long g_size = -1;
float     g_poll = 0.f;
bool      g_reload = false;

const char* kExt[] = { ".png", ".bmp", ".tga", ".jpg", ".jpeg", nullptr };

bool HasImageExt(const char* n)
{
    const char* d = strrchr(n, '.');
    if (!d) return false;
    for (int i = 0; kExt[i]; ++i) if (!strcasecmp(d, kExt[i])) return true;
    return false;
}

// case-insensitive lookup of `want` inside `dir` (Android storage is case-sensitive: "Lut.bmp" != "lut.bmp")
bool FindInDir(const char* dir, const char* want, char* out, size_t n)
{
    DIR* d = opendir(dir);
    if (!d) return false;
    bool ok = false;
    while (dirent* e = readdir(d)) if (!strcasecmp(e->d_name, want)) { snprintf(out, n, "%s/%s", dir, e->d_name); ok = true; break; }
    closedir(d);
    return ok;
}

bool Resolve(const char* dir, const char* name, char* out, size_t n)
{
    char luts[600]; snprintf(luts, sizeof(luts), "%s/luts", dir);
    const bool autoName = !name || !name[0] || !strcasecmp(name, "auto") || !strcasecmp(name, "lut");
    if (autoName)
    {
        for (int i = 0; kExt[i]; ++i)
        {
            char f[32]; snprintf(f, sizeof(f), "lut%s", kExt[i]);
            if (FindInDir(dir, f, out, n) || FindInDir(luts, f, out, n)) return true;
        }
        return false;
    }
    if (FindInDir(dir, name, out, n) || FindInDir(luts, name, out, n)) return true;
    if (!strchr(name, '.'))
        for (int i = 0; kExt[i]; ++i)
        {
            char f[200]; snprintf(f, sizeof(f), "%s%s", name, kExt[i]);
            if (FindInDir(dir, f, out, n) || FindInDir(luts, f, out, n)) return true;
        }
    return false;
}

bool Stat(const char* p, long long& mt, long long& sz)
{
    struct stat st;
    if (stat(p, &st) != 0) return false;
    mt = (long long)st.st_mtime; sz = (long long)st.st_size;
    return true;
}

const char* BaseName(const char* p) { const char* s = strrchr(p, '/'); return s ? s + 1 : p; }
}   // namespace

bool LUT_Decode(const unsigned char* data, size_t size, std::vector<unsigned char>& rgb, PSLutInfo& info)
{
    info.error[0] = 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 3);
    if (!px) { snprintf(info.error, sizeof(info.error), "cannot decode the image (%s)", stbi_failure_reason() ? stbi_failure_reason() : "unknown format"); return false; }
    // cube size: w*h = n^3 with whole tiles
    int n = (int)lround(cbrt((double)w * (double)h));
    if (n < 2 || n > 256 || (long long)n * n * n != (long long)w * h || w % n || h % n)
    {
        snprintf(info.error, sizeof(info.error), "%dx%d is not a LUT size (use 256x16, 1024x32, 4096x64 strips or 64x64 / 512x512 squares)", w, h);
        stbi_image_free(px); return false;
    }
    const int tpr = w / n, rows = h / n;
    if (tpr * rows != n) { snprintf(info.error, sizeof(info.error), "%dx%d: tile count does not match the cube size %d", w, h, n); stbi_image_free(px); return false; }
    // orientation: in every tile green grows from one row to the other. Standard (ReShade/Unity/PC) = top row green 0.
    // The old Proper Shader 1.x example had green 0 at the BOTTOM: detected here and turned around, so both work.
    long long top = 0, bot = 0;
    for (int x = 0; x < n; ++x) { top += px[((size_t)0 * w + x) * 3 + 1]; bot += px[((size_t)(n - 1) * w + x) * 3 + 1]; }
    const bool flipped = top > bot;
    rgb.resize((size_t)w * h * 3);
    for (int tr = 0; tr < rows; ++tr)
        for (int r = 0; r < n; ++r)
        {
            const int srcRow = tr * n + (flipped ? (n - 1 - r) : r);
            const int dstRow = tr * n + r;                  // GL row 0 = first uploaded row = v ~ 0 = green 0, first tile row = blue 0..
            memcpy(&rgb[(size_t)dstRow * w * 3], &px[(size_t)srcRow * w * 3], (size_t)w * 3);
        }
    stbi_image_free(px);
    info.w = w; info.h = h; info.n = n; info.tilesPerRow = tpr; info.flipped = flipped;
    return true;
}

bool LUT_Load(const char* dir, const char* name)
{
    snprintf(g_name, sizeof(g_name), "%s", name ? name : "");
    PSLutInfo ni; ni.reloads = g_info.reloads;
    char path[700];
    if (!Resolve(dir, name, path, sizeof(path)))
    {
        g_path[0] = 0; g_mtime = 0; g_size = -1;
        snprintf(ni.file, sizeof(ni.file), "%s", (!name || !name[0] || !strcasecmp(name, "auto") || !strcasecmp(name, "lut")) ? "lut.png/.bmp/.tga/.jpg" : name);
        snprintf(ni.error, sizeof(ni.error), "file not found in files/ProperGFX (or files/ProperGFX/luts)");
        // keep an already loaded texture? No: the user removed / renamed it, so the LUT must switch off
        ni.loaded = false; g_info = ni;
        return false;
    }
    snprintf(g_path, sizeof(g_path), "%s", path);
    Stat(path, g_mtime, g_size);
    snprintf(ni.file, sizeof(ni.file), "%s", BaseName(path));
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(ni.error, sizeof(ni.error), "cannot open the file"); g_info = ni; return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 32 || sz > 32 * 1024 * 1024) { fclose(f); snprintf(ni.error, sizeof(ni.error), "file size %ld is not valid", sz); g_info = ni; return false; }
    std::vector<unsigned char> d((size_t)sz);
    size_t rd = fread(d.data(), 1, (size_t)sz, f); fclose(f);
    if (rd != (size_t)sz) { snprintf(ni.error, sizeof(ni.error), "read error"); g_info = ni; return false; }
    std::vector<unsigned char> rgb;
    if (!LUT_Decode(d.data(), d.size(), rgb, ni)) { PS_Log("LUT %s: %s", ni.file, ni.error); g_info = ni; return false; }
    GLint maxTex = 2048; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if (ni.w > maxTex || ni.h > maxTex) { snprintf(ni.error, sizeof(ni.error), "%dx%d is larger than this GPU allows (%d)", ni.w, ni.h, maxTex); g_info = ni; return false; }
    while (glGetError() != GL_NO_ERROR) {}
    if (!g_tex) glGenTextures(1, &g_tex);
    GLint prevUnit = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
    glActiveTexture(GL_TEXTURE0);
    GLint prev = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    GLint pa = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &pa);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ni.w, ni.h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, pa);
    glBindTexture(GL_TEXTURE_2D, prev);
    glActiveTexture((GLenum)prevUnit);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { snprintf(ni.error, sizeof(ni.error), "GPU rejected the texture (GL error 0x%x)", err); g_info = ni; return false; }
    ni.loaded = true; ++ni.reloads;
    g_info = ni;
    PS_Log("LUT loaded: %s %dx%d (cube %d, %s)", ni.file, ni.w, ni.h, ni.n, ni.flipped ? "green bottom-up, turned around" : "standard orientation");
    return true;
}

void LUT_Poll(const char* dir, const char* name, float dt)
{
    g_poll += dt;
    static char lastDir[700] = "";
    const bool dirChanged = strcmp(lastDir, dir ? dir : "") != 0;          // [LUT] Folders changed
    if (dirChanged) snprintf(lastDir, sizeof(lastDir), "%s", dir ? dir : "");
    const bool nameChanged = strcmp(g_name, name ? name : "") != 0 || dirChanged;
    if (!g_reload && !nameChanged && g_poll < 1.0f) return;
    g_poll = 0.f;
    if (g_reload || nameChanged) { g_reload = false; LUT_Load(dir, name); return; }
    if (g_path[0])
    {   // the loaded (or tried) file was replaced / edited / deleted
        long long mt = 0, sz = -1;
        if (!Stat(g_path, mt, sz) || mt != g_mtime || sz != g_size) LUT_Load(dir, name);
        return;
    }
    char p[700];
    if (Resolve(dir, name, p, sizeof(p))) LUT_Load(dir, name);       // a LUT was dropped into the folder while the game runs
}

void LUT_RequestReload() { g_reload = true; }
GLuint LUT_Texture() { return g_info.loaded ? g_tex : 0; }
const PSLutInfo& LUT_Info() { return g_info; }
void LUT_ContextLost() { g_tex = 0; g_info.loaded = false; g_name[0] = 1; g_name[1] = 0; /* forces a reload on the next poll */ }

int LUT_ListFiles(const char* dir, std::vector<std::string>& out)
{
    out.clear();
    for (int pass = 0; pass < 2; ++pass)
    {
        char d[600]; snprintf(d, sizeof(d), pass ? "%s/luts" : "%s", dir);
        DIR* dd = opendir(d); if (!dd) continue;
        while (dirent* e = readdir(dd))
        {
            if (e->d_name[0] == '.' || !HasImageExt(e->d_name)) continue;
            out.emplace_back(e->d_name);
        }
        closedir(dd);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return (int)out.size();
}
