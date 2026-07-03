// FruitFactClassicFactPage -- v1.6.1 Classic-mode fact page (one fact card).
// Binary refs:
//   ctor        0x00174e30
//   DrawOrder   0x00175250

#include "FruitFactClassicFactPage.h"
#include "GameOverScreen.h"
#include "hud/GenericHUDControl.h"
#include "hud/FruitFactControl.h"
#include "engine/math/Vec3.h"
#include "engine/math/Vec2.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/asset/Mesh.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/MatrixStack.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/render/Utf8StringIterator.h"
#include "engine/util/StringTable.h"
#include "asset/TextureManager.h"

// Shared TTF font for fact-card BakedStringBox objects.
// Binary: game_work+0x614 (FontCacheObjectTTF*).
// DIFFERS: original = game_work+0x614; port caches in a file-local SmartPtr.
// v1.6.1 FruitFactClassicFactPage::DrawOrder @0x00175250
static Mortar::FontCacheObjectTTF* GetClassicFactTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) return 0;
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Binary @ 0x00174e30
// Builds 2 GenericHUDControls for the Classic fact card using the sensei
// head/body textures from GameOverScreen's static arrays.
// Binary: uses GameOverScreen::m_senseiHeads[headIdx] / m_senseiBody[bodyIdx].
// Port: accesses via GameOverScreen::GetSenseiHeadTex / GetSenseiBodyTex.
// ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
FruitFactClassicFactPage::FruitFactClassicFactPage(
    FruitFactControl* pCtrl, int headIdx, int bodyIdx)
    : FruitFactPage(pCtrl)
    , m_pTitleBox(NULL)
    , m_pBodyBox(NULL)
{
}

FruitFactClassicFactPage::~FruitFactClassicFactPage() {
}

// Binary @ 0x00174e30 (ctor body)
// Builds 2 GenericHUDControls: first uses m_senseiBody[bodyIdx], second uses
// m_senseiHeads[headIdx]. Positions from binary literal pool.
// Binary consts (v1.6.1 FruitFactClassicFactPage ctor @0x00174e30):
//   ctrl1 pos: Vec3(-202, -24, 0) (DAT_175020=-202, 0xc1c00000=-24)
//   ctrl2 pos: ctrl1 + Vec3(9, 40, 0) = Vec3(-193, 16, 0)
//   Both flags=1, scale unit, col white.
void FruitFactClassicFactPage::Init() {
    Vec3 scZero(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
    Colour white(255, 255, 255, 255);

    // ctrl 1: sensei body background slot (binary: m_senseiBody[bodyIdx=0])
    // Binary reads param_3 (bodyIdx) for this first control.
    // ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
    {
        Mortar::SmartPtr<Mortar::Texture> bodyTex =
            GameOverScreen::GetSenseiBodyTex(0);
        Vec3 pos1(-202.0f, -24.0f, 0.0f);
        GenericHUDControl* c1 = new GenericHUDControl(
            0.0f, 0.0f, bodyTex, NULL, pos1, scZero, white, 1);
        AddGenericControl(c1);
    }

    // ctrl 2: sensei head slot (binary: m_senseiHeads[headIdx=0])
    // Binary reads param_2 (headIdx) for this second control.
    // Position = Vec3(-202, -24, 0) + Vec3(9, 40, 0) = Vec3(-193, 16, 0).
    // ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
    {
        Mortar::SmartPtr<Mortar::Texture> headTex =
            GameOverScreen::GetSenseiHeadTex(0);
        Vec3 pos2(-202.0f + 9.0f, -24.0f + 40.0f, 0.0f);
        GenericHUDControl* c2 = new GenericHUDControl(
            0.0f, 0.0f, headTex, NULL, pos2, scZero, white, 1);
        AddGenericControl(c2);
    }
}

// Binary @ 0x00175250
// Draws the Classic fact-card board quad + title + body text.
// All constants verified from disasm / decompile v1.6.1 FruitFactClassicFactPage::DrawOrder.
void FruitFactClassicFactPage::DrawOrder(float* /*hudScaleRaw*/, int /*layerMask*/) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // 1. BOARD QUAD: lazy-load m_Texture (localised fact-board texture), then draw.
    // Binary: if(m_Texture) { draw } else { if(!m_Texture) LoadLocalisedTexture → assign }
    // v1.6.1 FruitFactClassicFactPage::DrawOrder @0x00175250
    if (m_Texture.IsValid()) {
        // Binary: vtable slot +0xc = Texture::SetUnCached() @ 0x00188da4
        m_Texture->SetUnCached();

        mm.GetWorldStack().Reset();

        // Scale = Vec3(3.0, 1.4, 0.0) * size
        // Binary: _Stack_4c=(3.0,1.4,0.0); _Stack_58=_Stack_4c * size
        // v1.6.1 @0x001752b0
        Vec3 scaleBase(3.0f, 1.4f, 0.0f);
        Vec3 scaled = scaleBase * size;
        mm.GetWorldStack().Scale(scaled);

        // Translate: t = (pos - Vec3(1,8,0)) + Vec3(-86,0,0) = pos + Vec3(-87,-8,0).
        // Binary Vec3 operator- @0x00136144 is out = r1 - r2 with r1=pos (minuend),
        // r2=(1,8,0) (subtrahend); operator+ @0x0013610c then adds local_64=(-86,0,0).
        // (A prior decompile read this as "local_64 - pos", REVERSING the operands,
        // which put the board on the opposite side of its own title/body text.)
        // ASM-verified: v1.6.1 FruitFactClassicFactPage::DrawOrder @0x00175368 (operator-), @0x00175378 (operator+)
        Vec3 t = pos + Vec3(-87.0f, -8.0f, 0.0f);
        mm.GetWorldStack().Translate(t);

        mm.UploadModelViewOnly();

        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255),
                                       0.0f, 1.0f, 0.0f, 1.0f, NULL);

        // Binary: vtable slot +0x10 = Texture::UnSetUnCached() @ 0x00188d9c
        m_Texture->UnSetUnCached();
    } else {
        if (!m_Texture.IsValid()) {
            // Lazy-load the fact board texture.
            // Binary: LoadLocalisedTexture("big_fact_board.tex") -> m_Texture
            // v1.6.1 FruitFactClassicFactPage::DrawOrder @0x00175410
            m_Texture = Mortar::TextureManager::LoadLocalisedTexture("big_fact_board.tex");
        }
    }

    // 2. TITLE BOX: lazy-build m_pTitleBox once (null-check gate).
    // Binary: if(m_pTitleBox == NULL) { new BakedStringBox(12.0, font, 148, 14, 0xf, 1, 3);
    //           SetColour(m_pController->m_FactColour); SetShadow(...); SetText(GETSTRING(0xAE)); }
    // v1.6.1 FruitFactClassicFactPage::DrawOrder @0x00175450
    if (m_pTitleBox == NULL) {
        Mortar::FontCacheObjectTTF* font = GetClassicFactTTFFont();
        if (font) {
            m_pTitleBox = new Mortar::BakedStringBox(
                font,
                12.0f,       // fontSize (binary s0=0x41400000=12.0f)
                148,         // boxW     (binary r2=0x94=148)
                14,          // boxH     (binary r3=0xe=14)
                (Mortar::ALIGNMENT_TYPE)0xf,         // align    (binary sp+0=15)
                1,           // maxLines (binary sp+4=1)
                3            // lineSpacing (binary sp+8=3; step = (int)(12+3) = 15px)
            );

            // SetColour from m_pController->m_FactColour (+0x98)
            // v1.6.1 @0x001754b4
            m_pTitleBox->SetColour(m_pController->m_FactColour, 0);

            // SetShadow(scale=3.0, colour=RGB(186,140,75), offset=Vec3(2,-2,0), flag=0)
            // Binary T_988: m_B=75, m_G=140, m_R=186, m_A=255 -> Colour(186,140,75,255)
            // v1.6.1 @0x001754d4
            m_pTitleBox->SetShadow(
                3.0f,
                Colour(186, 140, 75, 255),
                Vec3(2.0f, -2.0f, 0.0f),
                false
            );

            // SetText(GETSTRING(0xAE)) -- "SENSEI'S FRUIT FACT"
            // v1.6.1 @0x00175528
            m_pTitleBox->SetText(GETSTRING((LocalizedString)0xAE, 0));

            // SetHorizontalLineSpacing(-1) -- auto spacing
            // v1.6.1 @0x00175544
            m_pTitleBox->SetHorizontalLineSpacing(-1);
        }
    }

    // SetTranslation(pos + Vec3(-88, 68, 0), 0) then Draw
    // Binary: _Stack_b8=(-88,68,0); _Stack_c4 = pos + _Stack_b8
    // v1.6.1 @0x00175550
    if (m_pTitleBox) {
        Vec3 titleTrans = pos + Vec3(-88.0f, 68.0f, 0.0f);
        m_pTitleBox->SetTranslation(titleTrans, 0);
        // T_994 = BakedStringBox::Draw(Vec2(1,1), 0.0f, 1)
        m_pTitleBox->Draw(Vec2(1.0f, 1.0f), 0.0f, 1);
    }

    // 3. BODY BOX: lazy-build m_pBodyBox once.
    // Binary: if(m_pBodyBox == NULL) { new BakedStringBox(10.0, font, 130, 120, 0xf, 9, 3);
    //           SetText(m_pController->m_FactText); SetColour(RGB(75,50,40)); }
    // v1.6.1 FruitFactClassicFactPage::DrawOrder @0x001755a4
    if (m_pBodyBox == NULL) {
        Mortar::FontCacheObjectTTF* font = GetClassicFactTTFFont();
        if (font) {
            m_pBodyBox = new Mortar::BakedStringBox(
                font,
                10.0f,       // fontSize (binary s0=0x41200000=10.0f)
                130,         // boxW     (binary r2=0x82=130)
                120,         // boxH     (binary r3=0x78=120)
                (Mortar::ALIGNMENT_TYPE)0xf,         // align    (binary sp+0=15)
                9,           // maxLines (binary sp+4=9)
                3            // lineSpacing (binary sp+8=3; step = (int)(10+3) = 13px)
            );

            // SetText(m_pController->m_FactText at +0x7c)
            // Binary passes m_FactText (may be NULL if no fact loaded yet).
            // v1.6.1 @0x00175614
            m_pBodyBox->SetText(m_pController->m_FactText);

            // SetColour(RGB(75,50,40)) -- T_988(&col, 0x4b, 0x32, 0x28)
            // Binary layout: [0]=B=0x28=40, [1]=G=0x32=50, [2]=R=0x4b=75, [3]=A=0xFF
            // v1.6.1 @0x00175620
            m_pBodyBox->SetColour(Colour(75, 50, 40, 255), 0);

            // SetHorizontalLineSpacing(-1) -- auto
            // v1.6.1 @0x00175688
            m_pBodyBox->SetHorizontalLineSpacing(-1);
        }
    }

    // SetTranslation(pos + Vec3(-81, 41, 0), 0) then Draw
    // Binary: _Stack_d0=(-81,41,0); _Stack_dc = pos + _Stack_d0
    // v1.6.1 @0x00175694
    if (m_pBodyBox) {
        Vec3 bodyTrans = pos + Vec3(-81.0f, 41.0f, 0.0f);
        m_pBodyBox->SetTranslation(bodyTrans, 0);
        // T_994 = BakedStringBox::Draw(Vec2(1,1), 0.0f, 1)
        m_pBodyBox->Draw(Vec2(1.0f, 1.0f), 0.0f, 1);
    }

    // 4. AUTO-SHRINK measurement loop: measures body text height using the
    // bitmap font (pFontMain=pM_Fonts[1]) and computes a target font size.
    // Binary: loop while GetStringHeight > 96px, decrement by 0.125 each iter.
    // The computed fVar11 is NEVER applied back to m_pBodyBox -- this loop is
    // a binary no-op (dead store). Preserved for binary fidelity.
    // v1.6.1 FruitFactClassicFactPage::DrawOrder @0x001756e8
    {
        Mortar::Font* bitmapFont = game_work.pFontMain.Get();
        float targetFontSize = 16.0f;
        if (bitmapFont && m_pController && m_pController->m_FactText) {
            float h = bitmapFont->GetStringHeight(
                Mortar::Utf8StringIterator(m_pController->m_FactText),
                16.0f,
                128.0f
            );
            while (h > 96.0f) {
                targetFontSize -= 0.125f;
                h = bitmapFont->GetStringHeight(
                    Mortar::Utf8StringIterator(m_pController->m_FactText),
                    targetFontSize,
                    128.0f
                );
            }
        }
        (void)targetFontSize; // result never applied in binary (dead store)
    }
}

// Test support: delete and null both lazy BakedStringBox members so the next
// DrawOrder call rebuilds them from the controller's current m_FactText/m_FactColour.
// Used by test_fruitfact --fact= after-the-fact override; never called by the game.
void FruitFactClassicFactPage::ResetBakedTextBoxes() {
    delete m_pTitleBox;
    m_pTitleBox = NULL;
    delete m_pBodyBox;
    m_pBodyBox = NULL;
}
