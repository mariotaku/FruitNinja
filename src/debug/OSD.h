#ifndef FN_DEBUG_OSD_H
#define FN_DEBUG_OSD_H

// OSD — on-screen debug overlay stubs.
// Both functions are no-ops/identity: the debug OSD is compiled out in v1.6.1.
// Binary: OSD_Init @0x1ca2b4, OSD_AddMessage @0x1ca2b8

void OSD_Init();
const char* OSD_AddMessage(const char* s);

#endif // FN_DEBUG_OSD_H
