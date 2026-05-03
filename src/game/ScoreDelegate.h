#ifndef FN_GAME_SCORE_DELEGATE_H
#define FN_GAME_SCORE_DELEGATE_H

// Analysed: 2026-05-03T00:00
//
// g_ScoreDelegate — pragmatic port of the binary's StackAllocatedPointer<BaseDelegate,32>
// score-delegate storage @ 0x001fa388.
//
// Binary delegate types:
//   Global<int,int>(&DefaultScoreDelegate) — installed by SetDefaultScoreDelegate()
//   Callee<ScoreModifier>(m, &op())        — installed by SetScoreDelegate(ScoreModifier*)
//
// Port: single function pointer + static trampoline state (only one deferPoints
// modifier is active at a time per RE note).

class ScoreModifier;

typedef int (*ScoreDelegateFn)(int);

// binary @ 0x001fa388 (StackAllocatedPointer<BaseDelegate,32>)
extern ScoreDelegateFn g_ScoreDelegate;

// binary @ 0x0010a598
int DefaultScoreDelegate(int n);

// Installs Callee<ScoreModifier> trampoline — binary @ 0x0011cc1c
void SetScoreDelegate(ScoreModifier* m);

// Installs Global<int,int>(&DefaultScoreDelegate) — binary @ 0x0011cda4
void SetDefaultScoreDelegate();

#endif // FN_GAME_SCORE_DELEGATE_H
