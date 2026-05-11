// Analysed: 2026-05-04T00:00
// FruitFactControl implementation
// Binary: ctor 0x0013cb60, Init 0x0013a278, Release 0x00139d24, etc.

#include "hud/FruitFactControl.h"
#include "game/GameMode.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "game/BonusManager.h"
#include "game/Bonus.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/LeaderboardList.h"
#include "game/LeaderboardManager.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "engine/asset/TextureManager.h"
#include "engine/network/NetworkManager.h"
#include "engine/math/Colour.h"
#include "engine/util/Delegate.h"
#include "engine/render/Font.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Renderer.h"
#include "engine/math/Matrix44.h"
#include "engine/util/Localisation.h"
#include <cstring>
#include <cstdio>
#include <list>

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// Static (class-level) content -- 17 shared textures.
// Binary @ 0x001399fc (LoadContent), 0x00139f84 (UnLoadContent).
// ---------------------------------------------------------------------------

static bool s_bLoaded = false;

// ASM-verified: 2026-05-11 binary @ 0x001399fc (re-analyst)
// Per-mode backplate textures (DAT_0013a520 / DAT_0013a51c / DAT_0013a524).
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexClassic;  // "fact_board.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexArcade;   // "arcade_results_diolog_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexZen;      // "diolog_box_big.tex"
// TODO: 0x001399fc -- load full 17-texture set; names from binary DAT constants.
// (combo-star, connect/download/leaderboard frames; remaining ~14 textures not yet resolved)

void FruitFactControl::LoadContent() {
    if (s_bLoaded) return;
    s_bLoaded = true;

    s_PanelTexClassic = TextureManager::LoadLocalisedTexture("fact_board.tex");
    s_PanelTexArcade  = TextureManager::LoadLocalisedTexture("arcade_results_diolog_box.tex");
    s_PanelTexZen     = TextureManager::LoadLocalisedTexture("diolog_box_big.tex");
}

void FruitFactControl::UnLoadContent() {
    if (!s_bLoaded) return;
    s_bLoaded = false;

    s_PanelTexClassic.SetNull();
    s_PanelTexArcade.SetNull();
    s_PanelTexZen.SetNull();
}

// ---------------------------------------------------------------------------
// FruitFactControl ctor  (Binary @ 0x0013cb60)
// ---------------------------------------------------------------------------

FruitFactControl::FruitFactControl()
    : HUDControl3d()
    , m_AnimTimer(0.0f)
    , m_pCurFactString(nullptr)
    , m_FruitIdx(-1)
    , m_FactIdx(-1)
    , m_FactTexture()
    , m_FactPosOffset(-69.0f, 53.0f, 0.0f)
    , m_FactColour(0x74, 0x5d, 0x3b, 0xff)
    , m_ComboLength(0)
    , m_StarTimer(-8.0f)
    , m_bConnectPressed(0)
    , m_ComboStarTex()
    , m_ComboType(-1)
    , m_PomCount(0)
    , m_pLeaderboardMenu(nullptr)
    , m_pConnectButton(nullptr)
    , m_LBVisitedCount(0)
    , m_LBProgressTimer(0.0f)
    , m_LBState(0)
    , m_pLeftButton(nullptr)
    , m_pRightButton(nullptr)
    , m_StarType(0)
{
    memset(m_ComboHashArray, 0, sizeof(m_ComboHashArray));
    memset(_pad_factColour, 0, sizeof(_pad_factColour));
    memset(_pad_D9, 0, sizeof(_pad_D9));
    memset(_pad_E5, 0, sizeof(_pad_E5));
    memset(_pad_LocalScore, 0, sizeof(_pad_LocalScore));
    memset(_pad_FriendScore1, 0, sizeof(_pad_FriendScore1));
    memset(_pad_FriendScore2, 0, sizeof(_pad_FriendScore2));
    memset(_pad_201, 0, sizeof(_pad_201));

    // Binary: snapshot NetworkManager+4 byte to m_StarType
    // Defunct: NetworkManager online state -- no-op; m_StarType stays 0
    // m_StarType = *(uint8_t*)(NetworkManager::GetInstance() + 4); // Defunct

    // Binary: IsProviderOnline() -> m_LBState
    // Defunct: always offline
    m_LBState = 0;

    m_bNoDestructor = 1;
    m_Timer = 0.0f;

    LoadContent();

    // Binary calls Reset() at end of ctor (via vtable)
    Reset();
}

// ---------------------------------------------------------------------------
// FruitFactControl dtor  (Binary @ 0x00139e6c)
// ---------------------------------------------------------------------------

FruitFactControl::~FruitFactControl() {
    Release();
    // FNHighscore dtors fire automatically (trivial)
    // SmartPtr dtors fire automatically
}

// ---------------------------------------------------------------------------
// Init  (Binary @ 0x0013a278)
// ---------------------------------------------------------------------------

void FruitFactControl::Init() {
    m_StarTimer = -8.0f;
    m_ComboStarTex.SetNull();

    int savedLBState = m_LBState;
    m_LBState = 0;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game->gameMode : 0;

    // ASM-verified: 2026-05-11 binary @ 0x0013a2a6..0x0013a2ba (re-analyst)
    // Per-mode backplate pick (DAT_0013a520/51c/524).
    if (gameMode == 2) {
        if (s_PanelTexArcade.IsValid()) m_Texture = s_PanelTexArcade;
    } else if (gameMode == 3) {
        if (s_PanelTexZen.IsValid()) m_Texture = s_PanelTexZen;
    } else {
        if (s_PanelTexClassic.IsValid()) m_Texture = s_PanelTexClassic;
    }
    if (m_Texture.IsValid()) {
        size.x = (float)(m_Texture->m_Width + 1);
        size.y = (float)(m_Texture->m_Height + 1);
        size.z = 0.0f;
        // ASM-verified: 2026-05-11 binary @ 0x0013a31e (re-analyst)
        // 1.37f scale (DAT_0013a500) applies only in Classic (gameMode == 0).
        if (gameMode == 0) {
            size.x *= 1.37f;
            size.y *= 1.37f;
            size.z *= 1.37f;
        }
    }

    // ASM-verified: 2026-05-11 binary @ 0x0013a278 (re-analyst)
    // m_FactPosOffset: non-combo default (-69, 53, 0); combo path needs
    // FruitSaveData+0x208 ComboLength >= 3 which conflicts with m_BombQueueCount
    // at that offset -- defer combo branch until field identity confirmed.
    // TODO: 0x0013a3xx -- combo-length read from FruitSaveData+0x208; if >= 3
    //   and gameMode in {2,3}: m_FactPosOffset = Vec3(140.0f, -72.0f, 0.0f)
    m_FactPosOffset = Vec3(-69.0f, 53.0f, 0.0f);

    // TODO: 0x0013a278 -- m_PomCount set per gameMode / combo length
    // TODO: 0x0013a278 -- branch combo vs no-combo for status string
    // TODO: 0x0013a278 -- copy m_ComboHashArray + m_ComboLength from FruitSaveData
    // TODO: 0x0013a278 -- call CheckCombo / GetComboStarTexture

    m_FactColour.a = 0xFF;

    // Set FruitIdx from FindMostOfFruit result stored in Game/FruitSaveData
    // TODO: 0x0013a278 -- read most-fruit-idx from the correct game/save
    // offset (binary reads game+0x118 / GameOverScreen field_0x118).
    // Use random fruit index for now so the user doesn't always see apple.
    // Negative factIdx triggers Fruit::GetFact's random-fact path.
    if (game && game->pSaveData) {
        const int n = FruitInfo_GetCount();
        m_FruitIdx = (n > 0)
            ? (int)WaveManager::GetInstance()->GetRandom().Rand32((uint32_t)n)
            : 0;
    }

    // Load fact string for current fruit/fact index (factIdx<0 = random pick).
    if (m_FruitIdx >= 0) {
        int outType = 0, outFactIdx = 0;
        m_pCurFactString = Fruit::GetFact(&outType, &outFactIdx, m_FruitIdx, -1);
        m_FactIdx = outFactIdx;

        // Load per-fruit fact texture (binary @ 0x0013a3f0). The "rings"
        // decoration the user reported missing is baked INTO this per-fruit
        // sprite (e.g. apple_facts.tex draws fruit + decorative rings as a
        // single PNG). Format string from binary @ 0x001bcca0: "%s_facts".
        // ASM-verified: 2026-05-11 (re-analyst).
        const char* fruitBase = Fruit::FruitFactTexture(m_FruitIdx);
        if (fruitBase && *fruitBase) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s_facts.tex", fruitBase);
            m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
        }
    }

    m_LBState = savedLBState;
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    Reset();
}

// ---------------------------------------------------------------------------
// Release  (Binary @ 0x00139d24)
// ---------------------------------------------------------------------------

void FruitFactControl::Release() {
    m_FactTexture.SetNull();
    m_ComboStarTex.SetNull();
    // Binary: SetNull on a third texture (TODO: identify)

    Game* game = Game::GetInstance();
    HUD* hud = game ? game->hud : nullptr;

    // Order from binary: m_pLeaderboardMenu, m_pLeftButton, m_pRightButton, m_pConnectButton.
    // Each child gets its vtable Release() invoked before delete to mirror the
    // binary's HUD::Release lifecycle (so MenuButton::Release runs and clears
    // entity backrefs) -- direct delete here bypasses HUD::Release's loop.
    if (m_pLeaderboardMenu) {
        if (hud) hud->RemoveControl(m_pLeaderboardMenu);
        m_pLeaderboardMenu->Release();
        delete m_pLeaderboardMenu;
        m_pLeaderboardMenu = nullptr;
    }
    if (m_pLeftButton) {
        if (hud) hud->RemoveControl(m_pLeftButton);
        m_pLeftButton->Release();
        delete m_pLeftButton;
        m_pLeftButton = nullptr;
    }
    if (m_pRightButton) {
        if (hud) hud->RemoveControl(m_pRightButton);
        m_pRightButton->Release();
        delete m_pRightButton;
        m_pRightButton = nullptr;
    }
    if (m_pConnectButton) {
        if (hud) hud->RemoveControl(m_pConnectButton);
        m_pConnectButton->Release();
        delete m_pConnectButton;
        m_pConnectButton = nullptr;
    }
}

// ---------------------------------------------------------------------------
// BeginDraw  (Binary @ 0x0013a0bc)
// ---------------------------------------------------------------------------

void FruitFactControl::BeginDraw(float /*dt*/) {
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game->gameMode : 0;

    // If fact texture is set AND (dojo mode OR (arcade AND pom <= 1)):
    if (m_FactTexture) {
        if (gameMode == Mortar::GAME_MODE_ZEN || (gameMode == Mortar::GAME_MODE_ARCADE && m_PomCount <= 1)) {
            m_LayerFlags |= Mortar::HUD_LAYER_BUTTONS;
        }
    }
}

// ---------------------------------------------------------------------------
// Update  (Binary @ 0x0013b604)
// ---------------------------------------------------------------------------

void FruitFactControl::Update(float dt) {
    // m_AnimTimer += dt*8, wrap to [0, 8)
    m_AnimTimer += dt * 8.0f;
    if (m_AnimTimer >= 8.0f) m_AnimTimer -= 8.0f;

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    Game* game = Game::GetInstance();
    if (!game) return;

    uint8_t gameMode = game->gameMode;

    if (gameMode == Mortar::GAME_MODE_ARCADE) {
        // Arcade mode: lazy-create leaderboard menu
        // TODO: 0x0013b604 -- lazy-create m_pLeaderboardMenu (LeaderboardList) at pos.y-8

        if (m_PomCount == 1) {
            // Leaderboard tab
            UpdateLeaderboard(dt);
            return;
        }
        if (m_PomCount == 0) {
            // Bonus tab: BonusManager::GetFirstBestBonus() walk
            // TODO: 0x0013b604 -- BonusManager bonus walk + SetUpBonusScreen fallback
            std::list<Bonus>::iterator it;
            BonusManager* bm = BonusManager::GetInstance();
            Bonus* bonus = bm->GetFirstBestBonus(it);
            if (!bonus) {
                // TODO: 0x0013b604 -- BonusManager::SetUpBonusScreen(nullptr)
                bm->SetUpBonusScreen(nullptr);
            }
        }
    } else if (gameMode == Mortar::GAME_MODE_ZEN) {
        // Dojo/Zen: combo-star SFX cadence
        // TODO: 0x0013b604 -- combo-star SFX via GameSound::SFXPlay
    }
}

// ---------------------------------------------------------------------------
// UpdateLeaderboard  (Binary @ 0x0013afbc)
// ---------------------------------------------------------------------------

void FruitFactControl::UpdateLeaderboard(float dt) {
    // State machine 0..4: offline / loading / have-data / showing / error
    // With defunct online services, stays at state 0 (offline).
    // Call shape preserved; HUD wiring for connect button is maintained.

    switch (m_LBState) {
    case 0:
        // Offline: spawn connect MenuButton if not present
        // TODO: 0x0013afbc -- lazy-create m_pConnectButton with Delegate bound to ConnectPressed
        (void)dt;
        break;
    case 1:
        // Loading
        m_LBProgressTimer += dt;
        // Defunct: stays at loading
        break;
    case 2:
        // Have data
        // TODO: 0x0013afbc -- populate leaderboard rows
        break;
    case 3:
        // Showing
        break;
    case 4:
        // Error
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// DrawOrder  (Binary @ 0x0013b95c, vtable slot 9)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawOrder(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
    // ASM-spec: 2026-05-11 binary @ 0x0013b95c Classic branch (re-analyst).
    // Wires the minimum Classic-mode draw chain:
    //   1) Backplate quad (m_Texture from LoadContent) at pos - (1, 8, 0)
    //   2) "FRUIT FACT" title at (pos.x - 66, pos.y) size 16
    //   3) Fact body (m_pCurFactString) at (pos.x - 64, pos.y - 14)
    //      size 16 with maxWHx=128 (auto-shrink loop deferred -- TODO).
    //
    // Skipped (not yet ported):
    //   - Combo-bead row + sin-pulse (m_ComboHashArray walk, gameMode 3)
    //   - Auto-shrink loop: while GetStringHeight(scale, 128) > 96: scale -= 0.125
    //   - Arcade leaderboard / bonus list (gameMode 2)
    //   - Game[+3] flag-driven posY tweak (defaults to 0)

    // TEMP: Font::DrawString from FruitFactControl::DrawOrder crashes
    // screen_gameover test (segfault). Bisect found that ANY Font::DrawString
    // from this control crashes (even with literal "test" string at (0,0)),
    // while the same call from ScoreControl works fine. Some interaction with
    // HUD list ordering or per-instance Font state when called from this draw
    // position. Backplate-only path is safe -- only Font::DrawString crashes.
    //
    // Stubbed for now until the underlying Font / HUD interaction is RE'd.
    // Restore from this commit's predecessor (or this file's git history) once
    // the crash is understood.
    //
    // Bisect log:
    //   Backplate-only:           passes
    //   Backplate + title:        passes
    //   Backplate + body:         crashes
    //   Body alone (no other):    crashes
    Game* game = Game::GetInstance();
    if (!game || !game->pFontMain.IsValid()) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // ---- 1. Backplate quad (binary @ 0x0013c79c..0x0013c848) ----
    if (m_Texture.IsValid()) {
        // DIAGNOSTIC: shrink backplate scale to 0.25x to test if FFC backplate
        // is the "huge unidentifiable texture" the user is seeing.
        const float dbgK = 0.25f;
        fprintf(stderr, "[DBG FFC backplate] tex=%dx%d size=(%.1f,%.1f,%.1f) "
                "pos=(%.1f,%.1f,%.1f) drawSize=(%.1f,%.1f)\n",
                m_Texture->m_Width, m_Texture->m_Height,
                size.x, size.y, size.z,
                pos.x, pos.y, pos.z,
                size.x * dbgK, size.y * dbgK);
        m_Texture->Set();
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(size.x * dbgK, size.y * dbgK, 1.0f);
        mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_Texture->UnSet();
    }

    // ---- 2. "SENSEI'S FRUIT FACT" title (binary @ 0x0013c852..0x0013c92c) ----
    {
        // ASM-verified: 2026-05-11 binary @ 0x0013c886..0x0013c894 (asm-inspector)
        //   GETSTRING(0x9b) -> "CODE_FRUIT_FACT_TITLE" -> "SENSEI'S FRUIT FACT"
        //   translate = (pos.x - 66.0, pos.y + 42.0, 0.0)
        //   DAT_0013cb04 = 66.0, DAT_0013cb00 = 42.0
        const char* title = Localisation::Get("CODE_FRUIT_FACT_TITLE");
        if (!title) title = "FRUIT FACT";
        const float titleX = pos.x - 66.0f;
        const float titleY = pos.y + 42.0f;
        game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
            title, Vec3(titleX, titleY, 0.0f),
            m_FactColour, 0x0F);
    }

    // ---- 3. Fact body text (binary @ 0x0013c930..0x0013ca36) ----
    if (m_pCurFactString) {
        const Colour brown(0x74, 0x5D, 0x3B, 0xFF);
        // ASM-verified: 2026-05-11 binary @ 0x0013c9ea..0x0013ca02 (asm-inspector)
        //   translate = (pos.x - 64.0, pos.y - 14.0, 0.0)
        //   DAT_0013cb10 = 64.0, immediate 0x41600000 = 14.0
        //   (binary also has a conditional +4.0 Y bias driven by Game[+3],
        //    deferred -- TODO: 0x0013c9da resolve Game[+3] flag.)
        const float bodyX = pos.x - 64.0f;
        const float bodyY = pos.y - 14.0f;
        // ASM-verified: 2026-05-11 binary @ 0x0013c95e auto-shrink loop.
        //   scale = 16.0f
        //   while (Font::GetStringHeight(scale, 128.0f) > 96.0f)
        //       scale -= 0.125f
        // Shrinks the font until the wrapped fact body fits the 128x96 box.
        float scale = 16.0f;
        {
            Mortar::Utf8StringIterator iter(m_pCurFactString);
            while (game->pFontMain->GetStringHeight(iter, scale, 128.0f) > 96.0f) {
                if (scale <= 0.5f) break;  // safety floor
                scale -= 0.125f;
                iter = Mortar::Utf8StringIterator(m_pCurFactString);
            }
        }
        game->pFontMain->DrawString(scale, 1.0f, 0.0f,
            m_pCurFactString, Vec3(bodyX, bodyY, 0.0f),
            brown, 0x0F);
    }

    // ---- 4. Per-fruit fact icon (binary @ ~0x0013ca60) ----
    // The "rings" decoration is baked INTO this per-fruit sprite (e.g.
    // apple_facts.tex draws the fruit + decorative concentric rings as a
    // single asset).
    // ASM-verified: 2026-05-11 binary @ 0x0013caae (re-analyst).
    //   scale = (W, H, 0) -- texture's pixel dimensions; no +1, no 1.37x.
    //   translate = pos + m_FactPosOffset - (8, -8, 0)
    //             = (pos.x + offX - 8, pos.y + offY + 8, pos.z + offZ)
    if (m_FactTexture.IsValid()) {
        const float w = (float)m_FactTexture->m_Width;
        const float h = (float)m_FactTexture->m_Height;
        // DIAGNOSTIC: scale fact-icon to 0.25x.
        const float dbgK = 0.25f;
        fprintf(stderr, "[DBG FFC fact-icon] tex=%.0fx%.0f offset=(%.1f,%.1f,%.1f) "
                "translate=(%.1f,%.1f,%.1f) drawSize=(%.1f,%.1f)\n",
                w, h,
                m_FactPosOffset.x, m_FactPosOffset.y, m_FactPosOffset.z,
                pos.x + m_FactPosOffset.x - 8.0f,
                pos.y + m_FactPosOffset.y + 8.0f,
                pos.z + m_FactPosOffset.z,
                w * dbgK, h * dbgK);
        m_FactTexture->Set();
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(w * dbgK, h * dbgK, 0.0f);
        mat.GlobalTranslate44(Vec3(
            pos.x + m_FactPosOffset.x - 8.0f,
            pos.y + m_FactPosOffset.y + 8.0f,
            pos.z + m_FactPosOffset.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_FactTexture->UnSet();
    }
}

// ---------------------------------------------------------------------------
// DrawLeaderboard  (Binary @ 0x0013aac0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawLeaderboard() {
    // TODO: 0x0013aac0 -- backplate + state-dependent caption
    // Defunct online: stays at "Tap to connect" caption (GETSTRING(0x8F..0x94, 0x299, 0x72))
}

// ---------------------------------------------------------------------------
// DrawDownloadIcon  (Binary @ 0x001395d0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawDownloadIcon() {
    // TODO: 0x001395d0 -- 8-segment spinning ring (48 verts at angles 0/0x1ffe/...);
    // Texture::Set("blob_circle.tex"); identity scale ~1; translate pos+(-7,-23,0);
    // Mesh::DrawTriList(verts, 48, false, nullptr); UnSet.
    // Pending: Mesh port required.
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

// Binary @ 0x001394ec
bool FruitFactControl::LeftPressed(InputEvent* /*ev*/) {
    --m_FactIdx;
    const FruitInfo* fi = Fruit::FruitInfo(m_FruitIdx);
    if (fi && m_FactIdx < 0) {
        m_FactIdx = fi->m_FactCount - 1;
    }
    int outType = 0, outIdx = 0;
    m_pCurFactString = Fruit::GetFact(&outType, &outIdx, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x001394b0
bool FruitFactControl::RightPressed(InputEvent* /*ev*/) {
    ++m_FactIdx;
    const FruitInfo* fi = Fruit::FruitInfo(m_FruitIdx);
    if (fi && m_FactIdx >= fi->m_FactCount) {
        m_FactIdx = 0;
    }
    int outType = 0, outIdx = 0;
    m_pCurFactString = Fruit::GetFact(&outType, &outIdx, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x0013993c
bool FruitFactControl::UpPressed(InputEvent* /*ev*/) {
    // Cycle to next fruit with at least 1 fact
    int count = FruitInfo_GetCount();
    for (int i = 0; i < count; ++i) {
        m_FruitIdx = (m_FruitIdx + 1) % count;
        const FruitInfo* fi = Fruit::FruitInfo(m_FruitIdx);
        if (fi && fi->m_FactCount > 0) break;
    }
    m_FactIdx = 0;
    int outType = 0, outIdx = 0;
    m_pCurFactString = Fruit::GetFact(&outType, &outIdx, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x0013987c
bool FruitFactControl::DownPressed(InputEvent* /*ev*/) {
    // Cycle to previous fruit with at least 1 fact
    int count = FruitInfo_GetCount();
    for (int i = 0; i < count; ++i) {
        m_FruitIdx = (m_FruitIdx - 1 + count) % count;
        const FruitInfo* fi = Fruit::FruitInfo(m_FruitIdx);
        if (fi && fi->m_FactCount > 0) break;
    }
    m_FactIdx = 0;
    int outType = 0, outIdx = 0;
    m_pCurFactString = Fruit::GetFact(&outType, &outIdx, m_FruitIdx, m_FactIdx);
    return true;
}

// ---------------------------------------------------------------------------
// Button callbacks
// ---------------------------------------------------------------------------

// Binary @ 0x0013a130
void FruitFactControl::LeftButton() {
    // SFX play + --m_PomCount with wrap 0..1
    // TODO: 0x0013a130 -- SFX via Game::GetInstance()->pGameSound->SFXPlay(...)
    if (m_PomCount > 0) {
        --m_PomCount;
    } else {
        m_PomCount = 1;
    }
    // TODO: 0x0013a130 -- FruitSaveData::AddToTotal("PomTabIndex", hash, m_PomCount+1, true, true)
}

// Binary @ 0x0013a1d4
void FruitFactControl::RightButton() {
    // SFX play + ++m_PomCount with wrap 0..1
    // TODO: 0x0013a1d4 -- SFX via Game::GetInstance()->pGameSound->SFXPlay(...)
    if (m_PomCount < 1) {
        ++m_PomCount;
    } else {
        m_PomCount = 0;
    }
    // TODO: 0x0013a1d4 -- FruitSaveData::AddToTotal("PomTabIndex", hash, m_PomCount+1, true, true)
}

// Binary @ 0x00139440
void FruitFactControl::ConnectPressed() {
    // Defunct: online-services -- no-op stub; binary @ 0x00139440
    // NetworkManager::ConnectGameCenter() / NetworkManager::LaunchDashboard() are no-op stubs.
    // Preserve call shape so the connect button's delegate is still bindable.
    (void)Mortar::NetworkManager::GetInstance();
}
