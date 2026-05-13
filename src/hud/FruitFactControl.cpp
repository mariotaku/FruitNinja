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
#include "engine/audio/GameSound.h"
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

    // Non-combo path: store localised mode string
    // TODO: 0x0013a278 -- global g_factModeBuf not yet declared port-side;
    //   binary does strcpy(g_factModeBuf, Localisation::Get(0xB1))

    // Combo path (gameMode==3 with comboFlag OR gameMode==2 with len>=3):
    // TODO: 0x0013a278 -- CheckCombo / GetComboStarTexture not yet ported;
    //   copy m_ComboHashArray from saveData[+0x20C + i*4], call CheckCombo
    //   and GetComboStarTexture, assign texture to m_ComboStarTex.
    (void)comboFlag;

    // Always: GetFact with current fruit/fact indices
    m_pCurFactString = Fruit::GetFact(&m_FruitIdx, &m_FactIdx, m_FruitIdx, m_FactIdx);

    // Always: colour from fruit
    if (m_FruitIdx >= 0) {
        m_FactColour = Fruit::FruitFactColour(m_FruitIdx);
    }

    // Always: load per-fruit <name>_facts.tex into m_FactTexture
    if (m_FruitIdx >= 0) {
        const char* fruitBase = Fruit::FruitFactTexture(m_FruitIdx);
        if (fruitBase && *fruitBase) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s_facts.tex", fruitBase);
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
                // SFX floor name
                // TODO: 0x0013b604 -- SFX play floor name via GameSound::SFXPlay
            }
        } else {
            // Path B (per-second sin-pulse): StarTimer += 4*dt, 0.5-fractional gate
            m_StarTimer += 4.0f * dt;
            if (m_StarTimer - (float)(int)m_StarTimer >= 0.5f) {
                // SFX "Clean-Slice-%d" with idx in [1..8]
                // TODO: 0x0013b604 -- SFX "Clean-Slice-N" (N in 1..8) via GameSound::SFXPlay
            }
        }
    } else if (gameMode == Mortar::GAME_MODE_ARCADE) {
        // Arcade mode: lazy-create leaderboard menu ONCE
        // TODO: 0x0013b604 -- lazy-create m_pLeaderboardMenu (LeaderboardList) at
        //   position (75, pos.y-8, 0), dims 240x141, item height 47;
        //   hide leaderboard + connect button by default.
        //   LeaderboardList not yet ported.

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
    // Defunct: online-services -- no-op; binary @ 0x0013afbc (5-state machine).
    (void)dt;
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
        // Zen combo-bead overlay (HUD layer 0x08)
        if (game->gameMode != Mortar::GAME_MODE_ZEN) return;
        // TODO: 0x0013b95c -- draw backplate + combo-star with sin pulse (Zen layer-8 path)
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
            const char* title = Localisation::Get("CODE_FRUIT_FACT_TITLE");
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

            // Three bonus rows via BonusManager
            // TODO: 0x0013b95c -- BonusManager::GetFirstBestBonus / GetNextBestBonus
            //   row walk not yet fully ported (BonusScreen draw details unresolved).
            //   Row colours: gold(0xAD,0x7E,0x00,0xFF), red(0xA0,0x05,0x05,0xFF), blue(0x01,0x5C,0x95,0xFF)
            //   Row name at (Bonus+0x80) scale 16, align 0xF, maxWH=(193,0)
            //   Row score OS_SPrintf("%d", Bonus+0x3C) scale 16
            //   Row icon (Bonus+0xD0) scaled by icon W/2; Y step -= 20 per row

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
            const char* title = Localisation::Get("CODE_FRUIT_FACT_TITLE");
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
    // TODO: 0x001395d0 -- 8-segment spinning ring (48 verts); Mesh port required.
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
    snprintf(buf, sizeof(buf), "%s_facts", base);
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
    snprintf(buf, sizeof(buf), "%s_facts", base);
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
