// Analysed: 2026-05-04T00:00
//
// PowerUpShop : HUDControl3d — in-game power-up purchase screen.
//
// Binary vtable @ 0x002cdd88 (vptr->0x002cdd90) (15 slots); class size 0x138.
//
// ASM-spec v1.6.1 PowerUpShop -- DEAD CODE in this SKU: ctors @0x001a81f0/0x001a81a4 have
//   zero call-site xrefs (EXTERNAL only); LoadContent @0x001a7fdc is empty; s_boardTexture
//   @0x0031650c never loaded; GameModeScreen::CreateControls @0x001819bc doesn't create it;
//   BuyNow @0x00181290 routes to GotoFruitNinjaPage. Per CLAUDE.md linked-but-unreferenced =
//   dead code, do NOT instantiate. Class/vtable/bodies preserved for asm-verify coverage.

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
#include "render/Renderer.h"
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
// Field mapping: g_BuyBg = s_boardTexture; g_Arrow = s_strokeTexture (plausible-but-untraced);
//   g_FruitIcons[0..2] = s_buttonTextures[0..2].
static Mortar::SmartPtr<Mortar::Texture> g_BuyBg;
static Mortar::SmartPtr<Mortar::Texture> g_Arrow;
static Mortar::SmartPtr<Mortar::Texture> g_FruitIcons[3];

// `global.constructors.keyed.to.PowerUpShop.cpp` @0x001a9678 builds NO file-static vectors for
// this class -- it only constructs the COMDAT template statics of the Mortar math types, because
// this TU happens to be their first referencer: _Matrix44<float>::Identity @0x002d9248,
// _Vector3<float>::Zero @0x002d9288 / ::One @0x002d9294 / ::UnitY @0x002d9ed8,
// _Vector2<float>::Zero @0x002d92a0, DefaultBackgroundColour = Colour(0,0,0) (NOT white), and the
// null SmartPtrs above. GOT proof (base 0x002d1130): GOT[0x002d8248]->Zero, GOT[0x002d8704]->One,
// GOT[0x002d7fec]->UnitY. So every "origin" here is `_Vector3<float>::Zero()`, not a local global.
//
// TODO: the engine has no canonical `_Vector3<float>::UnitY` static (only Zero()/One() in
// src/engine/math/_Vector3.h). This file-local stand-in is temporary; fold it into _Vector3 when
// the engine constant lands.
static const _Vector3<float> s_UnitY(0.0f, 1.0f, 0.0f);

// TODO: tint source unverified -- the ctor block above builds only DefaultBackgroundColour = (0,0,0)
// and no white Colour, so this static has no traced counterpart. PowerUpShop::Draw @0x001a8364
// needs its own RE pass to establish where the quad/text tint really comes from.
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
// PowerUpShop ctor — v1.6.1 PowerUpShop ctor @0x001a81f0
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
// LoadContent @ 0x001a7fdc — empty body (file-static SmartPtrs resolve on first use).
// ============================================================
void PowerUpShop::LoadContent() {
    // Binary @ 0x001a7fdc: empty.
}

// ============================================================
// UnLoadContent @ 0x001a830c — nulls five file-static Mortar::SmartPtr<Texture>s
//   (g_BuyBg/g_Arrow/g_FruitIcons[0..2]).
// ============================================================
void PowerUpShop::UnLoadContent() {
    // Binary @ 0x001a830c:
    g_BuyBg.SetNull();
    g_Arrow.SetNull();
    g_FruitIcons[0].SetNull();
    g_FruitIcons[1].SetNull();
    g_FruitIcons[2].SetNull();
}

// ============================================================
// Init v1.6.1 PowerUpShop::Init @0x001a94b0 (vtable slot 2)
// ============================================================
void PowerUpShop::Init() {
    // Binary v1.6.1 PowerUpShop::Init @0x001a94b0:
    // Zero scalars.
    m_PurchasedCount = 0;
    m_SinIdx         = 0;
    m_SelectedIndex  = 0;

    m_LastSelectedIndex = -1;
    m_PulseScale        = 1.0f;
    m_FruitScale        = 1.0f;

    // ASM-spec v1.6.1 PowerUpShop::Init @0x001a94b0: size = boardTex(w,h,0)
    // Port specific: null guard; binary reads unconditionally but PowerUpShop is dead code in v1.6.1.
    if (g_BuyBg.IsValid()) {
        size = _Vector3<float>((float)g_BuyBg->GetWidth(), (float)g_BuyBg->GetHeight(), 0.0f);
    }

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Iterate PowerUpManager::m_PurchasablePowers (GetFirstPurchasable / GetNextPurchasable
    // are not in the port; walk the list directly — equivalent traversal).
    // Binary (PowerUpShop::Init @0x001a94b0): push_back each purchasable; if p->m_bCloned != 0, m_PurchasedCount++.
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
    // Binary (PowerUpShop::Init @0x001a94b0): centred horizontal layout.
    int count = (int)m_PurchasablePowerUps.size();
    m_SlotLayout.clear();
    for (int i = 0; i < count; ++i) {
        float x;
        if (count > 1) {
            x = (float)i * (250.0f / (float)(count - 1)) - 192.0f;
        } else {
            x = 0.0f;
        }
        m_SlotLayout.push_back(_Vector3<float>(x, 24.0f, 1.0f));
    }

    m_BuyButtonState = 0;
    SetBuyButtonState();

    // Binary: OS_SPrintf(m_BuyText, 128, "YOU HAVE %i COINS TO USE!", player.m_Coins)
    // Coin balance lives in game_work.m_CoinsBalance (+0x20), not FruitSaveData.
    snprintf(m_BuyText, sizeof(m_BuyText), "YOU HAVE %i COINS TO USE!", game_work.m_CoinsBalance);
}

// ============================================================
// Release @ 0x001a9124 (vtable slot 3)
// ============================================================
void PowerUpShop::Release() {
    // Binary @ 0x001a9124: tear down dynamic buy-fruit MenuButton.
    if (m_BuyButton != NULL) {
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        if (fruit != NULL) {
            // ASM-spec v1.6.1 PowerUpShop::Release @0x001a9124, fruit block @0x001a9154-0x001a9204:
            //   fruit->m_bBallisticEnable (+0x70) = 1      // strb @0x001a915c -- NOT m_bSliced (+0xB8)
            //   k = -_Vector3<float>::UnitY = (0,-1,0)     // thunk @0x001a90e0, GOT->0x002d9ed8
            //   s16 = 320.0f                               // literal @0x001a92a8 = 0x43a00000
            //   fruit->vel        (+0x1C) = k
            //   fruit->m_SecondVel(+0xD4) = k
            //   fruit->pos        (+0x10) = k * 320.0f     // operator* @0x0011139c
            //   fruit->m_SecondPos(+0xC8) = k * 320.0f
            // Binary calls the negate thunk 4x and reloads m_pTrackedFruit before each write.
            // m_RotVel1/m_RotVel2 (+0x100/+0x10C) are NEVER written here.
            LOG_INFO("FRUIT", "m_bBallisticEnable=1 set on entity=%p pos=(%.1f,%.1f) type=%d (in PowerUpShop teardown)",
                     static_cast<void*>(fruit), fruit->pos.x, fruit->pos.y, (int)fruit->m_FruitType);
            fruit->m_bBallisticEnable = 1;

            const _Vector3<float> k = -s_UnitY;
            fruit->vel         = k;
            fruit->m_SecondVel = k;
            fruit->pos         = k * 320.0f;
            fruit->m_SecondPos = k * 320.0f;
        }

        // Binary: MenuButton.m_bNoDestructor = 1; replace m_RemoveCallback with empty delegate.
        m_BuyButton->m_bNoDestructor = 1;
        m_BuyButton->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>();

        // Binary: HUD::RemoveControl(*Game.HUD, m_BuyButton); then delete.
        // No Game / mHud guard: v1.6.1 PowerUpShop::Release @0x001a9124 loads game_work
        // from the GOT and calls RemoveControl on mHud (+0x40) unguarded (@0x001a9278).
        game_work.mHud->RemoveControl(m_BuyButton);
        m_BuyButton->Release();   // binary's vtable Release before delete
        delete m_BuyButton;
        m_BuyButton = NULL;
    }
}

// ============================================================
// Reset v1.6.1 PowerUpShop::Reset @0x001a7fe0 (vtable slot 4) — empty body
// ============================================================
void PowerUpShop::Reset() {
    // Binary v1.6.1 PowerUpShop::Reset @0x001a7fe0: returns immediately.
}

// ============================================================
// PreDraw v1.6.1 PowerUpShop::PreDraw @0x001a7fe4 (vtable slot 6) — identity pass-through
// ============================================================
void PowerUpShop::PreDraw(float* hudScale) {
    // Binary v1.6.1 PowerUpShop::PreDraw @0x001a7fe4: returns param_1 unchanged (no-op).
    (void)hudScale;
}

// ============================================================
// Draw v1.6.1 PowerUpShop::Draw @0x001a8364 (vtable slot 7)
// ============================================================
void PowerUpShop::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    // Binary v1.6.1 PowerUpShop::Draw @0x001a8364:
    // Step 1: scale + translate world matrix, upload.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Push();

    Matrix44 scaleMat;
    scaleMat.Identity();
    scaleMat.ApplyScale(size.x, size.y, size.z);

    Matrix44 transMat;
    transMat.Identity();
    transMat.GlobalTranslate44(_Vector3<float>(pos.x, pos.y, pos.z));

    // Combine: translate * scale (transMat first, then scale columns)
    Matrix44 combined = transMat * scaleMat;
    SetMatrix(combined);
    UploadMatrices();

    // Step 2: draw buy background quad via g_BuyBg.
    // Binary @ 0x001a8364: calls g_BuyBg->Set(), Mesh::DrawQuadUnCached(Colour*, fx),
    // g_BuyBg->UnSet(). Default 1x1 quad with current world matrix.
    // Port specific: null guard; binary reads unconditionally but PowerUpShop is dead code in v1.6.1.
    if (g_BuyBg.IsValid()) {
        g_BuyBg->Set();
        Mortar::Mesh::DrawQuadUnCached(g_White, NULL);
        g_BuyBg->UnSet();
    }

    // Step 3: draw m_BuyText (header label) via font.
    // Binary (PowerUpShop::Draw @0x001a8364): pos = (shop.x, shop.y + 75), size 20, anchor 3, white.
    if (game_work.pFontMain.IsValid()) {
        _Vector3<float> textPos(pos.x, pos.y + 75.0f, 0.0f);
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

        _Vector3<float> slot = m_SlotLayout[i];
        float barScale = slot.z;
        float iconScale = barScale * 64.0f;

        // Slot translate+scale matrix (used for both quad bg and TriStrip bar).
        mm.GetWorldStack().Push();
        Matrix44 slotTrans;
        slotTrans.Identity();
        slotTrans.GlobalTranslate44(_Vector3<float>(pos.x + slot.x,
                                                    pos.y + slot.y, pos.z + slot.z));
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
                // Shader modulates texel*color for all Mesh::DrawTriStrip draws now.
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
        _Vector3<float> barPos(pos.x + slot.x, pos.y + slot.y, pos.z);

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
            _Vector3<float> labelPos(barPos.x, barPos.y + (-32.0f * barScale), barPos.z);
            game_work.pFontMain->DrawString(17.0f, 1.0f, 0.0f,
                                            costBuf, labelPos, labelColour, 0xF);
        }

        // Selected-slot title + description.
        // Binary v1.6.1 PowerUpShop::Draw @0x001a8364: title/desc drawn at pos DIRECTLY,
        // not pos+slot (binary reads this->pos, not the per-slot offset).
        if (i == m_SelectedIndex && game_work.pFontMain.IsValid()) {
            _Vector3<float> titlePos(pos.x - 224.0f, pos.y - 30.0f, pos.z);
            Colour titleColour = MakeColour(0xD1, 0x25, 0x0B);
            game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                                            pi->m_DisplayName, titlePos, titleColour, 0xF);

            _Vector3<float> descPos(pos.x - 224.0f, pos.y - 54.0f, pos.z);
            Colour descColour = MakeColour(0x74, 0x5D, 0x3B);
            game_work.pFontMain->DrawString(16.0f, 1.0f, 0.0f,
                                            pi->m_Description, descPos, descColour, 0xF);
        }
    }

    mm.GetWorldStack().Pop();
    UploadMatrices();
}

// ============================================================
// Update v1.6.1 PowerUpShop::Update @0x001a8b04 (vtable slot 10)
// ============================================================
void PowerUpShop::Update(float dt) {
    // Binary v1.6.1 PowerUpShop::Update @0x001a8b04:

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
        _Vector3<float>& slot = m_SlotLayout[i];
        float targetZ = (i == m_SelectedIndex) ? 1.25f : 1.0f;
        slot.z += (targetZ - slot.z) * 0.15f;

        // ASM-spec v1.6.1 PowerUpShop::Update @ 0x001a8b04 (touch block @0x001a8bdc)
        // TODO: v1.6.1 0x001a8bdc (PowerUpShop::Update) — the GameContext field offsets below
        // are UNVERIFIED and contradicted by a #104 read of the binary (which names the flag
        // bM_bTouchC and the position m_WorldPos.x/.y). Re-RE before trusting them.
        // Reads GameContext aliased fields:
        //   +0x9e  uint8_t  m_bPointerActive -- set by PointerDownCallback @ 0x001ca2bc,
        //                                       cleared by PointerUpCallback @ 0x001ca2e4.
        //   +0x90  float    worldPos.x       -- aliased with light direction; PointerMoveCallback
        //                                       action 0x74 writes the centered-ortho X.
        //   +0x94  float    worldPos.y       -- aliased with light direction; action 0x75 writes Y.
        //  Hit zone = (slot ± 32) on both axes. On hit: m_SelectedIndex = i; SetBuyButtonState().
        if (game && game_work.m_bPointerActive) {
            const float px = game_work.worldPos.x;
            const float py = game_work.worldPos.y;
            const float HALF = 32.0f;  // DAT_001566a0
            _Vector3<float> worldPt = pos + slot;
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

        // Spawn position: this->pos + Vector3(160.8, -6.0, 0.0).
        // v1.6.1 PowerUpShop::Update @0x001a8dfc-0x001a8e10 passes r2 = this+0x8 (HUDControl::pos),
        // NOT a global origin -- a zero-vector base is only accidentally right while pos == 0.
        _Vector3<float> spawnPos = pos + _Vector3<float>(160.8f, -6.0f, 0.0f);

        // Build slicedCb: v1.6.1 PowerUpShop::Update @0x001a8b04 binds PowerUpShop::ButtonSliced as
        // Delegate0<void> via QCallee.
        Mortar::Delegate0<void> slicedCb =
            Mortar::Delegate0<void>::QCallee(this, &PowerUpShop::ButtonSliced);

        // Build removeCb: binary binds PowerUpShop::ButtonDeleted as
        // Delegate1<void, HUDControl*> via QCallee, wired to m_RemoveCallback.
        Mortar::Delegate1<void, HUDControl*> removeCb =
            Mortar::Delegate1<void, HUDControl*>::QCallee(
                this, &PowerUpShop::ButtonDeleted);

        // Binary sequence @0x1a8fbc/0x1a8ffc:
        //   m_BuyButton = new MenuButton(NULL, &spawnPos, &slicedCb, fruitType,
        //                                &origin, &deletedCb)   -- param6 Delegate0<void>
        //                                                          deletedCb -> Global no-op @0x19a620
        //   m_BuyButton->Init()          — vtable no-arg Init (calls Reset, no-op)
        //   m_BuyButton->vel.x = 0
        //   m_BuyButton->m_bClearsMenuItems = 0  (+0x13a). NOTE: the old note mapping
        //     v1.0 field_0x123 -> m_bAcceptsTouch (+0x149) was WRONG -- field_0x123 is the
        //     clears-menu-items byte. v1.6.1 PowerUpShop::Update stores 0 to +0x13a
        //     @0x001a9030 and @0x001a8d18, and never writes +0x149 at all.
        //   m_BuyButton->m_RemoveCallback = removeCb   -- Delegate1<void,HUDControl*> @0x1a8ffc,
        //                                                 AFTER ctor/Init (order preserved)
        //   HUD::AddControl(game_work.mHud, m_BuyButton, false)
        //   Rand32(524287); Rand32(2)
        //   Fruit scale *= 0.85 on x, y and z
        //   Fruit::RotateFacingUp(fruit, false, Vec3(0,1,0))
        _Vector3<float> restPos = _Vector3<float>::Zero();
        m_BuyButton = new MenuButton(Mortar::SmartPtr<Mortar::Texture>(), spawnPos, slicedCb,
                                     fruitType, restPos,
                                     Mortar::Delegate0<void>::MakeFree(&MenuCallbackClicked));
        m_BuyButton->Init();
        m_BuyButton->m_RemoveCallback = removeCb;
        // Binary: m_BuyButton->vel.x = 0 (vel field not mapped; fruit piece vel zeroed below)
        // v1.6.1 PowerUpShop::Update @0x001a9030: strb r2,[r3,#0x13a] with r2 = 0 --
        // m_bClearsMenuItems, NOT m_bAcceptsTouch (+0x149, which this function never writes).
        m_BuyButton->m_bClearsMenuItems = 0;

        if (game_work.mHud) {
            game_work.mHud->AddControl(m_BuyButton, false);
        }

        Math::g_Random.Rand32(524287);
        Math::g_Random.Rand32(2);

        // ASM-verified: 2026-07-28T00:00Z v1.6.1 PowerUpShop::Update @ 0x001a8b04 (re-analyst)
        // Block @0x001a9064: r3 = this->m_BuyButton(+0x12c)->m_pTrackedFruit(+0x14c), then
        //   [r3,#0x28] *= 0.85; [r3,#0x2c] *= 0.85; [r3,#0x30] *= 0.85  -- Entity::scale (+0x28),
        //   all THREE components. (A prior port read this as m_RotVel1.x/.y, which is Fruit+0x100.)
        // Then @0x001a909c reloads the same fruit and calls RotateFacingUp(false, (0,1,0)).
        // DIFFERS: original has no null check on m_pTrackedFruit (`ldr r3,[r3,#0x14c]` is
        // dereferenced unconditionally), using a guard because MenuButton::CreateFruit can
        // legitimately leave it null in the port (FruitType lookup miss, ActorManager pool
        // exhaustion, or a bare unit-test fixture with no ActorManager).
        if (m_BuyButton->m_pTrackedFruit != NULL) {
            m_BuyButton->m_pTrackedFruit->scale.x *= 0.85f;
            m_BuyButton->m_pTrackedFruit->scale.y *= 0.85f;
            m_BuyButton->m_pTrackedFruit->scale.z *= 0.85f;
            m_BuyButton->m_pTrackedFruit->RotateFacingUp(false, _Vector3<float>(0.0f, 1.0f, 0.0f));
        }
    } else if (m_BuyButton != NULL) {
        // Step 4: update existing buy button.

        // Binary: move button to this->pos + (160.8, -6, 0).
        // v1.6.1 PowerUpShop::Update @0x001a8ca8-0x001a8cb4 passes r2 = this+0x8 (HUDControl::pos).
        m_BuyButton->pos = pos + _Vector3<float>(160.8f, -6.0f, 0.0f);
        // TODO: v1.6.1 0x001a8cc8 (PowerUpShop::Update) — the write below has NO counterpart in
        // the binary's else-branch: @0x001a8cc8 the binary stores 0.0f to m_BuyButton+0x10 (the
        // z component of the pos vec3 it just stmia'd), never touching the tracked fruit's vel.
        // m_PulseScale's actual consumer is unidentified. Left in place pending a re-RE pass.
        if (m_BuyButton->m_pTrackedFruit != NULL) {
            m_BuyButton->m_pTrackedFruit->vel.x = m_PulseScale;
        }

        // Binary: if last selected index changed and fruit alive and not sliced,
        // get push vector from origin, set fruit vel, trigger re-spawn.
        if ((m_LastSelectedIndex != m_SelectedIndex) &&
            (m_BuyTriggered == 0) &&
            (m_BuyButton->m_pTrackedFruit != NULL) &&
            !m_BuyButton->m_pTrackedFruit->Sliced()) {

            // v1.6.1 PowerUpShop::Update @0x001a8d10-0x001a8d28:
            //   strb r12,[r3,#0xb8]          -- fruit->m_bSliced = 1
            //   add  r3,r3,#0xd4 ; stmia r3  -- fruit->m_SecondVel = _Vector3<float>::One
            //                                   (GOT[0x1a8f00] -> One; NOT vel (+0x1C), NOT Zero)
            //   strb r0,[r2,#0x13a] with r0 = 0 -- m_bClearsMenuItems, not m_bAcceptsTouch
            //   (field_0x123 in the old v1.0 note is the clears-menu-items byte).
            m_BuyButton->m_pTrackedFruit->m_bSliced = 1;
            m_BuyButton->m_pTrackedFruit->m_SecondVel = _Vector3<float>::One();
            m_BuyButton->m_bClearsMenuItems = 0;
            m_BuyTriggered = 1;
        }
    }

    m_LastSelectedIndex = m_SelectedIndex;
}

// ============================================================
// SetBuyButtonState @ 0x001a8124 (non-virtual)
// ============================================================
void PowerUpShop::SetBuyButtonState() {
    // Binary @ 0x001a8124:
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
// ButtonSliced @ 0x001a7fe8 (non-virtual; bound as Mortar::Delegate0<void>)
// ============================================================
void PowerUpShop::ButtonSliced() {
    // Binary @ 0x001a7fe8: split predicate (avoids GCC 16-bit load-fuse on
    // combined &&; binary emits two ldrb.w, one per field).
    if (m_BuyTriggered != 0) {
        // Already-purchased "spit fruit out" branch
        if (m_BuyButton == NULL) return;
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        if (fruit == NULL) return;
        // 3-float ldmia/stmia copy block: freeze both halves at current position.
        fruit->m_SecondPos = fruit->pos;                   // +0xC8 <- +0x10
        fruit->vel         = _Vector3<float>::Zero();      // +0x1C
        fruit->m_SecondVel = _Vector3<float>::Zero();      // +0xD4
        fruit->m_Gravity   = _Vector3<float>::Zero();      // +0xA0
        return;
    }
    if (m_BuyButtonState != 0) return;

    PowerUp* p = m_PurchasablePowerUps[m_SelectedIndex];
    uint32_t hash = p->m_NameHash;

    // Binary: copy _Vector3<float>::Zero to a stack-local, then ActivatePower(hash, &local, NULL).
    // The 4th argument is NULL: @0x001a80a8 `cpy r3,r8` where r8 holds the just-tested
    // m_BuyButtonState, proven 0 on this path. (An older note claiming r2 and r3 both point at
    // the same Vec3 was wrong.)
    _Vector3<float> localOrigin = _Vector3<float>::Zero();
    PowerUpManager* pum = PowerUpManager::GetInstance();
    pum->ActivatePower(hash, localOrigin, NULL);

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
// ButtonDeleted @ 0x001a9438 (non-virtual; bound as Mortar::Delegate1<void,HUDControl*>)
// ============================================================
void PowerUpShop::ButtonDeleted(HUDControl* deletedCtrl) {
    // Binary @ 0x001a9438:
    if (deletedCtrl != m_BuyButton) {
        return;
    }
    if (m_BuyTriggered != 0 && m_BuyButton->m_pTrackedFruit != NULL) {
        Fruit* fruit = m_BuyButton->m_pTrackedFruit;
        // Binary @ 0x001a9438 (instruction-traced, v1.6.1):
        //   vldr s15 = -480.0f (0xc3f00000); vmov s15 = -10.0f (0xc1200000)
        //   vstr s15,[r6,#0x14] -> pos.y           (+0x14)
        //   vstr s15,[r6,#0xcc] -> m_SecondPos.y   (+0xCC)
        //   bl 0x001a90e0 -> Vec3 = -(*_Vector3<float>::UnitY @0x002d9ed8) = (0,-1,0);
        //     ldmia/stmia into [r6+0xa0] = m_Gravity (+0xA0)
        //   vstr s15,[r3,#0x20] -> vel.y           (+0x20)
        //   vstr s15,[r3,#0xd8] -> m_SecondVel.y   (+0xD8)
        fruit->m_SecondPos.y = -480.0f;  // +0xCC, 0xc3f00000
        fruit->pos.y         = -480.0f;  // +0x14, 0xc3f00000

        // The negated global at 0x002D9ED8 is _Vector3<float>::UnitY = (0,1,0),
        // NOT the file-static origin -- so the kicked buy-fruit gets downward
        // gravity and falls. Same semantic as ShopScreen::DeletedMenuItem; a zero
        // m_Gravity would leave Fruit::CheckHasGoneOffscreen unable to ever fire.
        fruit->m_Gravity = _Vector3<float>(0.0f, -1.0f, 0.0f);   // +0xA0

        fruit->m_SecondVel.y = -10.0f;   // +0xD8, 0xc1200000
        fruit->vel.y         = -10.0f;   // +0x20, 0xc1200000
    }
    m_BuyTriggered = 0;
    m_BuyButton    = NULL;
}
