// Analysed: 2026-05-03T00:00

#include "ScoreDelegate.h"
#include "ScoreModifier.h"
#include "PowerUpManager.h"

// TODO: read game-state byte at GameTaskState+0x4 (current state-machine slot).
// == 2 means "playing". Binary gates the gain/loss multiply on play-state; port applies unconditionally.

// ASM-spec v1.6.1 DefaultScoreDelegate @ 0x0011a23c: gates multiply on game-state==2; port applies unconditionally (TODO above).
int DefaultScoreDelegate(int n) {
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
