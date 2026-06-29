#ifndef FN_ENGINE_ASSET_FILEMANAGER_H
#define FN_ENGINE_ASSET_FILEMANAGER_H

// Binary @ Mortar::FileManager — IFileSystem registry singleton.
// Matching binary call shape: File -> FileManager -> IFileSystem (vtbl) -> IFile (vtbl) -> backend.

#include "core/Singleton.h"
#include "asset/IFile.h"
#include "asset/IFileSystem.h"

#include <list>

// Binary @ singleton pattern (Meyers via Singleton<> CRTP)
class FileManager : public Mortar::Singleton<FileManager> {
    friend class Mortar::Singleton<FileManager>;

public:
    // Binary @ 0x0019b170 — sets sys->m_systemId / m_priority then sorted insert (descending priority)
    void AddSystem(Mortar::IFileSystem* sys, unsigned int id, int priority);

    // Binary @ 0x0019afe4 — find by ptr; erase; calls sys->vtable[1] (D0 dtor) — owns systems
    void RemoveSystem(Mortar::IFileSystem* sys);

    // Binary @ 0x0019b02c — find by id; erase; calls D0 dtor
    void RemoveSystem(unsigned int id);

    // Binary @ 0x0019b074 — delete + erase all systems
    void ClearSystems();

    // Binary @ 0x0019afa8 — linear search by id
    Mortar::IFileSystem* FindSystem(unsigned int id);

    // Binary @ 0x0019ae68 — id-filtered walk; first non-null sys->OpenFile wins
    Mortar::IFile* OpenFile(const char* name, unsigned long flags, unsigned long idFilter);

    // Binary @ 0x0019af60
    bool FileExists(const char* name, unsigned long idFilter);

    // Binary @ 0x0019af18
    unsigned int FileSize(const char* name, unsigned long idFilter);

    // Binary @ 0x0019aeb4
    bool GetFileData(const char* name, void** outBuf, unsigned long* outSize,
                     unsigned long idFilter, bool& outOwned);

    // Binary @ 0x00250ed4 — Bada-specific save-root path resolution.
    // Defunct: GetSaveRootDirectory — no-op stub; binary's body is also `return 0`.
    //          The only caller (GetUserFilePath @ 0x0012b09c) is itself unused.
    //          Port uses platform-specific paths via FileSystemPosix/Win32 directly.
    int GetSaveRootDirectory(char* outBuf, const char* relPath, bool createDir);

private:
    FileManager();
    ~FileManager();

    // Binary @ Mortar::FileManager::m_FileSystems — descending priority order on insert.
    // std::list<IFileSystem*> at +0x00; sole member, so sizeof(FileManager) == 8 (Sourcery 2010q1 pre-C++11 list).
    std::list<Mortar::IFileSystem*> m_FileSystems;
};

// 32-bit ARM only (8B std::list, Sourcery 2010q1 pre-C++11).
#ifdef __bada__
static_assert(sizeof(FileManager) == 8, "FileManager sizeof mismatch (single 8B std::list field)");
#endif

#endif // FN_ENGINE_ASSET_FILEMANAGER_H
