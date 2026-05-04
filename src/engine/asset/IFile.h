#ifndef FN_ENGINE_ASSET_IFILE_H
#define FN_ENGINE_ASSET_IFILE_H

// Binary @ 0x001eb380 (vtable), sizeof(IFile) == 8 on 32-bit ARM

namespace Mortar {

class IFileSystem;

// Binary @ 0x001eb380
class IFile {
public:
    // Binary @ 0x0019baa8 (D2), 0x0019bb1c (D0)
    virtual ~IFile();

    // Binary @ vtbl+0x08 — pure
    virtual unsigned int Size() = 0;
    // Binary @ vtbl+0x0c — pure
    virtual void Close() = 0;
    // Binary @ vtbl+0x10 — pure
    virtual bool Read(void* dst, unsigned long n) = 0;
    // Binary @ vtbl+0x14 — pure
    virtual bool Write(const void* src, unsigned long n) = 0;
    // Binary @ vtbl+0x18 — pure; whence: 0=SET,1=CUR,2=END (matching POSIX)
    virtual int Seek(unsigned long newPos, long whence, bool) = 0;
    // Binary @ vtbl+0x1c — pure
    virtual unsigned int Tell() = 0;

protected:
    // IFileSystem* m_pSystem @ +0x04 (vtable* occupies +0x00)
    IFileSystem* m_pSystem;

    // Binary @ ctor: takes IFileSystem* and calls m_pSystem->RegisterIFile(this)
    explicit IFile(IFileSystem* sys);
};

// sizeof check — only valid on 32-bit (pointer size == 4)
#if __SIZEOF_POINTER__ == 4
static_assert(sizeof(IFile) == 8, "IFile must be 8 bytes on 32-bit (vtable* + IFileSystem*)");
#endif

} // namespace Mortar

#endif // FN_ENGINE_ASSET_IFILE_H
