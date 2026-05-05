// Analysed: 2026-05-04T00:00
//
// PowerUpShop : HUDControl3d — in-game power-up purchase screen.
//
// Binary vtable @ 0x001e9cb0 (15 slots); class size 0x138.
//
// TODO: <no addr resolved> — PowerUpShop instantiation site not yet RE'd; see
// tmp/re-powerupshop.md. Likely a HUD method that listens for a "show shop"
// event during gameplay.

#include "screens/PowerUpShop.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "game/PowerUpManager.h"
#include "game/FruitSaveData.h"
#include "entities/Fruit.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/Random.h"
#include "util/Delegate.h"
#include "Game.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <list>

// Binary @ 0x_GLOBAL__I_PowerUpShop_cpp:
// File-static SmartPtr<Texture> singletons, nulled by UnLoadContent.
// LoadContent is empty; textures are resolved on first use via TextureManager.
static SmartPtr<Mortar::Texture> g_BuyBg;
static SmartPtr<Mortar::Texture> g_Arrow;
static SmartPtr<Mortar::Texture> g_FruitIcons[3];

// Zero-vector and unit-vector constants (from _GLOBAL__I_PowerUpShop_cpp).
// Binary initialises these as file-static Vec3 (confirmed by GOT references in Update/Release).
static const Vec3 g_Origin(0.0f, 0.0f, 0.0f);
static const Vec3 g_OneVec(1.0f, 1.0f, 1.0f);

// White Colour (4-byte RGBA packed; used by Draw for text and quad tint).
static const Colour g_White(255, 255, 255, 255);

// ============================================================
// File-static helper wrappers (match Ghidra mis-named *_SpeedCtrl variants
// which are actually PowerUpShop.cpp file-statics).
// ============================================================

// Binary @ UploadMatrices_SpeedCtrl
static void UploadMatrices() {
    MatrixManager::GetInstance().UploadModelViewOnly();
}

// Binary @ SetMatrix_SpeedCtrl — world stack at game+0x1094
static void SetMatrix(const Matrix44& mat) {
    MatrixManager::GetInstance().GetWorldStack().SetCurrentMatrix(mat);
}

// Binary @ MakeColour_SpeedCtrl — packs RGBA into Colour (alpha=0xff always)
static Colour MakeColour(uint8_t r, uint8_t g, uint8_t b) {
    return Colour(r, g, b, 0xff);
}

// ============================================================
// PowerUpShop ctor — binary @ 0x00155cac (C1) / 0x00155ce4 (C2)
// ============================================================
PowerUpShop::PowerUpShop()
    : m_SelectedIndex(0)
    , m_SinIdx(0)
    , m_PulseScale(1.0f)
    , m_LastSelectedIndex(-1)
    , m_FruitScale(1.0f)
    , m_BuyButton(NULL)
    , m_BuyTriggered(0)
    , m_BuyButtonState(0)
    , m_PurchasedCount(0)
{
    // Binary step 4: m_bNoDestructor = 1 (so HUD::RemoveControl won't delete us).
    m_bNoDestructor = 1;

    memset(_pad98, 0, sizeof(_pad98));
    memset(_pad9e, 0, sizeof(_pad9e));
    memset(_pad132, 0, sizeof(_pad132));
    m_BuyText[0] = '\0';
}

// ============================================================
// dtor — binary @ 0x001569f4 (D2 non-deleting) / 0x00156994 (D0 deleting)
// Order matches binary: Release(), destruct vectors (Vec3 first, PowerUp* second),
// then chain HUDControl3d dtor.
// ============================================================
PowerUpShop::~PowerUpShop() {
    Release();
    m_SlotLayout.clear();
    m_PurchasablePowerUps.clear();
    // HUDControl3d base dtor called implicitly by C++ chain.
}

// ============================================================
// LoadContent @ 0x00155b50 — empty body (file-static SmartPtrs resolve on first use).
// ============================================================
void PowerUpShop::LoadContent() {
    // Binary @ 0x00155b50: empty.
}

// ============================================================
// UnLoadContent @ 0x00155dc4 — null three file-static SmartPtr<Texture>s.
// ============================================================
void PowerUpShop::UnLoadContent() {
    // Binary @ 0x00155dc4:
    g_BuyBg.SetNull();
    g_Arrow.SetNull();
    g_FruitIcons[0].SetNull();
    g_FruitIcons[1].SetNull();
    g_FruitIcons[2].SetNull();
}

// ============================================================
// Init @ 0x00156b08 (vtable slot 2)
// ============================================================
void PowerUpShop::Init() {
    // Binary @ 0x00156b08:
    // Zero scalars.
    m_PurchasedCount = 0;
    m_SinIdx         = 0;
    m_SelectedIndex  = 0;

    m_LastSelectedIndex = -1;
    m_PulseScale        = 1.0f;
    m_FruitScale        = 1.0f;

    // Load buy-background texture into m_SecondaryTex (+0x78).
    // Binary: SetPtr from GOT[buybg_texture]; port uses TextureManager.
    // TODO: 0x00156b08 — confirm exact buy-bg texture filename from binary DAT string
    // (binary references GOT-loaded SmartPtr; filename not resolved by RE pass yet).
    // g_BuyBg texture is loaded on demand; assign to m_SecondaryTex when available.
    if (!g_BuyBg.IsValid()) {
        g_BuyBg = Mortar::TextureManager::LoadLocalisedTexture("buy_bg.tex");
    }
    m_SecondaryTex = g_BuyBg.Get() ? g_BuyBg.Get()->m_TexId : 0;

    // Binary: reads texture w/h via vtable slots *(vtbl+0x14)/(+0x18) GetWidth/GetHeight.
    // Sets pivot = Vector3(w, h, 0) * 1.0.
    // TODO: 0x00156b08 — pivot assignment needs texture dims; stub at (0,0,0) until
    // texture filename resolved.

    m_LayerFlags = 0x80;

    // Iterate PowerUpManager::m_PurchasablePowers (GetFirstPurchasable / GetNextPurchasable
    // are not in the port; walk the list directly — equivalent traversal).
    // Binary @ 0x00156b08: push_back each purchasable; if p->m_bCloned != 0, m_PurchasedCount++.
    PowerUpManager* pum = PowerUpManager::GetInstance();
    std::list<PowerUp*>::iterator it = pum->m_PurchasablePowers.begin();
    std::list<PowerUp*>::iterator end = pum->m_PurchasablePowers.end();
    for (; it != end; ++it) {
        PowerUp* p = *it;
        m_PurchasablePowerUps.push_back(p);
        if (p->m_bCloned != 0) {
            m_PurchasedCount++;
        }
    }

    // Build m_SlotLayout: count entries, x = i*(250/(count-1)) - 192, y=24, z=1.
    // Binary @ 0x00156b08 centred horizontal layout.
    int count = (int)m_PurchasablePowerUps.size();
    m_SlotLayout.clear();
    for (int i = 0; i < count; ++i) {
        float x;
        if (count > 1) {
            x = (float)i * (250.0f / (float)(count - 1)) - 192.0f;
        } else {
            x = 0.0f;
        }
        m_SlotLayout.push_back(Vec3(x, 24.0f, 1.0f));
    }

    m_BuyButtonState = 0;
    SetBuyButtonState();

    // Binary: OS_SPrintf(m_BuyText, 128, "YOU HAVE %i COINS TO USE!", player.m_Coins)
    // player.m_Coins lives at game+pSaveData+0x20.
    Game* game = Game::GetInstance();
    int coins = 0;
    if (game && game->pSaveData) {
        coins = game->pSaveData->m_Coins;
    }
    snprintf(m_BuyText, sizeof(m_BuyText), "YOU HAVE %i COINS TO USE!", coins);
}

// ============================================================
// Release @ 0x0015685c (vtable slot 3)
// ============================================================
void PowerUpShop::Release() {
    // Binary @ 0x0015685c: tear down dynamic buy-fruit MenuButton.
    if (m_BuyButton != NULL) {
        // MenuButton stores Fruit* at +0x134 (m_pFruitPiece).
        Fruit* fruit = m_BuyButton->m_pFruitPiece;
        if (fruit != NULL) {
            // Binary: sets Fruit::m_bSliced=1 and zeroes velocity/spin vectors.
            fruit->m_bSliced = true;
            fruit->vel    = g_Origin;
            // m_AngularVel is not in Mortar::Entity base; Fruit stores RotVel in m_RotVel1/m_RotVel2.
            // Binary zeroes multiple vel-like fields. Port zeroes what's accessible.
            fruit->m_RotVel1 = g_Origin;
            fruit->m_RotVel2 = g_Origin;
        }

        // Binary: MenuButton.m_bNoDestructor = 1; replace m_RemoveCallback with empty delegate.
        m_BuyButton->m_bNoDestructor = 1;
        m_BuyButton->m_RemoveCallback = Mortar::Delegate<void(HUDControl*)>();

        // Binary: HUD::RemoveControl(*Game.HUD, m_BuyButton); then delete.
        Game* game = Game::GetInstance();
        if (game && game->hud) {
            game->hud->RemoveControl(m_BuyButton);
        }
        delete m_BuyButton;
        m_BuyButton = NULL;
    }
}

// ============================================================
// Reset @ 0x00155b54 (vtable slot 4) — empty body
// ============================================================
void PowerUpShop::Reset() {
    // Binary @ 0x00155b54: returns immediately.
}

// ============================================================
// PreDraw @ 0x00155b58 (vtable slot 6) — identity pass-through
// ============================================================
void PowerUpShop::PreDraw(const Vec3& hudScale) {
    // Binary @ 0x00155b58: returns param_1 unchanged (no-op).
    (void)hudScale;
}

// ============================================================
// Draw @ 0x00155e08 (vtable slot 7)
// ============================================================
void PowerUpShop::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Binary @ 0x00155e08:
    // Step 1: scale + translate world matrix, upload.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Push();

    Matrix44 scaleMat;
    scaleMat.Identity();
    scaleMat.ApplyScale(hudScale.x, hudScale.y, hudScale.z);

    Matrix44 transMat;
    transMat.Identity();
    transMat.GlobalTranslate44(Vec3(pos.x + 480.0f, pos.y + 320.0f, pos.z));

    // Combine: translate * scale (transMat first, then scale columns)
    Matrix44 combined = transMat * scaleMat;
    SetMatrix(combined);
    UploadMatrices();

    // Step 2: draw buy background quad via m_SecondaryTex.
    // TODO: 0x00155e08 — Mesh::DrawQuadUnCached call requires Mesh utility not yet
    // extracted as a standalone draw helper. Skipping quad draw until Mesh draw
    // helper ported (binary calls DrawQuadUnCached with a Colour*).

    // Step 3: draw m_BuyText via font.
    // Binary: Font::DrawString at (75 + ScreenY, 0, 20.5), anchor=3 (center+top).
    Game* game = Game::GetInstance();
    if (game && game->pFontMain.IsValid()) {
        // Binary: text Y = 75 + HUD bottomY (param_1[3]). Port uses a fixed offset
        // from the centered coordinate: pos.y is the control's Y in centered space.
        Vec3 textPos(pos.x + 75.0f, 0.0f, 20.5f);
        game->pFontMain->DrawString(1.0f, 1.0f, 20.5f, m_BuyText, textPos,
                                    g_White, Mortar::FONT_ALIGN_CENTER | Mortar::FONT_ALIGN_MIDDLE);
    }

    // Step 4: draw each slot icon.
    int count = (int)m_SlotLayout.size();
    for (int i = 0; i < count; ++i) {
        PowerUp* p = m_PurchasablePowerUps[i];
        // Binary: skip if m_pPurchaseInfo (+0x94) is NULL.
        if (p->m_pPurchaseInfo == NULL) {
            continue;
        }

        // TODO: 0x00155e08 — PurchaseInfo::GetTexture / GetGreyTexture / GetInUseTexture
        // not yet ported (PurchaseInfo is partially stubbed in PowerUp.h).
        // Icon draw, progress bars, and description text depend on these accessors.
        // Skip icon rendering until PurchaseInfo is fully ported.

        // Slot layout interpolation (z lerps toward 1.25 for selected, 1.0 for idle).
        // Slot world pos = pos + slot. Scale = slot.z * 64.0.
        // Description/title labels at selected slot (draw at -84 and -30 Y offset, scale 16).
        // Cost/timer label at slot bottom (scale 17, anchor 0xf).
        // All TODOs deferred to PurchaseInfo RE follow-up.
    }

    mm.GetWorldStack().Pop();
    UploadMatrices();
}

// ============================================================
// Update @ 0x00156398 (vtable slot 10)
// ============================================================
void PowerUpShop::Update(float dt) {
    // Binary @ 0x00156398:

    // Step 1: advance sin-phase and compute pulse scale.
    // m_SinIdx = max(0, m_SinIdx + dt * 32760 * 2.5)
    // m_PulseScale = (SinIdx >= 0) ? 1 + 0.1 * sin(sinAngle) : 1.0
    {
        int newIdx = (int)m_SinIdx + (int)(dt * 32760.0f * 2.5f);
        if (newIdx < 0) newIdx = 0;
        m_SinIdx = (uint16_t)(newIdx & 0xffff);

        float sinVal = sinf((float)m_SinIdx * (3.14159265f * 2.0f / 65536.0f));
        if (sinVal >= 0.0f) {
            m_PulseScale = 1.0f + 0.1f * sinVal;
        } else {
            m_PulseScale = 1.0f;
        }
    }

    // Step 2: per-slot z lerp and touch-rect test.
    Game* game = Game::GetInstance();
    for (int i = 0; i < (int)m_SlotLayout.size(); ++i) {
        Vec3& slot = m_SlotLayout[i];
        float targetZ = (i == m_SelectedIndex) ? 1.25f : 1.0f;
        slot.z += (targetZ - slot.z) * 0.15f;

        // Binary: touch hit-test against pos + slot ± 32.
        // TODO: 0x00156398 — touch read from game+0x9e/0x90/0x94 (InputManager last-touch
        // slot). Port InputManager touch API not fully wired; skip hit-test until done.
        (void)game;
    }

    // Step 3: if m_BuyButton == NULL and m_BuyTriggered == 0, create buy button.
    if (m_BuyButton == NULL && m_BuyTriggered == 0) {
        // Binary: first-time fruit-type lookup via cxa_guard (3 fruit types from DAT strings).
        // TODO: 0x00156398 — three buy-button fruit type strings (DAT_001566bc / DAT_001566c0)
        // not resolved by RE pass; use FruitType(-1) sentinel until confirmed.
        static bool s_fruitTypesInit = false;
        static int s_fruitType0 = -1;
        static int s_fruitType1 = -1;
        static int s_fruitType2 = -1;
        if (!s_fruitTypesInit) {
            s_fruitTypesInit = true;
            // TODO: 0x00156398 — resolve binary DAT_001566bc / DAT_001566c0 fruit name strings
            // and call Fruit::FruitType(<name>, false) for each of the three buy-button fruits.
            // s_fruitType0 = Fruit::FruitType("kiwi", false);
            // s_fruitType1 = Fruit::FruitType("watermelon", false);
            // s_fruitType2 = Fruit::FruitType("lemon", false);
        }

        // Pick fruit type based on m_BuyButtonState (0/1/2 indexing into g_FruitIcons).
        int fruitType = s_fruitType0;
        if (m_BuyButtonState == 1) fruitType = s_fruitType1;
        if (m_BuyButtonState == 2) fruitType = s_fruitType2;

        // Spawn position: origin + Vector3(160.8, -6.0, 0.0).
        Vec3 spawnPos = g_Origin + Vec3(160.8f, -6.0f, 0.0f);

        // Build delegates (Delegate0<void> for ButtonSliced, Delegate1 for ButtonDeleted).
        // TODO: 0x00156398 — Mortar::Delegate binding to member function; port uses
        // Mortar::Delegate<void()> and Mortar::Delegate<void(HUDControl*)> bound via
        // MakeDelegate / callee pattern. Stub with empty delegates until callee helper ported.
        Mortar::Delegate<void()>           slicedCb;
        Mortar::Delegate<void(HUDControl*)> deletedCb;

        // TODO: 0x00156398 — MenuButton constructor signature: (tex, &spawnPos, &delegate0,
        // fruitType, &origin, &delegate1). Port MenuButton init takes separate Init() call.
        // Skipping buy-button construction until delegate binding resolved.
        // Binary sequence:
        //   m_BuyButton = new MenuButton(...)
        //   m_BuyButton->Init()
        //   m_BuyButton->vel.x = 0
        //   MenuButton.field_0x123 = 0
        //   HUD::AddControl(game->hud, m_BuyButton, false)
        //   Rand32(524287); Rand32(2)
        //   Fruit angular vel *= 0.85 on x and y
        //   Fruit::RotateFacingUp(fruit, false, Vec3(0,1,0))
        (void)fruitType;
        (void)spawnPos;
        (void)slicedCb;
        (void)deletedCb;
    } else if (m_BuyButton != NULL) {
        // Step 4: update existing buy button.

        // Binary: move button to fixed position + set fruit vel.x = m_PulseScale.
        m_BuyButton->pos = g_Origin + Vec3(160.8f, -6.0f, 0.0f);
        if (m_BuyButton->m_pFruitPiece != NULL) {
            m_BuyButton->m_pFruitPiece->vel.x = m_PulseScale;
        }

        // Binary: if last selected index changed and fruit alive and not sliced,
        // get push vector from origin, set fruit vel, trigger re-spawn.
        if ((m_LastSelectedIndex != m_SelectedIndex) &&
            (m_BuyTriggered == 0) &&
            (m_BuyButton->m_pFruitPiece != NULL) &&
            !m_BuyButton->m_pFruitPiece->Sliced()) {

            // Binary: set Fruit vel = pushVec (origin), field_0x123 = 0, m_BuyTriggered = 1.
            m_BuyButton->m_pFruitPiece->vel = g_Origin;
            m_BuyButton->m_bEnabled = 0;    // field_0x123 -> m_bEnabled port analogue
            m_BuyTriggered = 1;
        }
    }

    m_LastSelectedIndex = m_SelectedIndex;
}

// ============================================================
// SetBuyButtonState @ 0x00155c4c (non-virtual)
// ============================================================
void PowerUpShop::SetBuyButtonState() {
    // Binary @ 0x00155c4c:
    if (m_PurchasablePowerUps.empty()) {
        return;
    }
    PowerUp* p = m_PurchasablePowerUps[m_SelectedIndex];
    if (p == NULL) {
        return;
    }

    if (p->m_bCloned == 0) {
        // Not yet active — check affordability and purchase cap.
        Game* game = Game::GetInstance();
        int coins = 0;
        if (game && game->pSaveData) {
            coins = game->pSaveData->m_Coins;
        }
        int cost = p->m_pPurchaseInfo ? p->m_pPurchaseInfo->m_Cost : 0;
        if (cost <= coins && m_PurchasedCount < 3) {
            m_BuyButtonState = 0;  // enabled
        } else {
            m_BuyButtonState = 1;  // greyed
        }
    } else {
        m_BuyButtonState = 2;  // already active
    }
}

// ============================================================
// ButtonSliced @ 0x00155b5c (non-virtual; bound as Delegate0<void>)
// ============================================================
void PowerUpShop::ButtonSliced(float pushScalar) {
    // Binary @ 0x00155bf0:
    if (m_BuyTriggered == 0 && m_BuyButtonState == 0) {
        if (m_PurchasablePowerUps.empty()) {
            return;
        }
        // hash = m_PurchasablePowerUps[m_SelectedIndex]->m_TypeId (m_NameHash in port)
        PowerUp* selected = m_PurchasablePowerUps[m_SelectedIndex];
        uint32_t hash = selected->m_NameHash;

        // Binary @ 0x00155bf0: ActivatePower(hash, &origin, &pushScalar)
        Vec3 origin = g_Origin;
        PowerUpManager* pum = PowerUpManager::GetInstance();
        PowerUp* singleActive = pum->ActivatePower(hash, &origin, &pushScalar);

        if (singleActive != NULL) {
            m_BuyButtonState = 2;
            m_PurchasedCount++;
            m_PurchasablePowerUps[m_SelectedIndex] = singleActive;
        }

        // Refresh coin text.
        Game* game = Game::GetInstance();
        int coins = 0;
        if (game && game->pSaveData) {
            coins = game->pSaveData->m_Coins;
        }
        snprintf(m_BuyText, sizeof(m_BuyText), "YOU HAVE %i COINS TO USE!", coins);

    } else if (m_BuyTriggered != 0) {
        // Safety reset: zero fruit velocity for despawning button-fruit.
        if (m_BuyButton != NULL && m_BuyButton->m_pFruitPiece != NULL) {
            Fruit* fruit = m_BuyButton->m_pFruitPiece;
            if (!fruit->Sliced()) {
                fruit->vel      = g_Origin;
                fruit->m_RotVel1 = g_Origin;
                fruit->m_RotVel2 = g_Origin;
            }
        }
    }
}

// ============================================================
// ButtonDeleted @ 0x00156aac (non-virtual; bound as Delegate1<void,HUDControl*>)
// ============================================================
void PowerUpShop::ButtonDeleted(HUDControl* deletedCtrl) {
    // Binary @ 0x00156aac:
    if (deletedCtrl != m_BuyButton) {
        return;
    }
    if (m_BuyTriggered != 0 && m_BuyButton->m_pFruitPiece != NULL) {
        Fruit* fruit = m_BuyButton->m_pFruitPiece;
        // Binary: m_AngularVel.y = 0, m_Spin to zero, vel.x = vel.y = -10.0
        // (0xc1200000 = -10.0f). Port sets RotVel2.y = 0 as best analogue.
        fruit->m_RotVel2.y = 0.0f;
        // vel.x = vel.y = -10.0f (nudge falling fruit)
        fruit->vel.x = -10.0f;  // 0xc1200000
        fruit->vel.y = -10.0f;  // 0xc1200000
        // Binary literal 0xc3f00000 = -480.0f: kick fruit piece off-screen.
        // vstr [r3,#0xbc] writes fruit->m_SecondPos.y = -480.0f.
        // TODO: 0x146824 -- bl helper + ldmia/stmia 3-float position-copy block also present in binary.
        fruit->m_SecondPos.y = -480.0f;  // 0xc3f00000
    }
    m_BuyTriggered = 0;
    m_BuyButton    = NULL;
}
