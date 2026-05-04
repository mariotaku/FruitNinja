// Analysed: 2026-05-04T00:00
#include "asset/File.h"
#include "asset/FileManager.h"
#include "asset/IFile.h"

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
        // flags: bit 0 = write, bit 1 = update (r+b); read-only = 0
        unsigned long flags = 0;
        if (m_openMode == 1)      flags = 1;       // write-only
        else if (m_openMode == 2) flags = 1 | 2;   // read-write

        IFile* ifile = FileManager::GetInstance().OpenFile(
            m_filename.CStr(), (unsigned int)m_systemID, flags);

        if (ifile) {
            m_pIFile  = ifile;
            m_bIsOpen = true;
            m_size    = ifile->Size();
        }
    }
    return m_bIsOpen;
}

// Binary @ 0x0019b780
void File::Close() {
    if (m_pIFile) {
        m_pIFile->Close();
        delete m_pIFile;
        m_pIFile = nullptr;
    }
    m_bIsOpen = false;
}

// Binary @ 0x0019b7ac
bool File::Read(void* dst, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return m_pIFile->Read(dst, n);
}

// Binary @ 0x0019b7b8
bool File::Write(const void* src, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return m_pIFile->Write(src, n);
}

// Binary @ 0x0019b7c4
bool File::Seek(FileSeekMode mode, long offset) {
    if (!m_bIsOpen || !m_pIFile) return false;
    long whence;
    if (mode == FSEEK_SET)       whence = 0;
    else if (mode == FSEEK_CUR)  whence = 1;
    else                         whence = 2;
    return m_pIFile->Seek((unsigned long)offset, whence, false) == 0;
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

    // Seek to start then read entire file
    m_pIFile->Seek(0, 0, false);
    bool ok = m_pIFile->Read(m_pData, m_size);
    if (!ok) {
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

// Binary @ 0x0019b808 — static; delegates to FileManager registry
bool File::Exists(const char* path, unsigned long systemID) {
    if (!path) return false;
    // Try registry first (binary-faithful)
    FileManager& fm = FileManager::GetInstance();
    if (fm.FileExists(path, (unsigned int)systemID)) return true;
    // Fallback: OpenCI for compat (e.g. files in working dir before FileSystem_Direct is registered)
    FILE* fp = FileManager::OpenCI(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

// Binary @ 0x0019b90c — static; returns -1 on missing file
long File::SizeOfFile(const char* path, unsigned long systemID) {
    if (!path) return -1L;
    // Try registry first
    FileManager& fm = FileManager::GetInstance();
    unsigned int sz = fm.FileSize(path, (unsigned int)systemID);
    if (sz > 0) return (long)sz;
    // Fallback: direct fopen
    FILE* fp = FileManager::OpenCI(path, "rb");
    if (!fp) return -1L;
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fclose(fp);
    return fsz;
}

}  // namespace Mortar
