// FNHighscore -- leaderboard score entry (v1.6.1 binary).
// Binary: default ctor @ 0x00178d5c, param ctor @ 0x00137e48, IsCurrentUser @ 0x00137034.

#include "game/FNHighscore.h"
#include "engine/network/NetworkManager.h"
#include "engine/util/StringHash.h"

#include <cstring>

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
bool FNHighscore::IsCurrentUser() const
{
    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();
    int provider = nm->GetPreferredNetworkProvider();
    if (provider == 1) {
        // Defunct: online leaderboard -- no-op stub; binary @ 0x00137034
        // LastLoggedInUser() returns nullptr in the defunct stub -> return false.
        return false;
    }
    char buf[32];
    buf[0] = 0;
    nm->GetPlayerName(0, buf, 0x1f);
    const char* playerName = (buf[0] != 0) ? buf : "";
    return m_NameHash == StringHash(playerName);
}
