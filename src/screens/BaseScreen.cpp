//
// BaseScreen — intermediate base for menu/overlay screens.
// Only DojoScreen and GameModeScreen inherit from this.
// See BaseScreen.h for binary refs.
//
// Analysed: 2026-04-17T04:00
//

#include "BaseScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "entities/Fruit.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/BakedStringBox.h"
#include "math/Matrix44.h"
#include "math/Vec2.h"
#include "math/Colour.h"
#include <cmath>
#include "game/GameWork.h"

// Static texture storage (binary: GOT-relative module-level singletons)
Mortar::SmartPtr<Mortar::Texture> BaseScreen::s_TexSmlTitle;
Mortar::SmartPtr<Mortar::Texture> BaseScreen::s_TexBlurryBacking;

// DrawBorders constants (literal pool @ 0x0013056c, resolved via read_memory)
// g_slideVec = Vec3(0, 1, 0) — vertical slide direction
// (initialized in _GLOBAL__I_BaseScreen.cpp @ 0x00130694)
static const Vec3 SLIDE_VEC(0.0f, 1.0f, 0.0f);

static const float TRI_HALF_H     =   82.0f;     // 0x00130578
static const float TRI_WIDTH      =  656.0f;     // 0x00130584
static const float TRI_BASE_Y     =  160.0f;     // 0x00130588
static const float TRI1_Y_SLOPE   =  -48.0f;     // 0x0013058c
static const float TRI1_X         =  240.0f;     // 0x00130590
static const float TRI2_Y_SLOPE   =   55.0f;     // 0x00130594
static const float TRI2_X         = -240.0f;     // 0x00130598
static const float DECO_X         =  182.0f;     // 0x0013059c
static const float DECO_Y         =  137.0f;     // 0x001305a0
static const float DECO_SLIDE_Y   =   48.0f;     // 0x001305a4
static const float SEC_SLIDE_Y    =   55.0f;     // 0x00130594 (reused)
static const float UV_NEAR        = 0.0078125f;  // 0x00130570 (= 1/128)
static const float UV_FAR         = 0.0153961f;  // DAT_00130574 = 0x3c7c0fc0

// ===================================================================
// Matches BaseScreen::BaseScreen @ 0x00138dc0
// ===================================================================
BaseScreen::BaseScreen()
    : m_TransitionAlpha(0.0f)  // DAT_00138df8 = 0.0
    , m_State(0)
{
}

// ===================================================================
// Matches BaseScreen::~BaseScreen @ 0x00138d60
// Binary: set vtable, Release(), ~list(m_HUDControls),
//         ~list(m_ScreenButtons), ~HUDControl3d()
// ===================================================================
BaseScreen::~BaseScreen() {
    // Binary @ 0x00138d94 explicitly invokes Release() in the dtor.
    // C++ static-dispatches the call here to BaseScreen::Release.
    BaseScreen::Release();
    // m_HUDControls cleared by list dtor.
}

// ===================================================================
// Matches BaseScreen::LoadContent @ 0x001305cc
// ===================================================================
void BaseScreen::LoadContent() {
    if (!s_TexSmlTitle.IsValid()) {
        s_TexSmlTitle = Mortar::TextureManager::LoadLocalisedTexture("sml_title.tex");
    }
    if (!s_TexBlurryBacking.IsValid()) {
        s_TexBlurryBacking = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    }
}

void BaseScreen::UnloadContent() {
    s_TexSmlTitle.SetNull();
    s_TexBlurryBacking.SetNull();
}

// ===================================================================
// Matches BaseScreen::DrawBorders @ 0x00130230
//
// Draws (in order):
//   1. Two shade triangles (blurry_backing.tex, Colour(0,0,0,128))
//   2. Decoration quad (sml_title.tex) with vertical slide
//   3. Optional secondary texture with vertical slide + offset
// All geometry at Z=0.0.
// ===================================================================
void BaseScreen::DrawBorders(Mortar::SmartPtr<Mortar::Texture> secondaryTex,
                             float alpha, Vec3 secondaryTexPos) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // --- 1. Shade triangles (blurry_backing.tex at stateObj+4) ---
    if (s_TexBlurryBacking.IsValid()) {
        // Binary lazy-init: builds static vertex buffers once (stateObj+0x08 flag).
        // Two triangles × 3 verts, Colour(0,0,0,0x80) baked in.
        static bool s_trisInitialized = false;
        static QUADCUSTOMVERTEX s_tri1[3];  // top/right (stateObj+0x78)
        static QUADCUSTOMVERTEX s_tri2[3];  // bottom/left (stateObj+0x0C)

        if (!s_trisInitialized) {
            s_trisInitialized = true;
            const uint32_t kCol = Colour(0, 0, 0, 128).PlatformColour();

            // Binary vertex data (verified from lazy-init fixup asm @ 0x001302ea).
            // Tri1 (at stateObj+0x78, drawn at (240, 160-48*alpha)): RIGHT apex.
            //   v0 (0, 0) = apex at anchor (right edge)
            //   v1 (0, 82) = straight up from apex
            //   v2 (-656, 82) = far upper-left, off-screen
            // Forms a thin wedge hanging from the top-right corner.
            // Binary @ 0x001302ea uses UV_FAR (DAT_00130574 = 0.0153961) at the
            // centre vertex's u, NOT 1.0.
            s_tri1[0] = {       0.0f,       0.0f, 0.0f,  0,0,1,  kCol,  0.0f,    UV_NEAR };
            s_tri1[1] = {       0.0f,  TRI_HALF_H, 0.0f,  0,0,1,  kCol,  UV_FAR,  1.0f    };
            s_tri1[2] = { -TRI_WIDTH,  TRI_HALF_H, 0.0f,  0,0,1,  kCol,  1.0f,    UV_NEAR };

            // Tri0 (at stateObj+0x0C, drawn at (-240, 55*alpha-160)): LEFT apex.
            //   v0 (0, 0) = apex at anchor (left edge)
            //   v1 (0, -82) = straight down from apex
            //   v2 (656, -82) = far lower-right, off-screen
            // Mirror wedge hanging from the bottom-left corner.
            s_tri2[0] = {      0.0f,        0.0f, 0.0f,  0,0,1,  kCol,  0.0f,    UV_NEAR };
            s_tri2[1] = {      0.0f, -TRI_HALF_H, 0.0f,  0,0,1,  kCol,  UV_FAR,  1.0f    };
            s_tri2[2] = { TRI_WIDTH, -TRI_HALF_H, 0.0f,  0,0,1,  kCol,  1.0f,    UV_NEAR };
        }

        s_TexBlurryBacking->Set();

        // Top triangle (stateObj+0x78)
        // Translate: (240, 160 + alpha*(-48), 0)
        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            mat.GlobalTranslate44(Vec3(TRI1_X,
                TRI_BASE_Y + alpha * TRI1_Y_SLOPE, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawTriList(s_tri1, 3, false, NULL);
        }

        // Bottom triangle (stateObj+0x0C)
        // Translate: (-240, alpha*55 - 160, 0)
        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            mat.GlobalTranslate44(Vec3(TRI2_X,
                alpha * TRI2_Y_SLOPE - TRI_BASE_Y, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawTriList(s_tri2, 3, false, NULL);
        }

        s_TexBlurryBacking->UnSet();
    }

    // --- 2. Decoration quad (sml_title.tex at stateObj+0) ---
    // pos = (182, 137, 0) + Vec3(0,1,0)*48*(1-alpha) = (182, 137+48*(1-a), 0)
    if (s_TexSmlTitle.IsValid()) {
        s_TexSmlTitle->Set();

        mm.GetWorldStack().Reset();
#if !defined(__bada__)
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexSmlTitle->m_Width + 1.0f,
            (float)s_TexSmlTitle->m_Height + 1.0f,
            1.0f);
#else
        Matrix44 mat = Matrix44::MakeScale(0.0f, 0.0f, 1.0f);
#endif
        Vec3 decoPos = Vec3(DECO_X, DECO_Y, 0.0f) +
                       SLIDE_VEC * (DECO_SLIDE_Y * (1.0f - alpha));
        mat.GlobalTranslate44(decoPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexSmlTitle->UnSet();
    }

    // --- 3. Optional secondary texture (e.g. dojo.tex) ---
    // Binary (register trace at 0x00130542, verified via asm disassembly
    // not Ghidra decompile):
    //   r0 = out, r1 = secondaryTexPos (this), r2 = SLIDE_VEC*55*(1-alpha)
    //   -> result = r1 - r2 = secondaryTexPos - SLIDE_VEC*55*(1-alpha)
    // Ghidra renders this as `operator-(&out, param_4)` which looks like
    // the slide term minus secondaryTexPos, but that's a struct-return
    // artefact: in ARM AAPCS the hidden out-ptr is r0, `this` is r1, and
    // rhs is r2. The primary sml_title.tex block above uses operator+
    // on the same slide-vec multiplier, so the two blocks have opposite
    // signs intentionally -- sml_title slides up out of frame, dojo.tex
    // slides down out of frame.
    //
    // At alpha=1 (settled): translate = secondaryTexPos = (-184, -136).
    // At alpha=0 (slid off): translate = (-184, -191).
    if (secondaryTex.IsValid()) {
        secondaryTex->Set();

        mm.GetWorldStack().Reset();
#if !defined(__bada__)
        Matrix44 mat = Matrix44::MakeScale(
            (float)secondaryTex->m_Width + 1.0f,
            (float)secondaryTex->m_Height + 1.0f,
            1.0f);
#else
        Matrix44 mat = Matrix44::MakeScale(0.0f, 0.0f, 1.0f);
#endif
        // At alpha=1 (on-screen rest): secPos = secondaryTexPos.
        // At alpha=0 (slide-out): secPos moves DOWN by SEC_SLIDE_Y
        // (subtracting a +Y slide vector), matching the visual where
        // the bottom-left decoration slides further down off-screen.
        Vec3 secPos = secondaryTexPos -
                      SLIDE_VEC * (SEC_SLIDE_Y * (1.0f - alpha));
        mat.GlobalTranslate44(secPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        secondaryTex->UnSet();
    }
}

// ===================================================================
// BaseScreen::DrawBorders(BakedStringBox*, float, Vec3)
// v1.6.1 @0x0015f878
//
// Draws the same shade-triangle + sml_title geometry as the SmartPtr
// overload (v1.6.1 @0x0015fcec) but does NOT draw a secondary texture.
// If box != nullptr, positions and draws the text box at the computed
// anchor. Returns the anchor Vec3.
// ASM-verified lhs-rhs at v1.6.1 @0x15fc80: anchor = arg3 - SLIDE_VEC*55*(1-alpha).
// ===================================================================
Vec3 BaseScreen::DrawBorders(Mortar::BakedStringBox* box,
                             float alpha, Vec3 arg3) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // --- 1. Shade triangles (blurry_backing.tex) --- identical to SmartPtr overload
    if (s_TexBlurryBacking.IsValid()) {
        static bool s_trisInitialized2 = false;
        static QUADCUSTOMVERTEX s_tri1b[3];
        static QUADCUSTOMVERTEX s_tri2b[3];

        if (!s_trisInitialized2) {
            s_trisInitialized2 = true;
            const uint32_t kCol = Colour(0, 0, 0, 128).PlatformColour();

            s_tri1b[0] = {       0.0f,       0.0f, 0.0f,  0,0,1,  kCol,  0.0f,    UV_NEAR };
            s_tri1b[1] = {       0.0f,  TRI_HALF_H, 0.0f,  0,0,1,  kCol,  UV_FAR,  1.0f    };
            s_tri1b[2] = { -TRI_WIDTH,  TRI_HALF_H, 0.0f,  0,0,1,  kCol,  1.0f,    UV_NEAR };

            s_tri2b[0] = {      0.0f,        0.0f, 0.0f,  0,0,1,  kCol,  0.0f,    UV_NEAR };
            s_tri2b[1] = {      0.0f, -TRI_HALF_H, 0.0f,  0,0,1,  kCol,  UV_FAR,  1.0f    };
            s_tri2b[2] = { TRI_WIDTH, -TRI_HALF_H, 0.0f,  0,0,1,  kCol,  1.0f,    UV_NEAR };
        }

        s_TexBlurryBacking->Set();

        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            mat.GlobalTranslate44(Vec3(TRI1_X,
                TRI_BASE_Y + alpha * TRI1_Y_SLOPE, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawTriList(s_tri1b, 3, false, NULL);
        }

        {
            mm.GetWorldStack().Reset();
            Matrix44 mat;
            mat.GlobalTranslate44(Vec3(TRI2_X,
                alpha * TRI2_Y_SLOPE - TRI_BASE_Y, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawTriList(s_tri2b, 3, false, NULL);
        }

        s_TexBlurryBacking->UnSet();
    }

    // --- 2. Decoration quad (sml_title.tex) --- identical to SmartPtr overload
    if (s_TexSmlTitle.IsValid()) {
        s_TexSmlTitle->Set();

        mm.GetWorldStack().Reset();
#if !defined(__bada__)
        Matrix44 mat = Matrix44::MakeScale(
            (float)s_TexSmlTitle->m_Width + 1.0f,
            (float)s_TexSmlTitle->m_Height + 1.0f,
            1.0f);
#else
        Matrix44 mat = Matrix44::MakeScale(0.0f, 0.0f, 1.0f);
#endif
        Vec3 decoPos = Vec3(DECO_X, DECO_Y, 0.0f) +
                       SLIDE_VEC * (DECO_SLIDE_Y * (1.0f - alpha));
        mat.GlobalTranslate44(decoPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);
        s_TexSmlTitle->UnSet();
    }

    // --- 3. Anchor and optional BakedStringBox draw ---
    // anchor = arg3 - SLIDE_VEC * SEC_SLIDE_Y * (1 - alpha)
    Vec3 anchor = arg3 - SLIDE_VEC * (SEC_SLIDE_Y * (1.0f - alpha));

    if (box != nullptr) {
        box->SetTranslation(anchor, 1);
        box->Draw(-7.0f, Vec2(1.0f, 1.0f), 1);
    }

    return anchor;
}

// ===================================================================
// Matches BaseScreen::UpdateButtons @ 0x00130ab4
// Binary iterates std::list<ScreenButton>: if m_pMenuButton is nullptr,
// calls the visibility delegate then lazily creates MenuButton; else
// calls the update delegate. ScreenButton struct (~0xCC bytes) with
// delegates for creation condition, update, press, draw.
// ===================================================================
void BaseScreen::UpdateButtons(float dt) {
    for (std::list<ScreenButton>::iterator it = m_ScreenButtons.begin(); it != m_ScreenButtons.end(); ++it) {
        ScreenButton& sb = *it;
        if (sb.m_pButton == nullptr) {
            // Not yet created — check visibility predicate
            if (!sb.m_visCheck || !sb.m_visCheck(dt)) continue;

            // Lazy-create MenuButton from descriptor
            MenuButton* btn = new MenuButton();
            if (sb.m_tex.IsValid()) {
                btn->m_Texture = sb.m_tex;
            }
            if (sb.m_clickCb) {
                btn->Init(sb.m_pos, sb.m_clickCb,
                          sb.m_tutorID, sb.m_fruitPos, nullptr);
            }
            sb.m_pButton = btn;

            // Scale: optional m_scaleA (if != 0), then always m_scaleB
            if (sb.m_scaleA != 0.0f)
                btn->m_RestScale = btn->m_RestScale * sb.m_scaleA;
            btn->m_RestScale = btn->m_RestScale * sb.m_scaleB;

            // Wire ControlDeleted as remove callback
            btn->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(&sb, &ScreenButton::ControlDeleted);

            // Apply fruit piece scale + optional rotation
            if (btn->m_pFruitPiece) {
                btn->m_pFruitPiece->scale =
                    btn->m_pFruitPiece->scale * sb.m_scaleB;
                if (!btn->m_pFruitPiece->m_bSliced &&
                    (fabsf(sb.m_rotX) + fabsf(sb.m_rotY)) > 0.0f) {
                    btn->m_pFruitPiece->RotateFacingUp(
                        true, Vec3(sb.m_rotX, sb.m_rotY, 0.0f));
                }
            }

            game_work.mHud->AddControl(btn, false);
            if (sb.m_tutorID >= 0)
                game_work.m_TutorialControl->ResetTutePos(btn);

            // First-frame update call with -1.0f
            if (sb.m_updateCb) sb.m_updateCb(btn, -1.0f, sb);
        } else {
            // Button exists — per-frame update
            if (!sb.m_updateCb) continue;
            bool remove = sb.m_updateCb(sb.m_pButton, dt, sb);
            if (remove) {
                MenuButton* btn = sb.m_pButton;
                if (btn->m_pFruitPiece && !btn->m_pFruitPiece->m_bSliced) {
                    // Fruit alive: disable taps + redirect tap to shrink-call
#if !defined(__bada__)
                    btn->m_bEnabled = 0;
#endif
                    btn->SetCallback(
                        Mortar::Delegate0<void>::Make(&sb, &ScreenButton::ShrinkButtonCall));
                } else {
                    btn->m_bPendingRemoval = 1;
                }
            }
        }
    }
}

// ===================================================================
// Matches the GenericHUDControl* push_back in binary
// (called when creating child screens like AboutScreen).
// ===================================================================
void BaseScreen::AddGenericControl(HUDControl* ctrl) {
    m_HUDControls.push_back(ctrl);
}

// ===================================================================
// Matches BaseScreen::Release @ 0x00130dd8
// Binary: iterates m_HUDControls → set m_bPendingRemoval, clear list.
// Then if game loaded: iterates m_ScreenButtons → set *(mbtn+0x32)=0,
// clear draw delegates on MenuButton+0x38 and ScreenButton+0x80.
// ===================================================================
void BaseScreen::Release() {
    // 1. Mark all registered HUDControls for pending removal
    for (std::list<HUDControl*>::iterator it = m_HUDControls.begin(); it != m_HUDControls.end(); ++it) {
        if (*it) {
            (*it)->m_bPendingRemoval = 1;
        }
    }
    m_HUDControls.clear();

    // 2. Disable ScreenButton MenuButtons + clear delegates
    // Binary: guarded by game_work.field_0x34 != 0 (direct check, no Game::GetInstance)
    if (game_work.field_0x34 != 0) {
        for (std::list<ScreenButton>::iterator it = m_ScreenButtons.begin(); it != m_ScreenButtons.end(); ++it) {
            ScreenButton& sb = *it;
            if (sb.m_pButton) {
                // Binary @ 0x00130e5e: *(byte*)(btn + 0x32) = 0 — clears
                // HUDControl::m_bNoDestructor (NOT m_bEnabled, which is +0x123).
                sb.m_pButton->m_bNoDestructor = 0;
                sb.m_pButton->m_RemoveCallback = nullptr;
                sb.m_deletedCb = nullptr;
            }
        }
    }
}

// ===================================================================
// Matches BaseScreen::RemoveButtons @ 0x00130eb8
// Unconditional — marks each ScreenButton's MenuButton pending-removal
// and clears delegates. No game-active guard (unlike Release).
// ===================================================================
void BaseScreen::RemoveButtons() {
    for (std::list<ScreenButton>::iterator it = m_ScreenButtons.begin(); it != m_ScreenButtons.end(); ++it) {
        ScreenButton& sb = *it;
        if (sb.m_pButton) {
            sb.m_pButton->m_bPendingRemoval = 1;
            sb.m_pButton->m_RemoveCallback = nullptr;
            sb.m_deletedCb = nullptr;
        }
    }
}
