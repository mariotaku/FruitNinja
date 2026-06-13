// FruitFactLeaderboard -- v1.6.1 leaderboard fact page.
// Binary refs: ctor 0x00176980.

#include "FruitFactLeaderboard.h"
#include "hud/FruitFactPageControl.h"
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
FruitFactLeaderboard::FruitFactLeaderboard(FruitFactPageControl* pCtrl, bool param2)
    : FruitFactPage(pCtrl)
    , m_field98(0)
    , m_field9C(0)
    , m_fieldA0(0)
    , m_fieldA4(0)
    , m_fieldA8(0)
    , m_fieldAC(0.0f)
    , m_fieldB0(0)
    , m_fieldB4(0.0f)
    , m_ModeSelector(param2 ? 3u : 0u)
    , m_DisplayMode(1)
    , m_Row0()
    , m_Row1()
    , m_Row2()
{
    _padB1[0] = 0; _padB1[1] = 0; _padB1[2] = 0;
    _padC0[0] = 0; _padC0[1] = 0; _padC0[2] = 0; _padC0[3] = 0;
    _padC0[4] = 0; _padC0[5] = 0; _padC0[6] = 0; _padC0[7] = 0;

    LoadContent();

    // Post-LoadContent zero-fills for own state fields (binary @ 0x00176980)
    m_fieldA4 = 0;
    m_fieldB4 = 0.0f;
    m_fieldB0 = 0;
    m_fieldAC = 0.0f;
    m_fieldA8 = 0;

    // Title: LSTR 0x7b if global (param2), else LSTR 0x363 (friends)
    const char* title = Mortar::GETSTRING(
        param2 ? LSTR_LEADERBOARD_GLOBAL : LSTR_LEADERBOARD_FRIENDS, 0);
    CreateTitleTextControl(title);

    CreateSenseisHead(68.0f);

    // Divider 0: pos.x=-7.5, size=Vec3(276,53,0), field[+0xE8]=0.48
    {
        // TODO: 0x00176d30 -- resolve divider0 texture name from GOT string pool
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
        // TODO: 0x00176d30 -- resolve divider1 texture name + size vec from GOT string pool
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
        // TODO: 0x00176d30 -- resolve divider2 texture name + size vec from GOT string pool
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
    // TODO: 0x00176980 -- read difficulty byte from settings global offset +4; resolve global addr
    {
        uint8_t diff = 0;
        if (diff != 2) {
            LeaderboardManager::GetInstance()->ClearScores(
                static_cast<int>(diff), static_cast<int>(m_ModeSelector));
        }
    }

    // Online gate: provider offline or friends not loaded -> local-only (m_DisplayMode=1).
    if (!Mortar::IsProviderOnline() || !Mortar::AreFriendsLoaded()) {
        m_DisplayMode = 1;
    } else {
        m_DisplayMode = 2;
    }

    m_field98 = 0;
    m_field9C = 0;
    m_fieldA0 = 0;
}

FruitFactLeaderboard::~FruitFactLeaderboard() {
}

// TODO: 0x00176980 -- row-population Update: populate m_Row0/1/2 via FNHighscore param-ctor
//   @ 0x00137e48, called via PTR_FNHighscore_002d6560; part of the display-refresh method.
void FruitFactLeaderboard::Update(float dt) {
    FruitFactPage::Update(dt);
}
