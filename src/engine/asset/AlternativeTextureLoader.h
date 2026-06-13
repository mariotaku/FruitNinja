#ifndef FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H
#define FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H

// Mortar::AlternativeTextureLoader -- v1.6.1 texture path-rewrite + substitution.
//
// Binary: CreateLoader @0x002252f8 (static factory, returns SmartPtr<TextureSource>).
// Prefix/Postfix globals set at static-init by _GLOBAL__I_AlternativeTextureLoader.cpp
// @0x002255f0 (GOT slots DAT_002255e8 -> &Prefix, DAT_002255ec -> &Postfix).
// Shipped data: both Prefix and Postfix are empty AsciiStrings.
//
// When both Prefix and Postfix are empty (the shipped default), CreateLoader
// short-circuits: returns SmartPtr<TextureSource>(TextureLoader::CreateLoader(path)).
// When either is non-empty: builds a substitute path (dir + Prefix + filename + Postfix),
// creates two TextureLoaders, composes them into a SubstituteApparentSizeTextureSource.
//
// SubstituteApparentSizeTextureSource (@0x00225ea8): wraps a data-source TextureSource
// (supplies pixels) and an apparent-size source (reports W/H). Used when a smaller/
// alternate texture file should pretend to be the original file's dimensions.

#include "asset/TextureSource.h"
#include "asset/TextureLoader.h"
#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include <cstdint>

namespace Mortar {

// ---------------------------------------------------------------------------
// SubstituteApparentSizeTextureSourceData -- locked-layer payload.
// Derives from TextureSourceData. Binary size 0x48 on ARM32.
// Binary ctor @0x00226524.
// Body: AutoLocks inner data source, copies DataInfo, overrides only w/h with
//   apparent values from the apparent-size source (or m_ApparentW/H fields).
// ---------------------------------------------------------------------------
struct SubstituteApparentSizeTextureSourceData : public TextureSourceData {
    // AutoLock keeping the inner data source locked for our lifetime.
    TextureSourceAutoLock m_innerLock; // size 8 (two pointers)
    // Apparent size fields (from the outer source's m_ApparentW/H).
    uint16_t m_apparentW;
    uint16_t m_apparentH;

    // Binary ctor @0x00226524: AutoLock inner, copy DataInfo, override w/h.
    SubstituteApparentSizeTextureSourceData(TextureSource* inner,
                                            unsigned long apparentW,
                                            unsigned long apparentH);
};

// ---------------------------------------------------------------------------
// Mortar::SubstituteApparentSizeTextureSource -- concrete TextureSource.
// Binary: operator new(0x28).
// Layout (0x28 bytes):
//   +0x00  0x1c  TextureSource base
//   +0x1c  4     TextureSource* m_DataSource         (SetSource: real pixels)
//   +0x20  4     TextureSource* m_ApparentSizeSource (SetApparentSize: W/H)
//   +0x24  2     u16 m_ApparentW
//   +0x26  2     u16 m_ApparentH
//
// Ctors:
//   default @0x002259 3c  -- all null
//   (SmartPtr&, u16, u16) @0x00225e0c -- SetSource(src); SetApparentSize(w,h)
//   (SmartPtr&, SmartPtr&) @0x00225ea8 -- SetSource(param1); SetApparentSize(param2)
//   <- used by AlternativeTextureLoader::CreateLoader.
// ---------------------------------------------------------------------------
class SubstituteApparentSizeTextureSource : public TextureSource {
public:
    // Default ctor @0x002259 3c: all null.
    SubstituteApparentSizeTextureSource();

    // (SmartPtr& data, u16 w, u16 h) @0x00225e0c:
    //   SetSource(data); SetApparentSize(w, h).
    SubstituteApparentSizeTextureSource(SmartPtr<TextureSource>& src,
                                        uint16_t w, uint16_t h);

    // (SmartPtr& data, SmartPtr& apparentSize) @0x00225ea8:
    //   SetSource(data); SetApparentSize(apparentSize).
    // Used by AlternativeTextureLoader::CreateLoader (binary arg order confirmed).
    SubstituteApparentSizeTextureSource(SmartPtr<TextureSource>& dataSrc,
                                        SmartPtr<TextureSource>& apparentSizeSrc);

    virtual ~SubstituteApparentSizeTextureSource();

    // Vtable slot [5]: AutoLock the inner data source, copy DataInfo, override w/h.
    virtual TextureSourceData* LockLayers();

    // Vtable slot [6]: release the SubstituteData (which releases inner AutoLock).
    virtual void UnlockLayers(TextureSourceData const* data);

    // SetSource @0x00225c0c: register on inner source events, store at +0x1c.
    void SetSource(SmartPtr<TextureSource>& src);
    // SetApparentSize(SmartPtr&) @0x00225b0c: register on apparent source Event0, store at +0x20.
    void SetApparentSize(SmartPtr<TextureSource>& src);
    // SetApparentSize(u16,u16) @0x00225e0c variant: store w/h into +0x24/+0x26.
    void SetApparentSize(uint16_t w, uint16_t h);

    // --- Binary-faithful field layout ---
    TextureSource* m_DataSource;         // +0x1c
    TextureSource* m_ApparentSizeSource; // +0x20
    uint16_t       m_ApparentW;          // +0x24
    uint16_t       m_ApparentH;          // +0x26
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
namespace { struct _SubstLayoutCheck {
    static_assert(sizeof(SubstituteApparentSizeTextureSource) == 0x28,
                  "SubstituteApparentSizeTextureSource size mismatch");
    static_assert(offsetof(SubstituteApparentSizeTextureSource, m_DataSource)         == 0x1c,
                  "SubstituteApparentSizeTextureSource::m_DataSource offset");
    static_assert(offsetof(SubstituteApparentSizeTextureSource, m_ApparentSizeSource) == 0x20,
                  "SubstituteApparentSizeTextureSource::m_ApparentSizeSource offset");
    static_assert(offsetof(SubstituteApparentSizeTextureSource, m_ApparentW)          == 0x24,
                  "SubstituteApparentSizeTextureSource::m_ApparentW offset");
    static_assert(offsetof(SubstituteApparentSizeTextureSource, m_ApparentH)          == 0x26,
                  "SubstituteApparentSizeTextureSource::m_ApparentH offset");
}; }
#endif

// ---------------------------------------------------------------------------
// AlternativeTextureLoader -- static factory class.
// ---------------------------------------------------------------------------
class AlternativeTextureLoader {
public:
    // Binary @0x002252f8 -- static factory returning SmartPtr<TextureSource>.
    // Fast path (both Prefix/Postfix empty): returns SmartPtr<TextureSource>
    //   wrapping TextureLoader::CreateLoader(path) unchanged.
    // Slow path (non-empty): builds substitute path, composes with
    //   SubstituteApparentSizeTextureSource.
    static SmartPtr<TextureSource> CreateLoader(const AsciiString& path);

    // Static globals set by _GLOBAL__I_AlternativeTextureLoader.cpp @0x002255f0.
    // Both empty in the shipped binary (GOT DAT_002255e8->&Prefix, DAT_002255ec->&Postfix).
    static AsciiString Prefix;
    static AsciiString Postfix;
};

} // namespace Mortar

#endif // FN_ENGINE_ASSET_ALTERNATIVE_TEXTURE_LOADER_H
