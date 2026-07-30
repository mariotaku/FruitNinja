#ifndef FN_ENGINE_ASSET_TEXTURE_SOURCE_H
#define FN_ENGINE_ASSET_TEXTURE_SOURCE_H

// Mortar::TextureSource -- abstract ref-counted texture data provider.
//
// Binary layout (0x1c bytes):
//   +0x00  void*  vptr              (vtable @ 0x2cf8f8 for TextureLoader subclass)
//   +0x04  int    m_RefCount        (ReferenceCounter strong count)
//   +0x08  int    m_WeakCount       (ReferenceCounter weak-data ptr; port: inline int)
//   +0x0c  8B     m_OnFormatChanged (Event0 = std::list<Delegate0<void>>, sentinel-only)
//   +0x14  8B     m_OnDataChanged   (Event1<MortarRectangleT<long>> = std::list<Delegate1<...>>, sentinel)
//   +0x1c  end
//
// Binary vtable @ 0x2cf8f8 (TextureLoader concrete slots):
//   [0] @0x002271b8  ~dtor in-place
//   [1] @0x00227200  ~dtor deleting
//   [2] @0x00159e6c  GetRefCounter()      (returns void; ReferenceCounter slot)
//   [3] @0x00226374  TriggerFormatChanged() fires m_OnFormatChanged (Event0 via PTR@0x2d646c)
//   [4] @0x00226674  TriggerDataChanged(MortarRectangleT<long>) fires m_OnDataChanged (Event1)
//   [5] @0x00226ee8  LockLayers() -> Data*
//   [6] @0x00226e74  UnlockLayers(Data const*)
//   [7] @0x00227154  GetHash() const -> *(this+0x50)
//   [8] @0x00227000  Debug_ToString() const
//
// ~TextureSource @0x2268d8: clears m_OnDataChanged (+0x14) then m_OnFormatChanged (+0x0c).
// Both lists init self-referential (sentinel prev=next=&self).

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/Event.h"
#include "core/MortarTypes.h"
#include <cstdint>

namespace Mortar {

// NumberFormat -- type enum used by TextureInfo::DataInfo at +0x0c.
// Values inferred from Tex1/Tex2/DDS reader code; only the values used by
// the port's live paths are named here.
enum NumberFormat_e {
    NF_UNORM   = 0,
    NF_FLOAT   = 1,
    NF_SNORM   = 2,
    NF_UINT    = 3,
    NF_SINT    = 4
};


// ----------------------------------------------------------------------------
// TextureInfo sub-namespace -- format/layout descriptors.
// Binary: Mortar::TextureInfo::* (v1.6.1).
// A global alias `namespace TextureInfo = Mortar::TextureInfo;` is defined at
// the bottom of this header so existing TextureInfo::Foo uses compile unchanged.
// ----------------------------------------------------------------------------
namespace TextureInfo {

// TextureInfo::PixelFormat -- opaque 12-byte channel-mapping block.
// Binary: Mortar::TextureInfo::PixelFormat (nested). The full channel-mapping
// sub-struct has named ChannelMapping[] fields in the binary, but the port
// treats it as an opaque blob (only Tex1/DDS paths need the detail; Tex1 is
// handled by direct GL format selection; DDS full decode is TODO).
struct PixelFormat {
    uint8_t data[12];
    PixelFormat() {
        for (int i = 0; i < 12; ++i) data[i] = 0;
    }
};

// TextureInfo::ChannelDescription -- 2-byte channel descriptor.
// Binary v1.6.1 Mortar::TextureInfo::ChannelDescription (2 bytes).
// Used by PixelFormat[4] channel array and GetConversionChannelRank.
struct ChannelDescription {
    uint8_t m_Bits;     // +0x00 -- bit depth
    uint8_t m_TypeFlag; // +0x01 -- channel type (low 7 bits); bit 7 = extra flag
};

// TextureInfo::Compression -- 2-byte compression descriptor.
// Binary v1.6.1 Mortar::TextureInfo::Compression (2 bytes).
// ASM-spec v1.6.1 Read(DataStreamReader&, Compression&) @0x0026bb84: ReadRaw x2.
struct Compression {
    uint8_t m_Field0; // +0x00
    uint8_t m_Field1; // +0x01
};

// TextureInfo::NumberFormat -- 2-byte number format descriptor.
// Binary v1.6.1 Mortar::TextureInfo::NumberFormat (2 bytes).
// ASM-spec v1.6.1 Read(DataStreamReader&, NumberFormat&) @0x0026bb44:
//   reads 2 raw bytes; m_Field1 is bit-shuffled from the on-disk packed format.
struct NumberFormat {
    uint8_t m_Field0; // +0x00 -- read raw
    uint8_t m_Field1; // +0x01 -- bit shuffle: (raw>>7)|((raw&0x7f)>>4)<<1|((raw&0x0f)<<4)
};

// TextureInfo::TextureType -- 1-byte texture type.
// Binary v1.6.1 Mortar::TextureInfo::TextureType (1 byte).
// ASM-spec v1.6.1 Read(DataStreamReader&, TextureType&) @0x0026bba4:
//   ReadBasicType<unsigned long>(v); m_Value = (uint8_t)v.
struct TextureType {
    uint8_t m_Value; // +0x00
};

// TextureInfo::DataInfo -- 0x20-byte header describing one texture layer.
// Binary ctor @0x0022630c; Read @0x0026bbec.
//
// Layout:
//   +0x00  PixelFormat (12B, channel-mapping opaque block)
//   +0x0c  u8  numberFormat/type enum (NumberFormat_e)
//   +0x0d  1B  pad
//   +0x0e  u16 rawWidth     (ctor default=1)
//   +0x10  u16 rawHeight    (ctor default=1)
//   +0x12  u16 depth        (ctor default=1)
//   +0x14  u16 levels/arraySize (ctor default=1)
//   +0x16  2B  pad
//   +0x18  u32 apparentWidth   -- apparent pixel width; MissControl reads Texture+0x24 = this field
//   +0x1c  u32 apparentHeight  -- apparent pixel height; Texture+0x28 = this field
struct DataInfo {
    PixelFormat pixelFormat; // +0x00 (TextureInfo::PixelFormat -- same namespace, unqualified)
    uint8_t  numberFormat;           // +0x0c
    uint8_t  _pad0d;                 // +0x0d
    uint16_t rawWidth;               // +0x0e  (was: width)
    uint16_t rawHeight;              // +0x10  (was: height)
    uint16_t depth;                  // +0x12
    uint16_t levels;                 // +0x14  (was: arraySize)
    uint16_t _pad16;                 // +0x16
    uint32_t apparentWidth;          // +0x18  consumers read Texture+0x24 here
    uint32_t apparentHeight;         // +0x1c  consumers read Texture+0x28 here

    // Binary ctor @0x0022630c
    DataInfo()
        : numberFormat(0)
        , _pad0d(0)
        , rawWidth(1)
        , rawHeight(1)
        , depth(1)
        , levels(1)
        , _pad16(0)
        , apparentWidth(1)
        , apparentHeight(1)
    {}
};

#if defined(__bada__)
namespace { struct _DataInfoSizeCheck {
    static_assert(sizeof(DataInfo) == 0x20, "TextureInfo::DataInfo size mismatch");
    static_assert(offsetof(DataInfo, numberFormat)   == 0x0c, "DataInfo::numberFormat offset");
    static_assert(offsetof(DataInfo, rawWidth)       == 0x0e, "DataInfo::rawWidth offset");
    static_assert(offsetof(DataInfo, rawHeight)      == 0x10, "DataInfo::rawHeight offset");
    static_assert(offsetof(DataInfo, apparentWidth)  == 0x18, "DataInfo::apparentWidth offset");
    static_assert(offsetof(DataInfo, apparentHeight) == 0x1c, "DataInfo::apparentHeight offset");
}; }
#endif

} // namespace TextureInfo

// Bridge so existing Mortar::PixelFormat / unqualified PixelFormat uses (inside
// namespace Mortar) keep compiling unchanged now that the binary-faithful
// declaration moved to the nested Mortar::TextureInfo::PixelFormat -- see
// TextureFileFormat.h/.cpp Read(DataStreamReader&, PixelFormat&) @0x0026bbc0.
typedef TextureInfo::PixelFormat PixelFormat;

// Forward declarations.
class TextureSourceAutoLock;

// Mortar::TextureSource -- abstract ref-counted texture data provider.
// Binary size = 0x1c.
//
// TextureSource::Data is the NESTED base class for locked-layer payloads returned
// by LockLayers(). Declared inside TextureSource so that UnlockLayers mangles as
//   _ZN...13TextureSource12UnlockLayersEPKNS0_4DataE (nested type)
// matching the binary's mangled symbols exactly.
//
// Binary: each reader allocates a different concrete Data subclass
//   (Tex1Data @0x48, Tex2Data @0x48, DDSTextureData @0x44, Tex3Data @0x4c).
// Port: all concrete Data types share this common base.
class TextureSource : public ReferenceCounter {
public:
    // Nested Data -- locked-layer payload base class.
    // Declared here (not globally) so UnlockLayers(Data const*) mangles as
    // TextureSource::Data, matching v1.6.1 binary mangled names.
    struct Data {
        TextureInfo::DataInfo info;   // parsed header fields
        const void*           pixels; // pointer into the mapped file buffer (not owned)
        unsigned long         pixelsSize;

        Data() : pixels(0), pixelsSize(0) {}
        virtual ~Data() {}
        // TODO: v1.6.1 0x2cf7e0 (Mortar::TextureSource::Data::vtable) -- binary base Data has a pure-virtual
        // GetLayer(unsigned long,unsigned long,unsigned long&) const (Tex1Data/SubstituteApparentSize
        // override it); port Data has only a virtual dtor. Vtable-shape divergence, defer to #376.
    };

    // Convenience typedefs (in-class scope).
    typedef TextureSourceAutoLock AutoLock;

    // Binary ctor: init self-referential sentinel lists.
    TextureSource();
    // Binary ~TextureSource @0x2268d8: clears +0x14 list then +0x0c list.
    virtual ~TextureSource();

    // Vtable slot [2] @0x00159e6c -- GetRefCounter (no-op stub; base calls into ReferenceCounter)
    virtual void GetRefCounter() {}

    // Vtable slot [3] @0x00226374 -- fires m_OnFormatChanged Event0
    virtual void TriggerFormatChanged();

    // Vtable slot [4] @0x00226674 -- fires m_OnDataChanged with a MortarRectangleT<long> arg.
    // Binary v1.6.1 TriggerDataChanged(MortarRectangleT<long>) @0x00226674.
    virtual void TriggerDataChanged(MortarRectangleT<long> rect);

    // Vtable slot [5] @0x00226ee8 -- return locked pixel data (concrete override)
    virtual Data* LockLayers() = 0;

    // Vtable slot [6] @0x00226e74 -- release locked data (concrete override)
    virtual void UnlockLayers(Data const* data) = 0;

    // Vtable slot [7] @0x00227154 -- hash (overridden by TextureLoader to return m_PathHash)
    virtual unsigned int GetHash() const { return 0; }

    // Vtable slot [8] @0x00227000 -- debug string (not used in live paths)
    virtual const char* Debug_ToString() const { return "TextureSource"; }

    // +0x0c: format-changed event (Event0 = list<Delegate0<void>>)
    Event0 m_OnFormatChanged;
    // +0x14: data-changed event (Event1<MortarRectangleT<long>>)
    Event1<MortarRectangleT<long> > m_OnDataChanged;
};

// Global typedef so existing consumers using TextureSourceData compile unchanged.
// The canonical type is TextureSource::Data (nested); this alias resolves identically
// in mangling (C++ mangler uses declaration site, not typedef alias).
typedef Mortar::TextureSource::Data TextureSourceData;

// TextureSource::AutoLock -- RAII helper that calls LockLayers/UnlockLayers.
// Binary: {SmartPtr<TextureSource> source @+0x00, TextureSourceData* data @+0x04}.
// Size = 8 bytes (SmartPtr<T> is a single 4-byte T* + a 4-byte data ptr on ARM32).
// ASM-spec v1.6.1 TextureSource::AutoLock @0x002264e0: SmartPtr member (AddRef via
// SetPtrCast), LockLayers unconditional; dtor UnlockLayers if m_data then Release.
class TextureSourceAutoLock {
public:
    Mortar::SmartPtr<TextureSource> m_source;
    TextureSourceData*              m_data;

    // Binary ctor @0x002264e0: AddRef via SmartPtr copy, then unconditional LockLayers
    // (no null guard in the binary).
    explicit TextureSourceAutoLock(const Mortar::SmartPtr<TextureSource>& src);
    // Binary dtor: UnlockLayers(m_data) if m_data, then SmartPtr releases m_source.
    ~TextureSourceAutoLock();
};

#if defined(__bada__)
namespace { struct _TextureSourceLayoutCheck {
    static_assert(sizeof(TextureSource) == 0x1c,
                  "TextureSource size mismatch (must be 0x1c)");
    static_assert(offsetof(TextureSource, m_OnFormatChanged) == 0x0c,
                  "TextureSource::m_OnFormatChanged offset");
    static_assert(offsetof(TextureSource, m_OnDataChanged)   == 0x14,
                  "TextureSource::m_OnDataChanged offset");
}; }
#endif

} // namespace Mortar

// Global alias so existing TextureInfo::Foo uses in consumers compile unchanged.
// Binary namespace is Mortar::TextureInfo; the alias makes unqualified TextureInfo::
// references resolve to it without editing every consumer TU.
namespace TextureInfo = Mortar::TextureInfo;

#endif // FN_ENGINE_ASSET_TEXTURE_SOURCE_H
