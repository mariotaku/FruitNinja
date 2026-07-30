// UpdateMusic — music crossfade state machine
// Binary: v1.6.1 UpdateMusic @0x001cc18c (entry; body 0x001cc18c-0x001cc593).
// NOTE: the inline per-instruction 0x0016a6xx / string-data 0x001bcxxx
// addresses below are STALE v1.5.1 residue (the function moved to 0x001cxxxx
// in v1.6.1); they are kept only as relative-order bookkeeping, not as
// authoritative v1.6.1 addresses. TODO: re-verify v1.6.1 inline addresses.
// Called every frame from GameUpdate once LoadingJob::IsLoaded() is true.
//
// Analysed: 2026-04-26T00:00

#include "UpdateMusic.h"
#include "GameMode.h"
#include "Game.h"
#include "entities/ActorManager.h"
#include "audio/SoundManager.h"
#include "audio/GameSound.h"
#include "debug/Logger.h"
#include <cmath>
#include "game/GameWork.h"
#include "screens/MainScreen.h"

// ---------------------------------------------------------------------------
// Static state (binary: BSS, zero-initialised at process start)
// ---------------------------------------------------------------------------

// g_currentVolume: signed float.
//   Negative (toward -1.0) = menu-music side.
//   Positive (toward 0.55 * MasterVolume) = gameplay-music side.
//   0.0 = silence / between tracks.
// Binary: pointer chain via GOT+0x76c0 -> BSS float
static float g_currentVolume = 0.0f;

// g_trackId: which track is currently playing.
//   -2 = Dojo/About track ("Music-Dojo")
//   -1 = menu track ("Music-menu")
//   +1 = gameplay track ("background")
//    0 = no track (initial / transient)
// Binary: pointer chain via GOT+0x74e8 -> BSS int
static int   g_trackId       = 0;

// g_MusicState armed flags (g_MusicState +0xe0 / +0xe1 in binary BSS at 0x00231404)
// Gate the preload countdown timers.
static bool  g_armedIngame  = false;   // g_MusicState+0xe0
static bool  g_armedArcade  = false;   // g_MusicState+0xe1

// Preload countdown timers (preload timer block +0x14 / +0x18, BSS at 0x001f3d84)
// Both start at 0.0 (BSS), so the fire condition (t <= 0) is true immediately
// after arming.
static float g_timerIngame  = 0.0f;   // preloadTimerBlock+0x14
static float g_timerArcade  = 0.0f;   // preloadTimerBlock+0x18

// Inner idempotency guards for the preload functions themselves.
// g_MusicState+0x21 (PreloadInGameSounds guard) and +0x20 (PreloadArcadeModeSounds guard).
static bool  g_preloadedIngame  = false;  // g_MusicState+0x21
static bool  g_preloadedArcade  = false;  // g_MusicState+0x20

// ---------------------------------------------------------------------------
// PreloadInGameSounds (v1.6.1 @0x001cad28)
// Guards with g_MusicState+0x21; returns immediately if already called.
// Sets flag, then calls SoundManager::PreLoadSound on four SFX assets.
// ---------------------------------------------------------------------------
void PreloadInGameSounds() {
    // Inner one-shot guard: g_MusicState+0x21
    if (g_preloadedIngame) {
        return;
    }
    g_preloadedIngame = true;

    Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();

    // Binary: four PreLoadSound calls in order (v1.6.1 PreloadInGameSounds @0x001cad28).
    // PreLoadSound is intentionally a no-op stub in the port (virtual nop in
    // SoundManagerFns); call sites are kept to preserve binary shape so they
    // light up when actual preload is wired. Do NOT skip or remove them.

    // 1. "Time-tock" (0x001bc29c)
    sm.PreLoadSound("Time-tock");
    // 2. "Time-tick" (0x001bc2a6)
    sm.PreLoadSound("Time-tick");
    // 3. "Critical" (0x001bceff)
    sm.PreLoadSound("Critical");
    // 4. Loop i=1..3: "Combo-1", "Combo-2", "Combo-3"
    {
        char buf[32];
        for (int i = 1; i <= 3; ++i) {
            // Binary: sprintf(buf, "%s%d", "Combo-", i)
            snprintf(buf, sizeof(buf), "Combo-%d", i);
            sm.PreLoadSound(buf);
        }
    }
    LOG_DEBUG("UPDATEMUSIC", "UpdateMusic: PreloadInGameSounds fired");
}

// ---------------------------------------------------------------------------
// PreloadArcadeModeSounds (v1.6.1 @0x001caba4)
// Guards with g_MusicState+0x20; returns immediately if already called.
// Sets flag, then calls SoundManager::PreLoadSound on twelve assets in fixed
// order. The two identical consecutive calls on "Combo-Blitz-Backing" are
// INTENTIONAL binary behavior -- do not simplify.
// ---------------------------------------------------------------------------
void PreloadArcadeModeSounds() {
    // Inner one-shot guard: g_MusicState+0x20
    if (g_preloadedArcade) {
        return;
    }
    g_preloadedArcade = true;

    Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();

    // Binary: 12 PreLoadSound calls (v1.6.1 PreloadArcadeModeSounds @0x001caba4).
    // PreLoadSound is intentionally a no-op stub; calls kept for binary shape.

    // 1. "Combo-Blitz-Backing-Light" (0x001bc258)
    sm.PreLoadSound("Combo-Blitz-Backing-Light");
    // 2. "Combo-Blitz-Backing" (0x001bc272)
    sm.PreLoadSound("Combo-Blitz-Backing");
    // 3. Same string again -- BINARY FAITHFUL: original calls PreLoadSound on
    //    "Combo-Blitz-Backing" twice (copy-paste in original source). Do not merge.
    sm.PreLoadSound("Combo-Blitz-Backing");
    // 4-12. Arcade-specific SFX strings (addresses 0x001ba775, 0x001ba9fa, etc.)
    sm.PreLoadSound("combo-blitz-1");
    sm.PreLoadSound("combo-blitz-2");
    sm.PreLoadSound("Bonus-Banana-Freeze");
    sm.PreLoadSound("Bonus-Banana-Frenzy");
    sm.PreLoadSound("Bonus-Banana-X2");
    // Remaining four arcade SFX (addresses in the spec; exact names from binary strings)
    // TODO: re-verify v1.6.1 string addresses; re-analyst to confirm remaining 4 names if needed.
    LOG_DEBUG("UPDATEMUSIC", "UpdateMusic: PreloadArcadeModeSounds fired");
}

// ---------------------------------------------------------------------------
// UpdateMusic(float dt)
// Binary entry: v1.6.1 UpdateMusic @0x001cc18c
// Called every frame from GameUpdate when LoadingJob::IsLoaded().
// ---------------------------------------------------------------------------
void UpdateMusic(float dt) {
    // 0x0016a6a6: per-frame delta = dt * 4.0 (volume ramp rate = 4.0 units/second)
    float delta = dt * 4.0f;

    // Save old volume for change-detection at end
    float oldVol = g_currentVolume;   // 0x0016a6ac

    // ASM-spec v1.6.1 UpdateMusic @0x001cc18c: entry loads g_currentVolume and
    // game_work from the GOT (`ldr r3,[r4,r3]; vldr.32 s15,[r3,#0xc]`).
    // No Game::GetInstance, no null test.

    // -----------------------------------------------------------------------
    // BLOCK 1: Arm preload-ingame-sounds countdown
    // Condition: NOT already armed  AND  m_TransitionTimer >= 0.0
    // Sub-condition (to SKIP arming): currentVol < 0.0
    //   AND GetNumEntities(Fruit==0) != 0
    //   AND GetNumEntities(Bomb==1)  != 0
    // -----------------------------------------------------------------------
    if (!g_armedIngame) {                                // 0x0016a6a0
        if (game_work.m_PauseAmount >= 0.0f) {           // 0x0016a6ba: vcmpe / blt
            bool skip_arm = false;
            if (g_currentVolume < 0.0f) {               // 0x0016a6c4: bpl
                // Only skip arming if fruits AND bombs are both present
                Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
                if (am && Mortar::ActorManager::GetInstance()->GetNumEntities(0) != 0) {  // type 0 = Fruit
                    am = Mortar::ActorManager::GetInstance();
                    if (am && am->GetNumEntities(1) != 0) {  // type 1 = Bomb
                        skip_arm = true;                 // 0x0016a6e4
                    }
                }
            }
            if (!skip_arm) {
                g_armedIngame = true;                    // 0x0016a6ec
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 2: Tick preload-ingame countdown; fire PreloadInGameSounds on expiry
    // -----------------------------------------------------------------------
    if (g_armedIngame) {                                 // 0x0016a6f4
        float t = g_timerIngame;                         // 0x0016a6fe
        if (t > 0.0f) {
            t -= delta;
            g_timerIngame = t;                           // 0x0016a714
            if (t <= 0.0f) {
                PreloadInGameSounds();                   // 0x0016a71e
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 3: Arm preload-arcade-sounds countdown
    // Condition: gameMode == 0x02 (GAME_MODE_ARCADE)  AND  NOT already armed
    //            AND  m_TransitionTimer >= 0.0
    // Sub-condition (to SKIP arming): currentVol < 0.0
    //   AND GetNumEntities(Fruit) != 0  AND GetNumEntities(Bomb) != 0
    // TODO: comment formerly said "Zen/ZenBlitz" -- binary's 0x02 is GAME_MODE_ARCADE.
    // -----------------------------------------------------------------------
    if (game_work.gameMode == GAME_MODE_ARCADE) {    // 0x0016a726
        if (!g_armedArcade) {                            // 0x0016a730
            if (game_work.m_PauseAmount >= 0.0f) {       // 0x0016a742
                bool skip_arm = false;
                if (g_currentVolume < 0.0f) {            // 0x0016a74c: bpl
                    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
                    if (am && am->GetNumEntities(0) != 0) {
                        am = Mortar::ActorManager::GetInstance();
                        if (am && am->GetNumEntities(1) != 0) {
                            skip_arm = true;
                        }
                    }
                }
                if (!skip_arm) {
                    g_armedArcade = true;                // 0x0016a774
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 4: Tick preload-arcade countdown; fire PreloadArcadeModeSounds on expiry
    // -----------------------------------------------------------------------
    if (g_armedArcade) {                                 // 0x0016a77c
        float t = g_timerArcade;                         // 0x0016a786
        if (t > 0.0f) {
            t -= delta;
            g_timerArcade = t;
            if (t <= 0.0f) {
                PreloadArcadeModeSounds();               // 0x0016a7a6
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 5: Volume ramp — split on m_bMusicOn flag (+0x49)
    // -----------------------------------------------------------------------
    if (game_work.m_bMusicOn == 0) {
        // ---- Music DISABLED branch (0x0016a868) ----
        // Ramp currentVol toward 0.0 from either direction
        float v = g_currentVolume;                       // 0x0016a86c
        float newV;
        if (v > 0.0f) {
            // Positive side: decrease toward 0
            newV = v - delta;
            if (newV <= 0.0f) {
                newV = 0.0f;                             // g_MusicVolZero = DAT_0016a934
            }
            g_currentVolume = newV;                      // 0x0016a8aa
        } else {
            // Zero or negative: do NOT go more negative
            if (v < 0.0f) {
                newV = v + delta;
                if (newV >= 0.0f) {                      // 0x0016a89c: vcmpe s16,#0 + it pl
                    newV = 0.0f;
                }
                g_currentVolume = newV;                  // 0x0016a8aa
            }
            // v == 0.0: nothing to do (goto LAB_end_ramp)
        }
    } else {
        // ---- Music ENABLED branch (0x0016a7b6) ----
        // Check if gameplay is in "transition" (m_TransitionTimer < 0)
        if (game_work.m_PauseAmount < 0.0f) {            // 0x0016a7ba: bpl -> 0x0016a80a
            // Transition active: 3-way split on MainScreen::m_State
            // (v1.6.1 UpdateMusic @0x001cc18c, disasm 0x001cc350-0x001cc3ec).
            MainScreen* ms = game_work.mMainScreen;
            if (ms != nullptr && ms->m_State == STATE_DOJO_WAIT_B) {
                // About/Dojo wait: ramp UP toward +1.0, play the Dojo track.
                float v = g_currentVolume + delta * 0.33f;   // 0x1cc37c: vmla, const 0.33f @0x1cc594
                if (v >= 1.0f) {                             // 0x1cc380/0x1cc38c: const 0x3f800000
                    v = 1.0f;
                }
                g_currentVolume = v;
                if (v <= 0.0f) {                             // 0x1cc39c
                    goto LAB_end_ramp;
                }
                if (g_trackId == -2) {                       // 0x1cc3ac: cmn r2,#2
                    goto LAB_end_ramp;                       // already on Dojo track, no re-play
                }
                g_trackId = -2;                              // 0x1cc3b4
                {
                    Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
                    sm.SongPlay("Music-Dojo");               // 0x1cc3c4: string @0x00283e1f
                }
            } else {
                // Ramp DOWN toward -1.0 (kill gameplay music)
                float v = g_currentVolume - delta;           // 0x0016a7d0: vsub s16,s14,s16
                if (v <= -1.0f) {                            // 0x0016a7d4: vcmpe s16,s15 (s15=-1.0)
                    v = -1.0f;                               // 0x0016a7de: vmov.le.f32 s16,s15
                }
                g_currentVolume = v;                         // 0x0016a7e2: vstr s16,[r3]
                // If still negative (ramping), or already on menu track: skip SongPlay
                if (v >= 0.0f) {                             // 0x0016a7e6: bpl -> LAB_end_ramp
                    goto LAB_end_ramp;
                }
                if (g_trackId == -1) {                       // 0x0016a7f6: cmp r2, #0xffffffff
                    goto LAB_end_ramp;                       // already on menu track, no re-play
                }
                // Flip track ID to -1 (menu) and call SongPlay("Music-menu")
                g_trackId = -1;                              // 0x0016a800: str r2,[r3]
                {
                    Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();  // 0x0016a802
                    sm.SongPlay("Music-menu");               // 0x0016a860: string at 0x001bc787
                }
            }
        } else {
            // No transition: gameplay / menu determination by vol sign + entity counts
            float v = g_currentVolume;
            if (v < 0.0f) {
                // Volume is on the menu side — check if we should ramp toward gameplay
                Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
                if (am == nullptr || am->GetNumEntities(0) == 0) {
                    // No fruits (or no actor manager): skip fast-ramp, fall through to slow ramp
                    goto slow_ramp;
                }
                // Fruits present: fast-ramp toward g_MusicVolMinCap (-0.001)
                // delta * 1.6f  -- g_MusicRampFastMultiplier = DAT_0016a92c = 1.6
                float newV = v + delta * 1.6f;           // 0x0016a830: vmla s15,s16,s14 (s14=1.6)
                if (newV >= -0.001f) {                   // 0x0016a838: vcmpe s15,s14 (s14=-0.001)
                    newV = -0.001f;                      // cap at g_MusicVolMinCap = DAT_0016a930
                }
                g_currentVolume = newV;                  // 0x0016a846: vstr s15,[r5]
                goto LAB_end_ramp;
            }

        slow_ramp:
            // Volume >= 0 (gameplay side) or no fruits: slow ramp upward
            // Cap = 0.55 * pGameSound->m_MasterVolume
            // g_MusicVolMaxScale = DAT_0016a93c = 0.55
            float masterVol = (game_work.mGameSound) ? game_work.mGameSound->m_MasterVolume : 1.0f;
            float cap = 0.55f * masterVol;               // 0x0016a8ee..0x0016a904
            float newV = g_currentVolume + delta;        // 0x0016a8fc: vadd s16,s16,s15
            if (newV >= cap) {                           // 0x0016a908: vcmpe s16,s15
                newV = cap;
            }
            g_currentVolume = newV;                      // 0x0016a916
            // If still at or below zero: no track flip needed
            if (newV <= 0.0f) {                          // 0x0016a91a: vcmpe s16,#0
                goto LAB_end_ramp;
            }
            if (g_trackId == 1) {                        // 0x0016a852: cmp r2,#0x1
                goto LAB_end_ramp;                       // already on gameplay track
            }
            // Flip track ID to +1 (gameplay) and call SongPlay("background")
            g_trackId = 1;                               // 0x0016a858: str r2,[r3]
            {
                Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();  // 0x0016a85a
                sm.SongPlay("background");               // 0x0016a860: string at 0x001bc792
            }
        }
    }

LAB_end_ramp:
    // -----------------------------------------------------------------------
    // BLOCK 6: Drive SetMusicVolume every frame when vol changed
    // Condition: trackId != 0  AND  currentVol != oldVol
    // -----------------------------------------------------------------------
    if (g_trackId != 0) {                               // 0x0016a8b4: cmp r3,#0
        float v = g_currentVolume;
        if (oldVol != v) {                              // 0x0016a8c0: vcmpe s17,s15
            Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();  // 0x0016a8ca
            // Math::Abs<float> at 0x00106200 = fabsf (vabs.f32 s0,s0; bx lr)
            float absV = fabsf(v);                      // 0x0016a8d4: blx 0x00106200
            // g_MusicVolOutputScale = DAT_0016a938 = 0.4
            sm.SetMusicVolume(absV * 0.4f);             // 0x0016a8de..0x0016a8e2
        }
    }
}
