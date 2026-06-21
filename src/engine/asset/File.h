#ifndef FN_ENGINE_ASSET_FILE_H
#define FN_ENGINE_ASSET_FILE_H

#include "util/AsciiString.h"
#include "asset/IFile.h"
#include <cstdint>

// Mortar::File — concrete file handle (sizeof 0x40 on 32-bit ARM).
// NOT polymorphic: +0x00 is IFile* m_pIFile (delegate pointer), NOT a vptr.
//
// Binary field layout (confirmed from ctor @ 0x00251604, v1.6.1):
//   +0x00  IFile*         m_pIFile        = NULL
//   +0x04  unsigned long  m_systemID      = systemID param (ctor arg3)
//   +0x08  AsciiString    m_path          = path param (ctor arg1)
//   +0x30  void*          m_pData         = NULL
//   +0x34  bool           m_bIsOpen       = false
//   +0x35  bool           m_bLocked       = false
//   +0x36  bool           m_bIsLoaded     = false
//   +0x37  bool           m_bOwnsBuffer   = true
//   +0x38  unsigned long  m_sizeCache     = 0
//   +0x3C  int            m_mode          = mode param (ctor arg2)
// Total: 0x40 (64)

namespace Mortar {

// FileSeekMode — values match IFile::Seek() / POSIX fseek convention.
// Binary @ 0x0019b7c4
enum FileSeekMode : int { FSEEK_SET = 0, FSEEK_CUR = 1, FSEEK_END = 2 };

class File {
public:
    // Binary @ 0x00251604 — record path/mode/systemID; defer all I/O to Open()/Load()
    File(const char* path, int openMode, unsigned long systemID);
    // Binary @ 0x002519ac — Unload() then dtor the embedded AsciiString
    ~File();

    // Binary @ 0x00251810 — open via IFileSystem* or FileManager; cache size from IFile::Size()
    bool Open(IFileSystem* pFileSystem = 0);
    // Binary @ 0x002517f0 — returns m_sizeCache
    unsigned long Size() const;
    // Binary @ 0x002519d8 — delegate to m_pIFile->Read()
    bool Read(void* dst, unsigned long n);
    // Binary @ 0x00251914 — close and delete m_pIFile
    void Close();

    // Binary @ 0x0025168c — load via FileManager::GetFileData (not via Open/Read)
    bool Load(void* userBuffer, unsigned long userBufferSize);
    // Binary @ 0x00251964 — free buffer, close, reset state
    void Unload();

    // Binary @ 0x002519f0 — write to IFile (normal path, encryption defunct)
    bool Write(const void* src, unsigned long n);
    // Binary @ 0x002224ac — write C string; delegates to Write(str, strlen(str))
    bool Write(const char* str);

    // Binary @ 0x00251a7c — delegate to m_pIFile->Seek(mode, offset); no mode conversion
    bool Seek(FileSeekMode mode, long offset);

    // Non-virtual accessors
    bool IsLoaded() const;     // Binary @ 0x002517a6
    bool IsOpen() const;       // Binary @ 0x002517aa
    bool CanWrite() const;     // Binary @ 0x002517b2
    void* Data() const;        // Binary @ 0x002517b6
    const AsciiString& FileName() const;  // Binary @ 0x002517ba
    bool IsLocked() const;     // Binary @ 0x002517a0
    void Lock(bool locked);    // Binary @ 0x0025179c

    unsigned int GetPosition() const;  // Binary @ 0x00251ac8 — m_pIFile->Tell()
    unsigned int Hash() const;         // Binary @ 0x00251808 — m_path.Hash()

    // Static helpers
    // Binary @ 0x0025164c
    static bool Exists(const char* path, unsigned long systemID);
    // Binary @ 0x0025166c
    static long SizeOfFile(const char* path, unsigned long systemID);

    // --- Binary-faithful field layout (+0x00..+0x3F) ---
    IFile*          m_pIFile;       // +0x00  (NOT a vptr — File is concrete)
    unsigned long   m_systemID;     // +0x04  (ctor arg3; original "unsigned long systemID")
    AsciiString     m_path;         // +0x08  (40B SSO)
    void*           m_pData;        // +0x30
    bool            m_bIsOpen;      // +0x34
    bool            m_bLocked;      // +0x35
    bool            m_bIsLoaded;    // +0x36
    bool            m_bOwnsBuffer;  // +0x37  (default = true in binary ctor)
    unsigned long   m_sizeCache;    // +0x38
    int             m_mode;         // +0x3C

#ifdef __bada__
    friend struct FileLayoutAssert;
#endif
};

#if defined(__bada__)
struct FileLayoutAssert {
    static_assert(offsetof(File, m_pIFile)     == 0x00, "File::m_pIFile offset");
    static_assert(offsetof(File, m_systemID)   == 0x04, "File::m_systemID offset");
    static_assert(offsetof(File, m_path)       == 0x08, "File::m_path offset");
    static_assert(offsetof(File, m_pData)      == 0x30, "File::m_pData offset");
    static_assert(offsetof(File, m_bIsOpen)    == 0x34, "File::m_bIsOpen offset");
    static_assert(offsetof(File, m_bLocked)    == 0x35, "File::m_bLocked offset");
    static_assert(offsetof(File, m_bIsLoaded)  == 0x36, "File::m_bIsLoaded offset");
    static_assert(offsetof(File, m_bOwnsBuffer)== 0x37, "File::m_bOwnsBuffer offset");
    static_assert(offsetof(File, m_sizeCache)  == 0x38, "File::m_sizeCache offset");
    static_assert(offsetof(File, m_mode)       == 0x3C, "File::m_mode offset");
    static_assert(sizeof(File) == 0x40,                  "File::sizeof");
};
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_FILE_H
