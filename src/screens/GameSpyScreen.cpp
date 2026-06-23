// Defunct: GameSpy screen -- empty TU in v1.6.1 binary @ 0x1891e4 (no class emitted).
// GameSpyScreen.cpp was compiled into the binary but emits only a _GLOBAL__I_ static-init
// constructor; no GameSpyScreen class, methods, or vtable exist in the binary.
// GameSpy connectivity is surfaced through Mortar::NetworkManager (provider enum).
