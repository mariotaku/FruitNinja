#ifndef FN_ENGINE_ASSET_FILE_H
#define FN_ENGINE_ASSET_FILE_H

#include "util/AsciiString.h"
#include "asset/IFile.h"
#include <cstdint>

// Mortar::File — polymorphic file handle (sizeof 0x40 on 32-bit ARM).
// Binary: abstract base; concrete vtable installed by derived subclass.
// Ctor @ 0x0019b970 writes vptr=0 (derived subclass installs real vtable).
// Virtual methods: Open, Size, Read, Close dispatched through vtable PTR slots.
//
// Binary field layout (confirmed from ctor + Font::Load stack array of 64 File instances):
//   +0x00  void**        vptr         (installed by derived class; ctor writes 0)
//   +0x04  unsigned long m_size       (ctor arg3; binary "unsigned long size")
//   +0x08  AsciiString   m_path       (40B SSO; ctor constructs in-place)
//   +0x30  uint32_t      m_position   (zeroed by ctor)
//   +0x34  bool          m_field34    (ctor: false)
//   +0x35  bool          m_field35    (ctor: false)
//   +0x36  bool          m_field36    (ctor: false)
//   +0x37  bool          m_field37    (ctor: true -- default=1)
//   +0x38  uint32_t      m_field38    (zeroed by ctor)
//   +0x3C  int           m_mode       (ctor arg2)
// Total: 0x40 (64)
//
// Port-only members (tail, guarded #if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)):
//   m_pIFile, m_systemID, m_pData, m_bOwnsBuffer
//
// POLYMORPHIC: vptr at +0x00. Virtual methods: Open, Size, Read, Close.
// The port's concrete implementation uses IFile delegation via the tail m_pIFile field.

namespace Mortar {

// FileSeekMode — values match binary IFile::Seek() convention.
// Binary @ 0x0019b7c4
enum FileSeekMode : int { FSEEK_SET = 0, FSEEK_CUR = 1, FSEEK_END = 2 };

class File {
public:
    // Binary @ 0x0019b970 — record path/mode/size; defer all I/O to Open()/Load()
    // Ctor writes vptr=0; derived subclass ctor installs real vtable.
    File(const char* path, int openMode, unsigned long systemID);
    // Binary @ 0x0019b948 — Unload() then dtor the embedded AsciiString
    virtual ~File();

    // Binary @ vtbl+0x08 — open via FileManager; cache size from IFile::Size()
    virtual bool Open();
    // Binary @ vtbl+0x0c — cached size
    virtual unsigned long Size() const;
    // Binary @ vtbl+0x10 — read from IFile
    virtual bool Read(void* dst, unsigned long n);
    // Binary @ vtbl+0x14 — virtual close
    virtual void Close();

    // Non-virtual accessors
    // Binary @ 0x0019b7dc
    bool IsLoaded() const;
    // Binary @ 0x0019b7ec
    bool IsOpen() const;
    // Binary @ 0x0019b7f4
    bool CanWrite() const;
    // Binary @ 0x0019b778
    void* Data() const;
    // Binary @ 0x0019b77c
    const AsciiString& FileName() const;

    // Binary @ 0x0019b7b8
    bool Write(const void* src, unsigned long n);
    // Binary @ 0x0019b7c4
    bool Seek(FileSeekMode mode, long offset);

    // Binary @ 0x0019b8c0
    bool Load(void* userBuffer, unsigned long userBufferSize);
    // Binary @ 0x0019b890
    void Unload();

    // Binary @ 0x0019b808
    static bool Exists(const char* path, unsigned long systemID);
    // Binary @ 0x0019b90c
    static long SizeOfFile(const char* path, unsigned long systemID);

    // STUB: File::GetPosition -- auto stub from binary missing-symbol set
    void GetPosition() const;
    // STUB: File::Hash -- auto stub from binary missing-symbol set
    void Hash() const;
    // STUB: File::IsLocked -- auto stub from binary missing-symbol set
    void IsLocked() const;
    // STUB: File::Lock -- auto stub from binary missing-symbol set
    void Lock(bool);

    // --- Binary-faithful field layout (+0x04..+0x3F after vptr at +0x00) ---
    unsigned long   m_size;         // +0x04  (binary ctor arg3)
    AsciiString     m_path;         // +0x08  (40B SSO)
    uint32_t        m_position;     // +0x30
    bool            m_field34;      // +0x34  (port: m_bIsOpen)
    bool            m_field35;      // +0x35  (port: m_bSaved)
    bool            m_field36;      // +0x36  (port: m_bIsLoaded)
    bool            m_field37;      // +0x37  (default=1 in binary ctor)
    uint32_t        m_field38;      // +0x38
    int             m_mode;         // +0x3C

    // --- Port-only tail fields (not present in binary at these offsets) ---
    // Placed after the binary-faithful fields; excluded from the cross-build sizeof.
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    IFile*        m_pIFile;
    unsigned long m_systemID;
    void*         m_pData;
    bool          m_bOwnsBuffer;
#endif

#ifdef __bada__
    friend struct FileLayoutAssert;
#endif
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
struct FileLayoutAssert {
    static_assert(offsetof(File, m_size)     == 0x04, "File::m_size offset");
    static_assert(offsetof(File, m_path)     == 0x08, "File::m_path offset");
    static_assert(offsetof(File, m_position) == 0x30, "File::m_position offset");
    static_assert(offsetof(File, m_field34)  == 0x34, "File::m_field34 offset");
    static_assert(offsetof(File, m_mode)     == 0x3C, "File::m_mode offset");
    static_assert(sizeof(File) == 0x40,               "File sizeof");
};
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_FILE_H
