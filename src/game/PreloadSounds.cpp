// Analysed: 2026-05-03T00:00
// PreloadSounds -- binary @ 0x0010b204 (PLT stub @ 0x00101cac).
// 24 hard-coded sound names + per-fruit sounds + 7 sword-swipe + 3 visceral-impact.
// Names are bare (no extension); SoundManager appends ".wav.pcm".

#include "PreloadSounds.h"

#include <cstdio>
#include "engine/audio/SoundManager.h"
#include "entities/FruitInfo.h"

static const char* const k_PreloadedSounds[24] = {
    "Clean-Slice-1", "Clean-Slice-2", "Clean-Slice-3",
    "Splatter-Medium-1", "Splatter-Medium-2",
    "Splatter-Small-1",  "Splatter-Small-2",
    "Pulp-drip-1", "Pulp-drip-2",
    "extra-life",
    "Throw-bomb", "Throw-fruit",
    "Game-start", "New-best-score",
    "menu-bomb", "equip-new-sword", "equip-new-wallpaper",
    "gank", "Bomb-explode", "equip-unlock",
    "achievement", "Bonus-count-up", "Pause", "time-up",
};

// ASM-verified: 2026-05-03T15:05 v1.6.1 PreloadSounds @ 0x0011bb94 (asm-inspector)
void PreloadSounds() {
    Mortar::SoundManager* sm = &Mortar::SoundManager::GetInstance();
    sm->Initialise(nullptr);

    for (int k = 0; k < 24; ++k)
        sm->PreLoadSound(k_PreloadedSounds[k]);

    const int fruitCount = FruitInfo_GetCount();
    for (int i = 0; i < fruitCount; ++i) {
        const FruitInfo* fi = FruitInfo_Get(i);
        // TODO: m_pSounds/m_SoundCount populated by fruit XML loader (binary offset +0x31C/+0x320)
        for (int j = 0; j < fi->m_SoundCount; ++j)
            sm->PreLoadSound(fi->m_pSounds[j].m_SoundName);
    }

    char buf[128];
    for (int i = 1; i <= 7; ++i) {
        snprintf(buf, sizeof(buf), "%s%d", "Sword-swipe-", i);
        sm->PreLoadSound(buf);
    }
    for (int i = 1; i <= 3; ++i) {
        snprintf(buf, sizeof(buf), "%s%d", "Visceral-impact-", i);
        sm->PreLoadSound(buf);
    }
}
