// SetupGameWork -- binary @ 0x0011c06c (0x0010ed34 is a PLT/GOT thunk to it).

#include "game/SetupGameWork.h"
#include "Game.h"
#include "game/FruitSaveData.h"
#include "util/StringHash.h"
#include "game/GameWork.h"

// ASM-verified: 2026-07-03T00:00:00Z v1.6.1 SetupGameWork @ 0x0011c06c (re-analyst)

// Binary @ 0x0011c06c: 24 field stores + sessions bump.
void SetupGameWork() {
    Game* app = Game::GetInstance();
    FruitSaveData* save = game_work.m_SaveData;

    // Bump cumulative session count (key confirmed via decompile: "sessions", #335).
    save->AddToTotal("sessions", StringHash("sessions"), 1, true, true);

    // 24 field stores (binary @ 0x0011c06c disasm-confirmed):
    //
    // TODO: SetupGameWork -- the v1.6.1 store set was re-read from the binary while
    // fixing this function's fabricated per-store addresses, and it does NOT match
    // this port body; several offsets below are v1.5.1 residue (project task #204).
    // v1.6.1 stores, in program order:
    //   strb: +0x00=2, +0x1a1, +0x34, +0x1c, +0x06, +0x174, +0x1a0, +0x1a6,
    //         +0x1bc, +0x1b4, +0x194=1
    //   str : +0x190, +0x30=save[+0x110], +0x170, +0x168, +0x164, +0x20, +0x24, +0x28
    //   vstr: +0x8c=50.0f, +0x10, +0x2c, +0x1a8, +0x1b8
    // Written here but NOT by v1.6.1: +0x160, +0x16C, +0x18C, +0x198, +0x199,
    //   +0x1A2..+0x1A5, +0x1B0.  Written by v1.6.1 but missing here: +0x190
    //   (port writes +0x18C instead), +0x174, +0x1a0, +0x1a1, +0x1a6, +0x168.
    // Closing this needs a re-port of the body against the v1.6.1 GameWork layout,
    // not a marker-only fix -- deliberately left out of the marker pass.

    // +0x000: set task state to 2 (Game state; binary field is "m_GameMode" in spec,
    //         maps to taskStateIndex in port which is 0=Splash/1=Frontend/2=Game).
    game_work.taskStateIndex = 2;

    // +0x006: retryFlag = 0.
    game_work.retryFlag = 0;

    // +0x010: bombHitTimer = 0.0f.
    game_work.m_BombHitTimer = 0.0f;

    // +0x18C: m_gameDataLicensedState = 0.
    game_work.m_gameDataLicensedState = 0;

    // +0x1B8: m_ElapsedGameTime = 0.0f (v1.6.1 SetupGameWork @0x0011c0f4).
    game_work.m_ElapsedGameTime = 0.0f;

    // +0x01C: m_bUnsullied = 0.
    game_work.m_bUnsullied = 0;

    // +0x020..+0x028: zero the coin trio. Binary writes 3 separate `str`
    // (int) ops at 0x0011c114..0x0011c11c -- confirmed int via re-analyst
    // (vstr would be used for floats, see neighbouring +0x10/+0x2c writes).
    game_work.m_CoinsBalance     = 0;
    game_work.m_CoinsTotalEarned = 0;
    game_work.m_CoinsAtGameStart = 0;

    // +0x02C: m_CritTimer = 0.0f.
    game_work.m_CritTimer = 0.0f;

    // +0x030: m_ScoreThreshold = save->m_CriticalChance.
    // Binary spec label: "m_CritChanceCounter"; port field at +0x30 is m_ScoreThreshold.
    // DIFFERS: binary stores FruitSaveData[+0x110] (m_CriticalChance) here; port
    //          field name m_ScoreThreshold is provisional -- rename when RE confirms.
    game_work.m_ScoreThreshold = save->m_CriticalChance;

    // +0x034: m_bHudDestructing = 0 (HUD-teardown guard).
    game_work.m_bHudDestructing = 0;

    // +0x08C: flM_BombSize = 50.0f.
    // Literal pool DAT_0011c130 confirmed = 0x42480000 = 50.0f.
    game_work.flM_BombSize = 50.0f;

    // +0x160: mainScreen = 0 (clear pointer).
    game_work.mMainScreen = nullptr;

    // +0x164: pGameOverScreen = 0 (clear pointer).
    game_work.pGameOverScreen = nullptr;

    // +0x16C: m_pActiveHUDControl = 0.
    // TODO: SetupGameWork -- address unresolved (cited value was an undecodable .plt
    // address; v1.6.1 SetupGameWork has no store to +0x16C -- see the write-set note above).
    game_work.m_pActiveHUDControl = nullptr;

    // +0x170: m_bMPRetryPending = 0 (str, v1.6.1 SetupGameWork @0x0011c108).
    game_work.m_bMPRetryPending = 0;

    // +0x198: m_bGameCenterConnecting = 0.
    // TODO: SetupGameWork -- address unresolved (cited value was an undecodable .plt
    // address; v1.6.1 SetupGameWork has no store to +0x198 -- see the write-set note above).
    game_work.m_bGameCenterConnecting = 0;

    // +0x199: m_bP2PReady = 0 (dead-code MP sync flag).
    // TODO: SetupGameWork -- address unresolved (cited value was an undecodable .plt
    // address; v1.6.1 SetupGameWork has no store to +0x199 -- see the write-set note above).
    game_work.m_bP2PReady = 0;

    // +0x19E was field_0x19e in v1.5.1; in v1.6.1 this byte is interior to
    // m_FrameTimer (int @ +0x19C); the explicit zero is dropped (m_FrameTimer
    // is not set here; zero-init from .bss suffices).

    // +0x194: m_reserved194 = 1 (v1.6.1; binary init confirmed by SetupGameWork; written-never-read).
    game_work.m_reserved194 = 1;

    // +0x1A2..+0x1A5: zero four P2P session flags (v1.6.1 SetupGameWork; written-never-read).
    game_work.m_reserved1a2 = 0;
    game_work.m_reserved1a3 = 0;
    game_work.m_reserved1a4 = 0;
    game_work.m_reserved1a5 = 0;

    // +0x1A8: m_QuitTransitionTimer = 0.0f.
    game_work.m_QuitTransitionTimer = 0.0f;

    // +0x1B0: m_UpsideDownTimer = 0.0f.
    // TODO: SetupGameWork -- address unresolved (cited value was an undecodable .plt
    // address; v1.6.1 SetupGameWork has no store to +0x1B0 -- see the write-set note above).
    game_work.m_UpsideDownTimer = 0.0f;

    // +0x1B4: m_bUpsideDownActive = 0 (v1.6.1; upside-down scoring flag zeroed by SetupGameWork).
    game_work.m_bUpsideDownActive = 0;

    // +0x1BC: m_reserved1bc = 0 (v1.6.1; written-never-read, zeroed by SetupGameWork).
    game_work.m_reserved1bc = 0;
}
