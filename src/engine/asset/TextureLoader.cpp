// Mortar::TextureLoader -- concrete file-backed TextureSource.
// Factory CreateLoader @0x002270f8; ctor @0x00227064;
// LockLayers @0x00226ee8; UnlockLayers @0x00226e74;
// GetHash @0x00227154; Debug_ToString @0x00227000.

#include "asset/TextureLoader.h"
#include "asset/TextureFileFormat.h"
#include "util/StringHash.h"
#include <cstring>

namespace Mortar {

// Binary @0x00227064: init base TextureSource, store path and hash, zero m_LoadedData/m_LockCount.
TextureLoader::TextureLoader(const AsciiString& path)
    : m_Path(path)
    , m_File(0)
    , m_LoadedData(0)
    , m_LockCount(0)
    , m_PathHash(StringHash(path.CStr()))
{
}

// Binary @0x002271b8 (in-place) / @0x00227200 (deleting).
TextureLoader::~TextureLoader() {
    // UnlockLayers handles cleanup of m_LoadedData and m_File.
    // If we're destroyed while locked (shouldn't happen normally), force cleanup.
    if (m_LoadedData) {
        delete m_LoadedData;
        m_LoadedData = 0;
    }
    if (m_File) {
        m_File->Unload();
        delete m_File;
        m_File = 0;
    }
}

// Binary @0x00226ee8 -- TextureLoader::LockLayers.
// Body (from spec):
//   if (++m_LockCount == 1) {
//     f = new(0x40) File(m_Path, 0, 0);
//     if (File::Load(f,0,0)) {
//       iterate g_readers[0..3]; first non-null result wins;
//     }
//     m_LoadedData = result;
//     if (!result) { --m_LockCount; delete f; }
//   }
//   return m_LoadedData;
TextureSourceData* TextureLoader::LockLayers() {
    if (++m_LockCount == 1) {
        File* f = new File(m_Path.CStr(), 0, 0);
        m_File = f;
        TextureSourceData* result = 0;
        if (f->Load(0, 0)) {
            // Iterate the 4-entry reader registry; first non-null wins.
            for (int i = 0; i < 4; ++i) {
                result = g_readers[i](f->Data(), (unsigned long)f->Size());
                if (result) break;
            }
        }
        m_LoadedData = result;
        if (!result) {
            --m_LockCount;
            f->Unload();
            delete f;
            m_File = 0;
        }
    }
    return m_LoadedData;
}

// ASM-spec v1.6.1 TextureLoader::UnlockLayers @0x00226e74:
//  1. identity guard (arg IS checked), 2. plain --lockcount, 3. File released first, 4. then Data (virtual D0).
void TextureLoader::UnlockLayers(TextureSourceData const* data) {
    if (m_LoadedData != data) return;
    if (--m_LockCount != 0) return;
    if (m_File) {
        m_File->Unload();
        delete m_File;
        m_File = 0;
    }
    if (m_LoadedData) {
        delete m_LoadedData;
        m_LoadedData = 0;
    }
}

// Binary @0x00227154 -- return *(this+0x50) = m_PathHash.
unsigned int TextureLoader::GetHash() const {
    return m_PathHash;
}

// Binary @0x00227000 -- return path for debug output.
const char* TextureLoader::Debug_ToString() const {
    return m_Path.CStr();
}

// Binary @0x002270f8 -- factory: File::Exists gate; lazy construction.
// If file doesn't exist returns null SmartPtr.
SmartPtr<TextureLoader> TextureLoader::CreateLoader(const AsciiString& path) {
    if (!File::Exists(path.CStr(), 0)) {
        return SmartPtr<TextureLoader>();
    }
    TextureLoader* t = new TextureLoader(path);
    return WrapPtr(t);
}

} // namespace Mortar
