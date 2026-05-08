#include "util/PathCI.h"

#if !defined(_WIN32)
#  include <dirent.h>
#  include <sys/stat.h>
#  include <strings.h>   // strcasecmp
#  include <vector>
#  include <cstring>
#endif

namespace Mortar {

#if defined(_WIN32)

// NTFS is case-insensitive at the kernel level; fopen / CreateFile etc.
// already match irrespective of case, so the resolver is a no-op.
std::string ResolvePathCI(const char* /*absPath*/) {
    return std::string();
}

#else

namespace {

bool ExistsOnDisk(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

struct SplitPath {
    std::string root;          // "/" or "" (relative)
    std::vector<std::string> parts;
};

SplitPath SplitPathParts(const char* path) {
    SplitPath sp;
    if (!path || !*path) return sp;
    size_t i = 0;
    if (path[0] == '/') { sp.root = "/"; i = 1; }
    std::string cur;
    for (; path[i] != '\0'; ++i) {
        if (path[i] == '/') {
            if (!cur.empty()) {
                sp.parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur += path[i];
        }
    }
    if (!cur.empty()) sp.parts.push_back(cur);
    return sp;
}

std::string ToOpenableDir(const std::string& prefix) {
    if (prefix.empty()) return ".";
    if (prefix == "/") return prefix;
    if (prefix[prefix.size() - 1] == '/') return prefix.substr(0, prefix.size() - 1);
    return prefix;
}

std::string FindEntryCI(const std::string& dir, const std::string& target) {
    DIR* d = opendir(dir.c_str());
    if (!d) return std::string();
    std::string match;
    while (struct dirent* ent = readdir(d)) {
        if (strcasecmp(ent->d_name, target.c_str()) == 0) {
            match = ent->d_name;
            break;
        }
    }
    closedir(d);
    return match;
}

} // namespace

std::string ResolvePathCI(const char* absPath) {
    if (!absPath || !*absPath) return std::string();

    SplitPath sp = SplitPathParts(absPath);
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

#endif // !_WIN32

} // namespace Mortar
