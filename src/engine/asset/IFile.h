#ifndef FN_ENGINE_ASSET_IFILE_H
#define FN_ENGINE_ASSET_IFILE_H

// Binary @ 0x001eb380 (vtable), sizeof(IFile) == 8 on 32-bit ARM
// v1.6.1 vtable layout:
//   +0x04 dtor
//   +0x08 Size
//   +0x0C Close
//   +0x10 Read
//   +0x14 WriteEncrypted
//   +0x18 Write
//   +0x1C Seek
//   +0x20 Tell

namespace Mortar {

class IFileSystem;

// Binary @ 0x001eb380
class IFile {
public:
    // Binary @ 0x0019baa8 (D2), 0x0019bb1c (D0)
    virtual ~IFile();

    // Binary @ vtbl+0x08
    virtual unsigned int Size() = 0;
    // Binary @ vtbl+0x0c
    virtual void Close() = 0;
    // Binary @ vtbl+0x10
    virtual bool Read(void* dst, unsigned long n) = 0;
    // Binary @ vtbl+0x14 — encryption path (defunct on port)
    virtual bool WriteEncrypted(const void* src, unsigned long n) = 0;
    // Binary @ vtbl+0x18
    virtual bool Write(const void* src, unsigned long n) = 0;
    // Binary @ vtbl+0x1c @ 0x00255104 — whence: 0=SET, 1=CUR, 2=END (matches FileSeekMode / POSIX).
    // 3rd param `absolute` is unused in the IFile_Direct body (m_Position set unconditionally
    // from offset); kept for vtable-slot fidelity, not given invented behavior.
    virtual int Seek(unsigned long whence, long offset, bool absolute) = 0;
    // Binary @ vtbl+0x20
    virtual unsigned int Tell() = 0;

protected:
    // IFileSystem* m_pSystem @ +0x04 (vtable* occupies +0x00)
    IFileSystem* m_pSystem;

    // Binary @ ctor: takes IFileSystem* and calls m_pSystem->RegisterIFile(this)
    explicit IFile(IFileSystem* sys);
};

// sizeof check — only valid on 32-bit (pointer size == 4)
#ifdef __bada__
static_assert(sizeof(IFile) == 8, "IFile must be 8 bytes on 32-bit (vtable* + IFileSystem*)");
#endif

} // namespace Mortar

#endif // FN_ENGINE_ASSET_IFILE_H
