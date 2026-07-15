#ifndef FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H
#define FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H

// Defunct: OpenFeintNewsRenderer -- in-game news overlay; no-op stub.
// Binary ctor @ 0x00191a94. Polymorphic: vptr @ +0x00 (binary vtable GOT-indirect).
// Binary size = 0x10D8 (4312 bytes, 8-byte aligned): vptr (4B) + 0x10D4 data.
// Confidence: low (no operator-new witness; layout from ctor disasm only).

#include <cstdint>
#include "engine/util/SmartPtr.h"

namespace Mortar {

class Texture;
class Font;

class OpenFeintNewsRenderer {
public:
    // Defunct: OpenFeintNewsRenderer -- no-op stub; v1.6.1 binary @ 0x00191a94
    OpenFeintNewsRenderer() {}

    // Polymorphic root: vptr @ +0x00; binary vtable resolved GOT-indirectly.
    virtual ~OpenFeintNewsRenderer() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; v1.6.1 binary @ 0x00190a4c
    virtual void StartNewsRender(const Mortar::SmartPtr<Mortar::Texture>& /*texture*/, Mortar::Font* /*font*/) {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; v1.6.1 binary @ 0x001900d0
    virtual void CancelNewsRender() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    virtual void Draw() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    virtual void Update(float /*dt*/) {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; v1.6.1 binary @ 0x00190a30
    void GetNewsString() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; v1.6.1 binary @ 0x00190840
    void ProcessNewsString() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    void Destroy() {}

private:
    // Data region after vptr: 0x10D4 bytes covering OpenFeintNewsRenderInfo
    // sub-object at +0x04, panel-rect fields, colour entries, news text buffer
    // (~4KB inline array at ~+0xB0..+0x10AF), and trailing float/flag fields.
    // Not accessed by port code.
    uint8_t m_pad[0x10D4];
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::OpenFeintNewsRenderer) == 0x10D8,
    "Mortar::OpenFeintNewsRenderer must be 0x10D8 bytes on ARM32/Bada");
#endif

#endif // FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H
