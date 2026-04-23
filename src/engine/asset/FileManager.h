#ifndef MORTAR_FILE_MANAGER_H
#define MORTAR_FILE_MANAGER_H

#include "core/Singleton.h"
#include <cstdio>

namespace Mortar {

// Stub — original FileManager managed Bada file systems.
// Port uses direct file I/O; AddSystem/RemoveSystem are no-ops.
class FileManager : public Singleton<FileManager> {
    friend class Singleton<FileManager>;

public:
    void AddSystem(void*) {}
    void RemoveSystem(void*) {}

    // Case-insensitive fopen. Bada's filesystem was case-insensitive so the
    // shipped asset paths (e.g. "models/Fruit/bomb.mmd") don't match the
    // actual on-disk casing ("models/fruit/bomb.mmd") on Linux/webOS.
    //
    // Tries fopen(path, mode) first (fast path on Windows/macOS).
    // On failure, walks path components from the root, looking up each
    // name case-insensitively via opendir/readdir and rebuilding the
    // real path, then retries fopen once. Returns NULL if still missing.
    //
    // `path` is an absolute or relative path using '/' or '\\' separators.
    static FILE* OpenCI(const char* path, const char* mode);

private:
    FileManager() {}
};

} // namespace Mortar

#endif
