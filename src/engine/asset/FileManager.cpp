// Analysed: 2026-05-04T00:00
#include "asset/FileManager.h"
#include "asset/IFileSystem.h"
#include "asset/IFile.h"

// Explicit instantiation so Mortar::Singleton<FileManager>::GetInstance() emits a T-symbol.
template class Mortar::Singleton<FileManager>;

using namespace Mortar;

FileManager::FileManager() {}

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
    while (!m_FileSystems.empty()) {
        RemoveSystem(m_FileSystems.front());
    }
}

// Binary @ dtor — calls ClearSystems() before list teardown
FileManager::~FileManager() {
    ClearSystems();
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
IFile* FileManager::OpenFile(const char* name, unsigned long flags, unsigned long idFilter) {
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
bool FileManager::FileExists(const char* name, unsigned long idFilter) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        if (sys->FileExists(name)) return true;
    }
    return false;
}

// Binary @ 0x0019af18
unsigned int FileManager::FileSize(const char* name, unsigned long idFilter) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        return sys->FileSize(name);    // first id-matching system wins, regardless of result
    }
    return 0xFFFFFFFFu;                // sentinel: no matching system (not 0 = empty file)
}

// Binary @ 0x0019aeb4
bool FileManager::GetFileData(const char* name, void** outBuf, unsigned long* outSize,
                               unsigned long idFilter, bool& outOwned) {
    for (std::list<IFileSystem*>::iterator it = m_FileSystems.begin();
         it != m_FileSystems.end(); ++it) {
        IFileSystem* sys = *it;
        if (idFilter != 0 && sys->m_systemId != idFilter) continue;
        if (sys->GetFileData(name, outBuf, outSize, outOwned)) return true;
    }
    return false;
}

// Defunct: GetSaveRootDirectory — no-op stub; v1.6.1 binary @ 0x00250ed4 body is `return 0`.
//          Only caller GetUserFilePath @ 0x00154494 is unused in the binary's call graph.
int FileManager::GetSaveRootDirectory(char* /*outBuf*/, const char* /*relPath*/, bool /*createDir*/, bool /*unknownFlag*/) {
    return 0;
}

