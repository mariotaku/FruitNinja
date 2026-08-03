#ifndef FN_GAME_FN_HIGHSCORE_H
#define FN_GAME_FN_HIGHSCORE_H

// FNHighscore -- single leaderboard score entry.
// Binary sizeof = 0x54 (84) bytes.
// Stride proved by 3 embedded copies at FruitFactLeaderboard+0xC8/+0x11C/+0x170
// (diff = 0x54).
//
// Reachability: dead code in v1.6.1. The only caller of the default ctor is the
// thunk @ 0x00115990, reached solely from the two FruitFactLeaderboard ctors
// (0x00176980 / 0x00176d54) -- and those have zero call sites. FNHighscore is a
// linked-but-unreferenced relic, kept per the stub-don't-skip policy.
//
// Binary ctors:
//   Default ctor @ 0x00178d5c -- null-terminates m_Name[0]/m_ExtraStr[0] (one
//     strb each, NOT a full clear of the 32-byte arrays) and zeroes the fields at
//     0x40..0x50. Full body is 9 instructions; see FNHighscore.cpp for the
//     listing and for the port's DIFFERS (it memsets both arrays).
//   Param ctor   @ 0x00137e48 -- (char* name, unsigned long nameHash,
//                                 int rank, int score, void* userData,
//                                 char* extraStr)
//
// Binary param-ctor write-back (0x00137e48):
//   T_896(this+0x00, name);        // strcpy m_Name
//   T_896(this+0x20, extraStr);    // strcpy m_ExtraStr
//   this[0x1f] = 0; this[0x3f] = 0;  // hard null-terminators
//   *(uint32_t*)(this+0x40) = nameHash;  // param_2
//   *(int*)(this+0x48)      = rank;      // param_3 -> 0x48
//   *(int*)(this+0x44)      = score;     // param_4 -> 0x44
//   *(void**)(this+0x4c)    = userData;  // param_5
//   this[0x50] = IsCurrentUser(this);

#include <cstdint>
#include <cstring>

struct FNHighscore {
    // +0x00: player display name (31 chars max + null terminator)
    char     m_Name[32];       // 0x00

    // +0x20: extra string (rank suffix / score string)
    char     m_ExtraStr[32];   // 0x20

    // +0x40: StringHash of player name (used by IsCurrentUser)
    uint32_t m_NameHash;       // 0x40

    // +0x44: score value (param_4 in binary write-back)
    int      m_Score;          // 0x44

    // +0x48: rank (param_3 in binary write-back)
    int      m_Rank;           // 0x48

    // +0x4c: opaque user-data pointer (avatar/profile)
    void*    m_UserData;       // 0x4c

    // +0x50: true if this entry's m_NameHash matches the local player's name hash
    bool     m_IsCurrentUser;  // 0x50

    // +0x51..+0x53: compiler padding to reach 0x54
    uint8_t  _pad[3];          // 0x51

    // Binary default ctor @ 0x00178d5c. Body out-of-line in FNHighscore.cpp so the
    // compiler emits a standalone C1/C2 symbol to pair against the binary's
    // out-of-line ctor (an inline-in-header body can get inlined away entirely,
    // leaving no symbol for asm-verify to match).
    FNHighscore();

    // Binary param ctor @ 0x00137e48
    FNHighscore(const char* name, unsigned long nameHash,
                int rank, int score, void* userData, const char* extraStr);

    // Binary @ 0x00137034
    // Compares m_NameHash against StringHash of the local player name.
    // NetworkManager::GetPreferredNetworkProvider() == 1 -> GameSpy/OF branch
    //   (defunct: LastLoggedInUser returns nullptr -> returns false).
    // Otherwise -> local device name via NetworkManager::GetPlayerName.
    bool IsCurrentUser();
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(FNHighscore) == 0x54, "FNHighscore size mismatch");
static_assert(offsetof(FNHighscore, m_Name)         == 0x00, "FNHighscore::m_Name");
static_assert(offsetof(FNHighscore, m_ExtraStr)     == 0x20, "FNHighscore::m_ExtraStr");
static_assert(offsetof(FNHighscore, m_NameHash)     == 0x40, "FNHighscore::m_NameHash");
static_assert(offsetof(FNHighscore, m_Score)        == 0x44, "FNHighscore::m_Score");
static_assert(offsetof(FNHighscore, m_Rank)         == 0x48, "FNHighscore::m_Rank");
static_assert(offsetof(FNHighscore, m_UserData)     == 0x4c, "FNHighscore::m_UserData");
static_assert(offsetof(FNHighscore, m_IsCurrentUser)== 0x50, "FNHighscore::m_IsCurrentUser");
#endif

#endif // FN_GAME_FN_HIGHSCORE_H
