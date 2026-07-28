// Defunct: DRMManager -- empty TU; v1.6.1 global.constructors.keyed.to.DRMManager.cpp @ 0x001348d8
// (address confirmed correct -- it is the keyed static-init ctor, not a class method).
// DRMManager.cpp was compiled into the binary but emits only a _GLOBAL__I_ static-init
// constructor; no DRMManager class, methods, or vtable exist in the binary.
// DRM is implemented as free function IsLicensed() + Game::SetAppLicensed/GetAppLicensedState.
