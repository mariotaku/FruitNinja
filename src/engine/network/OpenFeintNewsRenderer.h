#ifndef FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H
#define FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H

// Defunct: OpenFeintNewsRenderer -- in-game news overlay; no-op stub.
// Binary ctor @ 0x00191a94. Size ~0x10D4 (includes 4KB news buffer).

#include <cstdint>

namespace Mortar {

class OpenFeintNewsRenderer {
public:
    OpenFeintNewsRenderer() {}
    ~OpenFeintNewsRenderer() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; binary @ 0x00190a4c
    void StartNewsRender(void* /*texture*/, void* /*font*/) {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; binary @ 0x001900d0
    void CancelNewsRender() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    void Draw() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    void Update(float /*dt*/) {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; binary @ 0x00190a30
    void GetNewsString() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub; binary @ 0x00190840
    void ProcessNewsString() {}

    // Defunct: OpenFeintNewsRenderer -- no-op stub
    void Destroy() {}

private:
    uint8_t m_pad[0x10D4];
};

} // namespace Mortar

#endif // FN_ENGINE_NETWORK_OPEN_FEINT_NEWS_RENDERER_H
