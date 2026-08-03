// test_powerup.cpp -- PowerUpManager::ClearTimedPowers / Reset predicate regression guard.
//
// Bug: hitting an arcade bomb calls ClearTimedPowers(), which is supposed to
// strip all active TIMED, non-purchased power-ups (freeze/frenzy/score x2) so
// their HUD bars + effects don't persist to their own timeout. The port used
// PowerUp::IsPurchaseable() (the poweruplist.xml `single=` flag) as the
// filter instead of PowerUp::Purchaseable() (the coin COST, 0 when there's no
// <purchase_info> child). Every banana power in poweruplist.xml has
// single="true" (IsPurchaseable()==true) but no <purchase_info> (cost 0), so
// the old `!pwr->IsPurchaseable()` was always false and the clear loop never
// touched them.
//
// v1.6.1 PowerUpManager::ClearTimedPowers @0x0014136c decompile:
//   iVar2 = PowerUp::Purchaseable(this_00);
//   if ((iVar2 == 0) && PowerUp::IsTimed(this_00)) { ...erase... }
// v1.6.1 PowerUpManager::Reset @0x00142e08 decompile:
//   iVar3 = PowerUp::Purchaseable(this_00);
//   if (iVar3 != 0) { ...keep/re-purchase path... } else { ...erase... }
//
// Minimal stubbing, mirrors test_bonus_manager.cpp: a bare Game +
// FileSystem_Direct so PowerUpManager::Load() reads the real
// xml/poweruplist.xml. No SDL/GL/audio. Deterministic.
//
// Run:
//   build/host/tests/test_powerup
//   ctest -R powerup

#include <cstdio>
#include <cstdint>

#include "game/PowerUpManager.h"
#include "game/PowerUp.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "game/GameMode.h"
#include "game/WaveManager.h"
#include "audio/GameSound.h"
#include "hud/HUD.h"
#include "hud/TimeControl.h"
#include "Game.h"
#include "engine/asset/FileManager.h"
#include "engine/asset/FileSystem_Direct.h"
#include "engine/util/StringHash.h"
#include "config.h"

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL (line %d): %s\n", __LINE__, #cond); ++g_fail; } \
} while (0)

int main() {
    std::setvbuf(stdout, NULL, _IONBF, 0);

    // --- Minimal environment: Game + FileSystem_Direct so PowerUpManager::Load
    // can read the real xml/poweruplist.xml via FileManager. ---
    Game* game = new Game();  // intentionally leaked (test_bonus_manager.cpp pattern)
    game->data_dir = FN_DATA_DIR;
    {
        Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
        fs->Initialise(game->data_dir.c_str(), /*writable=*/false);
        FileManager::GetInstance().AddSystem(fs, /*id=*/0, /*priority=*/0);
    }

    // Freeze's <wave_mod waveOveride="-100"> makes WaveModifier::ApplyModifier
    // (v1.6.1 @0x001282d4) rewind the wave via WaveManager::SetCurrentWave(-100)
    // -> GetNextWave, which seeds m_pCurrentWave from m_WaveInfo[gameMode].front()
    // UNCONDITIONALLY (ASM-verified v1.6.1 WaveManager::GetNextWave @0x00125884 --
    // the binary has no empty-list guard; GameInit step 12 always runs
    // WaveManager::Init() before any power can activate). Mirror that guarantee
    // here: arcade mode (freeze is an arcade banana) + wave lists loaded.
    // Without this, front() on the empty vector aborts (MSVC debug STL).
    game_work.gameMode = GAME_MODE_ARCADE;
    WaveManager::GetInstance()->Init();

    // GetNextWave's head derefs game_work.m_SaveData unguarded, exactly like the
    // binary (v1.6.1 WaveManager::GetNextWave @0x0012573c -- `ldr r0,[r7,#0x50]`
    // @0x00125768 straight into FruitSaveData::UnlockTotals, no null test).
    // GameInitialise always creates the save before gameplay; mirror that here.
    static FruitSaveData s_saveData;
    game_work.m_SaveData = &s_saveData;

    // ScreenEffect::Update's SFX tail (v1.6.1 @0x00148d84) derefs
    // game_work.mGameSound unguarded, exactly like the binary (GameInitialise
    // always creates it before gameplay). SoundManager has no audio device in
    // this headless test, so SFXPlay is a silent no-op -- no audio is played.
    game_work.mGameSound = new GameSound();

    // WaveManager derefs game_work.mHud unguarded in three places -- ResetControls
    // (Resume @0x0012bf58) and two AddControl sites -- exactly like the binary,
    // which has no null test at any of them. The port used to carry an
    // `if (game_work.mHud)` guard there; it was port-added and removed in the #156
    // sweep, so a NULL here is now a segfault rather than a silently skipped call.
    // Per the standing rule the fixture supplies the global; production does not
    // re-grow a guard the binary lacks. The HUD ctor allocates nothing but the
    // object itself, and GameInitialise always creates it before gameplay.
    game_work.mHud = new HUD();

    // WaveManager also derefs game_work.mCountDown (TimeControl, +0x184) unguarded
    // in UpdateWave -- the binary reads the blitz timer straight through it. The
    // port's `if (mCountDown)` there was port-added AND actively wrong: its
    // zero-init fallback forced `-m_NextBlitzTime >= 0` and held the Arcade blitz
    // gate permanently SHUT. Removed in the #156 sweep, so the fixture must supply
    // it like real boot does.
    game_work.mCountDown = new TimeControl();

    PowerUpManager* pum = PowerUpManager::GetInstance();
    pum->Load();

    const uint32_t hFreeze = StringHash("freeze");
    CHECK(pum->m_AllPowerUps.find(hFreeze) != pum->m_AllPowerUps.end());
    if (pum->m_AllPowerUps.find(hFreeze) == pum->m_AllPowerUps.end()) {
        std::printf("FAIL: freeze power-up template missing from poweruplist.xml -- aborting\n");
        return 1;
    }
    PowerUp* freezeTpl = pum->m_AllPowerUps[hFreeze];
    // freeze is single="true" (IsPurchaseable()==true) with NO <purchase_info>
    // child (Purchaseable()==0, cost). This exact combination is what the
    // IsPurchaseable-vs-Purchaseable bug confused.
    CHECK(freezeTpl->IsPurchaseable());
    CHECK(freezeTpl->m_pPurchaseInfo == NULL);

    // ------------------------------------------------------------------
    // CASE 1: activate freeze (timed, cost-0, single=true) -- must land in
    // the active list and read as a live "special" (HUD bar shown).
    // ------------------------------------------------------------------
    std::printf("\n[CASE 1] Activate freeze -> active + IsSpecial\n");

    _Vector3<float> zeroPos(0.0f, 0.0f, 0.0f);
    PowerUp* activeFreeze = pum->ActivatePower(hFreeze, zeroPos, NULL);
    CHECK(activeFreeze != NULL);
    CHECK((int)pum->m_ActivePowerUps.size() == 1);
    CHECK(pum->GetActiveSingle(hFreeze) == activeFreeze);
    if (activeFreeze) {
        // m_TotalTime/m_LongestRemaining are only populated once a modifier
        // Update() tick runs (PowerUp::Update @0x00140600) -- Activate() alone
        // applies the modifier but doesn't advance the clock. Tick once
        // directly on the instance (not PowerUpManager::Update, which
        // unconditionally derefs game_work.mHud via SetDefaults -- not set up
        // in this minimal test).
        activeFreeze->Update(1.0f / 60.0f);
        CHECK(activeFreeze->Purchaseable() == 0);          // cost-0 predicate the bug conflated
        CHECK(activeFreeze->IsTimed());                     // m_TotalTime > 0 (7s time_mod)
        CHECK(activeFreeze->IsSpecial());                   // timed && !m_bIsSpecial && cost==0
    }

    // ------------------------------------------------------------------
    // CASE 2: ClearTimedPowers() must remove it (this is the bomb-hit path:
    // PowerUpManager::ClearTimedPowers @0x0014136c, filtered on Purchaseable()
    // == 0, not the single= flag).
    // ------------------------------------------------------------------
    std::printf("\n[CASE 2] ClearTimedPowers() removes the timed cost-0 power\n");

    pum->ClearTimedPowers();
    CHECK((int)pum->m_ActivePowerUps.size() == 0);
    CHECK(pum->GetActiveSingle(hFreeze) == NULL);
    CHECK(pum->m_pActiveSpecial == NULL);
    CHECK(pum->m_HighestActiveProgress == 0.0f);

    // ------------------------------------------------------------------
    // CASE 3 (complementary): a purchased power (Purchaseable() != 0, i.e.
    // cost > 0 via a real PurchaseInfo) must NOT be cleared by
    // ClearTimedPowers -- only the cost==0 timed powers are stripped.
    // Construct one directly: clone freeze's template shape but attach a
    // PurchaseInfo with a nonzero cost and reuse the SAME name-hash so it
    // exercises the exact PowerUp::Purchaseable() accessor (m_pPurchaseInfo
    // ? m_pPurchaseInfo->m_Cost : 0) the binary gates on.
    // ------------------------------------------------------------------
    std::printf("\n[CASE 3] Purchased (cost>0) power survives ClearTimedPowers\n");

    PowerUp* purchased = freezeTpl->Clone();
    purchased->m_pPurchaseInfo = new PurchaseInfo();
    purchased->m_pPurchaseInfo->m_Cost = 500;
    purchased->m_pPurchaseInfo->m_CurrentUses = 1;
    CHECK(purchased->Purchaseable() == 500);

    pum->m_ActivePowerUps.push_back(purchased);
    pum->m_ActiveByHash[purchased->m_NameHash] = purchased;
    // Force it into an active, timed state directly (bypassing Activate's
    // popup/coin side effects, which aren't the subject of this test) so
    // IsTimed() reads true the same way a real purchased-and-sliced power would.
    purchased->m_TotalTime = 7.0f;
    purchased->m_LongestRemaining = 7.0f;
    CHECK(purchased->IsTimed());

    pum->ClearTimedPowers();
    CHECK((int)pum->m_ActivePowerUps.size() == 1);
    CHECK(pum->GetActiveSingle(purchased->m_NameHash) == purchased);

    // Clean up the manually-pushed instance (ClearScreenEffects/dtor won't
    // reach it via the normal expiry path in this minimal test).
    pum->m_ActiveByHash.erase(purchased->m_NameHash);
    pum->m_ActivePowerUps.remove(purchased);
    purchased->Release();
    delete purchased;

    if (g_fail == 0) {
        std::printf("PASS: PowerUpManager Purchaseable()-based clear/reset predicate\n");
        return 0;
    }
    std::printf("FAIL: %d assertion(s) failed\n", g_fail);
    return 1;
}
