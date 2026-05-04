// Analysed: 2026-05-04T00:00
// FileSystem_Direct Win32 implementation.
// This translation unit is guarded so it emits no symbols on POSIX.
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "asset/FileSystem_Direct.h"
#include "asset/IFile_Direct.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace Mortar {

namespace {

static std::string FindEntryCI(const std::string& dirPath, const std::string& target) {
    std::string pattern = dirPath.empty() ? (".\\" + target) : (dirPath + "\\*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::string match;
    do {
        if (_stricmp(fd.cFileName, target.c_str()) == 0) {
            match = fd.cFileName;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return match;
}

static bool ExistsOnDisk(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

struct SplitPath {
    std::string root;
    std::vector<std::string> parts;
};

static SplitPath SplitPathParts(const char* path) {
    SplitPath out;
    if (!path || !*path) return out;

    std::string p(path);
    for (size_t ci = 0; ci < p.size(); ++ci) if (p[ci] == '\\') p[ci] = '/';

    size_t i = 0;
    if (p.size() >= 2 && p[1] == ':') {
        out.root = p.substr(0, 2) + "/";
        i = (p.size() >= 3 && p[2] == '/') ? 3 : 2;
    } else if (p[0] == '/') {
        out.root = "/";
        i = 1;
    }

    std::string cur;
    while (i < p.size()) {
        if (p[i] == '/') {
            if (!cur.empty()) { out.parts.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(p[i]);
        }
        ++i;
    }
    if (!cur.empty()) out.parts.push_back(cur);
    return out;
}

static std::string ToOpenableDir(const std::string& prefix) {
    if (prefix.empty()) return ".";
    if (prefix == "/") return prefix;
    if (prefix.size() == 3 && prefix[1] == ':' && prefix[2] == '/') return prefix;
    if (!prefix.empty() && prefix[prefix.size() - 1] == '/') return prefix.substr(0, prefix.size() - 1);
    return prefix;
}

static std::string ResolveCI(const char* path) {
    SplitPath sp = SplitPathParts(path);
    if (sp.parts.empty() && sp.root.empty()) return std::string();

    std::string built = sp.root;
    for (size_t i = 0; i < sp.parts.size(); ++i) {
        const std::string& part = sp.parts[i];
        if (part == "." || part.empty()) continue;
        if (part == "..") { built += "../"; continue; }

        std::string candidate = built + part;
        if (ExistsOnDisk(candidate.c_str())) {
            built = candidate + "/";
            continue;
        }

        std::string real = FindEntryCI(ToOpenableDir(built), part);
        if (real.empty()) return std::string();
        built += real;
        built += '/';
    }

    if (!sp.parts.empty() && !built.empty() && built[built.size() - 1] == '/') {
        built.erase(built.size() - 1);
    }
    return built;
}

} // namespace

// ---- FileSystem_Direct ctor / dtor ----

FileSystem_Direct::FileSystem_Direct()
    : m_rootPath(nullptr)
    , m_isWritable(false)
{
}

FileSystem_Direct::~FileSystem_Direct() {
    free(m_rootPath);
    m_rootPath = nullptr;
}

// Binary @ FileSystem_Direct vtable+0x18 — slot 6
void FileSystem_Direct::TranslateFileName(const char* in, char* out) {
    if (!out) return;
    if (!in) { out[0] = '\0'; return; }
    if (!m_rootPath || m_rootPath[0] == '\0') {
        strcpy(out, in);
        return;
    }
    size_t rlen = strlen(m_rootPath);
    size_t ilen = strlen(in);
    memcpy(out, m_rootPath, rlen);
    if (rlen > 0 && m_rootPath[rlen - 1] != '/' && m_rootPath[rlen - 1] != '\\') {
        out[rlen++] = '/';
    }
    memcpy(out + rlen, in, ilen + 1);
}

// Binary @ FileSystem_Direct vtable+0x1c — slot 7
void FileSystem_Direct::Initialise(const char* root, bool writable) {
    free(m_rootPath);
    m_rootPath = root ? _strdup(root) : _strdup("");
    m_isWritable = writable;
}

// Binary @ IFileSystem vtbl+0x08 (FileSystem_Direct override)
bool FileSystem_Direct::FileExists(const char* name) {
    if (!name) return false;
    char buf[4096];
    TranslateFileName(name, buf);
    if (ExistsOnDisk(buf)) return true;
    std::string real = ResolveCI(buf);
    return !real.empty();
}

// Binary @ IFileSystem vtbl+0x0c (FileSystem_Direct override)
unsigned int FileSystem_Direct::FileSize(const char* name) {
    if (!name) return 0;
    char buf[4096];
    TranslateFileName(name, buf);
    struct stat st;
    if (stat(buf, &st) != 0) {
        std::string real = ResolveCI(buf);
        if (real.empty()) return 0;
        if (stat(real.c_str(), &st) != 0) return 0;
    }
    return (unsigned int)st.st_size;
}

// Binary @ IFileSystem vtbl+0x10 (FileSystem_Direct override)
bool FileSystem_Direct::GetFileData(const char* name, void** outBuf,
                                     unsigned long* outSize, bool& outOwned) {
    if (!name || !outBuf || !outSize) return false;
    char buf[4096];
    TranslateFileName(name, buf);

    FILE* fp = fopen(buf, "rb");
    if (!fp) {
        std::string real = ResolveCI(buf);
        if (real.empty()) return false;
        fp = fopen(real.c_str(), "rb");
        if (!fp) return false;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return false; }

    unsigned char* data = new unsigned char[sz];
    if ((long)fread(data, 1, (size_t)sz, fp) != sz) {
        delete[] data;
        fclose(fp);
        return false;
    }
    fclose(fp);

    *outBuf  = data;
    *outSize = (unsigned long)sz;
    outOwned = true;
    return true;
}

// Binary @ IFileSystem vtbl+0x14 (FileSystem_Direct override)
IFile* FileSystem_Direct::OpenFile(const char* name, unsigned long flags) {
    if (!name) return nullptr;
    char buf[4096];
    TranslateFileName(name, buf);

    const char* mode = (flags & 1) ? ((flags & 2) ? "r+b" : "wb") : "rb";

    FILE* fp = fopen(buf, mode);
    if (!fp) {
        std::string real = ResolveCI(buf);
        if (real.empty()) return nullptr;
        fp = fopen(real.c_str(), mode);
        if (!fp) return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    unsigned long size = (unsigned long)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    return new IFile_Direct(this, fp, size);
}

} // namespace Mortar

#endif // _WIN32
