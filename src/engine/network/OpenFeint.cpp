// Defunct: OpenFeint / GameCenter online services -- no-op stubs.
// OpenFeintOnline / UserChoseOpenFeint: v1.6.1 binary around 0x0011f000-0x0011f100 area.
// ConnectGameCenter / UserChoseGameCenter: v1.6.1 NetworkManager area.
// All four are free functions (not NetworkManager members) in the binary.

// Defunct: OpenFeint -- no-op stub; v1.6.1 OpenFeintOnline @0x0011f0c0
void OpenFeintOnline() {
}

// Defunct: OpenFeint -- no-op stub; v1.6.1 UserChoseOpenFeint @0x0011f0d4
void UserChoseOpenFeint(int /*choice*/) {
}

// Defunct: GameCenter -- no-op stub; v1.6.1 ConnectGameCenter @0x0011f150
void ConnectGameCenter() {
}

// Defunct: GameCenter -- no-op stub; v1.6.1 UserChoseGameCenter @0x0011f164
void UserChoseGameCenter(int /*choice*/) {
}
