#ifndef FN_GAME_SCORE_DELEGATE_H
#define FN_GAME_SCORE_DELEGATE_H

#include "engine/util/Delegate.h"

// g_ScoreDelegate -- port of the binary's StackAllocatedPointer<BaseDelegate,32>
// score-delegate storage @ 0x001fa388, retyped to Mortar::Delegate1<int,int>
// (36-byte ABI-faithful; see src/engine/util/Delegate.h).
//
// Binary delegate types installed here:
//   Global<int,int>(&DefaultScoreDelegate) -- installed by SetDefaultScoreDelegate()
//   Callee<ScoreModifier>(m, &ScoreModifier::DeferPoints) -- installed by
//     SetScoreDelegate(Mortar::Delegate1<int,int>) when a ScoreModifier binds it
//
// Call g_ScoreDelegate(n) directly to invoke (Delegate1::operator()); do not
// reach into the storage otherwise.

class ScoreModifier;

// binary @ 0x001fa388 (StackAllocatedPointer<BaseDelegate,32>)
extern Mortar::Delegate1<int,int> g_ScoreDelegate;

// ASM-spec v1.6.1 DefaultScoreDelegate @ 0x0011a23c: applies the PowerUpManager gain/loss
// multiplier only when game_work.gameMode == GAME_MODE_ARCADE; all other modes
// return n unchanged.
int DefaultScoreDelegate(int n);

// v1.6.1 SetScoreDelegate @ 0x0011a440: takes a Delegate1<int,int> by value; if it is
// null/empty, substitutes Global(DefaultScoreDelegate); copies the result into the
// file-static g_ScoreDelegate storage.
void SetScoreDelegate(Mortar::Delegate1<int,int> d);

// Installs Global<int,int>(&DefaultScoreDelegate) -- addr unresolved in v1.6.1 .symtab
void SetDefaultScoreDelegate();

// ASM-spec v1.6.1 AddScoreNomals @0x001adee0: identity score delegate. Used by
// ScoreMultiplyerBoard::Update/Save to bank the x2 payout via AddToCurrentScore
// without DefaultScoreDelegate re-applying the ARCADE gain multiplier a second
// time (m_ScoreValue is already the doubled total). Installed directly via
// `g_ScoreDelegate = Mortar::Delegate1<int,int>::MakeFree(&AddScoreNomals);`;
// PowerUpManager::SetAppropriateScoreCallback() restores the normal delegate afterward.
int AddScoreNomals(int n);

#endif // FN_GAME_SCORE_DELEGATE_H
