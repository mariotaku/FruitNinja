// Analysed: 2026-06-18
// v1.6.1 binary faithful implementation. File is NOT polymorphic — +0x00 is IFile* delegate.
#include "asset/File.h"
#include "asset/FileManager.h"
#include "asset/IFile.h"
#include "asset/IFileSystem.h"

#include <cstring>

namespace Mortar {

// Binary @ 0x00251604
File::File(const char* path, int openMode, unsigned long systemID)
    : m_pIFile(0)            // +0x00
    , m_systemID(systemID)   // +0x04
    , m_path(path ? path : "") // +0x08
    , m_pData(0)             // +0x30
    , m_bIsOpen(false)       // +0x34
    , m_bLocked(false)       // +0x35
    , m_bIsLoaded(false)     // +0x36
    , m_bOwnsBuffer(true)    // +0x37  (default=true in binary ctor)
    , m_sizeCache(0)         // +0x38
    , m_mode(openMode)       // +0x3C
{
}

// Binary @ 0x002519ac
File::~File() {
    Unload();
    // m_path AsciiString dtor runs automatically
}

// Binary @ 0x00251810
bool File::Open(IFileSystem* pFileSystem) {
    if (m_bIsOpen) return true;

    if (pFileSystem) {
        if (!pFileSystem->FileExists(m_path.CStr())) {
            if ((m_mode & 1) == 0) return false;
            // File doesn't exist on the given filesystem but write mode is set;
            // fall through to try FileManager.
        } else {
            m_pIFile = pFileSystem->OpenFile(m_path.CStr(), (unsigned long)m_mode);
            if (m_pIFile) {
                m_bIsOpen = true;
                m_sizeCache = m_pIFile->Size();
            }
            return m_bIsOpen;
        }
    }

    // use FileManager (either because pFileSystem was NULL, or write-mode fallback)
    FileManager& fm = FileManager::GetInstance();
    if (!pFileSystem && !fm.FileExists(m_path.CStr(), m_systemID)) {
        if ((m_mode & 1) == 0) return false;
    }
    m_pIFile = fm.OpenFile(m_path.CStr(), (unsigned long)m_mode, m_systemID);
    if (m_pIFile) {
        m_bIsOpen = true;
        m_sizeCache = m_pIFile->Size();
    }
    return m_bIsOpen;
}

// Binary @ 0x002517f0
unsigned long File::Size() const {
    return m_sizeCache;
}

// Binary @ 0x002519d8
bool File::Read(void* dst, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return m_pIFile->Read(dst, n);
}

// Binary @ 0x00251914
void File::Close() {
    if (m_bIsOpen && m_pIFile) {
        m_pIFile->Close();
        delete m_pIFile;
        m_pIFile = 0;
    }
    m_bIsOpen = false;
    m_sizeCache = 0;
}

// Binary @ 0x002519f0 — normal Write path only (encryption defunct)
bool File::Write(const void* src, unsigned long n) {
    if (!m_bIsOpen || !m_pIFile) return false;
    return m_pIFile->Write(src, n);
}

// Binary @ 0x002224ac
bool File::Write(const char* str) {
    return Write(str, std::strlen(str));
}

// Binary @ 0x00251a7c — passes mode+offset directly; no conversion (mode values already match POSIX)
// IFile::Seek return is discarded (void) per binary; File::Seek returns true if file is open.
bool File::Seek(FileSeekMode mode, long offset) {
    if (!m_bIsOpen || !m_pIFile) return false;
    (void)m_pIFile->Seek(static_cast<int>(mode), offset);
    return true;
}

// Binary @ 0x0025168c — loads via FileManager::GetFileData (NOT via Open/Read path)
bool File::Load(void* userBuffer, unsigned long userBufferSize) {
    if (m_bIsLoaded) return true;

    if (userBuffer) {
        m_pData = userBuffer;
        m_sizeCache = userBufferSize;
    } else {
        m_pData = 0;
        m_sizeCache = 0;
    }

    m_mode = 0;

    FileManager& fm = FileManager::GetInstance();
    // DIFFERS: binary's GetFileData may reuse *outBuf (userBuffer) if non-null;
    // our port always allocates, so userBuffer is effectively overwritten.
    m_bIsLoaded = fm.GetFileData(m_path.CStr(), &m_pData, &m_sizeCache,
                                  m_systemID, m_bOwnsBuffer);
    return m_bIsLoaded;
}

// Binary @ 0x00251964
void File::Unload() {
    // DIFFERS: binary uses ::operator delete; port uses delete[] to match
    // our GetFileData implementations (FileSystem_Direct uses new[]).
    if (m_bOwnsBuffer && m_pData) {
        delete[] static_cast<unsigned char*>(m_pData);
    }
    m_pData = 0;
    Close();
    m_bLocked = false;
    m_bIsLoaded = false;
    m_bIsOpen = false;
    m_sizeCache = 0;
}

// Binary @ 0x0025164c
bool File::Exists(const char* path, unsigned long systemID) {
    if (!path) return false;
    return FileManager::GetInstance().FileExists(path, systemID);
}

// Binary @ 0x0025166c
long File::SizeOfFile(const char* path, unsigned long systemID) {
    if (!path) return -1L;
    unsigned int sz = FileManager::GetInstance().FileSize(path, systemID);
    if (sz == 0xFFFFFFFFu) return -1L;
    return (long)sz;
}

// Binary @ 0x002517aa
bool File::IsOpen() const { return m_bIsOpen; }
// Binary @ 0x002517a6
bool File::IsLoaded() const { return m_bIsLoaded; }
// Binary @ 0x002517b2
bool File::CanWrite() const { return (m_mode & 1) != 0; }
// Binary @ 0x002517b6
void* File::Data() const { return m_pData; }
// Binary @ 0x002517ba
const AsciiString& File::FileName() const { return m_path; }

// Binary @ 0x00251ac8 — tail-calls IFile vtable slot +0x20 (Tell) and returns result.
unsigned int File::GetPosition() const {
    return m_pIFile ? m_pIFile->Tell() : 0u;
}

// Binary @ 0x00251808 — delegates to AsciiString::Hash on m_path.
unsigned int File::Hash() const {
    return m_path.Hash();
}

// Binary @ 0x002517a0
bool File::IsLocked() const { return m_bLocked; }

// Binary @ 0x0025179c
void File::Lock(bool locked) { m_bLocked = locked; }

}  // namespace Mortar
