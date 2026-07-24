#include "MpTransport.h"

// Port-only enhancement (feat/mp-revival): no binary counterpart, so no
// // DIFFERS / // ASM-verified markers apply to this file.

namespace Mortar {

static IMpTransport* g_MpTransport = 0;

void SetMpTransport(IMpTransport* t) {
    g_MpTransport = t;
}

IMpTransport* GetMpTransport() {
    return g_MpTransport;
}

} // namespace Mortar
