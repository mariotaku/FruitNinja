// Defunct: GameSpy screen -- empty TU; v1.6.1 global.constructors.keyed.to.GameSpyScreen.cpp @ 0x001891e4
// (address confirmed correct -- it is the keyed static-init ctor, not a class method).
// GameSpyScreen.cpp was compiled into the binary but emits only a _GLOBAL__I_ static-init
// constructor; no GameSpyScreen class, methods, or vtable exist in the binary.
// GameSpy connectivity is surfaced through Mortar::NetworkManager (provider enum).
