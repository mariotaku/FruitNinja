//
// MainScreen — reimplemented from docs/screens/main.md
// Original: ctor 0x0014c430 (159 lines), Update 0x0014b278 (677 lines),
//           Draw 0x0014d4ec (171 lines)
//

#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "core/SystemManager.h"
#include <cstdio>
#include <cmath>

// Timing constants (verified from binary, see docs/screens/main.md)
static const float CAMERA_LERP_RATE    = 0.125f;
static const float CAMERA_THRESHOLD    = -0.999f;
static const float TIMER2_THRESHOLD    = 0.15f;
static const float STATE_0E_DECAY      = 0.85f;
static const float STATE_0E_THRESHOLD  = 0.25f;
static const float STATE_2_DECAY       = 0.75f;
static const float STATE_8_LERP_RATE   = 0.125f;
static const float STATE_8_DURATION    = 1.5f;
static const float STATE_8_RESET_TIMER = -0.85f;
static const float BOUNCE_LOSS         = -0.25f;
static const float BOUNCE_SETTLE       = 3.0f;
static const float ALPHA_LERP_RATE     = 0.25f;
static const float PAUSE_VISIBILITY    = 0.01f;
static const float SOUND_VOLUME_ON     = 0.5f;

// Helper: get GLuint from SmartPtr<Texture>
static GLuint TexId(const SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}

// Button positions (verified from read_memory, docs/screens/main.md)
static const Vec3 POS_PLAY_BUTTON(16.0f, -66.0f, -50.0f);
static const Vec3 POS_DOJO_BUTTON(-144.0f, -65.0f, -50.0f);
static const Vec3 POS_LEADERBOARD(182.0f, -106.0f, 0.0f);
static const Vec3 POS_MORE_GAMES(182.0f, -106.0f, 0.0f);
static const Vec3 POS_SOUND_TOGGLE(216.0f, 135.5f, 0.0f);
static const Vec3 POS_MUSIC_TOGGLE(176.0f, 135.5f, 0.0f);

// Matches ctor at 0x0014c430 (159 lines)
MainScreen::MainScreen(Game& g)
    : game(g),
      pPlayButton(NULL), pDojoButton(NULL),
      pLeaderboardBtn(NULL), pMoreGamesBtn(NULL),
      pSoundToggle(NULL), pMusicToggle(NULL),
      m_Alpha(1.0f),
      m_LogoNinjaTextX(0.0f), m_WindowCenter(0.0f), field_0x100(0.0f),
      m_BounceVelocity(0.0f), m_field108(0.0f),
      m_State(STATE_CAMERA_ZOOM), m_StateTimer(0.0f),
      m_Timer2(0.0f),
      m_CameraTransition(0.0f), m_GlobalAlphaTarget(1.0f), m_Time(0.0f)
{
    // Load global textures (assigned to globals via GOT in original)
    m_blurryBackingTex = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    m_fruitTextTex     = Mortar::TextureManager::LoadLocalisedTexture("fruit_text.tex");
    m_ninjaTextTex     = Mortar::TextureManager::LoadLocalisedTexture("ninja_text.tex");

    // Load background decoration
    m_TexSliceFruit = Mortar::TextureManager::LoadLocalisedTexture("slice_fruit.tex");

    // Load button textures
    m_TexNewGame        = Mortar::TextureManager::LoadLocalisedTexture("newgame.tex");
    m_TexDojoIcon       = Mortar::TextureManager::LoadLocalisedTexture("dojo_icon.tex");
    m_TexGCAchievements = Mortar::TextureManager::LoadLocalisedTexture("gc_achievements.tex");
    m_TexMoreGames      = Mortar::TextureManager::LoadLocalisedTexture("more_games.tex");
    m_TexQuit           = Mortar::TextureManager::LoadLocalisedTexture("quit.tex");
    m_TexOpenFeint      = Mortar::TextureManager::LoadLocalisedTexture("openfeint.tex");

    // Load toggle textures
    m_TexSoundOn  = Mortar::TextureManager::LoadLocalisedTexture("sound.tex");
    m_TexSoundOff = Mortar::TextureManager::LoadLocalisedTexture("sound_cross.tex");
    m_TexMusicOn  = Mortar::TextureManager::LoadLocalisedTexture("music.tex");
    m_TexMusicOff = Mortar::TextureManager::LoadLocalisedTexture("music_cross.tex");

    // Load logo overlay
    m_TexCommingSoon = Mortar::TextureManager::LoadLocalisedTexture("comming_soon.tex");

    // Font: fonts/verdana.fnt (TODO: implement Font system)

    // Set size = (480.0, 138.0, 1.0)
    size = Vec3(480.0f, 138.0f, 1.0f);

    // Set position = (0.0, (320.0 - size_y) * 0.5, 0.0) = (0.0, 91.0, 0.0)
    pos = Vec3(0.0f, (320.0f - size.y) * 0.5f, 0.0f);

    // m_WindowCenter = windowHeight/2 + 160.0
    m_WindowCenter = 320.0f / 2.0f + 160.0f;  // = 320.0

    // Copy original size
    m_OrigSize = size;

    // Zero all button pointers (already done in init list)
    // state=0, timers=0 (already done in init list)

    printf("MainScreen: ctor (size=%.0f,%.0f pos=%.0f,%.0f)\n",
           size.x, size.y, pos.x, pos.y);
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
    // Zero all button pointers (HUD owns them, don't delete here)
    pPlayButton = NULL;
    pDojoButton = NULL;
    pLeaderboardBtn = NULL;
    pMoreGamesBtn = NULL;
    pSoundToggle = NULL;
    pMusicToggle = NULL;

    // TODO: delete textures and font when proper resource management exists
}

// Matches Update at 0x0014b278 (677 lines) — state machine
void MainScreen::Update(float dt) {
    m_Time += dt;

    switch (m_State) {
    case STATE_CAMERA_ZOOM: {
        // Camera zoom-in from splash. Create toggles + play/dojo buttons.
        // Lerp camera transition toward -1.0
        m_CameraTransition += (-1.0f - m_CameraTransition) * CAMERA_LERP_RATE;
        m_Timer2 += dt;

        // Create toggles if they don't exist
        if (!pSoundToggle) {
            CreateToggles();
        }

        // Create play/dojo buttons if they don't exist
        if (!pPlayButton && m_Timer2 > TIMER2_THRESHOLD) {
            CreatePlayDojo();
        }

        // Transition to CREATE_BUTTONS when camera settles
        if (m_CameraTransition < CAMERA_THRESHOLD && m_Timer2 > TIMER2_THRESHOLD) {
            m_State = STATE_CREATE_BUTTONS;
            CreateLeaderboard();
        }
        break;
    }

    case STATE_CREATE_BUTTONS:
        // Active menu state — nothing to do, buttons handle themselves
        // TODO: check ItemManager::AreNewItems() for "new" badge
        break;

    case STATE_GAME_START: {
        // Direct game start. Decay camera × 0.75.
        m_CameraTransition *= STATE_2_DECAY;

        // When camera > 0.999: fully faded, transition to game
        if (m_CameraTransition > 0.999f) {
            m_CameraTransition = 1.0f;
        }

        // At threshold: reset wave, enter game
        if (fabsf(m_CameraTransition) < 0.01f) {
            m_State = STATE_CAMERA_FADE;
        }
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D:
        // Wait for ActorManager::GetNumEntities() == 0, decay timer2 × 0.75
        m_Timer2 *= 0.75f;
        // TODO: check entity count, create DojoScreen when clear
        if (m_Timer2 < 0.01f) {
            m_Timer2 = 0.0f;
            // TODO: create DojoScreen and transition
        }
        break;

    case STATE_SLIDE_IN: {
        // Slide-in return. Lerp timer2 -> 1.0.
        m_Timer2 += (1.0f - m_Timer2) * STATE_8_LERP_RATE;
        m_StateTimer += dt;

        // After settling + 1.5s: reset to CAMERA_ZOOM
        if (m_Timer2 > 0.99f && m_StateTimer > STATE_8_DURATION) {
            m_Timer2 = STATE_8_RESET_TIMER;
            m_State = STATE_CAMERA_ZOOM;
            m_StateTimer = 0.0f;
            m_CameraTransition = 0.0f;
            DeleteMenuButtons();
        }
        break;
    }

    case STATE_LEADERBOARD:
    case STATE_MORE_GAMES:
    case STATE_MATCHMAKER:
        // Network states — skip for port, return to menu
        m_State = STATE_CAMERA_ZOOM;
        m_Timer2 = 0.0f;
        m_StateTimer = 0.0f;
        m_CameraTransition = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_NEWS:
        // Network news — skip, return to active menu
        m_State = STATE_CREATE_BUTTONS;
        break;

    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2: {
        // Slide-out: decay timer2 × 0.85
        m_Timer2 *= STATE_0E_DECAY;

        // At threshold: create GameModeScreen
        if (m_Timer2 < STATE_0E_THRESHOLD) {
            // TODO: create GameModeScreen
            // For now: start game directly
            m_State = STATE_GAME_START;
            m_CameraTransition = -1.0f;
        }
        break;
    }

    case STATE_CAMERA_FADE:
        // Camera fade after game return. Decay × 0.75 until settled.
        m_CameraTransition *= STATE_2_DECAY;
        break;

    case STATE_LOADING_A:
    case STATE_LOADING_B:
        // Accumulate field108 += dt × 8. When >= 8.0 → reset.
        m_field108 += dt * 8.0f;
        if (m_field108 >= 8.0f) {
            m_field108 = 0.0f;
            m_State = STATE_CAMERA_ZOOM;
            m_StateTimer = 0.0f;
            m_CameraTransition = 0.0f;
            DeleteMenuButtons();
        }
        break;

    case STATE_QUIT_WAIT:
        // Wait for entities, then bomb transition
        // TODO: check entity count, HitMenuBomb
        m_State = STATE_QUIT_BOMB;
        break;

    case STATE_QUIT_BOMB:
        // BombFlash → SystemManager::QuitGame()
        Mortar::SystemManager::GetInstance().QuitGame();
        game.running = false;
        break;
    }

    // Position update (end of Update, all states)
    // Sound/music toggle texture swap
    if (pSoundToggle) {
        pSoundToggle->m_Texture = TexId(game.soundEnabled ? m_TexSoundOn : m_TexSoundOff);
    }
    if (pMusicToggle) {
        pMusicToggle->m_Texture = TexId(game.musicEnabled ? m_TexMusicOn : m_TexMusicOff);
    }

    // Toggle button positioning (matches docs: end of Update, all states)
    if (pSoundToggle && pMusicToggle) {
        pSoundToggle->pos.y = 135.5f;
        pMusicToggle->pos.y = 135.5f;

        if (m_CameraTransition <= 0.0f) {
            // Camera zoomed in — normal positions
            pSoundToggle->pos.x = 216.0f;
            pMusicToggle->pos.x = 176.0f;
        } else {
            // Camera transitioning out — slide to edges
            pSoundToggle->pos.x = 20.0f;
            pMusicToggle->pos.x = -20.0f;
        }

        // Docs: pauseAmount = clamp(cameraTransition + GetPauseAmount(), 0, 1)
        // m_CameraTransition lerps 0 → -1 during zoom-in. Use abs to get 0→1 range.
        float pauseAmount = fabsf(m_CameraTransition);
        if (pauseAmount > 1.0f) pauseAmount = 1.0f;

        // slideOffset: when pauseAmount=1 (fully zoomed), offset=0 (on screen)
        //              when pauseAmount=0 (not zoomed), offset=size.y*2 (off screen)
        float slideOffset = size.y * 2.0f * (1.0f - pauseAmount);
        pSoundToggle->m_bActive = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pMusicToggle->m_bActive = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pSoundToggle->pos.y += slideOffset;
        pMusicToggle->pos.y += slideOffset;
    }

    // Update logo animation
    // Original asm at 0x14c1e6-0x14c1f2:
    //   s0 = s17 = dt (saved at 0x14b296 from Update's float param)
    //   s1 = s16 = -s18 = -(Game+0x0c) (negated at 0x14b52a)
    // So: UpdateScreenElements(cameraTransition=dt, elapsedTime=-(Game+0x0c))
    float negCameraTransition = -m_CameraTransition;  // -(Game+0x0c): 0→+1 as camera zooms
    UpdateScreenElements(dt, negCameraTransition);
}

// Helper: setup world matrix for a textured quad at given position
static void SetupQuadMatrix(Mortar::MatrixManager& mm, const Vec3& hudScale,
                            float w, float h, const Vec3& drawPos) {
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    Vec3 offset(480.0f * hudScale.x, 320.0f * hudScale.y, 0.0f);
    Vec3 finalPos = offset + drawPos;
    mat.GlobalTranslate44(finalPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
}

// Matches Draw at 0x0014d4ec (171 lines)
void MainScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Skip drawing for certain states
    if (m_State == STATE_CAMERA_FADE) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B ||
         m_State == STATE_DOJO_WAIT_C || m_State == STATE_DOJO_WAIT_D) &&
        m_Timer2 == 0.0f) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    // 1+2. Shade triangle + fruit_text — guarded by fruit_text (GOT+0x6FCC, DAT_0014d844)
    // Original: single if-block for shade + fruit_text draw
    if (m_fruitTextTex.IsValid()) {
        // 1a. Background shade (blurry_backing.tex) — angled triangle, NOT a quad
        // Original: 3-vertex triangle (DrawTriList at 0x14d4ec lines 46-80)
        // Vertex cache at global+0x6cc, drawn with Scale(size)+Translate(pos)
        if (m_blurryBackingTex.IsValid()) {
            m_blurryBackingTex->Set();
            SetupQuadMatrix(mm, hudScale, size.x, size.y, pos);

            // Colour(0,0,0,0x80).PlatformColour() = 0x80000000
            static const uint32_t kShadeCol = 0x80000000u;
            // V0: bottom-left (Y=-0.6875 not -1.0 → creates angle)
            // V1: far-right top (X=3.5 extends past screen → clipped)
            // V2: top-left
            // UVs: DAT_0014d854=0.0, DAT_0014d830=0.0078125(=1/128), DAT_0014d834≈0.065694
            QUADCUSTOMVERTEX shadeVerts[3] = {
                { -1.0f, -0.6875f, 0.0f,  0,0,1,  kShadeCol,  0.0f,      0.0078125f },  // 0xBF300000
                {  3.5f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  1.0f,      0.0078125f },  // 0x40600000
                { -1.0f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  0.065694f, 1.0f       },  // 0x3D868A48
            };
            game.renderer.DrawTriList(shadeVerts, 3);

            m_blurryBackingTex->UnSet();
        }

        // 1b. "FRUIT" text logo (fruit_text.tex) — drawn at +0xEC (m_LogoFruitTextPos)
        // Original: Scale(texSize * 0.85) — DAT_0014d838 = 0.85
        static const float FRUIT_TEXT_SCALE = 0.85f;  // DAT_0014d838
        m_fruitTextTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_fruitTextTex->m_Width * FRUIT_TEXT_SCALE,
            (float)m_fruitTextTex->m_Height * FRUIT_TEXT_SCALE,
            m_LogoFruitTextPos);
        Colour fruitTint(255, 255, 255, (uint8_t)(m_Alpha * 255.0f));
        game.renderer.DrawQuad(fruitTint);
        m_fruitTextTex->UnSet();
    }

    // 3. "NINJA" text logo (ninja_text.tex) — drawn at +0xF8
    // Original: TranslateMatrix(&this+0xF8) reads 3 consecutive floats:
    //   +0xF8 = m_LogoNinjaTextX, +0xFC = m_WindowCenter, +0x100 = field_0x100
    if (m_ninjaTextTex.IsValid()) {
        Vec3 ninjaDrawPos(m_LogoNinjaTextX, m_WindowCenter, field_0x100);
        m_ninjaTextTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_ninjaTextTex->m_Width, (float)m_ninjaTextTex->m_Height,
            ninjaDrawPos);
        Colour ninjaTint(255, 255, 255, (uint8_t)(m_Alpha * 255.0f));
        game.renderer.DrawQuad(ninjaTint);
        m_ninjaTextTex->UnSet();
    }

    // 4. Dojo decoration (slice_fruit.tex)
    if (m_TexSliceFruit.IsValid()) {
        m_TexSliceFruit->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_TexSliceFruit->m_Width, (float)m_TexSliceFruit->m_Height,
            m_LogoFruitPos);
        Colour tint(255, 255, 255, (uint8_t)(m_Alpha * 255.0f));
        game.renderer.DrawQuad(tint);
        m_TexSliceFruit->UnSet();
    }

    // 5. Loading symbol (states 0x13, 0x14 only) — TODO

    // 6. "Coming soon" logo overlay — TODO (depends on pPlayButton existence)
}

// Matches 0x0014ad3c — constants verified from Ghidra decompilation + read_memory
//
// Binary constants (portrait coordinate space, literal pool at 0x14aec4):
//   CLAMP_THRESHOLD    = 0.04   (DAT_0014aec4)
//   BOUNCE_GRAVITY     = -55.0
//   LOGO_NARROW_POS    = -175.0 (position on narrow axis: portrait X, landscape Y)
//   ELAPSED_THRESHOLD  = 0.99   (DAT_0014aed8)
//
// Port specific: the original assigns positions to portrait axes (X=narrow, Y=wide).
// The port's landscape ortho has X=wide, Y=narrow. Logo positions swap X↔Y.
// All constants and bounce physics are unchanged from the binary.
//
void MainScreen::UpdateScreenElements(float cameraTransition, float time) {
    static const float CLAMP_THRESHOLD   = 0.04f;    // DAT_0014aec4
    static const float BOUNCE_GRAVITY    = -55.0f;   // DAT_0014aecc
    static const float ELAPSED_THRESHOLD = 0.99f;    // DAT_0014aed8

    if (cameraTransition < CLAMP_THRESHOLD) {
        cameraTransition = CLAMP_THRESHOLD;
    }

    // Original: m_LogoFruitTextPos.z and field_0x100 = DAT_0014aec8 (=0.0)
    // These act as z components for fruit text and ninja text draw positions
    m_LogoFruitTextPos.z = 0.0f;
    field_0x100 = 0.0f;

    // Ninja text X = 60.0 (DAT_0014aed0); Y comes from m_WindowCenter in Draw
    m_LogoNinjaTextX = 60.0f;  // DAT_0014aed0

    // Bounce physics (matches binary exactly)
    float newVel = m_BounceVelocity + cameraTransition * BOUNCE_GRAVITY;
    m_BounceVelocity = newVel;

    float newCenter = m_WindowCenter + newVel * cameraTransition * 15.0f;
    m_WindowCenter = newCenter;

    float floorPos = pos.y + 18.0f;

    // Fruit text position: use original values directly (no X↔Y swap)
    m_LogoFruitTextPos.x = -120.0f;     // DAT_0014aed4 (LOGO_NINJA_OFFSET_Y)
    m_LogoFruitTextPos.y = floorPos;     // pos.y + 18.0
    // m_LogoFruitTextPos.z (+0xF4) = 0.0, set above

    // Temporary copy (overwritten at end of function with correct formula)
    m_LogoFruitPos = m_LogoFruitTextPos;

    if (cameraTransition > 0.0f) {
        m_GlobalAlphaTarget = 1.0f;
    }

    float floorLimit = floorPos - 15.0f;
    if (newCenter < floorLimit) {
        m_WindowCenter = floorLimit;
        m_BounceVelocity = newVel * BOUNCE_LOSS;

        if (fabsf(newVel * BOUNCE_LOSS) < BOUNCE_SETTLE &&
            time > ELAPSED_THRESHOLD &&
            cameraTransition > 0.0f) {
            m_BounceVelocity = 0.0f;
            m_GlobalAlphaTarget = 0.0f;
        }
    }

    m_Alpha += (m_GlobalAlphaTarget - m_Alpha) * ALPHA_LERP_RATE;

    // LogoFruitPos (slice_fruit decoration): matches binary at end of 0x0014ad3c
    // m_LogoFruitPos = (-175, 26, 0) + (-120, -17, 0) * m_Alpha * 2.0
    Vec3 base(-175.0f, 26.0f, 0.0f);        // DAT_0014aedc, 26.0, DAT_0014aec8
    Vec3 offset(-120.0f, -17.0f, 0.0f);     // DAT_0014aed4, -17.0, DAT_0014aec8
    Vec3 scaled = offset * m_Alpha * 2.0f;
    m_LogoFruitPos = base + scaled;
}

// Matches 0x0014aee8 (~35 lines)
void MainScreen::DeleteMenuButtons() {
    RemoveButton(pPlayButton);
    RemoveButton(pDojoButton);
    RemoveButton(pMoreGamesBtn);
}

// Matches 0x0014ad04 (7 lines)
void MainScreen::Hide() {
    m_State = STATE_CAMERA_FADE;
    pos = Vec3(0.0f, 0.0f, 0.0f);
}

void MainScreen::RemoveButton(MenuButton*& btn) {
    if (btn && game.hud) {
        game.hud->RemoveControl(btn);
        // HUD::RemoveControl fires callback but doesn't delete
        // Original: vtable dtor + null
        btn = NULL;
    }
}

// Helper: get texture size as Vec3, fallback to default
static Vec3 TexSize(const SmartPtr<Mortar::Texture>& tex, float defW, float defH) {
    if (tex.IsValid() && tex->m_Width > 0)
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

void MainScreen::CreateToggles() {
    if (!game.hud) return;

    // Sound toggle: (216.0, 135.5, 0.0), 32x32, fruitType=-1 (no fruit)
    pSoundToggle = new MenuButton();
    pSoundToggle->m_Texture = TexId(game.soundEnabled ? m_TexSoundOn : m_TexSoundOff);
    pSoundToggle->size = TexSize(m_TexSoundOn, 32.0f, 32.0f);
    pSoundToggle->Init(POS_SOUND_TOGGLE,
        [this]() { SoundCallback(); }, -1, Vec3(0,0,0), nullptr);
    pSoundToggle->m_LayerFlags = 8;
    game.hud->AddControl(pSoundToggle);

    // Music toggle: (176.0, 135.5, 0.0), 32x32, fruitType=-1 (no fruit)
    pMusicToggle = new MenuButton();
    pMusicToggle->m_Texture = TexId(game.musicEnabled ? m_TexMusicOn : m_TexMusicOff);
    pMusicToggle->size = TexSize(m_TexMusicOn, 32.0f, 32.0f);
    pMusicToggle->Init(POS_MUSIC_TOGGLE,
        [this]() { MusicCallback(); }, -1, Vec3(0,0,0), nullptr);
    pMusicToggle->m_LayerFlags = 8;
    game.hud->AddControl(pMusicToggle);
}

void MainScreen::CreatePlayDojo() {
    if (!game.hud) return;

    // Play button: (16.0, -66.0, -50.0), fruitType=3 (watermelon)
    pPlayButton = new MenuButton();
    pPlayButton->m_Texture = TexId(m_TexNewGame);
    pPlayButton->size = TexSize(m_TexNewGame, 64.0f, 64.0f);
    pPlayButton->Init(POS_PLAY_BUTTON,
        [this]() { GameModeCallback(); }, 3, Vec3(0,0,0), nullptr);
    pPlayButton->m_LayerFlags = 8;
    game.hud->AddControl(pPlayButton);

    // Dojo button: (-144.0, -65.0, -50.0), fruitType=9 (mango)
    pDojoButton = new MenuButton();
    pDojoButton->m_Texture = TexId(m_TexDojoIcon);
    pDojoButton->size = TexSize(m_TexDojoIcon, 64.0f, 64.0f);
    pDojoButton->Init(POS_DOJO_BUTTON,
        [this]() { AboutCallback(); }, 9, Vec3(0,0,0), nullptr);
    pDojoButton->m_LayerFlags = 8;
    game.hud->AddControl(pDojoButton);
}

void MainScreen::CreateLeaderboard() {
    if (!game.hud) return;

    // Quit button: (182.0, -106.0, 0.0) — binary uses quit.tex (+0x98) at +0xA4
    pLeaderboardBtn = new MenuButton();
    pLeaderboardBtn->m_Texture = TexId(m_TexQuit);
    pLeaderboardBtn->size = TexSize(m_TexQuit, 48.0f, 48.0f);
    pLeaderboardBtn->Init(POS_LEADERBOARD,
        [this]() { QuitGamesCallback(); }, -1, Vec3(0,0,0), nullptr);
    pLeaderboardBtn->m_LayerFlags = 8;
    game.hud->AddControl(pLeaderboardBtn);
}

// --- Callbacks (all fully decompiled in docs/screens/main.md) ---

// Matches 0x0014b068
void MainScreen::GameModeCallback() {
    m_State = STATE_MODE_SELECT;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;
}

// Matches 0x0014c384
void MainScreen::NewGameCallback() {
    m_State = STATE_GAME_START;
    // TODO: GameSound::SFXPlay("swoosh_sound", 1.0, 1.0, NULL)
}

// Matches 0x0014afc4
void MainScreen::AboutCallback() {
    m_State = STATE_DOJO_WAIT_B;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;
}

// Matches 0x0014af64
void MainScreen::SoundCallback() {
    game.soundEnabled = !game.soundEnabled;
    // TODO: SoundManager::SetSFXVolume(game.soundEnabled ? 0.5f : 0.0f)
    printf("MainScreen: Sound %s\n", game.soundEnabled ? "ON" : "OFF");
}

// Matches 0x0014ac9c
void MainScreen::MusicCallback() {
    game.musicEnabled = !game.musicEnabled;
    // Note: no direct music play/stop — just flips flag
    printf("MainScreen: Music %s\n", game.musicEnabled ? "ON" : "OFF");
}

// Matches 0x0014b010
void MainScreen::LeaderboardsCallback() {
    m_State = STATE_LEADERBOARD;  // network — skip for port
}

// Matches 0x0014b000
void MainScreen::MoreGamesCallback() {
    m_State = STATE_MORE_GAMES;  // network — skip for port
}

// Matches 0x0014b1a0
void MainScreen::QuitGamesCallback() {
    Mortar::SystemManager::GetInstance().RequestQuit();
    m_State = STATE_QUIT_WAIT;
}

// Touch handling — routed from InputManager
bool MainScreen::HandleTouchDown(float x, float y) {
    // Route to visible buttons
    MenuButton* buttons[] = { pPlayButton, pDojoButton, pLeaderboardBtn,
                               pMoreGamesBtn, pSoundToggle, pMusicToggle };
    for (int i = 0; i < 6; i++) {
        if (buttons[i] && buttons[i]->m_bActive && buttons[i]->HitTest(x, y)) {
            buttons[i]->TouchDown(x, y);
            return true;
        }
    }
    return false;
}

void MainScreen::HandleTouchUp(float x, float y) {
    MenuButton* buttons[] = { pPlayButton, pDojoButton, pLeaderboardBtn,
                               pMoreGamesBtn, pSoundToggle, pMusicToggle };
    for (int i = 0; i < 6; i++) {
        if (buttons[i]) {
            buttons[i]->TouchUp(x, y);
        }
    }
}
