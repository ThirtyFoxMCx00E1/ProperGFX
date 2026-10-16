#include "ps_glsl.h"
#include "ps_glsl_embedded.h"
#include "ps_log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <set>

namespace {
std::vector<PSGlslFile> g_files;
std::recursive_mutex g_mu;

const char* Embedded(const char* rel)
{
    for (int i = 0; i < kEmbeddedGlslCount; ++i) if (!strcmp(kEmbeddedGlsl[i].path, rel)) return kEmbeddedGlsl[i].text;
    return nullptr;
}
PSGlslFile* Entry(const char* rel)
{
    for (auto& f : g_files) if (f.path == rel) return &f;
    PSGlslFile f; f.path = rel; g_files.push_back(f);
    return &g_files.back();
}
bool Expand(const char* rel, std::string& out, std::set<std::string>& seen, int depth)
{
    if (depth > 6) return false;
    if (seen.count(rel)) return true;           // include once
    seen.insert(rel);
    const char* src = Embedded(rel);
    if (!src) { PS_Log("GLSL: %s is not built into the mod", rel); return false; }
    std::string stage(rel, strchr(rel, '/') ? strchr(rel, '/') - rel : 0);
    const char* p = src;
    while (*p)
    {
        const char* e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
        std::string line(p, n);
        size_t a = line.find_first_not_of(" \t");
        if (a != std::string::npos && line.compare(a, 15, "#pragma include") == 0)
        {
            size_t q1 = line.find('"', a), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q2 != std::string::npos)
            {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                std::string incRel = inc.find('/') != std::string::npos ? inc : stage + "/" + inc;
                if (!Expand(incRel.c_str(), out, seen, depth + 1)) return false;
                p += n; if (*p) ++p; continue;
            }
        }
        out.append(line); out += '\n';
        p += n; if (*p) ++p;
    }
    return true;
}
}   // namespace

void GLSL_Init(const char* filesDir)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    g_files.clear();
    for (int i = 0; i < kEmbeddedGlslCount; ++i) Entry(kEmbeddedGlsl[i].path);
    // 1.4.0 wrote copies to files/ProperGFX/shaders and read them back from there - that folder is not used any more: remove our files
    if (!filesDir || !filesDir[0]) return;
    char p[700]; int n = 0;
    for (int i = 0; i < kEmbeddedGlslCount; ++i)
    {
        snprintf(p, sizeof(p), "%s/shaders/%s", filesDir, kEmbeddedGlsl[i].path);
        if (unlink(p) == 0) ++n;
    }
    snprintf(p, sizeof(p), "%s/shaders/fragment", filesDir); rmdir(p);
    snprintf(p, sizeof(p), "%s/shaders/vertex", filesDir); rmdir(p);
    snprintf(p, sizeof(p), "%s/shaders", filesDir); rmdir(p);
    if (n) PS_Log("GLSL: removed %d old shader copies from %s/shaders (shaders are built into the mod now)", n, filesDir);
}

bool GLSL_Source(const char* rel, std::string& out)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    out.clear(); std::set<std::string> seen;
    return Expand(rel, out, seen, 0);
}

bool GLSL_Section(const char* rel, const char* section, std::string& out)
{
    const char* src = Embedded(rel);
    if (!src) return false;
    std::string s(src), mark = std::string("//@@") + section + "\n";
    size_t a = s.find(mark);
    if (a == std::string::npos) return false;
    a += mark.size();
    size_t b = s.find("//@@", a);
    out = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
    return true;
}

void GLSL_MarkFailed(const char* rel, const char* error)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    PSGlslFile* e = Entry(rel);
    e->failed = true; e->error = error ? error : "";
    if (e->error.size() > 300) e->error.resize(300);
    PS_Log("GLSL: %s failed on this GPU: %.300s", rel, e->error.c_str());
    PS_FileLog("GLSL: %s failed on this GPU.\nDRIVER: %s", rel, e->error.c_str());
}
void GLSL_ClearFailed() { std::lock_guard<std::recursive_mutex> l(g_mu); for (auto& f : g_files) { f.failed = false; f.error.clear(); } }
const std::vector<PSGlslFile>& GLSL_Files() { return g_files; }
