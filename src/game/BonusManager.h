#ifndef FN_GAME_BONUS_MANAGER_H
#define FN_GAME_BONUS_MANAGER_H

// BonusManager -- combo/streak bonus tracker (binary address TBD).
// Tracks consecutive-slice bonuses and streak multipliers.
//
// TODO: RE class -- see docs/engine/initialisation-asm-audit.md Section 4,
//   InitialiseData step 15.

class BonusManager {
public:
    static BonusManager* GetInstance() {
        static BonusManager s_instance;
        return &s_instance;
    }

    // @ InitialiseData step 15 -- init internal state
    // TODO: implement
    void Init() {}

private:
    BonusManager() {}
    ~BonusManager() {}
};

#endif // FN_GAME_BONUS_MANAGER_H
