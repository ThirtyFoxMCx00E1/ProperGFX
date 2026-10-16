#include "ps_glsl.h"
#include "ps_glsl_embedded.h"
#include "ps_log.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <set>

namespace {
std::string g_dir;
std::vector<PSGlslFile> g_files;
std::recursive_mutex g_mu;
unsigned g_gen = 1;
float g_poll = 0.f;
bool g_reload = false;

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

bool StatFile(const std::string& p, long long& mt, long long& sz)
{
    struct stat st; if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    mt = (long long)st.st_mtime; sz = (long long)st.st_size; return true;
}

bool ReadFile(const std::string& p, std::string& out)
{
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 256 * 1024) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t rd = n ? fread(&out[0], 1, (size_t)n, f) : 0; fclose(f);
    if (rd != (size_t)n) return false;
    // strip a UTF-8 BOM and CRs (files edited on Windows / phone editors)
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3);
    std::string t; t.reserve(out.size()); for (char c : out) if (c != '\r') t += c; out.swap(t);
    return true;
}

// one file: user's copy unless builtinOnly / missing / marked failed
bool Raw(const char* rel, std::string& out, bool builtinOnly)
{
    PSGlslFile* e = Entry(rel);
    if (!builtinOnly && !e->failed)
    {
        std::string p = g_dir + "/" + rel;
        long long mt = 0, sz = 0;
        if (StatFile(p, mt, sz) && ReadFile(p, out)) { e->onDisk = true; e->mtime = mt; e->size = sz; return true; }
        e->onDisk = false;
    }
    const char* em = Embedded(rel);
    if (!em) return false;
    out = em;
    return true;
}

std::string StageOf(const char* rel) { const char* s = strchr(rel, '/'); return s ? std::string(rel, s - rel) : std::string(); }

bool Expand(const char* rel, std::string& out, bool builtinOnly, std::set<std::string>& seen, int depth)
{
    if (depth > 6) return false;
    if (seen.count(rel)) return true;           // include once
    seen.insert(rel);
    std::string src;
    if (!Raw(rel, src, builtinOnly)) { PS_Log("GLSL: %s not found (no file, no built-in copy)", rel); return false; }
    const std::string stage = StageOf(rel);
    size_t pos = 0;
    while (pos < src.size())
    {
        size_t eol = src.find('\n', pos); if (eol == std::string::npos) eol = src.size();
        std::string line = src.substr(pos, eol - pos);
        size_t a = line.find_first_not_of(" \t");
        if (a != std::string::npos && line.compare(a, 15, "#pragma include") == 0)
        {
            size_t q1 = line.find('"', a), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
            {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                std::string incRel = inc.find('/') != std::string::npos ? inc : stage + "/" + inc;
                if (!Expand(incRel.c_str(), out, builtinOnly, seen, depth + 1)) return false;
                pos = eol + 1; continue;
            }
        }
        out.append(line); out += '\n';
        pos = eol + 1;
    }
    return true;
}

void MkdirP(const std::string& p)
{
    for (size_t i = 1; i <= p.size(); ++i)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0777);
}
}   // namespace

void GLSL_Init(const char* dir)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    g_dir = dir ? dir : "";
    g_files.clear();
    for (int i = 0; i < kEmbeddedGlslCount; ++i) Entry(kEmbeddedGlsl[i].path);
    int n = GLSL_WriteDefaults(false);
    if (n) PS_Log("GLSL: wrote %d default shader files to %s", n, g_dir.c_str());
    for (auto& f : g_files) { long long mt, sz; f.onDisk = StatFile(g_dir + "/" + f.path, mt, sz); if (f.onDisk) { f.mtime = mt; f.size = sz; } }
}

const char* GLSL_Dir() { return g_dir.c_str(); }

bool GLSL_Source(const char* rel, std::string& out, bool builtinOnly)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    out.clear(); std::set<std::string> seen;
    return Expand(rel, out, builtinOnly, seen, 0);
}

bool GLSL_Section(const char* rel, const char* section, std::string& out, bool builtinOnly)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    std::string src;
    if (!Raw(rel, src, builtinOnly)) return false;
    std::string mark = std::string("//@@") + section + "\n";
    size_t a = src.find(mark);
    if (a == std::string::npos) return false;
    a += mark.size();
    size_t b = src.find("//@@", a);
    out = src.substr(a, b == std::string::npos ? std::string::npos : b - a);
    return true;
}

void GLSL_MarkFailed(const char* rel, const char* error)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    PSGlslFile* e = Entry(rel);
    e->failed = true;
    e->error = error ? error : "";
    if (e->error.size() > 300) e->error.resize(300);
    PS_Log("GLSL: %s failed on this GPU, using the built-in copy: %.300s", rel, e->error.c_str());
    PS_FileLog("GLSL: %s failed, built-in copy used instead.\nDRIVER: %s", rel, e->error.c_str());
}

void GLSL_ClearFailed()
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    for (auto& f : g_files) { f.failed = false; f.error.clear(); }
}

int GLSL_WriteDefaults(bool overwrite)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    if (g_dir.empty()) return 0;
    int n = 0;
    for (int i = 0; i < kEmbeddedGlslCount; ++i)
    {
        std::string p = g_dir + "/" + kEmbeddedGlsl[i].path;
        long long mt, sz;
        if (!overwrite && StatFile(p, mt, sz)) continue;
        MkdirP(p.substr(0, p.rfind('/')));
        FILE* f = fopen(p.c_str(), "wb"); if (!f) continue;
        fputs(kEmbeddedGlsl[i].text, f); fclose(f); ++n;
    }
    if (overwrite && n) { for (auto& f : g_files) { f.failed = false; f.error.clear(); } ++g_gen; }
    return n;
}

void GLSL_RequestReload() { g_reload = true; }

bool GLSL_PollChanges(float dt)
{
    std::lock_guard<std::recursive_mutex> l(g_mu);
    if (g_reload) { g_reload = false; for (auto& f : g_files) { f.failed = false; f.error.clear(); } ++g_gen; return true; }
    g_poll += dt;
    if (g_poll < 1.5f) return false;
    g_poll = 0.f;
    bool changed = false;
    for (auto& f : g_files)
    {
        long long mt = 0, sz = 0;
        bool on = StatFile(g_dir + "/" + f.path, mt, sz);
        if (on != f.onDisk || (on && (mt != f.mtime || sz != f.size))) { changed = true; f.onDisk = on; f.mtime = mt; f.size = sz; f.failed = false; f.error.clear(); }
    }
    if (changed) ++g_gen;
    return changed;
}

unsigned GLSL_Generation() { return g_gen; }
const std::vector<PSGlslFile>& GLSL_Files() { return g_files; }
