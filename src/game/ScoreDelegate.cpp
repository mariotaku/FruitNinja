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
    if (game_work.gameMode != GAME_MODE_ARCADE) return n;
    if (n > 0)  return n * PowerUpManager::GetInstance()->GetScoreGainMultiplier();
    if (n <= 0) return n * PowerUpManager::GetInstance()->GetScoreLossMultiplier();
    return n;
}

static int TrampolineCall(int n);
static ScoreModifier* s_activeMod = nullptr;

ScoreDelegateFn g_ScoreDelegate = &DefaultScoreDelegate;

// ASM-spec v1.6.1 SetScoreDelegate @ 0x0011a440: installs Callee<ScoreModifier> trampoline
// DIFFERS: original = SetScoreDelegate(Mortar::Delegate1<int,int>) by value (v1.6.1
//   SetScoreDelegate @0x0011a440), using ScoreModifier* until the Delegate1 subsystem is
//   ported (#29). ScoreModifier* is the Callee<ScoreModifier> target object the binary
//   would wrap in a Delegate1 at the call site; signature will not mangle-pair until
//   Mortar::Delegate1<int,int> exists in the port.
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

// AddScoreNomals (identity score delegate, v1.6.1 @0x001adee0) is defined once in
// ScoreState.cpp; declared in ScoreDelegate.h for ScoreMultiplyerBoard's use.
