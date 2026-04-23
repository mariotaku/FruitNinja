#include "asset/FileManager.h"

#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

namespace Mortar {

namespace {

// Split a path into components, preserving an optional leading "/" or drive
// ("C:/", "C:\\"). Returns the root-so-far-prefix and the remaining segments.
struct SplitPath {
    std::string root;                 // "/", "C:/", or "" for relative
    std::vector<std::string> parts;   // path components (no separators)
};

SplitPath SplitPathParts(const char* path) {
    SplitPath out;
    if (!path || !*path) return out;

    std::string p(path);
    // Normalise backslashes so MSYS2 paths work both ways.
    for (char& c : p) if (c == '\\') c = '/';

    size_t i = 0;
    // Windows drive letter like "C:" — treat as root.
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

// Case-insensitive directory entry lookup. Returns the real on-disk name
// matching `target` under `dirPath`, or empty string if none.
std::string FindEntryCI(const std::string& dirPath, const std::string& target) {
    DIR* d = opendir(dirPath.empty() ? "." : dirPath.c_str());
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

// Quick existence check — avoids a directory walk when the path works as-is.
bool ExistsAsFile(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (st.st_mode & S_IFREG) != 0 || (st.st_mode & S_IFDIR) != 0;
}

// Normalise a path prefix ending in '/' into something opendir() accepts:
//   ""        -> "."            (current directory)
//   "/"       -> "/"            (unix root)
//   "C:/"     -> "C:/"          (windows drive root)
//   "/foo/"   -> "/foo"         (strip trailing slash)
//   "foo/"    -> "foo"
std::string ToOpenableDir(const std::string& prefix) {
    if (prefix.empty()) return ".";
    if (prefix == "/") return prefix;
    if (prefix.size() == 3 && prefix[1] == ':' && prefix[2] == '/') return prefix;
    if (prefix.back() == '/') return prefix.substr(0, prefix.size() - 1);
    return prefix;
}

// Resolve `path` against the filesystem case-insensitively. Returns the
// real path on success, empty string on failure.
std::string ResolveCI(const char* path) {
    SplitPath sp = SplitPathParts(path);
    if (sp.parts.empty() && sp.root.empty()) return std::string();

    std::string built = sp.root;   // always ends in '/' when non-empty, or is ""
    for (const std::string& part : sp.parts) {
        if (part == "." || part.empty()) continue;
        if (part == "..") {
            // Append as-is; stat will resolve it during lookup.
            built += "..";
            built += '/';
            continue;
        }

        // Try exact match first (hot path for same-case hits).
        std::string candidate = built + part;
        if (ExistsAsFile(candidate.c_str())) {
            built = candidate + "/";
            continue;
        }

        // Fall back to a directory scan on the accumulated prefix.
        std::string real = FindEntryCI(ToOpenableDir(built), part);
        if (real.empty()) return std::string();
        built += real;
        built += '/';
    }

    // Strip the trailing slash we always appended after the last component.
    if (sp.parts.size() > 0 && !built.empty() && built.back() == '/') {
        built.pop_back();
    }
    return built;
}

} // namespace

FILE* FileManager::OpenCI(const char* path, const char* mode) {
    if (!path) return NULL;

    FILE* f = fopen(path, mode);
    if (f) return f;

    std::string real = ResolveCI(path);
    if (real.empty()) return NULL;
    return fopen(real.c_str(), mode);
}

} // namespace Mortar
