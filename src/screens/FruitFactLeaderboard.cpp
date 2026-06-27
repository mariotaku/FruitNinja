// FruitFactLeaderboard -- v1.6.1 leaderboard fact page.
// Binary refs: ctor 0x00176980, dtor 0x001764b8, Update 0x00177abc.

#include "FruitFactLeaderboard.h"
#include "hud/FruitFactControl.h"
#include "hud/GenericHUDControl.h"
#include "engine/asset/TextureManager.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/util/StringTable.h"
#include "engine/network/NetworkManager.h"
#include "game/LeaderboardManager.h"

// Binary @ 0x00176980
FruitFactLeaderboard::FruitFactLeaderboard(FruitFactControl* pCtrl, bool param2)
    : FruitFactPage(pCtrl)
    , m_pDownloadingLabel(0)
    , m_pProviderLabel(0)
    , m_pExtraLabel(0)
    , m_pScoreListHud(0)
    , m_pActionButton(0)
    , m_RefreshCount(0)
    , m_RefreshTimer(0.0f)
    , m_ConnectFlag(0)
    , m_FlashTimer(0.0f)
    , m_Mode(param2 ? 3 : 0)
    , m_State(1)
    , m_Row0()
    , m_Row1()
    , m_Row2()
{
    _padB5[0] = 0; _padB5[1] = 0; _padB5[2] = 0;
    _gapC4[0] = 0; _gapC4[1] = 0; _gapC4[2] = 0; _gapC4[3] = 0;
    _gapC4[4] = 0; _gapC4[5] = 0; _gapC4[6] = 0; _gapC4[7] = 0;

    LoadContent();

    // Post-LoadContent zero-fills for own state fields (binary @ 0x00176980)
    m_pScoreListHud  = 0;     // binary: str 0 @0xA4 (after FNHighscore ctors via LoadContent)
    m_ConnectFlag    = 0;     // binary: strb 0 @0xB4
    m_RefreshTimer   = 0.0f;  // binary: vstr 0.0 @0xB0
    m_RefreshCount   = 0;     // binary: str 0 @0xAC
    m_pActionButton  = 0;     // binary: str 0 @0xA8

    // Title: LSTR 0x7b if global (param2), else LSTR 0x363 (friends)
    const char* title = GETSTRING(
        param2 ? LSTR_LEADERBOARD_GLOBAL : LSTR_LEADERBOARD_FRIENDS, 0);
    CreateTitleTextControl(title);

    CreateSenseisHead(68.0f);

    // Divider 0: pos.x=-7.5, size=Vec3(276,53,0), field[+0xE8]=0.48
    {
        // TODO: v1.6.1 0x00176d30 (FruitFactLeaderboard::FruitFactLeaderboard) -- resolve divider0 texture name from GOT string pool
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("leaderboard_vertical_divider_1.tex");
        Vec3 pos(-7.5f, 0.0f, 0.0f);
        Vec3 sc(276.0f, 53.0f, 0.0f);
        Colour col(1.0f, 1.0f, 1.0f, 1.0f);
        GenericHUDControl* ctl = new GenericHUDControl(0.0f, 0.0f, tex, NULL, pos, sc, col, 8);
        ctl->m_AlphaTrans.f5 = 0.48f;
        AddGenericControl(ctl);
    }

    // Divider 1: pos.x=15.0, field[+0xE8]=0.5
    {
        // TODO: v1.6.1 0x00176d30 (FruitFactLeaderboard::FruitFactLeaderboard) -- resolve divider1 texture name + size vec from GOT string pool
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("leaderboard_vertical_divider_1.tex");
        Vec3 pos(15.0f, 0.0f, 0.0f);
        Vec3 sc(1.0f, 1.0f, 1.0f);
        Colour col(1.0f, 1.0f, 1.0f, 1.0f);
        GenericHUDControl* ctl = new GenericHUDControl(0.0f, 0.0f, tex, NULL, pos, sc, col, 8);
        ctl->m_AlphaTrans.f5 = 0.5f;
        AddGenericControl(ctl);
    }

    // Divider 2: pos.x=80.0, field[+0xE8]=0.5
    {
        // TODO: v1.6.1 0x00176d30 (FruitFactLeaderboard::FruitFactLeaderboard) -- resolve divider2 texture name + size vec from GOT string pool
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("leaderboard_vertical_divider_1.tex");
        Vec3 pos(80.0f, 0.0f, 0.0f);
        Vec3 sc(1.0f, 1.0f, 1.0f);
        Colour col(1.0f, 1.0f, 1.0f, 1.0f);
        GenericHUDControl* ctl = new GenericHUDControl(0.0f, 0.0f, tex, NULL, pos, sc, col, 8);
        ctl->m_AlphaTrans.f5 = 0.5f;
        AddGenericControl(ctl);
    }

    // Clear stale scores for (diff, mode). Difficulty byte from settings global +4.
    // TODO: v1.6.1 0x00176980 (FruitFactLeaderboard::FruitFactLeaderboard) -- read difficulty byte from settings global offset +4; resolve global addr
    {
        uint8_t diff = 0;
        if (diff != 2) {
            LeaderboardManager::GetInstance()->ClearScores(
                static_cast<int>(diff), m_Mode);
        }
    }

    // Online gate: provider offline or friends not loaded -> local-only (m_State=1).
    if (!IsProviderOnline() || !AreFriendsLoaded()) {
        m_State = 1;
    } else {
        m_State = 2;
    }

    m_pDownloadingLabel = 0;  // binary: str 0 @0x98
    m_pProviderLabel = 0;     // binary: str 0 @0x9C
    m_pExtraLabel = 0;        // binary: str 0 @0xA0
}

FruitFactLeaderboard::~FruitFactLeaderboard() {
}

// TODO: v1.6.1 0x00177abc (FruitFactLeaderboard::Update) -- full Update body:
//   m_FlashTimer accumulates dt (vldr/vmla/clamp/vstr @0x177ac0)
//   m_State jump-table switch @0x17801c writes 2/3/4
//   m_ConnectFlag ldrb check @0x1782bc
//   m_Mode ldr @0x1782f8 feeds ClearScores
//   m_RefreshTimer accumulates dt, clamp vs 30.0 @0x178394
//   m_RefreshCount ldr/add#1/str @0x178524
void FruitFactLeaderboard::Update(float dt) {
    FruitFactPage::Update(dt);
}
