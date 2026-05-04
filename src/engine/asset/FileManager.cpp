// Analysed: 2026-05-04T00:00
#include "asset/FileManager.h"
#include "asset/IFileSystem.h"
#include "asset/IFile.h"

#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>          // FindFirstFileA / FindNextFileA
  #define FN_STRCASECMP _stricmp
#elif !defined(__arm__)
  #include <dirent.h>
  #include <strings.h>          // strcasecmp (POSIX)
  #define FN_STRCASECMP strcasecmp
#else
  // Cross-build target (arm-none-eabi) — case-insensitive cmp not exercised
  #define FN_STRCASECMP strcmp
#endif

namespace {

struct SplitPath {
    std::string root;
    std::vector<std::string> parts;
};

SplitPath SplitPathParts(const char* path) {
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
        i++;
    }
    if (!cur.empty()) out.parts.push_back(cur);
    return out;
}

std::string FindEntryCI(const std::string& dirPath, const std::string& target) {
    const std::string dir = dirPath.empty() ? "." : dirPath;
    std::string match;
#ifdef _WIN32
    std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    do {
        if (FN_STRCASECMP(fd.cFileName, target.c_str()) == 0) {
            match = fd.cFileName;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#elif !defined(__arm__)
    DIR* d = opendir(dir.c_str());
    if (!d) return std::string();
    while (struct dirent* ent = readdir(d)) {
        if (FN_STRCASECMP(ent->d_name, target.c_str()) == 0) {
            match = ent->d_name;
            break;
        }
    }
    closedir(d);
#endif
    return match;
}

bool ExistsAsFile(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (st.st_mode & S_IFREG) != 0 || (st.st_mode & S_IFDIR) != 0;
}

std::string ToOpenableDir(const std::string& prefix) {
    if (prefix.empty()) return ".";
    if (prefix == "/") return prefix;
    if (prefix.size() == 3 && prefix[1] == ':' && prefix[2] == '/') return prefix;
    if (!prefix.empty() && prefix[prefix.size() - 1] == '/') return prefix.substr(0, prefix.size() - 1);
    return prefix;
}

std::string ResolveCI(const char* path) {
    SplitPath sp = SplitPathParts(path);
    if (sp.parts.empty() && sp.root.empty()) return std::string();

    std::string built = sp.root;
    for (std::vector<std::string>::const_iterator pit = sp.parts.begin(); pit != sp.parts.end(); ++pit) {
        const std::string& part = *pit;
        if (part == "." || part.empty()) continue;
        if (part == "..") {
            built += "..";
            built += '/';
            continue;
        }

        std::string candidate = built + part;
        if (ExistsAsFile(candidate.c_str())) {
            built = candidate + "/";
            continue;
        }

        std::string real = FindEntryCI(ToOpenableDir(built), part);
        if (real.empty()) return std::string();
        built += real;
        built += '/';
    }

    if (sp.parts.size() > 0 && !built.empty() && built[built.size() - 1] == '/') {
        built.erase(built.size() - 1);
    }
    return built;
}

} // namespace

using namespace Mortar;

// ---- FileManager registry methods ----

// Binary @ 0x0019b170 -- sets sys->m_systemId / m_priority then sorted insert (descending priority)
void FileManager::AddSystem(IFileSystem* sys, unsigned int id, int priority) {
    if (!sys) return;
    sys->m_systemId  = id;
    sys->m_priority  = priority;

    // Insert in descending priority order (higher priority = earlier in list)
    std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
    while (it != m_FileSystems.end() && (*it)->m_priority >= priority) {
        ++it;
    }
    m_FileSystems.insert(it, sys);
}

// Binary @ 0x0019afe4 — find by ptr; erase node; calls sys vtable[1] (D0 dtor) — owns systems
void FileManager::RemoveSystem(IFileSystem* sys) {
    if (!sys) return;
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        if (*it == sys) {
            m_FileSystems.erase(it);
            delete sys;
            return;
        }
    }
}

// Binary @ 0x0019b02c
void FileManager::RemoveSystem(unsigned int id) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        if ((*it)->m_systemId == id) {
            IFileSystem* sys = *it;
            m_FileSystems.erase(it);
            delete sys;
            return;
        }
    }
}

// Binary @ 0x0019b074
void FileManager::ClearSystems() {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        delete *it;
    }
    m_FileSystems.clear();
}

// Binary @ 0x0019afa8
IFileSystem* FileManager::FindSystem(unsigned int id) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        if ((*it)->m_systemId == id) return *it;
    }
    return nullptr;
}

// Binary @ 0x0019ae68 — id-filtered walk; first non-null sys->OpenFile wins
// idFilter == 0 means "any system" (no filtering)
IFile* FileManager::OpenFile(const char* name, unsigned int idFilter, unsigned long flags) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        IFile* f = sys->OpenFile(name, flags);
        if (f) return f;
    }
    return nullptr;
}

// Binary @ 0x0019af60
bool FileManager::FileExists(const char* name, unsigned int idFilter) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        if (sys->FileExists(name)) return true;
    }
    return false;
}

// Binary @ 0x0019af18
unsigned int FileManager::FileSize(const char* name, unsigned int idFilter) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        unsigned int sz = sys->FileSize(name);
        if (sz > 0) return sz;
    }
    return 0;
}

// Binary @ 0x0019aeb4
bool FileManager::GetFileData(const char* name, void** outBuf, unsigned long* outSize,
                               unsigned int idFilter, bool& outOwned) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        if (sys->GetFileData(name, outBuf, outSize, outOwned)) return true;
    }
    return false;
}

// Binary @ 0x0019ae64 — stub returning 0 in binary
const char* FileManager::GetSaveRootDirectory(unsigned int /*systemId*/) {
    return nullptr;
}

// ---- OpenCI compat shim ----

// DIFFERS: not in binary; binary File::Open calls FileManager::OpenFile (registry-only).
// Kept as a shim so existing call sites (Texture.cpp, etc.) stay unbroken.
// Binary: File -> FileManager::OpenFile -> IFileSystem::OpenFile -> IFile_Direct.
// The shim bypasses the registry and uses direct fopen + CI fallback so it
// never double-opens or races with the IFile lifetime.

unsigned long FileManager::ModeToFlags(const char* mode) {
    if (!mode) return 0;
    if (strchr(mode, 'w') != nullptr) return 1;   // write-only
    if (strchr(mode, '+') != nullptr) return 3;   // read-write
    return 0;                                       // read-only
}

FILE* FileManager::OpenCI(const char* path, const char* mode) {
    if (!path) return nullptr;

    FILE* f = fopen(path, mode);
    if (f) return f;

    std::string real = ResolveCI(path);
    if (real.empty()) return nullptr;
    return fopen(real.c_str(), mode);
}
