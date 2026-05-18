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
#include "game/LeaderboardManager.h"
#include "engine/audio/GameSound.h"
#include "engine/math/Colour.h"
#include "engine/util/Delegate.h"
#include "engine/render/Font.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/Renderer.h"
#include "engine/math/Matrix44.h"
#include "engine/math/MathUtil.h"
#include "engine/util/StringTable.h"
#include <cstring>
#include <cstdio>
#include <list>

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// File-scope stubs for binary helpers not yet ported.
// ---------------------------------------------------------------------------

// CheckCombo -- binary @ unknown (called from Init combo path).
// Walks the hash array for a matching combo pattern; returns combo type byte.
// Stub: always returns 0 (no combo match); outFruitIdx unchanged.
static uint8_t CheckCombo(int* /*hashes*/, int /*count*/, int* /*outFruitIdx*/) {
    return 0;
}

// GetComboStarTexture -- binary @ unknown (called from Init combo path).
// Returns the star-burst texture for the given combo type.
// Stub: returns empty SmartPtr (no texture available).
static Mortar::SmartPtr<Mortar::Texture> GetComboStarTexture(uint8_t /*comboType*/) {
    return Mortar::SmartPtr<Mortar::Texture>();
}

// ---------------------------------------------------------------------------
// Static (class-level) content -- 17 shared textures.
// Binary @ 0x001399fc (LoadContent), 0x00139f84 (UnLoadContent).
// ---------------------------------------------------------------------------

static bool s_bLoaded = false;

// Slot order matches binary @ 0x001399fc
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexClassic;      // slot1  "fact_board.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexZen;           // slot2  "diolog_box_big.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ComboDescTex;          // slot3  "combo_description.tex"
static Mortar::SmartPtr<Mortar::Texture> s_SenseiHeadTex;         // slot4  "sensei_head.tex"  -- used as LoadContent proxy in BeginDraw
static Mortar::SmartPtr<Mortar::Texture> s_BlurryBackingTex;      // slot5  "blurry_backing.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeArrowTex;        // slot6  "arcade_results_arrow.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeScoreBoxTex;     // slot7  "arcade_results_score_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_PanelTexArcade;        // slot8  "arcade_results_diolog_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_ArcadeBonusBoxTex;     // slot9  "arcade_results_bonus_box.tex"
static Mortar::SmartPtr<Mortar::Texture> s_OFTitleTex;            // slot10 "op_title.tex"           -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_OFConnectTex;          // slot11 "op_connect_button.tex"  -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_OFAddFriendsTex;       // slot12 "op_add_friends_button.tex" -- Defunct: OpenFeint
static Mortar::SmartPtr<Mortar::Texture> s_GCTitleTex;            // slot13 "gc_title.tex"           -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_GCConnectTex;          // slot14 "gc_connect_button.tex"  -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_GCNoScoreTex;          // slot15 "gc_no_score_this_week.tex" -- Defunct: GameCenter
static Mortar::SmartPtr<Mortar::Texture> s_ScoreYouTex;           // slot16 "score_you.tex"
static Mortar::SmartPtr<Mortar::Texture> s_NoScoreThisWeekTex;    // slot17 "no_score_this_week.tex"

void FruitFactControl::LoadContent() {
    if (s_bLoaded) return;
    s_bLoaded = true;

    s_PanelTexClassic   = TextureManager::LoadLocalisedTexture("fact_board.tex");
    s_PanelTexZen       = TextureManager::LoadLocalisedTexture("diolog_box_big.tex");
    s_ComboDescTex      = TextureManager::LoadLocalisedTexture("combo_description.tex");
    s_SenseiHeadTex     = TextureManager::LoadLocalisedTexture("sensei_head.tex");
    s_BlurryBackingTex  = TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_ArcadeArrowTex    = TextureManager::LoadLocalisedTexture("arcade_results_arrow.tex");
    s_ArcadeScoreBoxTex = TextureManager::LoadLocalisedTexture("arcade_results_score_box.tex");
    s_PanelTexArcade    = TextureManager::LoadLocalisedTexture("arcade_results_diolog_box.tex");
    s_ArcadeBonusBoxTex = TextureManager::LoadLocalisedTexture("arcade_results_bonus_box.tex");
    s_OFTitleTex        = TextureManager::LoadLocalisedTexture("op_title.tex");
    s_OFConnectTex      = TextureManager::LoadLocalisedTexture("op_connect_button.tex");
    s_OFAddFriendsTex   = TextureManager::LoadLocalisedTexture("op_add_friends_button.tex");
    s_GCTitleTex        = TextureManager::LoadLocalisedTexture("gc_title.tex");
    s_GCConnectTex      = TextureManager::LoadLocalisedTexture("gc_connect_button.tex");
    s_GCNoScoreTex      = TextureManager::LoadLocalisedTexture("gc_no_score_this_week.tex");
    s_ScoreYouTex       = TextureManager::LoadLocalisedTexture("score_you.tex");
    s_NoScoreThisWeekTex= TextureManager::LoadLocalisedTexture("no_score_this_week.tex");
}

void FruitFactControl::UnLoadContent() {
    if (!s_bLoaded) return;
    s_bLoaded = false;

    s_PanelTexClassic.SetNull();
    s_PanelTexZen.SetNull();
    s_ComboDescTex.SetNull();
    s_SenseiHeadTex.SetNull();
    s_BlurryBackingTex.SetNull();
    s_ArcadeArrowTex.SetNull();
    s_ArcadeScoreBoxTex.SetNull();
    s_PanelTexArcade.SetNull();
    s_ArcadeBonusBoxTex.SetNull();
    s_OFTitleTex.SetNull();
    s_OFConnectTex.SetNull();
    s_OFAddFriendsTex.SetNull();
    s_GCTitleTex.SetNull();
    s_GCConnectTex.SetNull();
    s_GCNoScoreTex.SetNull();
    s_ScoreYouTex.SetNull();
    s_NoScoreThisWeekTex.SetNull();
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
    , m_StarTimer(0.0f)
    , m_bConnectPressed(0)
    , m_ComboStarTex()
    , m_ComboType(-1)
    , m_PomCount(0)
    , m_pLeaderboardMenu(nullptr)
    , m_pConnectButton(nullptr)
    , m_LBVisitedCount(0)
    , m_LBProgressTimer(0.0f)
    , m_LBState(1)
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

    // Binary: IsProviderOnline() -> m_LBState (1 = always offline branch)
    // Defunct: always offline; m_LBState = 1 set in initializer list above

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
    m_StarTimer = -0.5f;
    m_ComboStarTex.SetNull();

    uint8_t savedPomCount = m_PomCount;
    m_PomCount = 0;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game->gameMode : 0;

    // Per-mode backplate stored in m_SecondaryTex (binary @ 0x0013a2a6..0x0013a2ba)
    if (gameMode == 2) {
        if (s_PanelTexArcade.IsValid()) m_SecondaryTex = s_PanelTexArcade;
    } else if (gameMode == 3) {
        if (s_PanelTexZen.IsValid()) m_SecondaryTex = s_PanelTexZen;
    } else {
        if (s_PanelTexClassic.IsValid()) m_SecondaryTex = s_PanelTexClassic;
    }
    if (m_SecondaryTex.IsValid()) {
        size.x = (float)(m_SecondaryTex->m_Width + 1);
        size.y = (float)(m_SecondaryTex->m_Height + 1);
        size.z = 0.0f;
        // ASM-verified: 2026-05-11 binary @ 0x0013a31e (re-analyst)
        // 1.37f scale (DAT_0013a500) applies only in Classic (gameMode == 0).
        if (gameMode == 0) {
            size.x *= 1.37f;
            size.y *= 1.37f;
            size.z *= 1.37f;
        }
    }

    m_FactColour.a = 0xFF;

    // Combo flag: gameMode==3 and saveData[+0x208] >= 3
    int comboFlag = 0;
    if (game && game->pSaveData) {
        if (gameMode == 3 && game->pSaveData->m_BombQueueCount >= 3) {
            comboFlag = 1;
        }
    }
    // Per-mode m_FactPosOffset: non-combo default (-69, 53, 0)
    m_FactPosOffset = Vec3(-69.0f, 53.0f, 0.0f);

    if (comboFlag) {
        // Combo path (binary @ 0x0013a278, comboFlag != 0 branch):
        // saveData->m_BombQueueCount holds m_ComboLength; m_BombQueue holds hashes.
        // Binary: snprintf(buf, "%s", Mortar::GETSTRING_CAST_0(LSTR_BEST_COMBO))
        // where LSTR_BEST_COMBO = 0x98 = "BEST COMBO: %i FRUIT!". The single
        // BakedString slot gets the formatted-with-count string. Port renders
        // it via the Font::DrawString path -- the BakedString optimisation
        // is unported (it caches the rendered glyph quads).
        char comboBuf[128];
        snprintf(comboBuf, sizeof(comboBuf),
                 Mortar::GETSTRING_CAST_0(LSTR_BEST_COMBO),
                 game->pSaveData->m_BombQueueCount);
        m_ComboLength = game->pSaveData->m_BombQueueCount;
        for (int i = 0; i < m_ComboLength && i < 11; i++) {
            m_ComboHashArray[i] = game->pSaveData->m_BombQueue[i];
        }
        int localFruitIdx = 0;
        m_ComboType = (int)CheckCombo(m_ComboHashArray, m_ComboLength, &localFruitIdx);
        m_ComboStarTex = GetComboStarTexture((uint8_t)m_ComboType);
        if (m_FruitIdx != localFruitIdx) m_FruitIdx = localFruitIdx;
        m_FactPosOffset = Vec3(140.0f, -72.0f, 0.0f);
    }

    // Always: GetFact with current fruit/fact indices
    m_pCurFactString = Fruit::GetFact(&m_FruitIdx, &m_FactIdx, m_FruitIdx, m_FactIdx);

    // Always: colour from fruit
    if (m_FruitIdx >= 0) {
        m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    }

    // ASM-verified: 2026-05-14 binary @ 0x0013a278..0x0013a4f6 (asm-inspector)
    //   GOT base = 0x001ec130 (ldr r5,[pc,#0x000b1eb0]; adds r5,r5,r3)
    //   GOT off  = 0xfffcdf83 (ldr r2,[pc,...])  -> r2 = 0x001ba0b3
    //   bytes @ 0x001ba0b3 = 25 73 2E 74 65 78 00 = "%s.tex"
    //   blx 0x001032b4 (OS_SPrintf)
    // Port previously had "%s_facts.tex" which produced filenames that
    // don't exist (sml_ap_facts.tex). FruitFactTexture returns the XML
    // factTexture attr directly (e.g. "sml_ap" for apple) which resolves
    // to the small per-fruit icon in FruitNinjaBada/Data/textures/.
    if (m_FruitIdx >= 0) {
        const char* fruitBase = Fruit::FruitFactTexture(m_FruitIdx);
        if (fruitBase && *fruitBase) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.tex", fruitBase);
            m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
        }
    }

    m_LBVisitedCount = 0;
    m_bConnectPressed = 0;
    m_LBProgressTimer = 0.0f;

    m_PomCount = savedPomCount;
    m_LayerFlags = 0x80;

    Reset();
}

// ---------------------------------------------------------------------------
// Release  (Binary @ 0x00139d24)
// ---------------------------------------------------------------------------

void FruitFactControl::Release() {
    m_FactTexture.SetNull();
    m_ComboStarTex.SetNull();

    Game* game = Game::GetInstance();
    HUD* hud = game ? game->hud : nullptr;

    // Order from binary: m_pLeaderboardMenu, m_pLeftButton, m_pRightButton, m_pConnectButton.
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

    // Validity check uses STATIC s_SenseiHeadTex (slot #4 in LoadContent) as
    // a proxy for "did LoadContent run successfully".
    if (!s_SenseiHeadTex.IsValid()) return;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game->gameMode : 0;

    if (gameMode == Mortar::GAME_MODE_ZEN || (gameMode == Mortar::GAME_MODE_ARCADE && m_PomCount <= 1)) {
        m_LayerFlags |= Mortar::HUD_LAYER_BUTTONS;
    }
}

// ---------------------------------------------------------------------------
// Update  (Binary @ 0x0013b604)
// ---------------------------------------------------------------------------

void FruitFactControl::Update(float dt) {
    m_AnimTimer += dt * 8.0f;
    if (m_AnimTimer >= 8.0f) m_AnimTimer = 0.0f;

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    Game* game = Game::GetInstance();
    if (!game) return;

    uint8_t gameMode = game->gameMode;

    if (gameMode == Mortar::GAME_MODE_ZEN) {
        // SecondaryTex = combo-star tex
        m_SecondaryTex = m_ComboStarTex;

        // Gate on m_TransitionTimer (game[+0xC]) <= 0.75 OR m_StarTimer >= m_ComboLength
        if (game->m_TransitionTimer <= 0.75f || m_StarTimer >= (float)m_ComboLength) {
            // Path A (faster combo cadence): StarTimer += 2*dt, 0.5-fractional gate
            m_StarTimer += 2.0f * dt;
            if (m_StarTimer - (float)(int)m_StarTimer >= 0.5f) {
                if (game->pGameSound) {
                    game->pGameSound->SFXPlay("Clean-Slice-1", 1.0f, 1.0f,
                        Mortar::Delegate1<bool, Mortar::MortarSound*>());
                }
            }
        } else {
            // Path B (per-second sin-pulse): StarTimer += 4*dt, 0.5-fractional gate
            m_StarTimer += 4.0f * dt;
            if (m_StarTimer - (float)(int)m_StarTimer >= 0.5f) {
                int n = (int)m_StarTimer;
                int idx = (n < 7) ? (n + 1) : 8;
                char sfx[16];
                snprintf(sfx, sizeof(sfx), "Clean-Slice-%d", idx);
                if (game->pGameSound) {
                    game->pGameSound->SFXPlay(sfx, 1.0f, 1.0f,
                        Mortar::Delegate1<bool, Mortar::MortarSound*>());
                }
            }
        }
    } else if (gameMode == Mortar::GAME_MODE_ARCADE) {
        // Arcade mode: lazy-create leaderboard menu ONCE (binary @ 0x0013b604)
        if (!m_pLeaderboardMenu) {
            m_pLeaderboardMenu = new LeaderboardList();
            m_pLeaderboardMenu->Init();
            m_pLeaderboardMenu->pos = Vec3(75.0f, pos.y - 8.0f, 0.0f);
            // Defunct: online-services -- SetItemHeight/SetWidth/SetHeight preserved for call shape.
            m_pLeaderboardMenu->SetItemHeight(47.0f);
            m_pLeaderboardMenu->SetWidth(240.0f);
            m_pLeaderboardMenu->SetHeight(141.0f);
            if (game->hud) game->hud->AddControl(m_pLeaderboardMenu, false);
        }
        if (m_pLeaderboardMenu) m_pLeaderboardMenu->m_bActive = 0;
        if (m_pConnectButton) m_pConnectButton->m_bActive = 0;

        if (m_PomCount == 1) {
            UpdateLeaderboard(dt);
            return;
        }
        if (m_PomCount == 0) {
            std::list<Bonus>::iterator it;
            BonusManager* bm = BonusManager::GetInstance();
            Bonus* bonus = bm->GetFirstBestBonus(it);
            if (!bonus) {
                bm->SetUpBonusScreen(nullptr);
            }
        }
    }
    // Classic: no per-frame dispatch
}

// ---------------------------------------------------------------------------
// UpdateLeaderboard  (Binary @ 0x0013afbc)
// ---------------------------------------------------------------------------

void FruitFactControl::UpdateLeaderboard(float dt) {
    // Binary @ 0x0013afbc -- 5-state machine for online leaderboard flow.
    // All branches are defunct (online services are not ported).
    Game* game = Game::GetInstance();
    if (!game) return;

    int provider = Mortar::GetSocialNetworkProvider();
    (void)provider;

    switch (m_LBState) {
        case 0:
            // Defunct: online-services -- offline-prompt: spawn connect button bound to ConnectPressed.
            // binary @ 0x0013afbc: creates m_pConnectButton at (pos.x-8, pos.y-8, 0),
            // shows provider title texture, shows connect button.
            // no observable effect; binary @ 0x0013afbc
            break;

        case 1:
            // Defunct: online-services -- initiating connection: NetworkManager::ConnectGameCenter().
            // binary @ 0x0013b0e4: calls ConnectGameCenter(), polls IsGameCenterAttemptingToConnect(),
            // transitions to state 2 or state 4 on timeout.
            // no observable effect; binary @ 0x0013b0e4
            (void)dt;
            break;

        case 2:
            // Defunct: online-services -- fetching: LeaderboardManager::RefreshLeaderboard() poll.
            // binary @ 0x0013b1e4: calls RefreshLeaderboard(gameMode, boardId),
            // draws download spinner via DrawDownloadIcon(), populates leaderboard rows.
            // no observable effect; binary @ 0x0013b1e4
            LeaderboardManager::GetInstance()->RefreshLeaderboard(0, 0);
            DrawDownloadIcon();
            if (m_pLeaderboardMenu) {
                // Defunct: online-services -- no rows to populate; list stays empty.
                // binary @ 0x0013b244: iterates FNHighscoreList rows into LeaderboardList items.
            }
            break;

        case 3:
            // Defunct: online-services -- showing: make leaderboard visible.
            // binary @ 0x0013b39c: sets m_pLeaderboardMenu->m_bActive = 1.
            // no observable effect; binary @ 0x0013b39c
            if (m_pLeaderboardMenu) m_pLeaderboardMenu->m_bActive = 1;
            break;

        case 4:
            // Defunct: online-services -- error/offline fallback.
            // binary @ 0x0013b418: shows no-score texture, hides leaderboard.
            // no observable effect; binary @ 0x0013b418
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

    Game* game = Game::GetInstance();
    if (!game || !game->pFontMain.IsValid()) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    if (layerMask == 8) {
        // Zen combo-bead overlay (HUD layer 0x08, binary @ 0x0013b95c)
        if (game->gameMode != Mortar::GAME_MODE_ZEN) return;
        if (!m_ComboStarTex.IsValid()) return;
        if (m_StarTimer <= (float)m_ComboLength) return;

        // Normalise elapsed post-combo time to [0..1]
        float t = (m_StarTimer - (float)m_ComboLength) * 2.0f - 1.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        // Sin-pulse scale: SinIdx maps [0..65535] -> full sin period.
        uint16_t idx = (uint16_t)(int)(t * 135.0f * 182.0f);
        float pulse = SinIdx(idx) * 1.41421356f;

        // Per-bead stride along the panel
        float stride = ((float)(m_ComboLength - 1)) * 40.0f;
        if (m_ComboLength > 1 && stride > 220.0f) {
            stride = 220.0f / (float)(m_ComboLength - 1);
        }

        // Draw the combo-star texture quad with sin-pulse scale
        // ASM-verified: 2026-05-18 binary @ 0x0013a06c (re-analyst)
        const float tw = (float)m_ComboStarTex->m_Width;
        const float th = (float)m_ComboStarTex->m_Height;
        m_ComboStarTex->Set();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale((tw + 1.0f) * pulse, (th + 1.0f) * pulse, 0.0f);
        mat.GlobalTranslate44(Vec3(pos.x + stride * 0.5f, pos.y, pos.z));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        r->DrawQuad(Colour(255, 255, 255, 255));
        m_ComboStarTex->UnSet();
        return;
    }

    if (game->gameMode == Mortar::GAME_MODE_ZEN) {
        // ---- Zen body ----
        // Backplate from m_SecondaryTex (set to combo-star or PanelTexZen in Init/Update)
        if (m_SecondaryTex.IsValid()) {
            m_SecondaryTex->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_SecondaryTex->UnSet();
        }

        // Title: pos.x - 8.0
        {
            const char* title = Mortar::GETSTRING_CAST_0(LSTR_FRUIT_FACT_TITLE);
            if (!title) title = "FRUIT FACT";
            const float titleX = pos.x - 8.0f;
            const float titleY = pos.y;
            // Triple-pass: shadow + dark-brown stroke at scale 20 + main colour at scale 20
            // Shadow pass
            game->pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                Colour(0, 0, 0, 128), 0x0F);
            // Dark-brown stroke
            game->pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                Colour(0x4B, 0x32, 0x28, 200), 0x0F);
            // Main colour
            game->pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                title, Vec3(titleX + 1.0f, titleY, 0.0f),
                m_FactColour, 0x0F);
        }

        // Fact body: wrap 127/89, step 0.25
        if (m_pCurFactString) {
            float scale = 16.0f;
            {
                Mortar::Utf8StringIterator iter(m_pCurFactString);
                while (game->pFontMain->GetStringHeight(iter, scale, 127.0f) > 89.0f) {
                    if (scale <= 0.5f) break;
                    scale -= 0.25f;
                    iter = Mortar::Utf8StringIterator(m_pCurFactString);
                }
            }
            game->pFontMain->DrawStringWrapped(scale, 127.0f, 0.0f,
                m_pCurFactString, Vec3(pos.x - 8.0f, pos.y, 0.0f),
                m_FactColour, 0x0D);
        }

        // Fact icon: does NOT use m_FactPosOffset
        if (m_FactTexture.IsValid()) {
            const float w = (float)m_FactTexture->m_Width;
            const float h = (float)m_FactTexture->m_Height;
            m_FactTexture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(w, h, 0.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 8.0f, pos.y + 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_FactTexture->UnSet();
        }

    } else if (game->gameMode == Mortar::GAME_MODE_ARCADE) {
        // ---- Arcade body ----
        if (m_PomCount == 1) {
            DrawLeaderboard();
            return;
        }

        if (m_PomCount == 0) {
            // Backplate from m_FactTexture (per-mode secondary backplate)
            if (m_FactTexture.IsValid()) {
                m_FactTexture->Set();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
                mat.GlobalTranslate44(Vec3(pos.x - 8.0f, pos.y + 8.0f, pos.z));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                r->DrawQuad(Colour(255, 255, 255, 255));
                m_FactTexture->UnSet();
            }

            // Three bonus rows via BonusManager (binary @ 0x0013b95c, m_PomCount==0 path)
            {
                const Colour rowColours[3] = {
                    Colour(0xAD, 0x7E, 0x00, 0xFF),  // gold  (1st)
                    Colour(0xA0, 0x05, 0x05, 0xFF),  // red   (2nd)
                    Colour(0x01, 0x5C, 0x95, 0xFF),  // blue  (3rd)
                };
                Vec3 rowPos(pos.x - 98.0f, pos.y + 58.0f, 0.0f);
                BonusManager* bm = BonusManager::GetInstance();
                std::list<Bonus>::iterator it;
                Bonus* bonus = bm->GetFirstBestBonus(it);
                for (int i = 0; i < 3 && bonus; i++) {
                    Colour col = rowColours[i];
                    // m_DisplayName at +0x80 (char[64])
                    if (bonus->m_DisplayName[0] != '\0') {
                        game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                            bonus->m_DisplayName,
                            Vec3(rowPos.x + 16.0f, rowPos.y, 0.0f),
                            col, 0x0F);
                    }
                    // m_Tier at +0x3C holds display score value
                    char scoreBuf[32];
                    snprintf(scoreBuf, sizeof(scoreBuf), "%d", bonus->m_Tier);
                    game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                        scoreBuf,
                        Vec3(rowPos.x + 193.0f, rowPos.y, 0.0f),
                        col, 0x0F);
                    if (bonus->m_StarTexture.IsValid()) {
                        // ASM-verified: 2026-05-18 binary @ 0x0013a06c (re-analyst)
                        bonus->m_StarTexture->Set();
                        mm.GetWorldStack().Reset();
                        Matrix44 starMat = Matrix44::MakeScale(
                            (float)bonus->m_StarTexture->m_Width * 0.5f,
                            (float)bonus->m_StarTexture->m_Height * 0.5f,
                            0.0f);
                        starMat.GlobalTranslate44(rowPos);
                        mm.GetWorldStack().SetCurrentMatrix(starMat);
                        mm.UploadModelViewOnly();
                        r->DrawQuad(Colour(255, 255, 255, 255));
                        bonus->m_StarTexture->UnSet();
                    }
                    rowPos.y -= 20.0f;
                    bonus = bm->GetNextBestBonus(it);
                }
            }

            // Title body (Zen-equivalent structure, 127/89/0.25)
            if (m_pCurFactString) {
                float scale = 16.0f;
                {
                    Mortar::Utf8StringIterator iter(m_pCurFactString);
                    while (game->pFontMain->GetStringHeight(iter, scale, 127.0f) > 89.0f) {
                        if (scale <= 0.5f) break;
                        scale -= 0.25f;
                        iter = Mortar::Utf8StringIterator(m_pCurFactString);
                    }
                }
                game->pFontMain->DrawStringWrapped(scale, 127.0f, 0.0f,
                    m_pCurFactString, Vec3(pos.x - 8.0f, pos.y, 0.0f),
                    m_FactColour, 0x0D);
            }
        }

    } else {
        // ---- Classic body ----

        // 1. Backplate quad
        // Offset from GOT[DAT_0013cb1c] + (-1, -8, 0); const unknown, treat as (0,0,0)
        if (m_SecondaryTex.IsValid()) {
            m_SecondaryTex->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(size.x, size.y, 1.0f);
            mat.GlobalTranslate44(Vec3(pos.x - 1.0f, pos.y - 8.0f, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            r->DrawQuad(Colour(255, 255, 255, 255));
            m_SecondaryTex->UnSet();
        }

        // 2. Title: dispatch on game->languageFlag (field_0x3)
        {
            const char* title = Mortar::GETSTRING_CAST_0(LSTR_FRUIT_FACT_TITLE);
            if (!title) title = "FRUIT FACT";

            if (!game->languageFlag) {
                // clear branch: pos.x - 66, pos.y + 42, maxWH=(42, 0), scale 16, align 0xF
                game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                    title, Vec3(pos.x - 66.0f, pos.y + 42.0f, 0.0f),
                    m_FactColour, 0x0F);
            } else {
                // set branch: pos.x - 64, maxWH=(128, 0), scale 16, align 0xF
                game->pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                    title, Vec3(pos.x - 64.0f, pos.y + 42.0f, 0.0f),
                    m_FactColour, 0x0F);
            }
        }

        // 3. Fact body: wrap 128/96, step 0.125
        if (m_pCurFactString) {
            const Colour brown(0x74, 0x5D, 0x3B, 0xFF);
            const float bodyX = pos.x - 64.0f;
            const float bodyY = pos.y - 14.0f;
            float scale = 16.0f;
            {
                Mortar::Utf8StringIterator iter(m_pCurFactString);
                while (game->pFontMain->GetStringHeight(iter, scale, 128.0f) > 96.0f) {
                    if (scale <= 0.5f) break;
                    scale -= 0.125f;
                    iter = Mortar::Utf8StringIterator(m_pCurFactString);
                }
            }
            game->pFontMain->DrawStringWrapped(scale, 128.0f, 0.0f,
                m_pCurFactString, Vec3(bodyX, bodyY, 0.0f),
                brown, 0x0D);
        }

        // 4. Per-fruit fact icon with m_FactPosOffset
        if (m_FactTexture.IsValid()) {
            const float w = (float)m_FactTexture->m_Width;
            const float h = (float)m_FactTexture->m_Height;
            m_FactTexture->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(w, h, 0.0f);
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
}

// ---------------------------------------------------------------------------
// DrawLeaderboard  (Binary @ 0x0013aac0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawLeaderboard() {
    // Defunct: online-services -- no-op stub; binary @ 0x0013aac0
}

// ---------------------------------------------------------------------------
// DrawDownloadIcon  (Binary @ 0x001395d0)
// ---------------------------------------------------------------------------

void FruitFactControl::DrawDownloadIcon() {
    // Defunct: online-services -- no-op stub; binary @ 0x001395d0
    // Binary @ 0x001395d0 builds an 8-segment ring with per-segment alpha
    // animation (48 verts, Mesh::DrawTriList). The only caller is
    // UpdateLeaderboard state 2 which is itself defunct.
    // Mesh::DrawTriList is not yet wired in port; ring draw omitted.
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

// Binary @ 0x001394ec
bool FruitFactControl::LeftPressed(InputEvent* /*ev*/) {
    --m_FactIdx;
    if (m_FactIdx < 0) m_FactIdx = Fruit::FruitInfo(m_FruitIdx)->m_FactCount - 1;
    m_pCurFactString = Fruit::GetFact(nullptr, nullptr, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x001394b0
bool FruitFactControl::RightPressed(InputEvent* /*ev*/) {
    ++m_FactIdx;
    if (m_FactIdx >= Fruit::FruitInfo(m_FruitIdx)->m_FactCount) m_FactIdx = 0;
    m_pCurFactString = Fruit::GetFact(nullptr, nullptr, m_FruitIdx, m_FactIdx);
    return true;
}

// Binary @ 0x0013993c
bool FruitFactControl::UpPressed(InputEvent* /*ev*/) {
    do {
        ++m_FruitIdx;
        if (m_FruitIdx >= FruitInfo_GetCount()) m_FruitIdx = 0;
    } while (Fruit::FruitInfo(m_FruitIdx)->m_FactCount < 1);
    m_pCurFactString = Fruit::GetFact(nullptr, &m_FactIdx, m_FruitIdx, -1);
    m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    char buf[128];
    const char* base = Fruit::FruitFactTexture(m_FruitIdx);
    snprintf(buf, sizeof(buf), "%s.tex", base);  // binary format DAT_001399f8 = "%s.tex"
    m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
    return true;
}

// Binary @ 0x0013987c
bool FruitFactControl::DownPressed(InputEvent* /*ev*/) {
    do {
        --m_FruitIdx;
        if (m_FruitIdx < 0) m_FruitIdx = FruitInfo_GetCount() - 1;
    } while (Fruit::FruitInfo(m_FruitIdx)->m_FactCount < 1);
    m_pCurFactString = Fruit::GetFact(nullptr, &m_FactIdx, m_FruitIdx, -1);
    m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    char buf[128];
    const char* base = Fruit::FruitFactTexture(m_FruitIdx);
    snprintf(buf, sizeof(buf), "%s.tex", base);  // binary format DAT_001399f8 = "%s.tex"
    m_FactTexture = TextureManager::LoadLocalisedTexture(buf);
    return true;
}

// ---------------------------------------------------------------------------
// Button callbacks
// ---------------------------------------------------------------------------

// Binary @ 0x0013a130
void FruitFactControl::LeftButton() {
    Game* g = Game::GetInstance();
    if (g && g->pGameSound) {
        g->pGameSound->SFXPlay("score_select_button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    --m_PomCount;
    if ((int)m_PomCount < 0) m_PomCount = 1;
    if (g && g->pSaveData) {
        g->pSaveData->SetTotal("PomTabIndex", (int)m_PomCount + 1, true, true);
    }
}

// Binary @ 0x0013a1d4
void FruitFactControl::RightButton() {
    Game* g = Game::GetInstance();
    if (g && g->pGameSound) {
        g->pGameSound->SFXPlay("score_select_button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    ++m_PomCount;
    if ((int)m_PomCount > 1) m_PomCount = 0;
    if (g && g->pSaveData) {
        g->pSaveData->SetTotal("PomTabIndex", (int)m_PomCount + 1, true, true);
    }
}

// Binary @ 0x00139440
void FruitFactControl::ConnectPressed() {
    // Defunct: online-services -- no-op stub; binary @ 0x00139440
    // NetworkManager::ConnectGameCenter() / NetworkManager::LaunchDashboard() are no-op stubs.
    (void)Mortar::NetworkManager::GetInstance();
}
