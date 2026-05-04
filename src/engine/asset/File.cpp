// Analysed: 2026-05-04T00:00
#include "asset/File.h"
#include "asset/FileManager.h"

#include <cstdio>
#include <cstring>

namespace Mortar {

// Binary @ 0x0019b970
File::File(const char* path, int openMode, unsigned long systemID)
    : m_pIFile(nullptr)
    , m_systemID(systemID)
    , m_filename(path ? path : "")
    , m_pData(nullptr)
    , m_bIsOpen(false)
    , m_bSaved(false)
    , m_bIsLoaded(false)
    , m_bOwnsBuffer(false)
    , m_size(0)
    , m_openMode(openMode)
{
}

// Binary @ 0x0019b948
File::~File() {
    Unload();
    // m_filename dtor runs via AsciiString dtor
}

// Binary @ 0x0019b81c
bool File::Open() {
    if (!m_bIsOpen) {
        if ((m_openMode & 1) == 0 && !Exists(m_filename.CStr(), m_systemID)) {
            return false;
        }
        const char* mode;
        if (m_openMode == 0) {
            mode = "rb";
        } else if (m_openMode == 1) {
            mode = "wb";
        } else {
            mode = "r+b";
        }
        FILE* fp = FileManager::OpenCI(m_filename.CStr(), mode);
        if (fp) {
            m_pIFile = fp;
            m_bIsOpen = true;
            long save = ftell(fp);
            fseek(fp, 0, SEEK_END);
            m_size = (unsigned long)ftell(fp);
            fseek(fp, save, SEEK_SET);
        }
    }
    return m_bIsOpen;
}

// Binary @ 0x0019b780
void File::Close() {
    if (m_pIFile) {
        fclose((FILE*)m_pIFile);
        m_pIFile = nullptr;
    }
    m_bIsOpen = false;
}

// Binary @ 0x0019b7ac
bool File::Read(void* dst, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return fread(dst, 1, n, (FILE*)m_pIFile) == n;
}

// Binary @ 0x0019b7b8
bool File::Write(const void* src, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return fwrite(src, 1, n, (FILE*)m_pIFile) == n;
}

// Binary @ 0x0019b7c4
bool File::Seek(FileSeekMode mode, long offset) {
    if (!m_bIsOpen || !m_pIFile) return false;
    int whence;
    if (mode == FSEEK_SET)       whence = SEEK_SET;
    else if (mode == FSEEK_CUR)  whence = SEEK_CUR;
    else                         whence = SEEK_END;
    return fseek((FILE*)m_pIFile, offset, whence) == 0;
}

// Binary @ 0x0019b8c0
bool File::Load(void* userBuffer, unsigned long userBufferSize) {
    if (m_bIsLoaded) return true;
    if (!Open()) return false;

    if (userBuffer && userBufferSize >= m_size) {
        m_pData = userBuffer;
        m_bOwnsBuffer = false;
    } else {
        m_pData = new unsigned char[m_size];
        m_bOwnsBuffer = true;
    }

    fseek((FILE*)m_pIFile, 0, SEEK_SET);
    unsigned long read = (unsigned long)fread(m_pData, 1, m_size, (FILE*)m_pIFile);
    if (read != m_size) {
        if (m_bOwnsBuffer) {
            delete[] (unsigned char*)m_pData;
            m_pData = nullptr;
            m_bOwnsBuffer = false;
        }
        return false;
    }

    m_bIsLoaded = true;
    return true;
}

// Binary @ 0x0019b890
void File::Unload() {
    if (m_bOwnsBuffer && m_pData) {
        delete[] (unsigned char*)m_pData;
        m_bOwnsBuffer = false;
    }
    m_pData = nullptr;
    m_bIsLoaded = false;
    Close();
}

// Binary @ 0x0019b808
bool File::Exists(const char* path, unsigned long /*systemID*/) {
    if (!path) return false;
    FILE* fp = FileManager::OpenCI(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

// Binary @ 0x0019b90c
long File::SizeOfFile(const char* path, unsigned long /*systemID*/) {
    if (!path) return -1L;
    FILE* fp = FileManager::OpenCI(path, "rb");
    if (!fp) return -1L;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fclose(fp);
    return sz;
}

}  // namespace Mortar
