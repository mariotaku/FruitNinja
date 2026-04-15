//
// GameModeScreen — mode select child screen (Classic / Zen / Arcade).
// See GameModeScreen.h and docs/screens/game-mode.md for binary refs.
//
// Analysed: 2026-04-15T14:00
//

#include "GameModeScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "asset/TextureManager.h"
#include "audio/GameSound.h"
#include <cmath>
#include <cstdio>

// Binary constants — resolved from read_memory at 0x0013ea00..0x0013ece0
// and 0x0013f458..0x0013f490 (see GameModeScreen::CreateControls and
// GameModeScreen::Update decompiles).

// Offline positions (online layout skipped — no network in port).
static const Vec3 POS_CLASSIC( 195.0f, -110.0f, 0.0f);  // DAT_0013ea04/08/0c
static const Vec3 POS_ZEN    ( -70.0f,   71.0f, 0.0f);  // DAT_0013ea18/1c/0c
static const Vec3 POS_ARCADE (  88.0f,   48.0f, 0.0f);  // DAT_0013ea58/5c/60

// Scale table used to shrink the fruit-piece entity + hit bounds after
// MenuButton::Init. Each slot comes from a distinct DAT address.
static const float CLASSIC_TARGET_SCALE = 0.75f;  // DAT_0013e59c, applied to m_TargetSize
static const float CLASSIC_FRUIT_SCALE  = 0.75f;  //                 and m_pFruitPiece->size
static const float ZEN_TARGET_SCALE     = 0.9f;   // DAT_0013ea20
static const float ZEN_FRUIT_SCALE      = 0.95f;  // DAT_0013ea24
static const float ZEN_HITBOUNDS_SCALE  = 0.85f;  // DAT_0013ea28
static const float ARCADE_FRUIT_SCALE   = 0.9f;   // DAT_0013ecac

// Update constants.
static const float ALPHA_LERP_STEP = 0.15f;    // DAT_0013f458 (state 0/1/2 alpha lerp rate)
static const float ALPHA_IN_DONE   = 0.9989f;  // DAT_0013f45c (state 2 clamp threshold)
static const float ALPHA_DECAY     = 0.85f;    // DAT_0013f480 (state 3-6 fade-out decay)
static const float ALPHA_OUT_DONE  = 0.001f;   // DAT_0013f484 (done threshold for state 3-6)
static const float CAMERA_DECAY    = 0.75f;    //              (state 3-6 m_CameraTransition decay)
static const float SECONDARY_CLAMP = 0.1f;     // DAT_0013f474 / DAT_0013f488 (clamp for m_SecondaryAlpha step)
static const float SECONDARY_RATE  = 0.25f;    //              m_SecondaryAlpha target step

// Port fruit indices for Zen/Arcade buttons. Binary resolves these via
// Fruit::FruitType("<name>", false) reading globals at DAT_0013ea50 /
// DAT_0013ecc4 / DAT_0013ecd8 — strings we haven't resolved. Port picks
// arbitrary non-bomb indices that don't clash with the MainScreen
// buttons (Play=3 watermelon, Dojo=9 mango).
//
// apple=0 banana=1 orange=2 watermelon=3 strawberry=4 kiwifruit=5
// pineapple=6 plum=7 pear=8 mango=9 ...
static const int FT_CLASSIC = 3;   // watermelon (binary also uses this — verified DAT_0013ea40 → g_pFruitInfo[0x80])
static const int FT_ZEN     = 5;   // kiwifruit (port choice)
static const int FT_ARCADE  = 8;   // pear      (port choice)

// Helper copied from MainScreen/DojoScreen.
static GLuint TexIdOf(const SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}
static Vec3 TexSizeOf(const SmartPtr<Mortar::Texture>& tex,
                      float defW, float defH) {
    if (tex.IsValid()) {
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    }
    return Vec3(defW, defH, 1.0f);
}

// Matches GameModeScreen::GameModeScreen(bool) @ 0x0013e524.
GameModeScreen::GameModeScreen(Game& g, bool isFromPause)
    : game(g)
    , m_TransitionAlpha(0.0f)   // field17_0x8c
    , m_State(0)                // field18_0x90
    , m_pClassicButton(NULL)    // field13_0xa0
    , m_pZenButton(NULL)
    , m_pArcadeButton(NULL)
    , m_ButtonDelay(-1.0f)      // field_0xa4 (-1 = inactive)
    , m_SecondaryAlpha(-2.5f)   // field24_0xb4 (DAT_0013e5a? = 0xc0200000)
    , m_FrameTimer(0.0f)        // field38_0xc8
    , m_bIsFromPause(isFromPause)
    , m_bButtonsCreated(false)
{
    // Binary writes 0x80 to HUDControl::m_LayerFlags (field37_0xc4) in
    // the ctor — the screen itself draws on layer 0x80.
    m_LayerFlags = 0x80;

    // BaseScreen's SetNull-on-primary-texture is mirrored by leaving
    // m_Texture at 0 (HUDControl3d default). The screen has no single
    // background texture — the button's textures are handled per-button.
}

GameModeScreen::~GameModeScreen() {
    RemoveButtons();
}

void GameModeScreen::Init() {
    m_State = 0;
    m_TransitionAlpha = 0.0f;
    m_bActive = 1;
}

void GameModeScreen::Release() {
    RemoveButtons();
}

// Matches GameModeScreen::CreateControls @ 0x0013e764.
//
// Binary creates 4 MenuButtons; port creates 3 (skips the multiplayer
// matchmaker button — no network). Each one:
//   1. Allocate a MenuButton.
//   2. Assign texture / size.
//   3. Init with position, callback, fruitType, zero-hit-bounds.
//   4. Post-Init tweak target/fruit scales.
//   5. HUD::AddControl.
//   6. (Binary only) TutorialControl::ResetTutePos — port has no tutorial.
void GameModeScreen::CreateControls() {
    if (m_bButtonsCreated) return;
    if (!game.hud) return;

    // The textures are stored on the FruitNinjaApp at offsets 0x17c /
    // +c790 / +c794 / +c78c per the decompile (via GOT). These are
    // intro banner textures — port doesn't have them loaded, so pass
    // a zeroed SmartPtr and MenuButton will render just the fruit.
    SmartPtr<Mortar::Texture> nullTex;

    // ---- Button 1: Classic mode ----
    // Binary: m_TargetSize *= 0.75, m_pFruitPiece->size *= 0.75
    m_pClassicButton = new MenuButton();
    m_pClassicButton->m_Texture = TexIdOf(nullTex);
    m_pClassicButton->size      = TexSizeOf(nullTex, 64.0f, 64.0f);
    m_pClassicButton->Init(POS_CLASSIC,
                           [this]() { ClassicModeCallback(); },
                           FT_CLASSIC, Vec3(0, 0, 0), nullptr);
    m_pClassicButton->m_TargetSize = m_pClassicButton->m_TargetSize * CLASSIC_TARGET_SCALE;
    if (m_pClassicButton->m_pFruitPiece) {
        // Binary: (fruit + 0x28) *= scale → Entity::scale (Vec3 at +0x28).
        m_pClassicButton->m_pFruitPiece->scale =
            m_pClassicButton->m_pFruitPiece->scale * CLASSIC_FRUIT_SCALE;
    }
    m_pClassicButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pClassicButton);

    // ---- Button 2: Zen mode ----
    m_pZenButton = new MenuButton();
    m_pZenButton->m_Texture = TexIdOf(nullTex);
    m_pZenButton->size      = TexSizeOf(nullTex, 64.0f, 64.0f);
    m_pZenButton->Init(POS_ZEN,
                       [this]() { ZenModeCallback(); },
                       FT_ZEN, Vec3(0, 0, 0), nullptr);
    m_pZenButton->m_TargetSize = m_pZenButton->m_TargetSize * ZEN_TARGET_SCALE;
    if (m_pZenButton->m_pFruitPiece) {
        m_pZenButton->m_pFruitPiece->scale =
            m_pZenButton->m_pFruitPiece->scale * ZEN_FRUIT_SCALE;
    }
    // m_HitBoundsScale *= 0.85 — binary writes to (iVar8 + DAT_0013ea48 + 0x20)
    m_pZenButton->m_HitBoundsScale = m_pZenButton->m_HitBoundsScale * ZEN_HITBOUNDS_SCALE;
    m_pZenButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pZenButton);

    // ---- Button 3: Arcade mode ----
    // Binary: fruit scale *= 0.9, m_TargetSize copied from globals
    // (matches the Zen globals — port uses same scales).
    m_pArcadeButton = new MenuButton();
    m_pArcadeButton->m_Texture = TexIdOf(nullTex);
    m_pArcadeButton->size      = TexSizeOf(nullTex, 64.0f, 64.0f);
    m_pArcadeButton->Init(POS_ARCADE,
                          [this]() { ArcadeModeCallback(); },
                          FT_ARCADE, Vec3(0, 0, 0), nullptr);
    m_pArcadeButton->m_TargetSize = m_pArcadeButton->m_TargetSize * ZEN_TARGET_SCALE;
    if (m_pArcadeButton->m_pFruitPiece) {
        m_pArcadeButton->m_pFruitPiece->scale =
            m_pArcadeButton->m_pFruitPiece->scale * ARCADE_FRUIT_SCALE;
    }
    m_pArcadeButton->m_LayerFlags = 0x80;
    game.hud->AddControl(m_pArcadeButton);

    m_bButtonsCreated = true;
    printf("[GameModeScreen] CreateControls: Classic/Zen/Arcade spawned\n");
}

void GameModeScreen::RemoveButtons() {
    if (m_pClassicButton) { m_pClassicButton->SetPendingRemoval(); m_pClassicButton = NULL; }
    if (m_pZenButton)     { m_pZenButton->SetPendingRemoval();     m_pZenButton     = NULL; }
    if (m_pArcadeButton)  { m_pArcadeButton->SetPendingRemoval();  m_pArcadeButton  = NULL; }
    m_bButtonsCreated = false;
}

// Matches GameModeScreen::Update @ 0x0013f10c.
//
// Port implements states 0, 2, 3-6, 0xe. Drops:
//   - State 1 (alternate entry from pause) — no pause in port.
//   - State 7/8/9 (network matchmaker recovery) — no network.
void GameModeScreen::Update(float dt) {
    switch (m_State) {
    case 0: {
        // Transition in: lerp m_TransitionAlpha toward 1.0 at step 0.15
        // Binary uses BaseScreen::IsTransitionInFinished (vtable +0x3c);
        // port uses a fixed threshold of 0.15 alpha which is enough for
        // the main-screen panel to slide far enough that the mode
        // buttons can appear under it.
        m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP;

        if (m_TransitionAlpha > ALPHA_IN_DONE) {
            m_TransitionAlpha = 1.0f;
            m_State = 2;
            // Binary calls vtable +0x40 → CreateControls here.
            CreateControls();
            // field_0xa8 = -1.0 (m_Unknown_A8); not needed in port.
        }
        break;
    }

    case 2: {
        // Idle: keep lerping alpha toward 1.0, tick button-delay timer.
        if (m_TransitionAlpha < ALPHA_IN_DONE) {
            m_TransitionAlpha += (1.0f - m_TransitionAlpha) * ALPHA_LERP_STEP;
        } else {
            m_TransitionAlpha = 1.0f;
        }

        // Lerp m_SecondaryAlpha toward 1.0 at step 0.25, clamped to
        // +/-0.1 per frame — used by Draw for a secondary fade effect.
        float step = (1.0f - m_SecondaryAlpha) * SECONDARY_RATE;
        if (step >  SECONDARY_CLAMP) step =  SECONDARY_CLAMP;
        if (step < -SECONDARY_CLAMP) step = -SECONDARY_CLAMP;
        m_SecondaryAlpha += step;

        // Tick button delay. Binary: if m_ButtonDelay > 0, subtract dt,
        // when <= 0 reset to -1.0 (inactive). Port mirrors.
        if (m_ButtonDelay > 0.0f) {
            m_ButtonDelay -= dt;
            if (m_ButtonDelay <= 0.0f) m_ButtonDelay = -1.0f;
        }
        break;
    }

    case 3:
    case 4:
    case 5:
    case 6: {
        // Mode-picked fade-out.
        m_TransitionAlpha *= ALPHA_DECAY;
        m_SecondaryAlpha = m_TransitionAlpha;

        // Decay the MainScreen camera transition. Binary reads/writes
        // game.m_TransitionTimer at +0x0c; port owns the same semantic
        // state in MainScreen::m_CameraTransition.
        if (game.mainScreen) {
            // vtable[+0x48] @ -0.9 threshold skipped — that's a camera
            // zoom-out hook; port's camera follows m_CameraTransition
            // directly and doesn't need the extra kick.

            float camT = game.mainScreen->GetCameraTransition();
            camT *= CAMERA_DECAY;
            game.mainScreen->SetCameraTransition(camT);

            if (fabsf(camT) < ALPHA_OUT_DONE) {
                // Play mode-selected SFX. Binary calls
                //   GameSound::SFXPlay(&game->pGameSound, "<name>", 1.0, 1.0, cb)
                // where <name> lives at DAT_0013f470. Port plays via
                // the game's singleton GameSound.
                if (game.pGameSound) {
                    game.pGameSound->SFXPlay("swoosh_sound", 1.0f, 1.0f);
                }

                game.mainScreen->SetCameraTransition(0.0f);
                game.pauseFlag = 0;  // binary (iVar3 + 5) = 0
                m_bPendingRemoval = 1;
                game.mainScreen->SetState(STATE_CAMERA_FADE);

                // Same-screen MP would iterate SlashEntities and call
                // SlashEntity::ColoursChanged here — port has no MP.
            }
        }
        break;
    }

    case 0xe: {
        // Back-out: quicker fade, push MainScreen into SLIDE_IN once
        // alpha crosses 0.25 downward. On full fade, mark for removal.
        float oldAlpha = m_TransitionAlpha;
        m_TransitionAlpha *= CAMERA_DECAY;  // DAT_0013f480 = 0.85? actually 0.75 in case 0xe per binary
        m_SecondaryAlpha  = m_TransitionAlpha;

        if (oldAlpha > 0.25f && m_TransitionAlpha <= 0.25f) {
            if (game.mainScreen) {
                game.mainScreen->SetState(STATE_SLIDE_IN);
            }
        }
        if (m_TransitionAlpha < ALPHA_OUT_DONE) {
            m_bPendingRemoval = 1;
        }
        break;
    }

    default:
        break;
    }

    // Binary m_FrameTimer accumulator (field38_0xc8): only ticks when
    // state > 2. Port uses it only for animation driving if any; keep
    // the accumulator faithful.
    if (m_State > 2) {
        m_FrameTimer += dt / 0.15f;  // DAT_0013f48c = 0.15
        if (m_FrameTimer < 0.0f) m_FrameTimer = 0.0f;
    }
}

// No background texture yet — the mode-select screen relies entirely
// on the sub-button fruits spinning in the main-screen backdrop. Draw
// is a no-op so the controls show through cleanly.
void GameModeScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
}

// --- Sub-button callbacks ---

// Matches ClassicModeCallback @ 0x0013dfb4.
void GameModeScreen::ClassicModeCallback() {
    printf("[GameModeScreen] Classic picked -> state 3, gameMode=0\n");
    m_State = 3;
    game.gameMode = 0;
}

// Matches ZenModeCallback @ 0x0013dffc.
void GameModeScreen::ZenModeCallback() {
    printf("[GameModeScreen] Zen picked -> state 6, gameMode=3\n");
    m_State = 6;
    game.gameMode = 3;
}

// Matches ArcadeModeCallback @ 0x0013e19c.
// Binary also calls FruitSaveData::AddToTotal for an arcade counter;
// port has no FruitSaveData so that tally is skipped.
void GameModeScreen::ArcadeModeCallback() {
    printf("[GameModeScreen] Arcade picked -> state 5, gameMode=2\n");
    m_State = 5;
    game.gameMode = 2;
}
