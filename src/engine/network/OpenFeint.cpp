// Defunct: OpenFeint / GameCenter online services -- no-op stubs.
// OpenFeintOnline / UserChoseOpenFeint: v1.6.1 binary around 0x0011f000-0x0011f100 area.
// ConnectGameCenter / UserChoseGameCenter: v1.6.1 NetworkManager area.
// All four are free functions (not NetworkManager members) in the binary.

// Defunct: OpenFeint -- no-op stub; v1.6.1 OpenFeintOnline @0x001543f4
void OpenFeintOnline() {
}

// Defunct: OpenFeint -- no-op stub; v1.6.1 UserChoseOpenFeint @0x001cab9c
void UserChoseOpenFeint(int /*choice*/) {
}

// Defunct: GameCenter -- no-op stub; v1.6.1 ConnectGameCenter @0x0011a200
// (free-fn shim that calls NetworkManager::GetInstance()->ConnectGameCenter())
void ConnectGameCenter() {
}

// Defunct: GameCenter -- no-op stub; v1.6.1 UserChoseGameCenter @0x001cab94
void UserChoseGameCenter(int /*choice*/) {
}
