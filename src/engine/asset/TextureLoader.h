#ifndef FN_ENGINE_ASSET_TEXTURE_LOADER_H
#define FN_ENGINE_ASSET_TEXTURE_LOADER_H

// Mortar::TextureLoader -- concrete file-backed TextureSource.
//
// Binary: operator new(0x54). Vtable @ 0x2cf8f8 (inherits TextureSource 9-slot vtable).
// Factory: CreateLoader(AsciiString const&) @0x002270f8 -- gates on File::Exists,
//   returns SmartPtr<TextureLoader> (lazy: no parse at construction time).
// LockLayers() @0x00226ee8 -- opens the file on first lock, iterates g_readers[4]
//   (first non-null wins); stores result in m_LoadedData.
// UnlockLayers() @0x00226e74 -- decrements m_LockCount; on zero: File::Unload, delete File.
//
// Binary field layout (0x54 on 32-bit ARM):
//   +0x00  0x1c  TextureSource base (vptr/rc/weak/2 lists)
//   +0x1c  0x2c  AsciiString m_Path  (binary AsciiString = 0x2c = 44 bytes on Bada ARM)
//   +0x48  4     TextureSource::Data* m_LoadedData
//   +0x4c  4     int   m_LockCount
//   +0x50  4     uint  m_PathHash   (= FileStringHash(path))
//   +0x54       end
//
// DIFFERS: port's AsciiString is 40 bytes (not 44); 4-byte gap before m_LoadedData
//   is filled by m_File (port-specific heap File* cached between Lock/Unlock).
//   On the Bada cross-build (32-bit) AsciiString = 44 bytes, so binary offsets
//   for m_LoadedData/m_LockCount/m_PathHash match exactly without extra padding.

#include "asset/TextureSource.h"
#include "asset/File.h"
#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include <cstdint>

namespace Mortar {

class TextureLoader : public TextureSource {
public:
    // Binary ctor @0x00227064: stores path, computes PathHash, zeros m_LoadedData/m_LockCount.
    explicit TextureLoader(const AsciiString& path);
    // Binary ~TextureLoader @0x002271b8 (in-place) / @0x00227200 (deleting).
    virtual ~TextureLoader();

    // Vtable slot [5] @0x00226ee8 -- open file on first lock, iterate g_readers[].
    virtual TextureSourceData* LockLayers();

    // Vtable slot [6] @0x00226e74 -- decrement lock; on zero release file + data.
    virtual void UnlockLayers(TextureSourceData const* data);

    // Vtable slot [7] @0x00227154 -- return m_PathHash (*(this+0x50)).
    virtual unsigned int GetHash() const;

    // Vtable slot [8] @0x00227000 -- debug: return path string.
    virtual const char* Debug_ToString() const;

    // Factory @0x002270f8: File::Exists gate + lazy TextureLoader construction.
    // Returns SmartPtr<TextureLoader> (null if file missing).
    static SmartPtr<TextureLoader> CreateLoader(const AsciiString& path);

    // --- Binary-faithful field layout ---
    AsciiString         m_Path;         // +0x1c (binary: 0x2c = 44 bytes on ARM32)
    // Port-only: heap File* held between LockLayers/UnlockLayers.
    // Binary @+0x44 has a File* too (new(0x40) File); on the bada cross-build
    // AsciiString is 44 bytes so this field sits at exactly +0x44 after m_Path.
    // DIFFERS: port uses heap File* here rather than stack-allocating in LockLayers.
    File*               m_File;         // port-only; sits at +0x44 on bada (AsciiStr=44B)
    TextureSourceData*  m_LoadedData;   // +0x48 (binary)
    int                 m_LockCount;    // +0x4c (binary)
    unsigned int        m_PathHash;     // +0x50 (binary)
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
namespace { struct _TextureLoaderLayoutCheck {
    static_assert(offsetof(Mortar::TextureLoader, m_Path)       == 0x1c,
                  "TextureLoader::m_Path offset");
    // m_LoadedData, m_LockCount, m_PathHash offsets depend on AsciiString being 44B on bada.
}; }
#endif

#endif // FN_ENGINE_ASSET_TEXTURE_LOADER_H
