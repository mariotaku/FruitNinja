// Side-by-side ARM Thumb-2 demo: WaveManager::GameOver (Fix #5).
// Compiled with the Bada SDK arm-bada-eabi-g++ to mirror the original
// build, so register allocation / instruction selection should align
// closely with the binary's gcc 4.4.1 output (we use 4.5.3 here).

class PowerUpManager {
public:
    static PowerUpManager* GetInstance();
    void Reset(bool fullReset);
};
class WaveManager {
public:
    static WaveManager* GetInstance();
    void ResetGlobalDt(float dt);
    bool PowersEnabled();   // post-fix only
    static void GameOver_Before();
    static void GameOver_After();
};

// === BEFORE FIX (port had this) ===
void WaveManager::GameOver_Before() {
    PowerUpManager::GetInstance()->Reset(false);
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
}

// === AFTER FIX (matches binary @ 0x00121f74) ===
void WaveManager::GameOver_After() {
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
    if (self && self->PowersEnabled()) {
        PowerUpManager::GetInstance()->Reset(false);
    }
}
