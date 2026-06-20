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

// ASM-spec v1.6.1 DefaultScoreDelegate @ 0x0011a23c
int DefaultScoreDelegate(int n);

// ASM-spec v1.6.1 SetScoreDelegate @ 0x0011a440: installs Callee<ScoreModifier> trampoline
void SetScoreDelegate(ScoreModifier* m);

// Installs Global<int,int>(&DefaultScoreDelegate) — addr unresolved in v1.6.1 .symtab
void SetDefaultScoreDelegate();

#endif // FN_GAME_SCORE_DELEGATE_H
