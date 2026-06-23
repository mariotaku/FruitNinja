// Mortar::AlternativeTextureLoader + SubstituteApparentSizeTextureSource.
//
// AlternativeTextureLoader::CreateLoader @0x002252f8.
// SubstituteApparentSizeTextureSource ctor (SmartPtr&, SmartPtr&) @0x00225ea8.
// SubstituteApparentSizeTextureSourceData ctor @0x00226524.
// SetSource @0x00225c0c; SetApparentSize(SmartPtr&) @0x00225b0c.
//
// Live branch: fast path (both Prefix and Postfix empty in shipped data) -- returns
//   SmartPtr<TextureSource>(TextureLoader::CreateLoader(path)), unchanged.
// Non-live branch: path-rewrite + SubstituteApparentSizeTextureSource composition.
//   The string-building logic is implemented per spec for binary fidelity.

#include "asset/AlternativeTextureLoader.h"
#include <cstring>
#include <cstdlib>

namespace Mortar {

// Static globals -- _GLOBAL__I_AlternativeTextureLoader.cpp @0x002255f0.
// Both constructed as empty AsciiStrings (the shipped default).
AsciiString AlternativeTextureLoader::Prefix;
AsciiString AlternativeTextureLoader::Postfix;

// ---------------------------------------------------------------------------
// SubstituteApparentSizeTextureSourceData ctor @0x00226524.
// AutoLocks the inner data source, copies DataInfo, overrides w/h with apparent values.
// ---------------------------------------------------------------------------
SubstituteApparentSizeTextureSourceData::SubstituteApparentSizeTextureSourceData(
        TextureSource* inner,
        unsigned long apparentW,
        unsigned long apparentH)
    : m_innerLock(inner) // TextureSourceAutoLock: calls inner->LockLayers()
    , m_apparentW((uint16_t)apparentW)
    , m_apparentH((uint16_t)apparentH)
{
    TextureSourceData* innerData = m_innerLock.m_data;
    if (innerData) {
        // Binary: memcpy DataInfo(this+4) <- innerData->DataInfo (8 words: +4..+0x20).
        // Port: copy the DataInfo struct, then override w/h.
        info = innerData->info;
        pixels     = innerData->pixels;
        pixelsSize = innerData->pixelsSize;
    }
    // Override apparent dims with the scale-substitute values.
    // Binary: *(this+0x1c) = apparentW; *(this+0x20) = apparentH (DataInfo +0x18/+0x1c).
    info.apparentWidth  = m_apparentW;
    info.apparentHeight = m_apparentH;
}

// ---------------------------------------------------------------------------
// SubstituteApparentSizeTextureSource -- default ctor.
// ---------------------------------------------------------------------------
SubstituteApparentSizeTextureSource::SubstituteApparentSizeTextureSource()
    : m_DataSource(0)
    , m_ApparentSizeSource(0)
    , m_ApparentW(0)
    , m_ApparentH(0)
{
}

// ---------------------------------------------------------------------------
// SubstituteApparentSizeTextureSource -- (SmartPtr& src, u16 w, u16 h) ctor.
// Binary @0x00225e0c: SetSource(src); SetApparentSize(w, h).
// ---------------------------------------------------------------------------
SubstituteApparentSizeTextureSource::SubstituteApparentSizeTextureSource(
        SmartPtr<TextureSource>& src, uint16_t w, uint16_t h)
    : m_DataSource(0)
    , m_ApparentSizeSource(0)
    , m_ApparentW(0)
    , m_ApparentH(0)
{
    SetSource(src);
    SetApparentSize(w, h);
}

// ---------------------------------------------------------------------------
// SubstituteApparentSizeTextureSource -- (SmartPtr& data, SmartPtr& size) ctor.
// Binary @0x00225ea8: SetSource(param1); SetApparentSize(param2).
// Used by AlternativeTextureLoader::CreateLoader.
// ---------------------------------------------------------------------------
SubstituteApparentSizeTextureSource::SubstituteApparentSizeTextureSource(
        SmartPtr<TextureSource>& dataSrc, SmartPtr<TextureSource>& apparentSizeSrc)
    : m_DataSource(0)
    , m_ApparentSizeSource(0)
    , m_ApparentW(0)
    , m_ApparentH(0)
{
    SetSource(dataSrc);
    SetApparentSize(apparentSizeSrc);
}

SubstituteApparentSizeTextureSource::~SubstituteApparentSizeTextureSource() {
    if (m_DataSource) {
        m_DataSource->Release();
        m_DataSource = 0;
    }
    if (m_ApparentSizeSource) {
        m_ApparentSizeSource->Release();
        m_ApparentSizeSource = 0;
    }
}

// SetSource @0x00225c0c: un/register as callee on inner source events, store at +0x1c.
// Binary: un-registers old source's Event1(+0x14) and Event0(+0x0c) callbacks,
//   SetPtrCast into +0x1c, re-registers + TriggerFormatChanged.
// Port: simplified -- store raw pointer (AddRef on set, Release on previous).
void SubstituteApparentSizeTextureSource::SetSource(SmartPtr<TextureSource>& src) {
    TextureSource* prev = m_DataSource;
    m_DataSource = src.Get();
    if (m_DataSource) m_DataSource->AddRef();
    if (prev)         prev->Release();
    if (m_DataSource) {
        TriggerFormatChanged();
    }
}

// SetApparentSize(SmartPtr&) @0x00225b0c: register on apparent source Event0, store at +0x20.
// Port: simplified -- store raw pointer.
void SubstituteApparentSizeTextureSource::SetApparentSize(SmartPtr<TextureSource>& src) {
    TextureSource* prev = m_ApparentSizeSource;
    m_ApparentSizeSource = src.Get();
    if (m_ApparentSizeSource) m_ApparentSizeSource->AddRef();
    if (prev)                 prev->Release();
}

// SetApparentSize(u16, u16): store w/h into +0x24/+0x26.
void SubstituteApparentSizeTextureSource::SetApparentSize(uint16_t w, uint16_t h) {
    m_ApparentW = w;
    m_ApparentH = h;
}

// LockLayers: allocate SubstituteData, AutoLock inner, copy + override DataInfo.
TextureSourceData* SubstituteApparentSizeTextureSource::LockLayers() {
    if (!m_DataSource) return 0;

    // Determine apparent W/H: from m_ApparentSizeSource if available, else m_ApparentW/H.
    unsigned long aw = m_ApparentW;
    unsigned long ah = m_ApparentH;
    if (m_ApparentSizeSource) {
        TextureSourceAutoLock sizeLock(m_ApparentSizeSource);
        if (sizeLock.m_data) {
            aw = sizeLock.m_data->info.apparentWidth;
            ah = sizeLock.m_data->info.apparentHeight;
        }
    }

    SubstituteApparentSizeTextureSourceData* d =
        new SubstituteApparentSizeTextureSourceData(m_DataSource, aw, ah);
    return d;
}

// UnlockLayers: delete the SubstituteData (its dtor releases the inner AutoLock).
void SubstituteApparentSizeTextureSource::UnlockLayers(TextureSourceData const* data) {
    delete data;
}

// ---------------------------------------------------------------------------
// AlternativeTextureLoader::CreateLoader @0x002252f8.
//
// Fast path (both Prefix/Postfix empty): returns SmartPtr<TextureSource>
//   wrapping TextureLoader::CreateLoader(path) unmodified.
// Slow path: builds substitute path = dir(path) + Prefix + filename(path) + Postfix,
//   then if substitute file exists composes SubstituteApparentSizeTextureSource.
//   data = substitute, apparent size = original.
// ---------------------------------------------------------------------------
SmartPtr<TextureSource> AlternativeTextureLoader::CreateLoader(const AsciiString& path) {
    // Load original path (lazy TextureLoader, File::Exists-gated).
    SmartPtr<TextureLoader> baseLoader = TextureLoader::CreateLoader(path);
    SmartPtr<TextureSource> origSrc;
    if (baseLoader.IsValid()) {
        origSrc = SmartPtr<TextureSource>(baseLoader.Get());
    }

    // Fast path: both Prefix and Postfix have length == 1 (i.e. empty; AsciiString
    // length field = length-including-NUL, so empty => length==1 from spec comment).
    // Binary check: Prefix.length()==1 && Postfix.length()==1.
    // Port: use Empty() which tests m_size == 0; the shipped default has both empty.
    if (Prefix.Empty() && Postfix.Empty()) {
        return origSrc;
    }

    // Build substitute path = dir + Prefix + filename + Postfix.
    const char* p  = path._GetPtr();
    const char* sl = ::strrchr(p, '/');
    const char* bs = ::strrchr(p, '\\');
    // Start of filename: take the later of sl+1 and bs+1.
    const char* fn = p;
    if (sl  && sl  + 1 > fn) fn = sl  + 1;
    if (bs  && bs  + 1 > fn) fn = bs  + 1;

    // Length calculations (spec: -3 = -1 for each NUL in path/Prefix/Postfix counts).
    // path.Length() = strlen (without NUL); Prefix.Length() = strlen; Postfix.Length() = strlen.
    unsigned long dirLen    = (unsigned long)(fn - p);
    unsigned long baseLen   = (unsigned long)(path.Length() - dirLen);  // filename portion
    unsigned long totalLen  = dirLen + Prefix.Length() + baseLen + Postfix.Length();

    AsciiString sub;
    sub.Resize(totalLen);
    char* d = const_cast<char*>(sub._GetPtr());
    ::memcpy(d, p, dirLen);                              d += dirLen;
    ::memcpy(d, Prefix._GetPtr(), Prefix.Length());      d += Prefix.Length();
    ::memcpy(d, fn, baseLen);                            d += baseLen;
    ::memcpy(d, Postfix._GetPtr(), Postfix.Length());
    // Resize already NUL-terminated the buffer.

    SmartPtr<TextureLoader> subLoader = TextureLoader::CreateLoader(sub);
    SmartPtr<TextureSource> subSrc;
    if (subLoader.IsValid()) {
        subSrc = SmartPtr<TextureSource>(subLoader.Get());
    }

    if (!subSrc.IsValid()) {
        return origSrc;   // substitute file missing -> original unchanged
    }
    if (!origSrc.IsValid()) {
        return subSrc;    // original missing -> substitute
    }

    // Both exist: compose.
    // Binary arg order @0x00225ea8: SubstituteApparentSizeTextureSource(this, &subSrc, &origSrc)
    //   => SetSource(subSrc) = data (substitute provides pixels),
    //      SetApparentSize(origSrc) = apparent size (original's W/H is reported).
    SubstituteApparentSizeTextureSource* s =
        new SubstituteApparentSizeTextureSource(subSrc, origSrc);
    return WrapPtr(static_cast<TextureSource*>(s));
}

} // namespace Mortar
