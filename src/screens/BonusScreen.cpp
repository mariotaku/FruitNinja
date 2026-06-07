// Analysed: 2026-05-03T00:00
// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "game/FruitCamera.h"
#include "game/GameOver.h"
#include "entities/Coin.h"
#include "engine/audio/MortarSound.h"
#include "engine/audio/GameSound.h"
#include "engine/asset/TextureManager.h"
#include "engine/math/MathUtil.h"
#include "engine/particle/PSPParticleManager.h"
#include "math/Random.h"
#include "engine/util/StringHash.h"
#include "util/Delegate.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "render/Utf8StringIterator.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "game/GameWork.h"

using Mortar::TextureManager;

// Phase-timer rodata — binary @ 0x001f3d48, seven floats.
// ASM-verified: 2026-05-18 binary @ 0x001f3d48 (re-analyst)
// Corrected values from read_memory decode (0x3F2A7EFA = 0.6660f, 0x3EAA7EFA = 0.3330f):
static const float kRevealStart     = 0.6660f;    // pfVar14[0] DAT_001f3d48
static const float kPerAward        = 0.6f;       // pfVar14[1] DAT_001f3d4c
static const float kRevealHalfBeat  = 0.3330f;    // pfVar14[2] DAT_001f3d50
static const float kFinaleHoldExtra = 0.25f;      // pfVar14[3] DAT_001f3d54
static const float kDismissBuffer   = 7.0f;       // pfVar14[4] DAT_001f3d58
static const float kAwardYStep      = -42.0f;     // pfVar14[5] DAT_001f3d5c
static const float kTextOffsetScalar = 250.0f;    // pfVar14[6] DAT_001f3d60

// Per-award reveal animation constants — binary @ 0x00133080..0x001330b4
static const float kAlphaRampWidth  = 0.1f;       // DAT_00133080: beat/0.1 for alpha
static const float kOnsetThresh     = 0.2f;       // DAT_00133084: beat crossing for SFX onset
static const float kDrumRollPreThresh = 0.2f - 0.3330f; // DAT_00132ccc - pfVar14[2] = -0.133f

// ---------------------------------------------------------------------------
// Per-coin arrival callback — binary @ 0x0013243c
// Fires on each coin that reaches the HUD score target.
// Milestones at 3, 6, >= 9: camera shake + firework SFX + particles.
// Always credits coin.scoreValue via AddToCurrentScore.
// ---------------------------------------------------------------------------

static int s_BonusCoinCounter = 0;
// Per-award explosion SFX counter: starts at 1, increments by 2 per award.
// Binary @ 0x00132f2c: uVar17 starts at 1 and += 2 per reveal.
static uint32_t s_BonusExplosionCounter = 1;

static void AddToScoreOnArrival(Coin* coin) {
    if (!coin) return;
    s_BonusCoinCounter++;

    bool milestone = (s_BonusCoinCounter == 3 || s_BonusCoinCounter == 6
                      || s_BonusCoinCounter >= 9);

    if (milestone) {
        Game* game = Game::GetInstance();
        if (game && game_work.m_FruitCamera) {
            Vec3 coinPos = coin->pos;
            game_work.m_FruitCamera->CreateCameraShake(coinPos, 0.3f, 0.75f);
        }
        if (game && game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Bonus-Firework-Explode", 1.0f, 1.0f);
        }
        {
            PSPParticleManager& pm = PSPParticleManager::GetInstance();
            PSPParticleEmitter* e1 = pm.AddEmitter(StringHash("bonus_mode_fx_red"));
            if (e1) e1->m_Pos = coin->pos;
            // sprintf'd particle: "Bonus-Explosion-%i" with i in {1, 3, 5} cycling per milestone
            char pname[32];
            static const int kExplosionIds[3] = {1, 3, 5};
            int slot = (s_BonusCoinCounter == 3) ? 0 : (s_BonusCoinCounter == 6) ? 1 : 2;
            snprintf(pname, sizeof(pname), "Bonus-Explosion-%i", kExplosionIds[slot]);
            PSPParticleEmitter* e2 = pm.AddEmitter(StringHash(pname));
            if (e2) {
                e2->m_Pos = coin->pos;
                // random offset: x in [-12..0], y in [+3..15]
                e2->m_Pos.x += (float)(rand() % 13) * -1.0f;
                e2->m_Pos.y += 3.0f + (float)(rand() % 13);
            }
        }
        if (s_BonusCoinCounter >= 9) {
            s_BonusCoinCounter = 0;
        }
    }

    FN::AddToCurrentScore(coin->m_CoinValue, 0, false, false);
}

// ---------------------------------------------------------------------------
// BonusScreen ctor (binary @ 0x00132048)
// ---------------------------------------------------------------------------

BonusScreen::BonusScreen()
    : HUDControl3d(),
      m_DisplayedScore(0),
      m_TotalScore(0),
      m_PulseField15(0.0f),
      m_PulseTimer(0.0f),
      m_PulseField17(1.0f),
      m_PulseAngle(0),
      _padPulse(0),
      m_PulseTarget(0.0f, 0.0f, 0.0f),
      m_NameScale(1.0f),
      m_LeaderboardSubmitted(0),
      _pad1(0),
      m_DrumRollFired(0),
      _pad3(0),
      m_RushSFX(nullptr),
      // ASM-verified: 2026-05-22 binary @ 0x00132048 ctor reads
      // -kRevealHalfBeat (0.33298f) from a global timer array.
      // Negative phase = slide-in animation; transitions to 0 then positive
      // for the reveal beats.
      m_PhaseTimer(-kRevealHalfBeat),
      m_PosOffset(0.0f, 0.0f, 0.0f)
{
    m_Awards.reserve(3);

    // Binary @ 0x0013211c: m_LayerFlags = 0x80 (HUD_LAYER_POST_ACTOR).
    // ASM-verified: 2026-05-18 binary @ 0x0013211c (re-analyst)
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // ASM-verified: 2026-05-22 binary @ 0x0013207e..0x00132088 (re-analyst).
    // DAT_00132204 -> rodata "arcade_diolog_box.tex" (literal typo "diolog"
    // matches the asset on disk at Data/textures/arcade_diolog_box.tex).
    // Prior port string "dialog-box-big.tex" was a guess and didn't resolve --
    // m_Texture invalid -> size stayed at (0,0,0) -> HUDControl3d::Draw produced
    // a degenerate 0x0 quad and the dialog box was invisible.
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_Texture = bgTex;

    // ASM-verified: 2026-05-22 binary @ 0x001320cc..0x001320f4 (re-analyst).
    // Without this write, HUDControl3d::Draw renders a degenerate 0x0 quad.
    if (m_Texture.IsValid()) {
        float w = (float)m_Texture->m_Width;
        float h = (float)m_Texture->m_Height;
        size = Vec3(w, h, 0.0f);
    }

    // ASM-verified: 2026-05-22 binary @ 0x0013218a..0x001321c8 (re-analyst).
    // SFX preloads in order; clip names resolved from literal pool
    // 0x00132210..0x00132224. PreLoadSoundEx variant for the drum-roll
    // (vtbl[1]) -- looping/streamed flavour vs the one-shot fireworks/explosions.
    {
        Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
        sm.PreLoadSound  ("Bonus-Firework-Explode");
        sm.PreLoadSoundEx("Bonus-drum-roll", true);
        sm.PreLoadSound  ("equip-unlock");
        sm.PreLoadSound  ("Bonus-Explosion-1");
        sm.PreLoadSound  ("Bonus-Explosion-3");
        sm.PreLoadSound  ("Bonus-Explosion-5");
    }
}

// ---------------------------------------------------------------------------
// BonusScreen dtor (binary @ 0x00131F9C)
// ---------------------------------------------------------------------------

BonusScreen::~BonusScreen() {
    // m_RushSFX: not owned (pointer to a manager-owned sound), no delete.
    // m_Awards vector destructs its BonusAwardHud entries (SmartPtr released).
}

// ---------------------------------------------------------------------------
// AddAward (binary @ 0x00133664)
// ---------------------------------------------------------------------------

// Port convenience overload: BonusManager (and the binary's SetUpBonusScreen)
// carry the award colour as a packed BGRA uint32_t. Unpack into the engine
// Colour and delegate to the canonical binary AddAward(Colour, ...) below.
// (b,g,r,a byte order matches Colour's field layout.)
void BonusScreen::AddAward(uint32_t colour, Mortar::SmartPtr<Mortar::Texture> tex,
                           const char* name, int tier) {
    Colour c;
    c.b = (uint8_t)((colour >>  0) & 0xFF);
    c.g = (uint8_t)((colour >>  8) & 0xFF);
    c.r = (uint8_t)((colour >> 16) & 0xFF);
    c.a = (uint8_t)((colour >> 24) & 0xFF);
    AddAward(c, tex, name, tier);
}

// ---------------------------------------------------------------------------
// STUBS (binary methods not yet ported)
// ---------------------------------------------------------------------------

// Binary @ 0x00133664 -- BonusScreen::AddAward(Colour, SmartPtr<Texture>, char const*, int).
// Default-constructs a BonusAwardHud, copies the name, assigns the star texture,
// credits m_TotalScore += tier, sets m_TierBase = tier, m_DisplayedScore = 0,
// stores the award colour, then push_back into m_Awards.
// Binary field map (port BonusAwardHud):
//   this+0x80   (m_TotalScore)     += tier        (str r3,[r4,#0x80] @ 0x0013369c)
//   entry+0x40  (m_TierBase)        = tier         (str r6,[sp,#0x40]  @ 0x001336a2)
//   entry+0x4c  (m_DisplayedScore)  = 0            (str r3,[sp,#0x4c]  @ 0x001336a4)
//   entry+0x50/+0x58 colour copies  = colour       (Colour::operator= x2)
//   entry+0x54                       = DAT_001336e4 = 0.0f (initial; Update recomputes m_Scale)
//   m_Multiplier (+0x44)            left at default 1 (binary never writes +0x44 here).
// The binary keeps two colour fields (+0x50 m_Colour0, +0x58 m_Colour1); the port
// collapses these into the single ASM-verified m_Colour used by Update/Draw.
void BonusScreen::AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                           const char* name, int tier) {
    BonusAwardHud entry;                 // default ctor: m_Multiplier = 1, m_Scale = 1.0
    if (name) {
        std::strncpy(entry.m_Name, name, sizeof(entry.m_Name) - 1);
        entry.m_Name[sizeof(entry.m_Name) - 1] = '\0';
    }
    entry.m_StarTex        = tex;
    m_TotalScore          += tier;
    entry.m_TierBase       = tier;
    entry.m_DisplayedScore = 0;
    entry.m_Colour         = colour;
    // entry+0x54 = DAT_001336e4 (0.0f) in the binary; the port keeps m_Scale at its
    // default 1.0 since Update overwrites it each frame via the SinIdx wobble.
    m_Awards.push_back(entry);
}

// Port specific: the phantom Draw(float*) overload was removed — the single binary
// Draw @ 0x0013325C is the HUDControl3d vtable override Draw(const Vec3&, int)
// which carries the canonical logic.

// Binary @ 0x00131d58 — returns pfVar14[0] = kRevealStart (0.6660f).
// ASM-verified: 2026-05-23 binary @ 0x00131d58 (re-analyst)
float BonusScreen::GetTimeFirstAward() {
    return kRevealStart;
}

// Binary @ 0x00131d74 — returns pfVar14[1] = kPerAward (0.6f).
// ASM-verified: 2026-05-23 binary @ 0x00131d74 (re-analyst)
float BonusScreen::GetTimePerAward() {
    return kPerAward;
}

// Binary @ 0x00131d50 — confirmed empty (single bx lr).
// ASM-verified: 2026-05-23 binary @ 0x00131d50 (re-analyst)
void BonusScreen::LoadContent() {}

// Binary @ 0x00131d94 — sets pulse-shake fields and randomizes angle.
// param_1 = amplitude, param_2 = duration.
// ASM-verified: 2026-05-23 binary @ 0x00131d94 (re-analyst)
void BonusScreen::Shake(float amplitude, float duration) {
    m_PulseField15 = duration;
    m_PulseField17 = amplitude;
    m_PulseTimer   = amplitude;
    m_PulseAngle   = (int16_t)Math::g_Random.Rand32(0xff3a);
}

// Binary @ 0x00131d54 — confirmed empty (single bx lr).
// ASM-verified: 2026-05-23 binary @ 0x00131d54 (re-analyst)
void BonusScreen::UnLoadContent() {}

// ---------------------------------------------------------------------------
// AwardScores (binary @ 0x0013260C)
// One-shot finale: coin spawn, camera shake, big particle, finish SFX.
// ---------------------------------------------------------------------------

// Binary @ 0x0013260C
void BonusScreen::AwardScores() {
    // Spawn position: base pos + m_PosOffset chain (binary: pos + member field chain).
    // TODO: resolve exact member offsets from _Stack_9c/_Stack_a8/_Stack_b4 in binary frame.
    Vec3 spawnPos = pos;
    spawnPos.x += m_PosOffset.x;
    spawnPos.y += m_PosOffset.y;
    spawnPos.z += m_PosOffset.z;

    // Reset per-coin milestone counter for this batch.
    s_BonusCoinCounter = 0;

    Mortar::Delegate1<void, Coin*> onArrived =
        Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival);

    int total = m_TotalScore;
    if (total <= 5) {
        // Single batch, all coins, delay step -0.05 / cap -0.3.
        Coin::MakeCoins(total, 6,
                        Vec3(-0.05f, -0.3f, 0.0f),
                        0xff3a, 0, &spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        onArrived, false);
    } else {
        // First batch: 6 coins, tighter delay cap -0.5.
        Coin::MakeCoins(6, 6,
                        Vec3(-0.05f, -0.5f, 0.0f),
                        0xff3a, 0, &spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        onArrived, false);
        // Second batch: leftover, no arrival callback (silent score credit).
        Coin::MakeCoins(total - 6, 6,
                        Vec3(-0.05f, -0.3f, 0.0f),
                        0xff3a, 0, &spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        Mortar::Delegate1<void, Coin*>(), false);
    }

    // ASM-verified: 2026-05-20 binary @ 0x0013260C -- game_work.field_0x34 = 3 (BonusFinalePhase).
    // Field is uint8 in the port -- binary uses 4-byte `str.w` instructions at +0x34 because
    // the source value (small enum) is known-zero in upper bytes, not because the field is
    // int32. The adjacent +0x35 (m_bSlowMotion) is a true byte field (`strb.w`), confirmed
    // by independent writes at 0x0016c178/0x0016c1b6/0x0016c324/0x00141bec/0x00141e52/
    // 0x00142002. Layout stays as {uint8 field_0x34; uint8 m_bSlowMotion;}.
    game_work.field_0x34 = 3;

    Game* game = Game::GetInstance();
    if (game && game_work.m_FruitCamera) {
        game_work.m_FruitCamera->CreateCameraShake(spawnPos, 0.3f, 1.0f);
    }

    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        PSPParticleEmitter* e = pm.AddEmitter(StringHash("impact_fx"));
        if (e) { e->m_Pos = spawnPos; }
    }

    // Return value discarded in binary (cache-warming call).
    if (game) {
        (void)game_work.currentScore;
    }

    // Vol = 0.0f is the literal binary value (DAT_00132910 = 0x00000000).
    // Per-coin fireworks in AddToScoreOnArrival handle audibility.
    if (game && game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("equip-unlock", 0.0f, 1.0f,
                                  Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

// ---------------------------------------------------------------------------
// Update (binary @ 0x00132930) — three-phase state machine driven by m_PhaseTimer
// ---------------------------------------------------------------------------

// Binary @ 0x00132930
void BonusScreen::Update(float dt) {
    // ASM-verified: 2026-05-18 binary @ 0x00132930 (re-analyst). Timer is owned by GameOverScreen, not BonusScreen.
    // m_PhaseTimer is written by GameOverScreen::Update case-1 postlude: m_pBonusScreen->m_PhaseTimer = m_Timer;
    // BonusScreen::Update only READS m_PhaseTimer; it must NOT advance it here.
#ifndef __bada__
    {
        static int s_LogFrame = 0;
        static int s_LogThrottle = 0;
        if (s_LogThrottle == 0) {
            LOG_VERBOSE("BONUS_SCREEN", "frame=%d phaseTimer=%.3f awards=%d",
                        s_LogFrame,
                        m_PhaseTimer,
                        (int)m_Awards.size());
        }
        s_LogFrame++;
        s_LogThrottle = (s_LogThrottle + 1) % 60;
    }
#endif

    // -----------------------------------------------------------------------
    // Dismiss check (binary: early-out before phase B/C logic)
    // pfVar14[4] + pfVar14[3] = 7.0 + 0.25 = 7.25
    // -----------------------------------------------------------------------
    if (m_PhaseTimer > kDismissBuffer + kFinaleHoldExtra) {
        m_bPendingRemoval = 1;
    }

    // -----------------------------------------------------------------------
    // Phase A: pre-show slide-in (timer < 0). Binary @ 0x00132a98 (else of
    // timer >= 0). The drum-roll RushSFX gate already runs above its sibling
    // branch in the binary; here we handle the positional/camera animation.
    // -----------------------------------------------------------------------
    if (m_PhaseTimer < 0.0f) {
        // phase01 = timer / kRevealHalfBeat; wrap into [0,1) toward the reveal.
        // Binary @ 0x00132b2a: s15 = field30_0xb8 / pfVar15[2]; if <0 +=1 else 1-x.
        float phase01 = m_PhaseTimer / kRevealHalfBeat;
        if (phase01 < 0.0f) phase01 += 1.0f;
        else                phase01 = 1.0f - phase01;

        // Positional offset = -g_BonusSlideOffset * (1 - SinIdx(100*phase01*182)/SinIdx(0x4718)).
        // Binary @ 0x00132b88..0x00132bf6: reads a module .bss Vec3 @ 0x00235920,
        // negates it, scales by the eased factor, writes field31/32/33 (m_PosOffset).
        // That .bss Vec3 has no static initialiser and no writer xref -> it is the
        // engine zero-vector, so the offset resolves to (0,0,0). The visible
        // slide-in is driven instead by the camera/projection matrix lerp below.
        // Binary @ 0x00132a98 (DAT_00132cd0=100.0, DAT_00132cd4=182.0, denom idx 0x4718).
        float easeFactor = 1.0f
            - Math::SinIdx((unsigned short)(int)(100.0f * phase01 * 182.0f))
              / Math::SinIdx(0x4718);
        // g_BonusSlideOffset is the engine zero-vector; -zero * easeFactor == zero.
        (void)easeFactor;
        m_PosOffset = Vec3(0.0f, 0.0f, 0.0f);

        // TODO: 0x00132a98 -- camera/projection slide-in lerp. Binary lerps the
        // active camera matrix at GameWork.field_0x3c, columns +0x14/+0x18/+0x1c,
        // each toward 0.5 by `easeFactor` (val += (0.5 - val) * easeFactor), then
        // squares easeFactor into the eased scalar. Blocked on FruitCamera
        // projection-matrix access (no port handle to the per-frame camera matrix
        // columns yet). The m_PosOffset write above is faithful (resolves to zero);
        // only the camera-matrix component of the slide is deferred.
        return;
    }

    // Compute finaleStart: revealStart + perAward * (numAwards + 0.25)
    float finaleStart = kRevealStart + kPerAward * ((float)m_Awards.size() + 0.25f);

    // Binary @ 0x00132c06: drum-roll fires when timer crosses kDrumRollPreThresh = -0.133f
    // from below (DAT_00132ccc=0.2f minus pfVar14[2]=0.3330f = -0.133f).
    // Gate: prevTimer <= thresh < newTimer (ascending, prev = m_PhaseTimer - dt).
    // ASM-verified: 2026-05-23 binary @ 0x00132c06 (re-analyst)
    {
        float prevTimer = m_PhaseTimer - dt;
        if (!m_DrumRollFired
                && kDrumRollPreThresh < m_PhaseTimer
                && prevTimer <= kDrumRollPreThresh) {
            m_DrumRollFired = 1;
            s_BonusExplosionCounter = 1;
            if (game_work.mGameSound) {
                game_work.mGameSound->SFXPlay("Bonus-drum-roll", 1.0f, 1.0f);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase B: per-award reveal (0 <= timer < finaleStart)
    // -----------------------------------------------------------------------
    if (m_PhaseTimer < finaleStart) {
        // ASM-verified: 2026-05-22 binary @ 0x00132930 (re-analyst).
        m_DisplayedScore = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float normT = (m_PhaseTimer - (float)i * kPerAward) / kPerAward;

            if (normT < 0.0f || normT > 1.0f) {
                // Outside this award's reveal window.
                entry.m_Colour.a = 0xff;
                entry.m_DisplayedScore = entry.m_TierBase * entry.m_Multiplier;
                entry.m_Scale = 1.0f;
            } else {
                // Within reveal beat.
                float beat = fmodf(m_PhaseTimer, kPerAward);
                // Alpha ramp: beat / kAlphaRampWidth (0.1f), clamped 0..1.
                // Binary @ 0x00132d60: fVar20 = fVar19 / DAT_00133080 (0.1f).
                float alpha = beat / kAlphaRampWidth;
                if (alpha > 1.0f) alpha = 1.0f;
                if (alpha < 0.0f) alpha = 0.0f;
                entry.m_Colour.a = (uint8_t)(alpha * 255.0f);
                if (beat <= 0.0f) {
                    entry.m_DisplayedScore = 0;
                } else {
                    entry.m_DisplayedScore = (int)((float)(entry.m_TierBase * entry.m_Multiplier) * (beat * 0.5f + 0.5f));
                }
                // Scale wobble: SinIdx(beat*120*182) / SinIdx(0x5550).
                // SinIdx is BAM-style (65536 = 2*pi). sinf approximation pending BAM-table port.
                // Binary @ 0x00133068: DAT_00133090=120, DAT_001330b0=182.
                float angle = beat * 120.0f * 182.0f * (2.0f * 3.14159265f / 65536.0f);
                float denom = 0.882f;  // sinf(0x5550 * 2*pi/65536) ~= 0.882
                entry.m_Scale = sinf(angle) / denom;

                // Per-award explosion SFX onset gate: fires when beat crosses kOnsetThresh (0.2f).
                // Binary @ 0x00132f2c: gate = (beat-dt-0.2f)/0.1f <= 0 AND (beat-0.2f)/0.1f > 0
                //   simplified: prevBeat <= 0.2f AND beat > 0.2f  (prevBeat = beat - dt).
                // ASM-verified: 2026-05-23 binary @ 0x00132f2c (re-analyst)
                float prevBeat = beat - dt;
                if (prevBeat <= kOnsetThresh && beat > kOnsetThresh) {
                    char sfxName[32];
                    snprintf(sfxName, sizeof(sfxName), "Bonus-Explosion-%u",
                             (unsigned)s_BonusExplosionCounter);
                    if (game_work.mGameSound) {
                        game_work.mGameSound->SFXPlay(sfxName, 1.0f, 1.0f);
                    }
                    s_BonusExplosionCounter += 2;
                }
            }
            m_DisplayedScore += entry.m_DisplayedScore;
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Phase C: finale one-shot (timer >= finaleStart, only once)
    // Binary: edge triggered — fires when prev <= finaleStart < m_PhaseTimer.
    // m_LeaderboardSubmitted is reused as the "finale fired" latch.
    // -----------------------------------------------------------------------
    if (m_LeaderboardSubmitted == 0) {
        m_LeaderboardSubmitted = 1;
        AwardScores();
        // Defunct: LeaderboardManager::RefreshLeaderboard (online-services-audit).

        // Tally final displayed scores.
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            m_Awards[i].m_DisplayedScore = m_Awards[i].m_TierBase * m_Awards[i].m_Multiplier;
            totalDisplayed += m_Awards[i].m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
    }

    // -----------------------------------------------------------------------
    // Pulse / shake wandering-target update (independent of phase).
    // Binary @ 0x00133166 (post-loop tail of Update).
    // m_PulseTimer (field16_0x94) decays; a unit vector at angle m_PulseAngle
    // scaled by (m_PulseTimer*m_PulseField15)/m_PulseField17 walks the shake
    // target. When the desired point gets within 5 units (MagnitudeSqr < 25)
    // the angle is re-randomised. m_PulseTarget integrates 0.2 of the delta
    // each frame. When the timer expires, m_PulseTarget snaps back to origin
    // (the binary reads a shared zero-Vec3 global @ GOT+0x73ec).
    // Binary @ 0x00133166
    if (m_PulseTimer > 0.0f) {
        // field16_0x94 -= dt
        m_PulseTimer -= dt;
        // fVar26 = (timer * field15) / field17
        float radius = (m_PulseTimer * m_PulseField15) / m_PulseField17;
        // unit dir at current angle, z = 0 (DAT_00133240 = 0.0)
        float s = Math::SinIdx((unsigned short)m_PulseAngle);
        float c = Math::CosIdx((unsigned short)m_PulseAngle);
        Vec3 desired(s * radius, c * radius, 0.0f);
        // delta = desired - m_PulseTarget
        Vec3 delta = desired - m_PulseTarget;
        if (delta.MagnitudeSqr() < 25.0f) {
            // angle += (int)((150.0 + RandF(1.0)*60.0) * 182.0)
            // DAT_00133248 = 150.0, DAT_00133244 = 60.0, DAT_0013324c = 182.0
            float bump = (150.0f + Math::g_Random.RandF(1.0f) * 60.0f) * 182.0f;
            m_PulseAngle = (int16_t)((int)m_PulseAngle + (int)bump);
        }
        // m_PulseTarget += delta * 0.2  (DAT_00133250 = 0.2)
        m_PulseTarget += delta * 0.2f;
    } else {
        // Snap shake target back to origin (binary: *(zero-Vec3 global)).
        m_PulseTarget = Vec3(0.0f, 0.0f, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0013325C)
// ---------------------------------------------------------------------------

// Binary @ 0x0013325C -- BonusScreen::Draw
// Layout: HUDControl3d::Draw renders dialog box; then total score (pFontBlue2,
// scale 26..40 by displayed/total ratio); then per-award row loop with
// star icon + name + score (pFontMain). Row Y step = -42.0 (timerArr[5]).
// Row 0 base = pos + m_PosOffset + Vec3(-105, 40, 0).
// Loop break (NOT continue) on first not-yet-revealed award.
// ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
void BonusScreen::Draw(const Vec3& hudScale, int layerMask) {
    // Apply m_PosOffset to pos before base draw, save original.
    Vec3 savedPos = pos;
    pos.x += m_PosOffset.x;
    pos.y += m_PosOffset.y;
    pos.z += m_PosOffset.z;

    // Background dialog box.
    HUDControl3d::Draw(hudScale, layerMask);

    Game* game = Game::GetInstance();
    if (!game) {
        pos = savedPos;
        return;
    }

    // --- Total-score text (pFontBlue2, scale 26..40, alignment 0x0F) ---
    // Binary @ 0x0013325C: scale = 26 + 14 * (displayed/total), clamped when total <= 0.
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%i", m_DisplayedScore);

        float scale;
        if (m_DisplayedScore < 1) {
            scale = 26.0f;
        } else {
            scale = ((float)m_DisplayedScore / (float)m_TotalScore) * 14.0f + 26.0f;
        }
        scale *= m_NameScale;

        // kScoreOffset = Vec3(40, -60, 0) — BonusScreen .bss +0x0c @ 0x0022f32C
        Vec3 textPos(pos.x + 40.0f, pos.y + (-60.0f), pos.z + 0.0f);
        // kTotalScoreColour = Colour(0, 0, 0, 0xff) — DAT @ 0x001f34d4
        Colour totalColour(0, 0, 0, 0xff);

        Mortar::Font* fontBlue2 = game_work.pFontBlue2.Get();
        if (fontBlue2) {
            Mortar::Utf8StringIterator iter(buf);
            // TODO: CopyGlobalVec2_BonusScreen — pass Vec2(0,0) until global is RE'd
            Vec2 align(0.0f, 0.0f);
            fontBlue2->DrawString(scale, 1.0f, 0.0f, iter, textPos, totalColour,
                                  align, 0x0F, 0.0f, nullptr);
        }
    }

    // --- Pre-loop offset: kStarOffset = Vec3(-105, 40, 0) — .bss +0x44 @ 0x0022f364 ---
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    pos.x += -105.0f;
    pos.y += 40.0f;
    // pos.z += 0.0f  (no z change)

    // --- Per-award row loop (pFontMain) ---
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    Mortar::Font* fontMain = game_work.pFontMain.Get();
    MatrixManager& mm = MatrixManager::GetInstance();

    for (int i = 0; i < (int)m_Awards.size(); ++i) {
        // Visibility gate — break (not continue): awards revealed in order.
        if (m_PhaseTimer - kRevealStart < (float)i * kPerAward) break;

        const BonusAwardHud& entry = m_Awards[i];

        // TEMP DEBUG: BonusScreen black block
        {
            static int s_DbgBonusDrawFrame = 0;
            if (s_DbgBonusDrawFrame++ < 5) {
                LOG_INFO("BONUS/Draw",
                    "row %d pos=(%.1f,%.1f) colour=BGRA(%02x,%02x,%02x,%02x) hasTex=%d",
                    i, pos.x, pos.y,
                    (unsigned)entry.m_Colour.b, (unsigned)entry.m_Colour.g,
                    (unsigned)entry.m_Colour.r, (unsigned)entry.m_Colour.a,
                    (int)entry.m_StarTex.IsValid());
            }
        }

        // Star quad: DrawQuadUnCached equivalent — port uses renderer.DrawQuad.
        if (entry.m_StarTex.IsValid()) {
            entry.m_StarTex->Set();
            uint32_t texW = (uint32_t)entry.m_StarTex->m_Width;
            uint32_t texH = (uint32_t)entry.m_StarTex->m_Height;
            float w = (float)texW + 1.0f;
            float h = (float)texH + 1.0f;

            Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
            mat.GlobalTranslate44(pos);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            game->renderer.DrawQuad(entry.m_Colour, 0.0f, 0.0f, 1.0f, 1.0f);
            entry.m_StarTex->UnSet();
        }

        if (fontMain) {
            // Award name: scale = 20, clamped so measure*20 <= 220.
            Mortar::Utf8StringIterator nameIter(entry.m_Name);
            float measure = fontMain->MeasureString(nameIter);
            float nameScale = 20.0f;
            if (measure * 20.0f > 220.0f) {
                nameScale = 220.0f / measure;
            }
            Vec2 align(0.0f, 0.0f);
            // Re-construct iter (MeasureString may advance it).
            Mortar::Utf8StringIterator nameIter2(entry.m_Name);
            fontMain->DrawString(nameScale, 1.0f, 0.0f, nameIter2, pos,
                                 entry.m_Colour, align, 0x0D, 0.0f, nullptr);

            // Award score: scale = entry.m_Scale * 24, pos += Vec3(250,250,250).
            // NOTE: Vec3(250,250,250) is the literal binary constant (timerArr[6] = 250.0).
            char scoreBuf[64];
            snprintf(scoreBuf, sizeof(scoreBuf), "%i", entry.m_DisplayedScore);
            Vec3 scorePos(pos.x + kTextOffsetScalar,
                          pos.y + kTextOffsetScalar,
                          pos.z + kTextOffsetScalar);
            float scoreScale = entry.m_Scale * 24.0f;
            Mortar::Utf8StringIterator scoreIter(scoreBuf);
            // ASM-verified: 2026-05-22 binary @ 0x001335ae (re-analyst).
            // Per-award SCORE uses flag 0x0F (movs r6,#0xf), NOT 0x0D like
            // the name above. Prior port used 0x0D for both.
            fontMain->DrawString(scoreScale, 1.0f, 0.0f, scoreIter, scorePos,
                                 entry.m_Colour, align, 0x0F, 0.0f, nullptr);
        }

        // Advance Y for next row: kAwardYStep = -42.0 (timerArr[5]).
        pos.y += kAwardYStep;
    }

    // Restore saved pos.
    pos = savedPos;
}
