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

Mortar::Delegate1<int,int> g_ScoreDelegate = Mortar::Delegate1<int,int>::MakeFree(&DefaultScoreDelegate);

// ASM-spec v1.6.1 SetScoreDelegate @ 0x0011a440:
//   if (Delegate1<int,int>::IsNull(&d)) d = Global(DefaultScoreDelegate);
//   s_scoreDelagate = d;
void SetScoreDelegate(Mortar::Delegate1<int,int> d) {
    if (!d) d = Mortar::Delegate1<int,int>::MakeFree(&DefaultScoreDelegate);
    g_ScoreDelegate = d;
}

// ASM-spec v1.6.1 SetDefaultScoreDelegate: installs Global<int,int>(&DefaultScoreDelegate) -- addr unresolved in v1.6.1 .symtab
void SetDefaultScoreDelegate() {
    g_ScoreDelegate = Mortar::Delegate1<int,int>::MakeFree(&DefaultScoreDelegate);
}

// AddScoreNomals (identity score delegate, v1.6.1 @0x001adee0) is defined once in
// ScoreState.cpp; declared in ScoreDelegate.h for ScoreMultiplyerBoard's use.
