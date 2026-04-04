#ifndef MORTAR_FILE_MANAGER_H
#define MORTAR_FILE_MANAGER_H

#include "core/Singleton.h"

namespace Mortar {

// Stub — original FileManager managed Bada file systems.
// Port uses direct file I/O; AddSystem/RemoveSystem are no-ops.
class FileManager : public Singleton<FileManager> {
    friend class Singleton<FileManager>;

public:
    void AddSystem(void*) {}
    void RemoveSystem(void*) {}

private:
    FileManager() {}
};

} // namespace Mortar

#endif
