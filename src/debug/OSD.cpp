// OSD (on-screen debug display) — no-op stubs.
// The debug OSD is compiled out in v1.6.1; both functions are stub bodies.

#include "OSD.h"

// ASM-spec v1.6.1 OSD_Init @0x1ca2b4: empty body (single bx lr).
void OSD_Init() {}

// ASM-spec v1.6.1 OSD_AddMessage @0x1ca2b8: identity — returns argument unchanged.
const char* OSD_AddMessage(const char* s) {
    return s;
}
