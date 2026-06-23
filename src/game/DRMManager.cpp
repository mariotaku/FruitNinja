// Defunct: DRMManager -- empty TU in v1.6.1 binary @ 0x1348d8 (no class emitted).
// DRMManager.cpp was compiled into the binary but emits only a _GLOBAL__I_ static-init
// constructor; no DRMManager class, methods, or vtable exist in the binary.
// DRM is implemented as free function IsLicensed() + Game::SetAppLicensed/GetAppLicensedState.
