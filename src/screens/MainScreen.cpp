//
// MainScreen — reimplemented from docs/screens/main.md
// Original: ctor 0x0014c430 (159 lines), Update 0x0014b278 (677 lines),
//           Draw 0x0014d4ec (171 lines)
// v1.6.1 struct re-layout applied: m_StateTimer=bounce velocity, m_Timer2=transition timer,
//   m_BounceY=bounce position, m_LogoPos=fruit_text draw pos, etc.
//

#include "MainScreen.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "DojoScreen.h"
#include "GameModeScreen.h"
#include "entities/ActorManager.h"
#include "Game.h"
#include "entities/FruitInfo.h"
#include "entities/Bomb.h"
#include "entities/BombBlast.h"
#include "entities/Fruit.h"
#include "entities/Entity.h"
#include "game/BombHit.h"
#include "game/FruitCamera.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "math/Random.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "core/SystemManager.h"
#include "audio/GameSound.h"
#include "audio/SoundManager.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "engine/util/StringTable.h"
#include <cstdio>
#include <cmath>
#include "game/GameWork.h"

// Timing constants (verified from binary, see docs/screens/main.md)
static const float CAMERA_LERP_RATE    = 0.125f;
static const float CAMERA_THRESHOLD    = -0.999f;
static const float TIMER2_THRESHOLD    = 0.15f;
static const float STATE_0E_DECAY      = 0.85f;
static const float STATE_0E_THRESHOLD  = 0.25f;
static const float STATE_2_DECAY       = 0.75f;
static const float STATE_8_LERP_RATE     = 0.125f;
static const float STATE_8_LERP_THRESHOLD = 0.999f;   // DAT_0014bf28 = 0x3f7fbe77
static const float STATE_8_DURATION      = 1.5f;
static const float STATE_8_RESET_TIMER   = 0.15f;     // DAT_0014bf2c (was -0.85)
static const float BOUNCE_LOSS         = -0.25f;
static const float BOUNCE_SETTLE       = 3.0f;
static const float ALPHA_LERP_RATE     = 0.25f;
static const float PAUSE_VISIBILITY    = 0.01f;
static const float SOUND_VOLUME_ON     = 0.5f;

// Helper: get GLuint from Mortar::SmartPtr<Texture>
static GLuint TexId(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}

// Button positions (verified from read_memory, docs/screens/main.md)
static const Vec3 POS_PLAY_BUTTON(16.0f, -66.0f, 0.0f);
static const Vec3 POS_DOJO_BUTTON(-144.0f, -65.0f, 0.0f);
static const Vec3 POS_QUIT(182.0f, -106.0f, 0.0f);
static const Vec3 POS_MORE_GAMES(182.0f, -106.0f, 0.0f);
static const Vec3 POS_SOUND_TOGGLE(216.0f, 135.5f, 0.0f);
static const Vec3 POS_MUSIC_TOGGLE(176.0f, 135.5f, 0.0f);

void MainScreen::SetState(MainScreenState s) {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(s), "SetState");
    m_State = s;
    if (s == STATE_CAMERA_ZOOM) {
        // Port specific: m_pDojoScreen is a weak pointer to a child screen managed
        // by HUD. Its removal callback can race the state transition. Forcibly clear
        // it here so STATE_CAMERA_ZOOM doesn't keep a stale pointer.
        m_pDojoScreen = nullptr;
    }
}

// Matches ctor at 0x0014c430 (159 lines)
MainScreen::MainScreen(Game& g)
    : pPlayButton(nullptr), pDojoButton(nullptr),
      pLeaderboardBtn(nullptr), pMoreGamesBtn(nullptr),
      pToggleA(nullptr), pToggleB(nullptr),
      pMusicToggle(nullptr), pSoundToggle(nullptr),
      _pad_0xA8(0),
      m_Lean(1.0f),
      m_NinjaTextX(0.0f), m_NinjaTextY(0.0f), m_NinjaTextZ(0.0f),
      m_BounceVel(0.0f), m_BounceY(0.0f), m_field10C(0.0f),
      m_StateTimer(0.0f),
      m_Field114(0.0f),
      m_State(STATE_CAMERA_ZOOM),
      m_Timer2(0.0f),
      m_TimeRemainingDisplay(-1.0f),
      m_GlobalAlphaTarget(1.0f), m_Time(0.0f),
      m_bGameStartReset(false),
      m_pDojoScreen(nullptr),
      game(g),
      m_pSliceInstrBox(nullptr)
{
    // Load global textures (assigned to globals via GOT in original)
    m_blurryBackingTex = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    m_fruitTextTex     = Mortar::TextureManager::LoadLocalisedTexture("fruit_text.tex");
    m_ninjaTextTex     = Mortar::TextureManager::LoadLocalisedTexture("ninja_text.tex");

    // m_TexFruitText mirrors m_fruitTextTex (fruit_text.tex at binary +0xE4)
    m_TexFruitText = m_fruitTextTex;

    // Load slice_fruit parchment frame
    m_TexSliceFruit = Mortar::TextureManager::LoadLocalisedTexture("slice_fruit.tex");

    // v1.6.1: button textures come from game_work.pM_Textures[n] in CreateButtons.
    // The ctor no longer loads m_TexNewGame / m_TexDojoIcon / m_TexQuit etc. as struct members.
    // Load sound/music toggle textures into the binary-faithful slots.
    m_Tex3    = Mortar::TextureManager::LoadLocalisedTexture("sound.tex");
    m_Tex4    = Mortar::TextureManager::LoadLocalisedTexture("sound_cross.tex");
    m_Tex120  = Mortar::TextureManager::LoadLocalisedTexture("music.tex");
    // m_TexSoundOn/Off/MusicOn/Off also loaded into their own slots (binary-faithful)
    m_TexSoundOn  = Mortar::TextureManager::LoadLocalisedTexture("sound.tex");
    m_TexSoundOff = Mortar::TextureManager::LoadLocalisedTexture("sound_cross.tex");
    m_TexMusicOn  = Mortar::TextureManager::LoadLocalisedTexture("music.tex");
    m_TexMusicOff = Mortar::TextureManager::LoadLocalisedTexture("music_cross.tex");

    // m_TexBc (was m_TexCommingSoon)
    m_TexBc = Mortar::TextureManager::LoadLocalisedTexture("comming_soon.tex");

    // m_TexMoreGames loaded as a texture; .f0 is also read as a float countdown by some states.
    m_TexMoreGames = Mortar::TextureManager::LoadLocalisedTexture("more_games.tex");

    // Load verdana.fnt into m_pFont (+0x110 port, binary +0x128).
    {
        m_pFont = Mortar::Font::Create("fonts/verdana.fnt");
    }

    // v1.6.1: Load TTF font for the "SLICE FRUIT TO BEGIN" BakedStringBox.
    // Binary: FontCacheObjectTTF over "fontstruetype/gangofchinese.ttf" (256x256 atlas).
    // m_BakedStrSmart holds the SmartPtr to the font (binary +0xDC / port +0xC4).
    // m_pSliceInstrBox holds the BakedStringBox* (binary +0xE0 / port +0xC8).
    // TODO: if the port later adds Arabic language support, swap to "fontstruetype/arabic.ttf".
    {
        m_BakedStrSmart = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    }
    {
        Mortar::FontCacheObjectTTF* ttf = nullptr;
        if (m_BakedStrSmart.IsValid()) {
            ttf = Mortar::FontTTFRegistry::GetInstance().Lookup(m_BakedStrSmart.Get());
        }
        if (ttf) {
            m_pSliceInstrBox = new Mortar::BakedStringBox(
                ttf,
                9.0f,   // fontSize
                75.0f,  // width
                30.0f,  // height
                0x0d,   // align: centre-H(0x01) | centre-V(0x04) | fit(0x08)
                3,      // maxLines
                3.0f    // lineSpacing
            );
            const char* sliceText = Mortar::GETSTRING_CAST_0(LSTR_MENU_TEXTURE_13);
            m_pSliceInstrBox->SetText(sliceText ? sliceText : "SLICE FRUIT TO BEGIN");
            // Colour: binary reads game_work.m_RingColours[14] (+0x6a0) = RGB(0x6F,0x46,0x1E).
            m_pSliceInstrBox->SetColour(game_work.m_RingColours[14], /*setBase*/0);
            m_pSliceInstrBox->SetHorizontalLineSpacing(-1);
            m_pSliceInstrBox->FitIntoVerticalBounds();
        }
    }

    // Set size = (480.0, 138.0, 1.0)
    size = Vec3(480.0f, 138.0f, 1.0f);

    // Set position = (0.0, (320.0 - size_y) * 0.5, 0.0) = (0.0, 91.0, 0.0)
    pos = Vec3(0.0f, (320.0f - size.y) * 0.5f, 0.0f);

    // m_BounceY = ninja_text.tex height / 2 + 160.0
    // Binary ctor @ 0x0014c430: calls ninja_text_tex->GetHeight() (vtable +0x18)
    // -> shifts right by 1 -> adds 160.0.
    // m_BounceY is the bounce POSITION; starts at top of screen and falls into place.
    const float ninjaH = m_ninjaTextTex.IsValid()
                       ? (float)(m_ninjaTextTex->m_Height / 2)
                       : 0.0f;
    m_BounceY = ninjaH + 160.0f;
}

MainScreen::~MainScreen() {
    Release();
}

// Matches 0x0014ac80 (13 lines): calls Reset via vtable
void MainScreen::Init() {
    Reset();
}

// Matches 0x0014ac8c: no-op
void MainScreen::Reset() {
}

// Matches 0x0014cd20 (~40 lines): cleanup all resources
void MainScreen::Release() {
    pPlayButton    = nullptr;
    pDojoButton    = nullptr;
    pLeaderboardBtn = nullptr;
    pMoreGamesBtn  = nullptr;
    pToggleA       = nullptr;
    pToggleB       = nullptr;
    pMusicToggle   = nullptr;
    pSoundToggle   = nullptr;

    delete m_pSliceInstrBox;
    m_pSliceInstrBox = nullptr;
}

// Matches Update at 0x0014b278 (677 lines) — state machine
void MainScreen::Update(float dt) {
    m_Time += dt;

    // Binary @ 0x0014b2a4: toggle null-check + create runs at the top of Update,
    // before the state switch, so destroyed toggles are recreated in any state.
    if (!pSoundToggle || !pMusicToggle) {
        CreateToggles();
    }

    switch (m_State) {
    case STATE_CAMERA_ZOOM: {
        // ASM-verified: 2026-05-20 binary @ 0x0014b278 case 0 (re-analyst)
        // Two-phase: while (bombHitTimer > 0.7), just lerp the camera and idle.
        // Once elapsed, start incrementing m_Timer2 and clear levelTransitionFlag.
        // m_StateTimer is the BOUNCE VELOCITY (set to 0.5f by QuitToMenu to seed
        // logo bounce on menu return). NOT a flash countdown in v1.6.1.
        const bool flashActive = (game_work.m_BombHitTimer > 0.7f);
        if (flashActive) {
            game_work.m_GameDt += (-1.0f - game_work.m_GameDt) * CAMERA_LERP_RATE;
        } else {
            // Binary @ 0x0014b60e: writes 0 to g_GameData+0x04 (gameMode).
            game_work.gameMode = 0;
            m_Timer2 += dt;
            game_work.m_GameDt += (-1.0f - game_work.m_GameDt) * CAMERA_LERP_RATE;
        }

        // Binary: CreateButtons called unconditionally every frame; gate inside CreatePlayDojo.
        CreatePlayDojo();

        if (game_work.m_GameDt < CAMERA_THRESHOLD && m_Timer2 > TIMER2_THRESHOLD) {
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CREATE_BUTTONS), "Update/CAMERA_ZOOM camera settled");
            m_State = STATE_CREATE_BUTTONS;
        }
        break;
    }

    case STATE_CREATE_BUTTONS: {
        // Binary @ 0x0014bbe2..0x0014bdf2.
        if (pDojoButton) {
            pDojoButton->SetNewSymbol(false);
        }

        if (!pLeaderboardBtn) {
            CreateQuitButton();
        }

        game_work.m_GameDt += (-1.0f - game_work.m_GameDt) * CAMERA_LERP_RATE;

        const float sizeY_1 = size.y;
        const float alpha_1 = -game_work.m_GameDt;
        pos.y = (sizeY_1 + 320.0f - 2.0f * sizeY_1 * alpha_1) * 0.5f;
        break;
    }

    case STATE_GAME_START: {
        if (-game_work.m_GameDt > 0.999f && !m_bGameStartReset) {
            WaveManager::GetInstance()->Reset(true);
            m_bGameStartReset = true;
            game_work.m_LevelTransitionFlag = 1;
        }
        game_work.m_GameDt *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
        if (fabsf(game_work.m_GameDt) < 0.001f) {
            game_work.m_GameDt = 0.0f;
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_FADE), "Update/GAME_START camera settled");
            m_State = STATE_CAMERA_FADE;
            m_bGameStartReset = false;
            game_work.m_LevelTransitionFlag = 0;
        }

        const float sizeY_2 = size.y;
        const float alpha_2 = fabsf(game_work.m_GameDt);
        const float tt_2 = sizeY_2 * alpha_2;
        pos.y = (sizeY_2 + 320.0f - 2.0f * tt_2) * 0.5f;
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D: {
        // Binary @ 0x0014be80: cases 3/4/0x15/0x16 share one block.
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int fruitCount = am ? am->GetNumEntities(0) : 0;

        if (fruitCount == 0) {
            m_Timer2 *= 1.0f - (1.0f - 0.75f) * FN::g_DebugTimeScale;
        }

        const float sizeY_d = size.y;
        const float tt_d = sizeY_d * m_Timer2;
        pos.y = (sizeY_d + 320.0f - 2.0f * tt_d) * 0.5f;

        if (!m_pDojoScreen && fruitCount == 0 && m_Timer2 < 0.001f) {
            m_Timer2 = 0.0f;
            m_pDojoScreen = new DojoScreen(game);
            m_pDojoScreen->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::DojoScreenRemoved);
            game_work.mHud->AddControl(m_pDojoScreen);
        }
        break;
    }

    case STATE_SLIDE_IN: {
        // Binary @ ~0x0014beec, two-phase lerp + pos.y animation.
        float posAlpha;
        if (m_Timer2 <= STATE_8_LERP_THRESHOLD) {
            m_Timer2 += (1.0f - m_Timer2) * STATE_8_LERP_RATE * FN::g_DebugTimeScale;
            posAlpha = m_Timer2;
        } else {
            m_Timer2 += dt;
            if (m_Timer2 > STATE_8_DURATION) {
                m_Timer2 = STATE_8_RESET_TIMER;
                LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/SLIDE_IN hold expired");
                m_State = STATE_CAMERA_ZOOM;
            }
            posAlpha = 1.0f;
        }
        const float sizeY_8 = size.y;
        const float tt_8 = sizeY_8 * posAlpha;
        pos.y = (sizeY_8 + 320.0f - 2.0f * tt_8) * 0.5f;
        break;
    }

    case STATE_LEADERBOARD:     // binary case 9
    case STATE_MORE_GAMES:      // binary case 10
    case STATE_MATCHMAKER:      // binary case 0x10
        // Defunct — OpenFeint / GameCenter / matchmaker states.
        // Binary resets m_StateTimer = 0 (bounce velocity cleared) and m_Timer2 = -0.85.
        LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/defunct-network state");
        m_State = STATE_CAMERA_ZOOM;
        m_StateTimer = 0.0f;
        m_Timer2 = -0.85f;
        game_work.m_GameDt = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_NEWS:            // binary case 0xb
        // Defunct — NetworkManager::UpdateNews.
        // Binary: m_StateTimer=0, m_State=1, m_Timer2=-0.85.
        LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CREATE_BUTTONS), "Update/defunct-news state");
        m_State = STATE_CREATE_BUTTONS;
        m_StateTimer = 0.0f;
        m_Timer2 = -0.85f;
        break;

    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2: {
        // Binary @ 0x0014bf40 cases 0xe/0xf: decay m_Timer2 and slide pos.y upward.
        const float oldTimer2 = m_Timer2;
        const float decay = 1.0f - (1.0f - STATE_0E_DECAY) * FN::g_DebugTimeScale;
        m_Timer2 *= decay;

        const float sizeY = size.y;
        const float tt = sizeY * m_Timer2;
        pos.y = (sizeY + 320.0f - 2.0f * tt) * 0.5f;

        if (oldTimer2 > STATE_0E_THRESHOLD && m_Timer2 <= STATE_0E_THRESHOLD) {
            GameModeScreen* gms = new GameModeScreen(game, false);
            game_work.mHud->AddControl(gms);
        }
        break;
    }

    case STATE_CAMERA_FADE:
        // Binary @ 0x0014c19a-0x0014c1d2
        if (game_work.m_GameDt < 0.0f) {
            game_work.m_GameDt *= 0.75f;
            if (game_work.m_GameDt > -0.001f) {
                game_work.m_GameDt = 0.0f;
                game_work.m_LevelTransitionFlag = 0;
                LOG_INFO("SCREEN/MainScreen", "STATE_CAMERA_FADE: timer clamped to 0.0f, levelTransitionFlag cleared");
            }
        }
        break;

    case STATE_LOADING_A:
    case STATE_LOADING_B:
        // Binary @ 0x0014c010: state always resets to 0 + clears menu buttons.
        m_Field114 += dt * 8.0f;
        if (m_Field114 >= 8.0f) {
            m_Field114 = 0.0f;
        }
        LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/LOADING");
        m_State = STATE_CAMERA_ZOOM;
        m_StateTimer = 0.0f;
        game_work.m_GameDt = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_QUIT_WAIT: {
        // ASM-verified: 2026-04-30 binary @ 0x0014c078..0x0014c0ea (asm-inspector)
        if (game_work.m_TutorialControl) {
            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
        }
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int liveEntities = am ? am->GetNumEntities(0) : 0;
        if (liveEntities != 0) break;

        const uint8_t qs = SystemManager::GetInstance().GetQuitState();
        if (qs == 2) {
            Bomb::HitMenuBomb(Vec3(163.0f, -96.0f, 0.0f));
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_QUIT_BOMB), "Update/QUIT_WAIT qs==2");
            m_State = STATE_QUIT_BOMB;
            m_StateTimer = 0.0f;
        } else if (qs == 3) {
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/QUIT_WAIT qs==3 cancelled");
            m_State = STATE_CAMERA_ZOOM;
            m_StateTimer = 0.0f;
            m_Timer2 = 0.15f;       // DAT_0014c298
        }
        break;
    }

    case STATE_QUIT_BOMB: {
        // Binary @ 0x0014c0f2 case 0x18
        if (game_work.m_TutorialControl) {
            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
        }
        if (Bomb::BombFlashFull()) {
            SystemManager::GetInstance().QuitGame();
            game.running = false;
        }
        break;
    }
    }

    // Sound/music toggle texture swap
    if (pSoundToggle) {
        pSoundToggle->m_Texture = (game_work.m_bSoundOn ? m_TexSoundOn : m_TexSoundOff);
    }
    if (pMusicToggle) {
        pMusicToggle->m_Texture = (game_work.m_bMusicOn ? m_TexMusicOn : m_TexMusicOff);
    }

    // Compute the state-dependent "elapsedTime" / pause driver.
    float elapsedTime;
    switch (m_State) {
    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D:
    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2:
    case STATE_SLIDE_IN:
        elapsedTime = m_Timer2;
        break;
    default:
        elapsedTime = -game_work.m_GameDt;
        break;
    }

    // Toggle button positioning.
    if (pSoundToggle && pMusicToggle) {
        pSoundToggle->pos.y = 135.5f;
        pMusicToggle->pos.y = 135.5f;

        if (elapsedTime <= 0.0f) {
            pSoundToggle->pos.x = 20.0f;
            pMusicToggle->pos.x = -20.0f;
        } else {
            pSoundToggle->pos.x = 216.0f;
            pMusicToggle->pos.x = 176.0f;
        }

        float pauseAmount = elapsedTime;
        if (pauseAmount < 0.0f) pauseAmount = 0.0f;
        if (pauseAmount > 1.0f) pauseAmount = 1.0f;

        float slideOffset = size.y * 2.0f * (1.0f - pauseAmount);
        pSoundToggle->m_Active = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pMusicToggle->m_Active = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pSoundToggle->pos.y += slideOffset;
        pMusicToggle->pos.y += slideOffset;
    }

    UpdateScreenElements(elapsedTime, m_Time);
}

// Binary @ 0x0014b278: Game+0x0c is the camera-transition timer.
float MainScreen::GetCameraTransition() const { return game_work.m_GameDt; }
void  MainScreen::SetCameraTransition(float v) { game_work.m_GameDt = v; }

// Helper: setup world matrix for a textured quad at given position
static void SetupQuadMatrix(MatrixManager& mm, const Vec3& hudScale,
                            float w, float h, const Vec3& drawPos) {
    (void)hudScale;
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
}

// Matches Draw at 0x0014d4ec (171 lines)
// DIFFERS: binary signature is Draw(float*) at vtable slot 7 @ 0x0014D4EC;
//   port uses Draw(Vec3&, int) for ergonomic param-passing.
void MainScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    if (m_State == STATE_CAMERA_FADE) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B ||
         m_State == STATE_DOJO_WAIT_C || m_State == STATE_DOJO_WAIT_D) &&
        m_Timer2 == 0.0f) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    // 1+2. fruit_text guard (GOT+0x6FCC, binary: gate on m_TexFruitText/global s_fruitTex)
    if (m_TexFruitText.IsValid()) {
        // 1a. Background shade (blurry_backing.tex) — angled triangle
        if (m_blurryBackingTex.IsValid()) {
            m_blurryBackingTex->Set();
            SetupQuadMatrix(mm, hudScale, size.x, size.y, pos);

            static const uint32_t kShadeCol = 0x80000000u;
            QUADCUSTOMVERTEX shadeVerts[3] = {
                { -1.0f, -0.6875f, 0.0f,  0,0,1,  kShadeCol,  0.0f,      0.0078125f },
                {  3.5f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  1.0f,      0.0078125f },
                { -1.0f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  0.065694f, 1.0f       },
            };
            game.renderer.DrawTriList(shadeVerts, 3);

            m_blurryBackingTex->UnSet();
        }

        // 1b. fruit_text logo (m_TexFruitText) drawn at {m_NinjaTextX, m_NinjaTextY, m_NinjaTextZ}
        // Binary: TranslateMatrix(&this+0xF8) reads 3 consecutive floats at binary +0xF8..+0x100
        // (port +0xE0..+0xE8 = m_NinjaTextX, m_NinjaTextY, m_NinjaTextZ).
        static const float FRUIT_TEXT_SCALE = 0.85f;  // DAT_0014d838
        m_TexFruitText->Set();
        Vec3 fruitTextDrawPos(m_NinjaTextX, m_NinjaTextY, m_NinjaTextZ);
        SetupQuadMatrix(mm, hudScale,
            (float)m_TexFruitText->m_Width * FRUIT_TEXT_SCALE,
            (float)m_TexFruitText->m_Height * FRUIT_TEXT_SCALE,
            fruitTextDrawPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_TexFruitText->UnSet();
    }

    // 3. ninja_text drawn at {m_BounceVel, m_BounceY, m_field10C} (+0x104..+0x10C binary)
    // Binary: TranslateMatrix(&this+0x104) reads 3 consecutive floats.
    // m_BounceY is the bounce POSITION (the Y of ninja_text in Draw).
    if (m_ninjaTextTex.IsValid()) {
        Vec3 ninjaDrawPos(m_BounceVel, m_BounceY, m_field10C);
        m_ninjaTextTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_ninjaTextTex->m_Width, (float)m_ninjaTextTex->m_Height,
            ninjaDrawPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_ninjaTextTex->UnSet();
    }

    // 4. Parchment frame (slice_fruit.tex) at m_LogoPos, then BakedStringBox on top.
    if (m_TexSliceFruit.IsValid()) {
        m_TexSliceFruit->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_TexSliceFruit->m_Width, (float)m_TexSliceFruit->m_Height,
            m_LogoPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_TexSliceFruit->UnSet();
    }
    if (m_pSliceInstrBox) {
        Vec3 instrPos = m_LogoPos + Vec3(-4.0f, -4.0f, 0.0f);
        m_pSliceInstrBox->SetTranslation(instrPos, 1);
        m_pSliceInstrBox->Draw(8.0f, Vec2(1.0f, 1.0f), 1);
    }

    // 5. Loading symbol (states 0x13, 0x14 only)
    if (m_State == STATE_LOADING_A || m_State == STATE_LOADING_B) {
        DrawLoadingSymbol(&hudScale.x);
    }

    // 6. m_TexBc (comming_soon overlay) — drawn when valid AND pPlayButton exists.
    if (m_TexBc.IsValid() && pPlayButton != NULL) {
        float csW = (float)m_TexBc->m_Width;
        float csH = (float)m_TexBc->m_Height;
        float scaleX = csW * 0.5f;
        float scaleY = csH * 0.5f * (csW > 0.0f ? (csH / csW) : 1.0f);
        Vec3 csPos(0.0f, 7.0f, 0.0f);
        m_TexBc->Set();
        SetupQuadMatrix(mm, hudScale, scaleX, scaleY, csPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_TexBc->UnSet();
    }
}

// Matches 0x0014ad3c — logo integrator (v1.6.1 semantics)
//
// arg1 = cameraTransition = ct = -game_work.m_GameDt, clamped to 0.04
// arg2 = time = m_Time (accumulated seconds, used for the settle gate time > 0.99)
//
// v1.6.1 field semantics:
//   m_StateTimer (+0xF8 port / +0x110 binary) = BOUNCE VELOCITY accumulator
//   m_BounceY    (+0xF0 port / +0x108 binary) = bounce POSITION (ninja_text Y in Draw)
//   m_BounceVel  (+0xEC port / +0x104 binary) = 60.0 decorative; ninja_text X in Draw
//   m_NinjaTextX (+0xE0 port / +0xF8 binary)  = fruit_text X (-120/frame)
//   m_NinjaTextY (+0xE4 port / +0xFC binary)  = fruit_text Y (pos.y+18)
//   m_NinjaTextZ (+0xE8 port / +0x100 binary) = 0
//   m_field10C   (+0xF4 port / +0x10C binary) = 0; ninja_text Z
//   m_Lean       (+0xDC port / +0xF4 binary)  = logo lean lerp (init 1.0)
//   m_LogoPos    (+0xD0 port / +0xE8 binary)  = fruit_text + sliceInstrBox draw pos
//
// Binary constants (literal pool at 0x14aec4):
//   CLAMP_THRESHOLD    = 0.04   (DAT_0014aec4)
//   BOUNCE_GRAVITY     = -55.0  (DAT_0014aecc)
//   ELAPSED_THRESHOLD  = 0.99   (DAT_0014aed8)
//
void MainScreen::UpdateScreenElements(float cameraTransition, float time) {
    static const float MAX_CT            = 0.04f;    // DAT_0014aec4 — MAX clamp (not min)
    static const float BOUNCE_GRAVITY    = -55.0f;   // DAT_0014aecc
    static const float ELAPSED_THRESHOLD = 0.99f;    // DAT_0014aed8

    if (cameraTransition > MAX_CT) {
        cameraTransition = MAX_CT;
    }

    // m_NinjaTextZ and m_field10C = 0.0 (binary DAT_0014aec8)
    m_NinjaTextZ = 0.0f;
    m_field10C   = 0.0f;

    // fruit_text X = -120.0 (constant per binary @ 0x00195a58)
    m_NinjaTextX = -120.0f;

    // m_BounceVel = 60.0 decorative (set per-frame; also used as ninja_text X in Draw)
    m_BounceVel = 60.0f;

    // Bounce physics:
    // m_StateTimer = VELOCITY accumulator
    // m_BounceY    = POSITION integrator
    float newVel = m_StateTimer + cameraTransition * BOUNCE_GRAVITY;
    m_StateTimer = newVel;

    float newPos = m_BounceY + newVel * cameraTransition * 15.0f;
    m_BounceY = newPos;

    float floorPos = pos.y + 18.0f;

    // fruit_text Y = pos.y + 18 (the floor level)
    m_NinjaTextY = floorPos;

    // tute = 1.0 while ct > 0, else 0.0
    if (cameraTransition > 0.0f) {
        m_GlobalAlphaTarget = 1.0f;
    } else {
        m_GlobalAlphaTarget = 0.0f;
    }

    // Bounce floor: floorLimit = pos.y + 18 - 15 = pos.y + 3
    float floorLimit = floorPos - 15.0f;
    if (newPos < floorLimit) {
        m_BounceY    = floorLimit;
        m_StateTimer = newVel * BOUNCE_LOSS;

        if (fabsf(newVel * BOUNCE_LOSS) < BOUNCE_SETTLE &&
            time > ELAPSED_THRESHOLD &&
            cameraTransition > 0.0f) {
            m_StateTimer = 0.0f;
            m_GlobalAlphaTarget = 0.0f;
        }
    }

    // m_Lean lerp: m_Lean += (tute - m_Lean) * 0.25
    // tute = m_GlobalAlphaTarget (1.0 when ct > 0, else 0.0)
    m_Lean += (m_GlobalAlphaTarget - m_Lean) * ALPHA_LERP_RATE * FN::g_DebugTimeScale;

    // m_LogoPos = (-175, 26, 0) + (-120, -17, 0) * m_Lean * 2.0
    // fruit_text + sliceInstrBox draw position (binary @ 0x0014ad3c end)
    Vec3 base(-175.0f, 26.0f, 0.0f);      // DAT_0014aedc, 26.0, DAT_0014aec8
    Vec3 offset(-120.0f, -17.0f, 0.0f);   // DAT_0014aed4, -17.0, DAT_0014aec8
    m_LogoPos = base + offset * m_Lean * 2.0f;
}

// Matches 0x0014aee8 (~35 lines).
void MainScreen::DeleteMenuButtons() {
    RemoveButton(pPlayButton);
    RemoveButton(pDojoButton);
    RemoveButton(pMoreGamesBtn);
    RemoveButton(pLeaderboardBtn);
}

// Matches 0x0014ad04 (7 lines)
void MainScreen::Hide() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_FADE), "Hide");
    m_State = STATE_CAMERA_FADE;
    pos = Vec3(0.0f, 0.0f, 0.0f);
}

void MainScreen::RemoveButton(MenuButton*& btn) {
    if (btn) {
        btn->m_bPendingRemoval = 1;
        btn = nullptr;
    }
}

// Helper: get texture size as Vec3, fallback to default
static Vec3 TexSize(const Mortar::SmartPtr<Mortar::Texture>& tex, float defW, float defH) {
    if (tex.IsValid() && tex->m_Width > 0)
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

void MainScreen::CreateToggles() {
    if (!game_work.mHud) return;

    pSoundToggle = new MenuButton();
    pSoundToggle->m_Texture = (game_work.m_bSoundOn ? m_TexSoundOn : m_TexSoundOff);
    pSoundToggle->Init(POS_SOUND_TOGGLE,
        Mortar::Delegate0<void>::Make(this, &MainScreen::SoundCallback), -1,
        TexSize(m_TexSoundOn, 32.0f, 32.0f), nullptr);
    pSoundToggle->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
    game_work.mHud->AddControl(pSoundToggle);

    pMusicToggle = new MenuButton();
    pMusicToggle->m_Texture = (game_work.m_bMusicOn ? m_TexMusicOn : m_TexMusicOff);
    pMusicToggle->Init(POS_MUSIC_TOGGLE,
        Mortar::Delegate0<void>::Make(this, &MainScreen::MusicCallback), -1,
        TexSize(m_TexMusicOn, 32.0f, 32.0f), nullptr);
    pMusicToggle->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
    game_work.mHud->AddControl(pMusicToggle);
}

void MainScreen::CreatePlayDojo() {
    if (!game_work.mHud) return;

    if (pPlayButton || game_work.m_BombHitTimer >= 1.45f) return;

    // v1.6.1: button textures come from game_work.pM_Textures[n] in the binary's CreateButtons.
    // Port falls back to loading them here (same texture files).
    Mortar::SmartPtr<Mortar::Texture> texNewGame =
        Mortar::TextureManager::LoadLocalisedTexture("newgame.tex");
    Mortar::SmartPtr<Mortar::Texture> texDojoIcon =
        Mortar::TextureManager::LoadLocalisedTexture("dojo_icon.tex");

    pPlayButton = new MenuButton();
    pPlayButton->m_Texture = texNewGame;
    pPlayButton->Init(POS_PLAY_BUTTON,
        Mortar::Delegate0<void>::Make(this, &MainScreen::GameModeCallback), 3, Vec3(0,0,0), nullptr);
    // TODO: 0x0014b782 -- RE whether play block truly overrides m_RestScale to texWidth+1
    //   or is a no-op *1.0 relying on CreateFruit entityScale*200.
    if (texNewGame.IsValid()) {
        pPlayButton->m_RestScale.x = (float)(texNewGame->m_Width  + 1);
        pPlayButton->m_RestScale.y = (float)(texNewGame->m_Height + 1);
        pPlayButton->m_RestScale.z = 1.0f;
    }
    pPlayButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    pPlayButton->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    // ASM-verified: 2026-05-09 binary @ 0x0014b818..0x0014b82c (re-analyst).
    pPlayButton->m_HitInsetY  = -50.0f;
    pPlayButton->m_HitInsetX  = -50.0f;
    pPlayButton->m_AnimScale  = 0.5f;
    pPlayButton->m_GrowInTimer = 0.25f;
    game_work.mHud->AddControl(pPlayButton);

    if (game_work.m_TutorialControl)
        game_work.m_TutorialControl->ResetTutePos(pPlayButton);

    pDojoButton = new MenuButton();
    pDojoButton->m_Texture = texDojoIcon;
    pDojoButton->Init(POS_DOJO_BUTTON,
        Mortar::Delegate0<void>::Make(this, &MainScreen::AboutCallback),
        Fruit::FruitType("mango", false), Vec3(0,0,0), nullptr);
    pDojoButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    pDojoButton->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    if (pDojoButton->m_pTrackedFruit) {
        pDojoButton->m_pTrackedFruit->scale = pDojoButton->m_pTrackedFruit->scale * 0.9f;
    }
    pDojoButton->m_RestScale   = pDojoButton->m_RestScale * 1.05f;
    pDojoButton->m_ShakeScale.x = 0.5f;
    pDojoButton->m_HitInsetX    = -50.0f;
    pDojoButton->m_HitInsetY    = -50.0f;
    pDojoButton->m_GrowInTimer  = 0.25f;
    game_work.mHud->AddControl(pDojoButton);
}

void MainScreen::CreateQuitButton() {
    if (!game_work.mHud) return;

    Mortar::SmartPtr<Mortar::Texture> texQuit =
        Mortar::TextureManager::LoadLocalisedTexture("quit.tex");

    pLeaderboardBtn = new MenuButton();
    pLeaderboardBtn->m_Texture = texQuit;
    pLeaderboardBtn->m_bRespondsToBackKey = 1;
    int fruitCount = FruitInfo_GetCount();
    pLeaderboardBtn->Init(POS_QUIT,
        Mortar::Delegate0<void>::Make(this, &MainScreen::QuitGamesCallback), fruitCount, Vec3(0,0,0), nullptr);
    pLeaderboardBtn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    pLeaderboardBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    if (texQuit.IsValid()) {
        pLeaderboardBtn->m_RestScale.x = (float)(texQuit->m_Width  + 1);
        pLeaderboardBtn->m_RestScale.y = (float)(texQuit->m_Height + 1);
        pLeaderboardBtn->m_RestScale.z = 1.0f;
    }
    game_work.mHud->AddControl(pLeaderboardBtn);
}

// Matches MainScreen::ButtonDeleted @ 0x0014acc0.
void MainScreen::ButtonDeleted(HUDControl* ctrl) {
    if (ctrl == pDojoButton)    pDojoButton    = nullptr;
    if (ctrl == pPlayButton)    pPlayButton    = nullptr;
    if (ctrl == pLeaderboardBtn) pLeaderboardBtn = nullptr;
    if (ctrl == pMoreGamesBtn)  pMoreGamesBtn  = nullptr;
}

// --- Callbacks ---

// Matches 0x0014b068
void MainScreen::GameModeCallback() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MODE_SELECT), "GameModeCallback");
    m_State = STATE_MODE_SELECT;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    FruitSaveData::DownloadTweaks();  // defunct stub
    pLeaderboardBtn = nullptr;
    // Binary @ 0x0014b020: re-seed engine PRNG with m_FrameTimer.
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}

// Defunct: orphaned callback in shipping binary -- binary @ 0x0014c384
// ZERO inbound xrefs. STATE_GAME_START is genuinely unreachable in shipping FruitNinja.exe.
// Body retained for vtable / layout fidelity.
// Matches 0x0014c384
void MainScreen::NewGameCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_GAME_START), "NewGameCallback");
    m_State = STATE_GAME_START;
    // ASM-verified: 2026-05-08 binary @ 0x0014c3ce (re-analyst).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay(
            "Game-start", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}

// Matches 0x0014afc4
void MainScreen::AboutCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_DOJO_WAIT_B), "AboutCallback");
    m_State = STATE_DOJO_WAIT_B;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    pLeaderboardBtn = nullptr;
}

// Matches 0x0014af64
void MainScreen::SoundCallback() {
    game_work.m_bSoundOn = !game_work.m_bSoundOn;
    Mortar::SoundManager::GetInstance().SetSFXVolume(
        game_work.m_bSoundOn ? SOUND_VOLUME_ON : 0.0f);
}

// Matches 0x0014ac9c
void MainScreen::MusicCallback() {
    game_work.m_bMusicOn = !game_work.m_bMusicOn;
}

// Matches 0x0014b010
void MainScreen::LeaderboardsCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_LEADERBOARD), "LeaderboardsCallback");
    m_State = STATE_LEADERBOARD;
}

// Matches 0x0014b000
void MainScreen::MoreGamesCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MORE_GAMES), "MoreGamesCallback");
    m_State = STATE_MORE_GAMES;
}

// ASM-verified: 2026-04-30 binary @ 0x0014b1a0..0x0014b1ed (asm-inspector + re-analyst).
void MainScreen::QuitGamesCallback() {
    SystemManager::GetInstance().RequestQuit();

    if (pLeaderboardBtn && pLeaderboardBtn->m_pFruitPiece) {
        Bomb* bomb = static_cast<Bomb*>(
            static_cast<Mortar::Entity*>(pLeaderboardBtn->m_pFruitPiece));
        bomb->m_bMovement = 1;
        bomb->m_AccelForce = Vec3(0.0f, 10.0f, 0.0f);
    }

    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_QUIT_WAIT), "QuitGamesCallback");
    m_State = STATE_QUIT_WAIT;
    m_StateTimer = 0.0f;
}


// @ 0x0016bbb0
void MainScreen::DrawPostEffects() {
    // TODO: implement -- post-effect overlays (score flash, bonus anim, etc.)
}

// Binary @ 0x0014D1F8 — 8-segment radial loading spinner.
void MainScreen::DrawLoadingSymbol(const float* hudScale) {
    if (!m_blurryBackingTex.IsValid()) return;

    int idx   = (int)m_Field114 & 7;  // DAT_0014D4B8
    int phase = (7 - idx) & 7;

    static const float kSmallR = 0.03125f;  // DAT_0014D4C0
    static const float kBigR   = 1.0f;      // DAT_0014D4C4

    static QUADCUSTOMVERTEX s_verts[48];
    static bool s_built = false;

    if (!s_built) {
        static const float kTwoPi = 6.283185307f;
        for (int seg = 0; seg < 8; seg++) {
            float a1    = (float)seg * (kTwoPi / 8.0f);
            float a1end = a1 + (kTwoPi / 8.0f);
            float a2    = a1 + (kTwoPi / 4.0f);

            float sx1 = sinf(a1)    * kBigR,   cy1 = cosf(a1)    * kBigR;
            float ex1 = sinf(a1end) * kBigR,   ey1 = cosf(a1end) * kBigR;
            float sx2 = sinf(a2)    * kSmallR, cy2 = cosf(a2)    * kSmallR;

            float corners[4][2] = {
                { sx1 + sx2, cy1 + cy2 },
                { sx1 - sx2, cy1 - cy2 },
                { ex1 + sx2, ey1 + cy2 },
                { ex1 - sx2, ey1 - cy2 },
            };

            static const int kOrder[6][2] = {{0,0},{1,1},{2,2},{1,1},{3,3},{2,2}};
            int vbase = seg * 6;
            for (int v = 0; v < 6; v++) {
                QUADCUSTOMVERTEX& qv = s_verts[vbase + v];
                qv.x = corners[kOrder[v][0]][0];
                qv.y = corners[kOrder[v][0]][1];
                qv.z = 0.0f;
                qv.nx = 0.0f; qv.ny = 0.0f; qv.nz = 1.0f;
                qv.colour = 0xC8FFFFFFu;
                qv.u = 0.0f; qv.v = 0.0f;
            }
        }
        s_built = true;
    }

    for (int seg = 0; seg < 8; seg++) {
        int fadeIdx   = (phase + seg) & 7;
        int raw       = fadeIdx * 32;
        int intensity = (raw < 64) ? 64 : ((raw > 255) ? 255 : raw);
        uint32_t col  = ((uint32_t)200 << 24) |
                        ((uint32_t)intensity << 16) |
                        ((uint32_t)intensity << 8) |
                        (uint32_t)intensity;
        for (int v = 0; v < 6; v++) {
            s_verts[seg * 6 + v].colour = col;
        }
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    m_blurryBackingTex->Set();

    float scale = (*hudScale) * 0.0625f;  // DAT_0014D4C8

    float tx = 0.0f, ty = 0.0f;
    if (m_State == STATE_LOADING_B) {
        // TODO: DAT_0014D4CC = state 0x14 X offset (unresolved)
        // TODO: DAT_0014D4D0 = state 0x14 Y offset (unresolved)
        tx = 0.0f; ty = 0.0f;
    } else {
        // STATE_LOADING_A: DAT_0014D4D8 X (unresolved), Y = 7.0
        tx = 0.0f; ty = 7.0f;
    }

    Vec3 drawPos(tx, ty, 0.0f);
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    game.renderer.DrawTriList(s_verts, 48);

    m_blurryBackingTex->UnSet();
}

// Defunct: vtable PreDraw — no-op stub; binary @ 0x0014AC94
void* MainScreen::PreDraw(float* /*hudScale*/) {
    return this;
}

// Defunct: NetworkManager::CancelNewsDisplay — no-op stub; binary @ 0x0014AFB8
void MainScreen::CancelNews() {
    // Defunct: NetworkManager — no-op stub; binary @ 0x0014AFB8
}

// Defunct: network UI button — empty in binary (single bx lr); binary @ 0x0014ACFC
void MainScreen::ClearNetworkButton() {
    // Defunct: network UI button — no-op stub; binary @ 0x0014ACFC
}

// Defunct: leaderboard UI — returns this in binary; binary @ 0x0014AD00
MainScreen* MainScreen::CreateNormalLeaderboardButton() {
    // Defunct: leaderboard UI — no-op stub; binary @ 0x0014AD00
    return this;
}

// Binary @ 0x0014AC98 — empty event hook.
void MainScreen::OnMenuItemsCleared() {
    // no-op — empty in binary; binary @ 0x0014AC98
}

// Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF).
void MainScreen::MultiplayerGameModeCallback() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MODE_SELECT_2), "MultiplayerGameModeCallback");
    m_State = STATE_MODE_SELECT_2;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    pLeaderboardBtn = nullptr;
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}
