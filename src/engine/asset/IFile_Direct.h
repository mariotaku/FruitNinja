#ifndef FN_ENGINE_ASSET_IFILE_DIRECT_H
#define FN_ENGINE_ASSET_IFILE_DIRECT_H

// Binary @ 0x001eb3b8 (vtable), sizeof(IFile_Direct) == 0x14 on 32-bit ARM

#include "asset/IFile.h"
#include <cstdio>

namespace Mortar {

// Binary @ 0x001eb3b8 — FILE*-backed IFile concrete.
// Extends IFile base with: m_size @ +0x08, m_cursor @ +0x0c, m_fp @ +0x10.
class IFile_Direct : public IFile {
public:
    // Binary @ ctor: (IFileSystem*, FILE*, unsigned long size)
    IFile_Direct(IFileSystem* sys, FILE* fp, unsigned long size);
    virtual ~IFile_Direct();

    // IFile overrides (slots 2..8) — Binary @ 0x001eb3b8 vtable
    virtual unsigned int Size() override;
    virtual void         Close() override;
    virtual bool         Read(void* dst, unsigned long n) override;
    virtual bool         WriteEncrypted(const void* src, unsigned long n) override;
    virtual bool         Write(const void* src, unsigned long n) override;
    virtual int          Seek(unsigned long whence, long offset, bool absolute) override;
    virtual unsigned int Tell() override;

private:
    unsigned long m_size;   // Binary @ IFile_Direct +0x08
    unsigned long m_cursor; // Binary @ IFile_Direct +0x0c
    FILE*         m_fp;     // Binary @ IFile_Direct +0x10
};

} // namespace Mortar

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Mortar::IFile_Direct) == 0x14, "Mortar::IFile_Direct size mismatch"); // v1.6.1 FileSystem_Direct::OpenFile @0x002511ac -- operator new(0x14) sizes IFile_Direct
#endif

#endif // FN_ENGINE_ASSET_IFILE_DIRECT_H
