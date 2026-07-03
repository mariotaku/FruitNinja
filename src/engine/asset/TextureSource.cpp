// Mortar::TextureSource -- abstract ref-counted texture data provider.
// Binary ctor (implicit, via subclass ctors): init self-referential sentinel lists.
// Binary ~TextureSource @0x2268d8: clears m_OnDataChanged (+0x14) then m_OnFormatChanged (+0x0c).
// TriggerFormatChanged @0x00226374: fires +0x0c Event0.
// TriggerDataChanged @0x00226674: fires +0x14 Event1.

#include "asset/TextureSource.h"

namespace Mortar {

// AutoLock ctor @0x002264e0: AddRef src into m_source, unconditionally LockLayers
// (binary has no null guard here -- all live call sites already checked non-null).
TextureSourceAutoLock::TextureSourceAutoLock(const Mortar::SmartPtr<TextureSource>& src)
    : m_source(src)
    , m_data(0)
{
    m_data = src.Get()->LockLayers();
}

// AutoLock dtor: releases the locked data (m_source's SmartPtr dtor handles Release).
TextureSourceAutoLock::~TextureSourceAutoLock() {
    if (m_data) {
        m_source.Get()->UnlockLayers(m_data);
    }
}

// Binary ctor: both event lists initialise as self-referential sentinels
// (std::list default ctor handles this).
TextureSource::TextureSource() {
}

// Binary ~TextureSource @0x2268d8: clears m_OnDataChanged (+0x14) then m_OnFormatChanged (+0x0c).
// The binary swaps vptr twice during teardown (first to base-sub-object vtable, then back);
// in C++ the virtual dtor mechanism handles vtable restoration automatically.
TextureSource::~TextureSource() {
    // Event list dtors run via member destructors (std::list clears on dtor).
}

// Vtable slot [3] @0x00226374 -- fires m_OnFormatChanged event (no-arg).
void TextureSource::TriggerFormatChanged() {
    m_OnFormatChanged();
}

// Vtable slot [4] @0x00226674 -- fires m_OnDataChanged event with rect arg.
// Binary v1.6.1 TriggerDataChanged(MortarRectangleT<long>) @0x00226674.
// No subscribers register in the current live paths.
void TextureSource::TriggerDataChanged(MortarRectangleT<long> rect) {
    m_OnDataChanged(rect);
}

} // namespace Mortar
