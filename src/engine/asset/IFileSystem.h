#ifndef FN_ENGINE_ASSET_IFILESYSTEM_H
#define FN_ENGINE_ASSET_IFILESYSTEM_H

// Binary @ 0x001eb360 (vtable), sizeof(IFileSystem) == 12 on 32-bit ARM

namespace Mortar {

class IFile;

// Binary @ 0x001eb360
class IFileSystem {
public:
    // Binary @ vtbl+0x00 (D2), vtbl+0x04 (D0)
    virtual ~IFileSystem();

    // Binary @ vtbl+0x08 — pure
    virtual bool FileExists(const char* name) = 0;
    // Binary @ vtbl+0x0c — pure
    virtual unsigned int FileSize(const char* name) = 0;
    // Binary @ vtbl+0x10 — pure; outOwned indicates whether caller must free *outBuf
    virtual bool GetFileData(const char* name, void** outBuf, unsigned long* outSize, bool& outOwned) = 0;
    // Binary @ vtbl+0x14 — pure
    virtual IFile* OpenFile(const char* name, unsigned long flags) = 0;

    // Non-virtual helpers — no-op stubs in port (no per-system IFile tracking storage)
    // Binary @ IFileSystem body (not in vtable)
    void RegisterIFile(IFile* f);
    void DeregisterIFile(IFile* f);

    // Written by FileManager::AddSystem — Binary @ 0x0019b170
    unsigned int m_systemId;  // +0x04
    int          m_priority;  // +0x08

protected:
    // Binary @ base ctor: zeros m_systemId and m_priority
    IFileSystem();
};

// sizeof check — only valid on 32-bit
#if __SIZEOF_POINTER__ == 4
static_assert(sizeof(IFileSystem) == 12,
    "IFileSystem must be 12 bytes on 32-bit (vtable* + systemId + priority)");
#endif

} // namespace Mortar

#endif // FN_ENGINE_ASSET_IFILESYSTEM_H
