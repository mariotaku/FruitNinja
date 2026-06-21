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
#include "debug/Logger.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "game/PowerUpManager.h"
#include "game/FruitSaveData.h"
#include "entities/Fruit.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/Random.h"
#include "screens/PurchaseInfo.h"
#include "util/Delegate.h"
#include "Game.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <list>
#include "game/GameWork.h"

// Binary @ 0x_GLOBAL__I_PowerUpShop_cpp:
// File-static Mortar::SmartPtr<Texture> singletons, nulled by UnLoadContent.
// LoadContent is empty; textures are resolved on first use via TextureManager.
static Mortar::SmartPtr<Mortar::Texture> g_BuyBg;
static Mortar::SmartPtr<Mortar::Texture> g_Arrow;
static Mortar::SmartPtr<Mortar::Texture> g_FruitIcons[3];

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
// UnLoadContent @ 0x00155dc4 — null three file-static Mortar::SmartPtr<Texture>s.
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

    // Defunct: binary @ 0x00156b08 — m_Texture(+0x74) SmartPtr static at .bss
    // 0x00231288 never assigned. Buy-bg path is dead in shipped binary. Init's
    // GetWidth/GetHeight calls only fire when IsValid().
    // m_Texture is default-constructed (null); pivot assignment only executes when valid.

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

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
    // Coin balance lives in game_work.m_CoinsBalance (+0x20), not FruitSaveData.
    snprintf(m_BuyText, sizeof(m_BuyText), "YOU HAVE %i COINS TO USE!", game_work.m_CoinsBalance);
}

// ============================================================
// Release @ 0x0015685c (vtable slot 3)
// ============================================================
void PowerUpShop::Release() {
    // Binary @ 0x0015685c: tear down dynamic buy-fruit MenuButton.
    if (m_BuyButton != NULL) {
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        if (fruit != NULL) {
            // Binary: sets Fruit::m_bSliced=1 and zeroes velocity/spin vectors.
            LOG_INFO("FRUIT", "m_bSliced=1 set on entity=%p pos=(%.1f,%.1f) type=%d (in PowerUpShop teardown)",
                     static_cast<void*>(fruit), fruit->pos.x, fruit->pos.y, (int)fruit->m_FruitType);
            fruit->m_bSliced = true;
            fruit->vel    = g_Origin;
            // m_AngularVel is not in Mortar::Entity base; Fruit stores RotVel in m_RotVel1/m_RotVel2.
            // Binary zeroes multiple vel-like fields. Port zeroes what's accessible.
            fruit->m_RotVel1 = g_Origin;
            fruit->m_RotVel2 = g_Origin;
        }

        // Binary: MenuButton.m_bNoDestructor = 1; replace m_RemoveCallback with empty delegate.
        m_BuyButton->m_bNoDestructor = 1;
        m_BuyButton->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>();

        // Binary: HUD::RemoveControl(*Game.HUD, m_BuyButton); then delete.
        Game* game = Game::GetInstance();
        if (game && game_work.mHud) {
            game_work.mHud->RemoveControl(m_BuyButton);
        }
        m_BuyButton->Release();   // binary's vtable Release before delete
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

    // Step 2: draw buy background quad via g_BuyBg.
    // Binary @ 0x00155e08: calls g_BuyBg->Set(), Mesh::DrawQuadUnCached(Colour*, fx),
    // g_BuyBg->UnSet(). Binary @ 0x00194180: default 1x1 quad with current world matrix.
    if (g_BuyBg.IsValid()) {
        g_BuyBg->Set();
        Mortar::Mesh::DrawQuadUnCached(g_White, NULL);
        g_BuyBg->UnSet();
    }

    // Step 3: draw m_BuyText (header label) via font.
    // Binary @ 0x00155e08: pos = (shop.x, shop.y + 75), size 20, anchor 3, white.
    if (game_work.pFontMain.IsValid()) {
        Vec3 textPos(pos.x, pos.y + 75.0f, 0.0f);
        game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f, m_BuyText, textPos,
                                        g_White, 3);
    }

    // Step 4: draw each slot icon.
    int count = (int)m_SlotLayout.size();
    for (int i = 0; i < count; ++i) {
        PowerUp* p = m_PurchasablePowerUps[i];
        // Binary: skip if m_pPurchaseInfo (+0x94) is NULL.
        if (p->m_pPurchaseInfo == NULL) {
            continue;
        }

        PurchaseInfo* pi = p->m_pPurchaseInfo;

        // Determine affordability for label colour and inactive texture selection.
        // Coin balance lives in game_work.m_CoinsBalance (+0x20), not FruitSaveData.
        int coins = game_work.m_CoinsBalance;
        bool affordable = (pi->m_Cost <= coins && m_PurchasedCount < 3
                           && game_work.gameMode < 3);

        Vec3 slot = m_SlotLayout[i];
        float barScale = slot.z;
        float iconScale = barScale * 64.0f;

        // Slot translate+scale matrix (used for both quad bg and TriStrip bar).
        mm.GetWorldStack().Push();
        Matrix44 slotTrans;
        slotTrans.Identity();
        slotTrans.GlobalTranslate44(Vec3(pos.x + slot.x + 480.0f,
                                        pos.y + slot.y + 320.0f, pos.z + slot.z));
        Matrix44 slotScaleMat;
        slotScaleMat.Identity();
        slotScaleMat.ApplyScale(iconScale, iconScale, 1.0f);
        SetMatrix(slotTrans * slotScaleMat);
        UploadMatrices();

        if (p->m_bCloned != 0) {
            // Active branch: grey bg quad, then TriStrip progress bar.
            Mortar::ReloadableTexture& greyTex = pi->GetGreyTexture();
            if (greyTex.IsLoaded()) {
                greyTex.Set();
                Mortar::Mesh::DrawQuadUnCached(g_White, NULL);
                greyTex.UnSet();
            }

            Mortar::ReloadableTexture& barTex = pi->GetInUseTexture();
            if (barTex.IsLoaded() && pi->m_MaxUses > 0) {
                float fillFrac = ((float)pi->m_CurrentUses * 0.75f / (float)pi->m_MaxUses)
                                 + 0.125f;
                if (fillFrac > 1.0f) fillFrac = 1.0f;
                if (fillFrac < 0.0f) fillFrac = 0.0f;

                // Restore slot matrix (DrawQuadUnCachedDefault resets the world stack).
                SetMatrix(slotTrans * slotScaleMat);
                UploadMatrices();

                // 4-vertex TriStrip bar in unit space (-0.5..0.5 icon extent).
                // Fill clips the right edge to fillFrac.
                float x0 = -0.5f;
                float x1 = -0.5f + fillFrac;
                float y0 = -0.5f;
                float y1 =  0.5f;
                QUADCUSTOMVERTEX barVerts[4];
                // bottom-left
                barVerts[0].x = x0; barVerts[0].y = y0; barVerts[0].z = 0.0f;
                barVerts[0].nx = 0.0f; barVerts[0].ny = 0.0f; barVerts[0].nz = 1.0f;
                barVerts[0].colour = 0xFFFFFFFFu;
                barVerts[0].u = 0.0f; barVerts[0].v = 1.0f;
                // bottom-right
                barVerts[1].x = x1; barVerts[1].y = y0; barVerts[1].z = 0.0f;
                barVerts[1].nx = 0.0f; barVerts[1].ny = 0.0f; barVerts[1].nz = 1.0f;
                barVerts[1].colour = 0xFFFFFFFFu;
                barVerts[1].u = fillFrac; barVerts[1].v = 1.0f;
                // top-left
                barVerts[2].x = x0; barVerts[2].y = y1; barVerts[2].z = 0.0f;
                barVerts[2].nx = 0.0f; barVerts[2].ny = 0.0f; barVerts[2].nz = 1.0f;
                barVerts[2].colour = 0xFFFFFFFFu;
                barVerts[2].u = 0.0f; barVerts[2].v = 0.0f;
                // top-right
                barVerts[3].x = x1; barVerts[3].y = y1; barVerts[3].z = 0.0f;
                barVerts[3].nx = 0.0f; barVerts[3].ny = 0.0f; barVerts[3].nz = 1.0f;
                barVerts[3].colour = 0xFFFFFFFFu;
                barVerts[3].u = fillFrac; barVerts[3].v = 0.0f;

                barTex.Set();
                TexEnvModulate();  // Set owns tex-env (binary model); port Set() doesn't set it.
                Mortar::Mesh::DrawTriStrip(barVerts, 4, true, NULL);
                barTex.UnSet();
            }
        } else {
            // Inactive branch: affordable or greyed icon quad.
            Mortar::ReloadableTexture* iconTex = affordable
                ? &pi->GetTexture()
                : &pi->GetGreyTexture();
            if (iconTex && iconTex->IsLoaded()) {
                iconTex->Set();
                Mortar::Mesh::DrawQuadUnCached(g_White, NULL);
                iconTex->UnSet();
            }
        }

        mm.GetWorldStack().Pop();
        UploadMatrices();

        // Text labels (drawn in centered coordinate space via active world matrix).
        Vec3 barPos(pos.x + slot.x, pos.y + slot.y, pos.z);

        // Cost-or-READY label: pos (barPos.x, barPos.y - 32*barScale), size 17, anchor 0xF.
        if (game_work.pFontMain.IsValid()) {
            char costBuf[32];
            Colour labelColour;
            if (p->m_bCloned != 0) {
                // Active: "READY"
                snprintf(costBuf, sizeof(costBuf), "READY");
                labelColour = MakeColour(0xFF, 0xFF, 0xFF);
            } else if (affordable) {
                snprintf(costBuf, sizeof(costBuf), "%i", pi->m_Cost);
                labelColour = MakeColour(0x8B, 0x4F, 0x22);
            } else {
                snprintf(costBuf, sizeof(costBuf), "%i", pi->m_Cost);
                labelColour = MakeColour(0x80, 0x80, 0x80);
            }
            Vec3 labelPos(barPos.x, barPos.y + (-32.0f * barScale), barPos.z);
            game_work.pFontMain->DrawString(17.0f, 1.0f, 0.0f,
                                            costBuf, labelPos, labelColour, 0xF);
        }

        // Selected-slot title + description.
        if (i == m_SelectedIndex && game_work.pFontMain.IsValid()) {
            float sx = pos.x + slot.x;
            float sy = pos.y + slot.y;
            Vec3 titlePos(sx - 224.0f, sy - 30.0f, pos.z);
            Colour titleColour = MakeColour(0xD1, 0x25, 0x0B);
            game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                                            pi->m_DisplayName, titlePos, titleColour, 0xF);

            Vec3 descPos(sx - 224.0f, sy - 54.0f, pos.z);
            Colour descColour = MakeColour(0x74, 0x5D, 0x3B);
            game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                                            pi->m_Description, descPos, descColour, 0xF);
        }
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

        // ASM-verified: 2026-05-18 binary @ 0x00156398 (re-analyst)
        // Reads GameContext aliased fields:
        //   +0x9e  uint8_t  m_bPointerActive -- set by PointerDownCallback @ 0x00168e24,
        //                                       cleared by PointerUpCallback @ 0x00168e48.
        //   +0x90  float    worldPos.x       -- aliased with light direction; PointerMoveCallback
        //                                       action 0x74 writes the centered-ortho X.
        //   +0x94  float    worldPos.y       -- aliased with light direction; action 0x75 writes Y.
        //  Hit zone = (slot ± 32) on both axes. On hit: m_SelectedIndex = i; SetBuyButtonState().
        if (game && game_work.m_bPointerActive) {
            const float px = game_work.worldPos.x;
            const float py = game_work.worldPos.y;
            const float HALF = 32.0f;  // DAT_001566a0
            Vec3 worldPt = pos + slot;
            if (px > worldPt.x - HALF && px < worldPt.x + HALF &&
                py > worldPt.y - HALF && py < worldPt.y + HALF) {
                m_SelectedIndex = i;
                SetBuyButtonState();
            }
        }
    }

    // Step 3: if m_BuyButton == NULL and m_BuyTriggered == 0, create buy button.
    if (m_BuyButton == NULL && m_BuyTriggered == 0) {
        // Binary: first-time fruit-type lookup via cxa_guard (3 fruit types from DAT strings).
        // DAT_001566bc = "banana" @ 0x001b9a70; DAT_001566c0 = "banana_locked" @ 0x001bbf1c.
        // Binary calls FruitType("banana_locked", false) twice for state 1 and 2 -- intentional dup;
        // preserve verbatim per binary fidelity policy.
        static int s_fruitTypeCache[3] = { -1, -1, -1 };
        static bool s_fruitTypeCacheInit = false;
        if (!s_fruitTypeCacheInit) {
            s_fruitTypeCacheInit = true;
            s_fruitTypeCache[0] = Fruit::FruitType("banana",        false);  // state 0 = enabled
            s_fruitTypeCache[1] = Fruit::FruitType("banana_locked", false);  // state 1 = greyed
            s_fruitTypeCache[2] = Fruit::FruitType("banana_locked", false);  // state 2 = active (intentional dup per binary @ DAT_001566c0)
        }

        // Pick fruit type based on m_BuyButtonState (0/1/2).
        int fruitType = s_fruitTypeCache[m_BuyButtonState];

        // Spawn position: origin + Vector3(160.8, -6.0, 0.0).
        Vec3 spawnPos = g_Origin + Vec3(160.8f, -6.0f, 0.0f);

        // Build slicedCb: binary @ 0x00156398 binds PowerUpShop::ButtonSliced as
        // Delegate0<void> via QCallee.
        Mortar::Delegate0<void> slicedCb =
            Mortar::Delegate0<void>::QCallee(this, &PowerUpShop::ButtonSliced);

        // Build removeCb: binary binds PowerUpShop::ButtonDeleted as
        // Delegate1<void, HUDControl*> via QCallee, wired to m_RemoveCallback.
        Mortar::Delegate1<void, HUDControl*> removeCb =
            Mortar::Delegate1<void, HUDControl*>::QCallee(
                this, &PowerUpShop::ButtonDeleted);

        // Binary sequence:
        //   m_BuyButton = new MenuButton(NULL, &spawnPos, &slicedCb, fruitType,
        //                                &origin, &deletedCb)
        //   m_BuyButton->Init()          — vtable no-arg Init (calls Reset, no-op)
        //   m_BuyButton->vel.x = 0
        //   m_BuyButton->m_bEnabled = 0  (field_0x123)
        //   HUD::AddControl(game_work.mHud, m_BuyButton, false)
        //   Rand32(524287); Rand32(2)
        //   Fruit angular vel *= 0.85 on x and y
        //   Fruit::RotateFacingUp(fruit, false, Vec3(0,1,0))
        Vec3 restPos = g_Origin;
        m_BuyButton = new MenuButton(static_cast<Mortar::SmartPtr<Mortar::Texture>*>(NULL),
                                     &spawnPos, &slicedCb,
                                     fruitType, &restPos, &removeCb);
        m_BuyButton->Init();
        // Binary: m_BuyButton->vel.x = 0 (vel field not mapped; fruit piece vel zeroed below)
#if !defined(__bada__)
        m_BuyButton->m_bEnabled = 0;
#endif

        if (game_work.mHud) {
            game_work.mHud->AddControl(m_BuyButton, false);
        }

        Math::g_Random.Rand32(524287);
        Math::g_Random.Rand32(2);

        if (m_BuyButton->m_pTrackedFruit != NULL) {
            m_BuyButton->m_pTrackedFruit->m_RotVel1.x *= 0.85f;
            m_BuyButton->m_pTrackedFruit->m_RotVel1.y *= 0.85f;
            // ASM-verified: 2026-05-20 binary @ 0x00156398 — RotateFacingUp(false, (0,1,0)).
            m_BuyButton->m_pTrackedFruit->RotateFacingUp(false, Vec3(0.0f, 1.0f, 0.0f));
        }
    } else if (m_BuyButton != NULL) {
        // Step 4: update existing buy button.

        // Binary: move button to fixed position + set fruit vel.x = m_PulseScale.
        m_BuyButton->pos = g_Origin + Vec3(160.8f, -6.0f, 0.0f);
        if (m_BuyButton->m_pTrackedFruit != NULL) {
            m_BuyButton->m_pTrackedFruit->vel.x = m_PulseScale;
        }

        // Binary: if last selected index changed and fruit alive and not sliced,
        // get push vector from origin, set fruit vel, trigger re-spawn.
        if ((m_LastSelectedIndex != m_SelectedIndex) &&
            (m_BuyTriggered == 0) &&
            (m_BuyButton->m_pTrackedFruit != NULL) &&
            !m_BuyButton->m_pTrackedFruit->Sliced()) {

            // Binary: set Fruit vel = pushVec (origin), field_0x123 = 0, m_BuyTriggered = 1.
            m_BuyButton->m_pTrackedFruit->vel = g_Origin;
#if !defined(__bada__)
            m_BuyButton->m_bEnabled = 0;    // field_0x123 -> m_bEnabled port analogue
#endif
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
        // Coin balance lives in game_work.m_CoinsBalance (+0x20), not FruitSaveData.
        int coins = game_work.m_CoinsBalance;
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
// ButtonSliced @ 0x00155b5c (non-virtual; bound as Mortar::Delegate0<void>)
// ============================================================
void PowerUpShop::ButtonSliced() {
    // Binary @ 0x00155b70: split predicate (avoids GCC 16-bit load-fuse on
    // combined &&; binary emits two ldrb.w, one per field).
    if (m_BuyTriggered != 0) {
        // Already-purchased "spit fruit out" branch (binary @ 0x00155b80)
        if (m_BuyButton == NULL) return;
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        if (fruit == NULL) return;
        // 3-float ldmia/stmia copy block: freeze both halves at current position.
        fruit->m_SecondPos = fruit->pos;       // +0xB8 <- +0x10
        fruit->vel         = g_Origin;         // +0x1C
        fruit->m_SecondVel = g_Origin;         // +0xC4
        fruit->m_Gravity   = g_Origin;         // +0xA0
        return;
    }
    if (m_BuyButtonState != 0) return;

    PowerUp* p = m_PurchasablePowerUps[m_SelectedIndex];
    uint32_t hash = p->m_NameHash;

    // Binary: copy g_Origin to stack-local, then ActivatePower(hash, &local, &local.x).
    // r2 AND r3 both point to the same Vec3 -- ActivatePower treats the float* as Vec3*.
    Vec3 localOrigin = g_Origin;
    PowerUpManager* pum = PowerUpManager::GetInstance();
    pum->ActivatePower(hash, localOrigin, reinterpret_cast<float*>(&localOrigin));

    PowerUp* singleActive = PowerUpManager::GetInstance()->GetActiveSingle(hash);
    if (singleActive != NULL) {
        m_PurchasablePowerUps[m_SelectedIndex] = singleActive;
        m_BuyButtonState  = 2;
        m_PurchasedCount += 1;
    }

    // Coin balance lives in game_work.m_CoinsBalance (+0x20), not FruitSaveData.
    snprintf(m_BuyText, sizeof(m_BuyText), "YOU HAVE %i COINS TO USE!", game_work.m_CoinsBalance);
}

// ============================================================
// ButtonDeleted @ 0x00156aac (non-virtual; bound as Mortar::Delegate1<void,HUDControl*>)
// ============================================================
void PowerUpShop::ButtonDeleted(HUDControl* deletedCtrl) {
    // Binary @ 0x00156aac:
    if (deletedCtrl != m_BuyButton) {
        return;
    }
    if (m_BuyTriggered != 0 && m_BuyButton->m_pTrackedFruit != NULL) {
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        // Binary @ 0x00156aac (instruction-traced):
        //   0xc3f00000 = -480.0f (DAT_00156b04); 0xc1200000 = -10.0f.
        // Kick the falling buy-fruit piece off-screen:
        //   vstr s15(-480), [r5,#0xbc] -> m_SecondPos.y (+0xBC)
        //   vstr s15(-480), [r5,#0x14] -> pos.y         (+0x14)
        fruit->m_SecondPos.y = -480.0f;  // +0xBC, 0xc3f00000
        fruit->pos.y         = -480.0f;  // +0x14, 0xc3f00000

        // bl 0x00156824 (NegateVec3_SpeedCtrl) negates the file-static origin
        // Vec3 and stm's the 3 floats into fruit->m_Gravity (+0xA0..0xAB).
        // -g_Origin == (0,0,0); the negate is just how the binary materialises a
        // zero vec from the stored origin constant (same helper used by Release).
        fruit->m_Gravity = g_Origin;     // +0xA0, stm from NegateVec3(origin)

        //   vstr s15(-10), [r3,#0xc8] -> m_SecondVel.y (+0xC8)
        //   vstr s15(-10), [r3,#0x20] -> vel.y         (+0x20)
        fruit->m_SecondVel.y = -10.0f;   // +0xC8, 0xc1200000
        fruit->vel.y         = -10.0f;   // +0x20, 0xc1200000
    }
    m_BuyTriggered = 0;
    m_BuyButton    = NULL;
}
