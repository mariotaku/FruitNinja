// FNHighscore -- leaderboard score entry (v1.6.1 binary).
// Binary: default ctor @ 0x00178d5c, param ctor @ 0x00137e48, IsCurrentUser @ 0x00137034.

#include "game/FNHighscore.h"
#include "engine/network/NetworkManager.h"
#include "engine/util/StringHash.h"

#include <cstring>

// ASM-spec v1.6.1 FNHighscore::FNHighscore @ 0x00178d5c: the whole body is 9
// instructions --
//   mov  r2,#0
//   strb r2,[r0,#0x00]   // m_Name[0]     = 0  -- null terminator ONLY
//   strb r2,[r0,#0x20]   // m_ExtraStr[0] = 0  -- null terminator ONLY
//   str  r2,[r0,#0x40]   // m_NameHash
//   str  r2,[r0,#0x48]   // m_Rank
//   str  r2,[r0,#0x44]   // m_Score
//   str  r2,[r0,#0x4c]   // m_UserData
//   strb r2,[r0,#0x50]   // m_IsCurrentUser
//   bx   lr
// So the binary zeroes fields 0x40..0x50 (that part of the old comment was right)
// but it does NOT clear m_Name[1..31] / m_ExtraStr[1..31], and never touches _pad.
// The previous "(asm-inspector)" stamp claimed the strings were zeroed outright --
// 9 instructions cannot clear 64 bytes. Demoted to ASM-spec: this is a
// disassembly read, not a compile+diff.
//
// DIFFERS: original = null-terminator byte only (strb at +0x00 / +0x20); port
// memsets both 32-byte arrays and zeroes _pad. The port is a strict superset --
// every byte the binary writes gets the same value -- and the extra zeroing is
// unobservable, since every reader treats m_Name/m_ExtraStr as NUL-terminated C
// strings and so stops at index 0.
FNHighscore::FNHighscore()
{
    memset(m_Name,     0, sizeof(m_Name));
    memset(m_ExtraStr, 0, sizeof(m_ExtraStr));
    m_NameHash       = 0;
    m_Score          = 0;
    m_Rank           = 0;
    m_UserData       = 0;
    m_IsCurrentUser  = false;
    _pad[0] = 0; _pad[1] = 0; _pad[2] = 0;
}

// Binary param ctor @ 0x00137e48.
// Signature: (char* name, unsigned long nameHash, int rank, int score,
//              void* userData, char* extraStr)
// Binary write-back order (Ghidra): param_2->0x40, param_3->0x48, param_4->0x44, param_5->0x4c.
FNHighscore::FNHighscore(const char* name, unsigned long nameHash,
                         int rank, int score, void* userData, const char* extraStr)
{
    strncpy(m_Name,     name ? name : "",         sizeof(m_Name));
    strncpy(m_ExtraStr, extraStr ? extraStr : "", sizeof(m_ExtraStr));
    m_Name[0x1f]     = 0;
    m_ExtraStr[0x1f] = 0;
    m_NameHash     = static_cast<uint32_t>(nameHash);
    m_Rank         = rank;
    m_Score        = score;
    m_UserData     = userData;
    m_IsCurrentUser = IsCurrentUser();
    _pad[0] = 0; _pad[1] = 0; _pad[2] = 0;
}

// Binary @ 0x00137034.
// Returns true when m_NameHash equals StringHash(localPlayerName).
// GameSpy/OpenFeint branch (GetPreferredNetworkProvider()==1) is defunct:
//   LastLoggedInUser() returns nullptr -> falls through to false.
// Local branch: GetPlayerName(0, buf, 0x1f).
bool FNHighscore::IsCurrentUser()
{
    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();
    int provider = nm->GetPreferredNetworkProvider();
    if (provider == 1) {
        // Defunct: online leaderboard -- no-op stub; v1.6.1 binary @ 0x00137034
        // LastLoggedInUser() returns nullptr in the defunct stub -> return false.
        return false;
    }
    char buf[32];
    buf[0] = 0;
    nm->GetPlayerName(0, buf, 0x1f);
    const char* playerName = (buf[0] != 0) ? buf : "";
    return m_NameHash == StringHash(playerName);
}
