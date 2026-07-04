// Analysed: 2026-05-03T00:00

#include "ScoreDelegate.h"
#include "ScoreModifier.h"
#include "PowerUpManager.h"
#include "game/GameWork.h"
#include "game/GameMode.h"

// ASM-spec v1.6.1 DefaultScoreDelegate @ 0x0011a23c: gates the PowerUpManager gain/loss multiply
// on GameWork::gameMode==GAME_MODE_ARCADE(2) (ldrb r3,[r3,#0x4]; cmp #2; bne -> return n unmodified).
// All other modes pass n through unchanged.
int DefaultScoreDelegate(int n) {
    if (game_work.gameMode != Mortar::GAME_MODE_ARCADE) return n;
    if (n > 0)  return n * PowerUpManager::GetInstance()->GetScoreGainMultiplier();
    if (n <= 0) return n * PowerUpManager::GetInstance()->GetScoreLossMultiplier();
    return n;
}

static int TrampolineCall(int n);
static ScoreModifier* s_activeMod = nullptr;

ScoreDelegateFn g_ScoreDelegate = &DefaultScoreDelegate;

// ASM-spec v1.6.1 SetScoreDelegate @ 0x0011a440: installs Callee<ScoreModifier> trampoline
void SetScoreDelegate(ScoreModifier* m) {
    s_activeMod  = m;
    g_ScoreDelegate = &TrampolineCall;
}

// ASM-spec v1.6.1 SetDefaultScoreDelegate: installs Global<int,int>(&DefaultScoreDelegate) — addr unresolved in v1.6.1 .symtab
void SetDefaultScoreDelegate() {
    s_activeMod  = nullptr;
    g_ScoreDelegate = &DefaultScoreDelegate;
}

static int TrampolineCall(int n) {
    if (s_activeMod) return s_activeMod->DeferPoints(n);
    return n;
}
