// Analysed: 2026-05-04T00:00
#include "asset/File.h"
#include "asset/FileManager.h"
#include "asset/IFile.h"

#include <cstring>

namespace Mortar {

// Binary @ 0x0019b970
// Args: path -> m_path (+0x08), openMode -> m_mode (+0x3C), systemID -> m_size (+0x04).
// Ctor writes vptr=0; derived class installs the real vtable.
// Port tail fields m_pIFile/m_systemID/m_pData/m_bOwnsBuffer are port-only.
File::File(const char* path, int openMode, unsigned long systemID)
    : m_size(systemID)         // +0x04
    , m_path(path ? path : "") // +0x08
    , m_position(0)            // +0x30
    , m_field34(false)         // +0x34: m_bIsOpen
    , m_field35(false)         // +0x35: m_bSaved
    , m_field36(false)         // +0x36: m_bIsLoaded
    , m_field37(true)          // +0x37: default=1 in binary ctor
    , m_field38(0)             // +0x38
    , m_mode(openMode)         // +0x3C
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_pIFile(0)
    , m_systemID(systemID)
    , m_pData(0)
    , m_bOwnsBuffer(false)
#endif
{
}

// Binary @ 0x0019b948
File::~File() {
    Unload();
    // m_path dtor runs via AsciiString dtor
}

// Binary @ vtbl+0x08 (virtual Open)
bool File::Open() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_field34) {
        if ((m_mode & 1) == 0 && !Exists(m_path.CStr(), m_systemID)) {
            return false;
        }
        unsigned long flags = 0;
        if (m_mode == 1)      flags = 1;
        else if (m_mode == 2) flags = 1 | 2;

        IFile* ifile = FileManager::GetInstance().OpenFile(
            m_path.CStr(), flags, (unsigned int)m_systemID);

        if (ifile) {
            m_pIFile  = ifile;
            m_field34 = true;
            m_size    = ifile->Size();
        }
    }
    return m_field34;
#else
    return false;
#endif
}

// Binary @ vtbl+0x0c (virtual Size)
unsigned long File::Size() const {
    return m_size;
}

// Binary @ vtbl+0x10 (virtual Read)
bool File::Read(void* dst, unsigned long n) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_field34 || !m_pIFile) return false;
    return m_pIFile->Read(dst, n);
#else
    return false;
#endif
}

// Binary @ vtbl+0x14 (virtual Close)
void File::Close() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (m_pIFile) {
        m_pIFile->Close();
        delete m_pIFile;
        m_pIFile = 0;
    }
    m_field34 = false;
#endif
}

// Binary @ 0x0019b7b8
bool File::Write(const void* src, unsigned long n) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_field34 || !m_pIFile) return false;
    return m_pIFile->Write(src, n);
#else
    return false;
#endif
}

// Binary @ 0x0019b7c4
bool File::Seek(FileSeekMode mode, long offset) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_field34 || !m_pIFile) return false;
    long whence;
    if (mode == FSEEK_SET)       whence = 0;
    else if (mode == FSEEK_CUR)  whence = 1;
    else                         whence = 2;
    return m_pIFile->Seek((unsigned long)offset, whence, false) == 0;
#else
    return false;
#endif
}

// Binary @ 0x0019b8c0
bool File::Load(void* userBuffer, unsigned long userBufferSize) {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (m_field36) return true;
    if (!Open()) return false;

    if (userBuffer && userBufferSize >= m_size) {
        m_pData = userBuffer;
        m_bOwnsBuffer = false;
    } else {
        m_pData = new unsigned char[m_size];
        m_bOwnsBuffer = true;
    }

    m_pIFile->Seek(0, 0, false);
    bool ok = m_pIFile->Read(m_pData, m_size);
    if (!ok) {
        if (m_bOwnsBuffer) {
            delete[] (unsigned char*)m_pData;
            m_pData = 0;
            m_bOwnsBuffer = false;
        }
        return false;
    }

    m_field36 = true;
    return true;
#else
    return false;
#endif
}

// Binary @ 0x0019b890
void File::Unload() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (m_bOwnsBuffer && m_pData) {
        delete[] (unsigned char*)m_pData;
        m_bOwnsBuffer = false;
    }
    m_pData = 0;
    m_field36 = false;
    Close();
#endif
}

// Binary @ 0x0019b808
bool File::Exists(const char* path, unsigned long systemID) {
    if (!path) return false;
    return FileManager::GetInstance().FileExists(path, (unsigned int)systemID);
}

// Binary @ 0x0019b90c
long File::SizeOfFile(const char* path, unsigned long systemID) {
    if (!path) return -1L;
    unsigned int sz = FileManager::GetInstance().FileSize(path, (unsigned int)systemID);
    if (sz == 0xFFFFFFFFu) return -1L;
    return (long)sz;
}

// Binary @ 0x0019b7ec
bool File::IsOpen() const { return m_field34; }
// Binary @ 0x0019b7dc
bool File::IsLoaded() const { return m_field36; }
// Binary @ 0x0019b7f4
bool File::CanWrite() const { return (m_mode & 1) != 0; }
// Binary @ 0x0019b778
void* File::Data() const {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    return m_pData;
#else
    return 0;
#endif
}
// Binary @ 0x0019b77c
const AsciiString& File::FileName() const { return m_path; }

// STUB: File::GetPosition -- auto stub from binary missing-symbol set
void File::GetPosition() const {}
// STUB: File::Hash -- auto stub from binary missing-symbol set
void File::Hash() const {}
// STUB: File::IsLocked -- auto stub from binary missing-symbol set
void File::IsLocked() const {}
// STUB: File::Lock -- auto stub from binary missing-symbol set
void File::Lock(bool) {}

}  // namespace Mortar
