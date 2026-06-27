// Analysed: 2026-05-04T00:00
// IFile_Direct — portable FILE*-backed IFile implementation.
// FILE* semantics are identical on POSIX and Win32; no platform split needed.
#include "asset/IFile_Direct.h"
#include "asset/IFileSystem.h"

#include <cstdio>
#include <cstring>

namespace Mortar {

// Binary @ IFile_Direct ctor (IFileSystem*, FILE*, unsigned long size)
IFile_Direct::IFile_Direct(IFileSystem* sys, FILE* fp, unsigned long size)
    : IFile(sys)   // calls RegisterIFile
    , m_size(size)
    , m_cursor(0)
    , m_fp(fp)
{
}

// Binary @ IFile_Direct dtor — closes the file before IFile base dtor (DeregisterIFile)
IFile_Direct::~IFile_Direct() {
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
}

// Binary @ IFile vtbl+0x08 (IFile_Direct slot 2)
unsigned int IFile_Direct::Size() {
    return m_size;
}

// Binary @ IFile vtbl+0x0c (IFile_Direct slot 3)
void IFile_Direct::Close() {
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
    }
    m_cursor = 0;
}

// Binary @ IFile vtbl+0x10 (IFile_Direct slot 4)
bool IFile_Direct::Read(void* dst, unsigned long n) {
    if (!m_fp || !dst) return false;
    return fread(dst, 1, (size_t)n, m_fp) == n;
}

// Binary @ IFile vtbl+0x14 (IFile_Direct slot 5) — encryption path; defunct, delegate to Write
bool IFile_Direct::WriteEncrypted(const void* src, unsigned long n) {
    // Defunct: encryption — WriteEncrypted (v1.6.1: encryption defunct — IFile vtable slot +0x14, no standalone symbol)
    // DIFFERS: original may encrypt, using plain Write because encryption is defunct.
    return Write(src, n);
}

// Binary @ IFile vtbl+0x18 (IFile_Direct slot 6)
bool IFile_Direct::Write(const void* src, unsigned long n) {
    if (!m_fp || !src) return false;
    return fwrite(src, 1, (size_t)n, m_fp) == n;
}

// Binary @ IFile vtbl+0x1c (IFile_Direct slot 7)
// mode: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END (matches POSIX / FileSeekMode)
int IFile_Direct::Seek(int mode, long offset) {
    if (!m_fp) return -1;
    int posixWhence;
    if (mode == 0)      posixWhence = SEEK_SET;
    else if (mode == 1) posixWhence = SEEK_CUR;
    else                posixWhence = SEEK_END;
    int result = fseek(m_fp, offset, posixWhence);
    if (result == 0) {
        long pos = ftell(m_fp);
        if (pos >= 0) m_cursor = (unsigned long)pos;
    }
    return result;
}

// Binary @ IFile vtbl+0x20 (IFile_Direct slot 8)
unsigned int IFile_Direct::Tell() {
    if (!m_fp) return m_cursor;
    long pos = ftell(m_fp);
    if (pos >= 0) m_cursor = (unsigned long)pos;
    return m_cursor;
}

} // namespace Mortar
