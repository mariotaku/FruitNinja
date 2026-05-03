// Analysed: 2026-05-03T00:00

#include "ScoreDelegate.h"
#include "ScoreModifier.h"
#include "PowerUpManager.h"

// TODO: read game-state byte at GameTaskState+0x4 (current state-machine slot).
// == 2 means "playing". For now unconditionally applies multipliers.

// binary @ 0x0010a598
int DefaultScoreDelegate(int n) {
    if (n > 0)  return n * PowerUpManager::GetInstance()->GetScoreGainMultiplier();
    if (n <= 0) return n * PowerUpManager::GetInstance()->GetScoreLossMultiplier();
    return n;
}

static int TrampolineCall(int n);
static ScoreModifier* s_activeMod = nullptr;

ScoreDelegateFn g_ScoreDelegate = &DefaultScoreDelegate;

// binary @ 0x0011cc1c
void SetScoreDelegate(ScoreModifier* m) {
    s_activeMod  = m;
    g_ScoreDelegate = &TrampolineCall;
}

// binary @ 0x0011cda4
void SetDefaultScoreDelegate() {
    s_activeMod  = nullptr;
    g_ScoreDelegate = &DefaultScoreDelegate;
}

static int TrampolineCall(int n) {
    if (s_activeMod) return (*s_activeMod)(n);
    return n;
}
