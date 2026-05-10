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

// TODO: 0x001399fc -- full 17-texture name list
// (panel backgrounds, fact backplate, combo-star, connect/download/leaderboard frames)
// Only the core two textures are loaded here; the rest are TODO stubs.
static Mortar::SmartPtr<Mortar::Texture> s_PanelTex;
static Mortar::SmartPtr<Mortar::Texture> s_FactBackplateTex;
// ... 15 more textures to be resolved from binary DAT strings at 0x001399fc

void FruitFactControl::LoadContent() {
    if (s_bLoaded) return;
    s_bLoaded = true;

    // TODO: 0x001399fc -- load full 17-texture set; names from binary DAT constants.
    // Prototype: load panel + fact backplates.
    s_PanelTex        = TextureManager::LoadLocalisedTexture("fact_board.tex");
    s_FactBackplateTex = TextureManager::LoadLocalisedTexture("diolog_box_big.tex");
}

void FruitFactControl::UnLoadContent() {
    if (!s_bLoaded) return;
    s_bLoaded = false;

    s_PanelTex.SetNull();
    s_FactBackplateTex.SetNull();
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
    memset(_pad_8C_gap, 0, sizeof(_pad_8C_gap));
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

    // TODO: 0x0013a278 -- pick panel-bg texture by gameMode; set size+pos pivot from tex dimensions
    // TODO: 0x0013a278 -- m_PomCount set per gameMode / combo length
    // TODO: 0x0013a278 -- branch combo vs no-combo for status string
    // TODO: 0x0013a278 -- copy m_ComboHashArray + m_ComboLength from FruitSaveData
    // TODO: 0x0013a278 -- call CheckCombo / GetComboStarTexture
    // TODO: 0x0013a278 -- call Fruit::GetFact + reload fact texture

    m_FactColour.a = 0xFF;

    // Set FruitIdx from FindMostOfFruit result stored in Game/FruitSaveData
    if (game && game->pSaveData) {
        // Binary reads from game+0x118 (which mirrors GameOverScreen::field_0x118 = most-fruit-idx)
        // TODO: 0x0013a278 -- read most-fruit-idx from the correct game/save offset
        m_FruitIdx = 0;  // placeholder
    }

    // Load fact string for current fruit/fact index
    if (m_FruitIdx >= 0) {
        int outType = 0, outFactIdx = 0;
        m_pCurFactString = Fruit::GetFact(&outType, &outFactIdx, m_FruitIdx, m_FactIdx);
        m_FactIdx = outFactIdx;
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
        m_Texture->Set();
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
        mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_Texture->UnSet();
    }

    // ---- 2. "FRUIT FACT" title (binary @ 0x0013c852..0x0013c92c) ----
    {
        // TODO: 0x0013c852 -- resolve GETSTRING(0x9b) key string (likely
        //   "CODE_FACTS_TITLE" or similar). Literal until that's RE'd.
        const char* title = "FRUIT FACT";
        const float titleX = pos.x - 66.0f;
        const float titleY = pos.y;
        game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
            title, Vec3(titleX, titleY, 0.0f),
            m_FactColour, 0x0F);
    }

    // ---- 3. Fact body text (binary @ 0x0013c930..0x0013ca36) ----
    if (m_pCurFactString) {
        const Colour brown(0x74, 0x5D, 0x3B, 0xFF);
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
