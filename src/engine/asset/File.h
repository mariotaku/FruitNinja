#ifndef FN_ENGINE_ASSET_FILE_H
#define FN_ENGINE_ASSET_FILE_H

#include "util/AsciiString.h"
#include <cstdint>

namespace Mortar {

// FileSeekMode — values match binary IFile::Seek() convention.
// Binary @ 0x0019b7c4
enum FileSeekMode : int { FSEEK_SET = 0, FSEEK_CUR = 1, FSEEK_END = 2 };

class File {
public:
    // Binary @ 0x0019b970 — record path/mode/systemID; defer all I/O to Open()/Load()
    File(const char* path, int openMode, unsigned long systemID);
    // Binary @ 0x0019b948 — Unload() then dtor the embedded AsciiString
    ~File();

    // Binary @ 0x0019b81c — open via FileManager; cache size from IFile::Size()
    bool Open();
    // Binary @ 0x0019b780 — virtual close + ~IFile (binary leaks IFile heap alloc; port does fclose)
    void Close();

    // Binary @ 0x0019b7ec — return m_bIsOpen
    bool IsOpen() const { return m_bIsOpen; }
    // Binary @ 0x0019b7dc — return m_bIsLoaded
    bool IsLoaded() const { return m_bIsLoaded; }
    // Binary @ 0x0019b7f4 — bit-0 of m_openMode is the write flag
    bool CanWrite() const { return (m_openMode & 1) != 0; }

    // Binary @ 0x0019b774 — cached size set by Open() / Load()
    unsigned long Size() const { return m_size; }
    // Binary @ 0x0019b778 — return m_pData (only valid after Load())
    void* Data() const { return m_pData; }
    // Binary @ 0x0019b77c — return the embedded AsciiString reference
    const AsciiString& FileName() const { return m_filename; }

    // Binary @ 0x0019b7ac — IFile::Read (vtbl+0x10) -> fread
    bool Read(void* dst, unsigned long n);
    // Binary @ 0x0019b7b8 — IFile::Write (vtbl+0x14) -> fwrite
    bool Write(const void* src, unsigned long n);
    // Binary @ 0x0019b7c4 — IFile::Seek (vtbl+0x18); FileSeekMode 0=SET,1=CUR,2=END
    bool Seek(FileSeekMode mode, long offset);

    // Binary @ 0x0019b8c0 — slurp file via GetFileData; nullptr buffer => new[]
    bool Load(void* userBuffer, unsigned long userBufferSize);
    // Binary @ 0x0019b890 — delete[] m_pData if owned, then Close()
    void Unload();

    // Binary @ 0x0019b808 — static; delegates to FileManager
    static bool Exists(const char* path, unsigned long systemID);
    // Binary @ 0x0019b90c — static; returns -1 on missing file
    static long SizeOfFile(const char* path, unsigned long systemID);

private:
    void*         m_pIFile;      // +0x00  opaque platform file handle (FILE* in port)
    unsigned long m_systemID;    // +0x04
    // DIFFERS: AsciiString sizeof in port (std::string-backed, ~4-8B) differs from binary
    //          (40B SSO buffer) -- field offsets within File shift accordingly
    AsciiString   m_filename;    // +0x08
    void*         m_pData;       // port offset shifts due to AsciiString sizeof
    bool          m_bIsOpen;
    bool          m_bSaved;
    bool          m_bIsLoaded;
    bool          m_bOwnsBuffer;
    unsigned long m_size;
    int           m_openMode;
};

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_FILE_H
