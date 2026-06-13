// FruitFactZenPage -- v1.6.1 Zen-mode fruit-fact page.
// Binary refs: ctor 0x0017fcd4, Init 0x00180320, etc.

#include "FruitFactZenPage.h"
#include "engine/asset/TextureManager.h"
#include "engine/render/MatrixManager.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/asset/Mesh.h"
#include "engine/math/Vec3.h"
#include "engine/math/Vec2.h"
#include "engine/math/Colour.h"
#include "engine/util/StringTable.h"
#include "hud/GenericHUDControl.h"
#include "hud/FruitFactControl.h"
#include <cstring>
#include <cstdio>

// Zen-page shared content: two localised textures + a one-shot loaded guard.
// Binary: PC-relative file-static globals (GOT object @ 0x002C1130); members at
// +0x720c (DAT_0017fb38), +0x6c44 (DAT_0017fb3c), guard byte +0x43c08 (DAT_0017fb40).
// LoadContent (@0x0017fa34) sets the guard and fills both via LoadLocalisedTexture;
// UnloadContent (@0x0017fb00) nulls both and clears the guard.
static bool g_ZenContentLoaded = false;
static Mortar::SmartPtr<Mortar::Texture> g_ZenTex720c;  // "blank_dialog_box.tex"
static Mortar::SmartPtr<Mortar::Texture> g_ZenTex6c44;  // "combo_description.tex"

// Shared TTF font pointer (same as FruitFactPage.cpp helper).
// DIFFERS: original = *(g_GameData+0x614) shared face; using a file-local lookup
//   because the port has not extended game_work past 0x608.
static Mortar::FontCacheObjectTTF* GetZenTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Binary @ 0x0017fcd4
FruitFactZenPage::FruitFactZenPage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
    , m_ZenHasCombo(0)
    , m_ZenFruitInfoIdx(0)
    , m_ZenComboCount(0)
    , m_ZenC8(-0.5f)
    , m_ZenStarResult(0xff)
{
    memset(m_ZenPad9C, 0, sizeof(m_ZenPad9C));
    memset(m_ZenPadD1, 0, sizeof(m_ZenPadD1));
}

FruitFactZenPage::~FruitFactZenPage() {
}

// Binary @ 0x0017fa34
void FruitFactZenPage::LoadContent() {
    if (g_ZenContentLoaded) return;
    g_ZenContentLoaded = true;  // set before loading, matching binary @ 0x0017fa5c (guard set prior to the two loads)

    // Strings resolved from the literal pool / GOT at 0x0017fae8 and 0x0017faf0.
    g_ZenTex720c = Mortar::TextureManager::LoadLocalisedTexture("blank_dialog_box.tex");
    g_ZenTex6c44 = Mortar::TextureManager::LoadLocalisedTexture("combo_description.tex");
}

// Binary @ 0x0017fb00
// Symmetric inverse of LoadContent (@0x0017fa34): the two Zen-page textures
// and the "loaded" guard byte are PC-relative file-static globals (base @ GOT
// object 0x002C1130 in the binary), NOT instance members -- LoadContent/
// UnloadContent are static methods with no `this`.
// Disasm: two `bl 0x0017faf8` thunks each call SmartPtr<Texture>::SetPtr(this, NULL)
//   (T.1015 -> SetPtr<Texture>(r1=0)), clearing member +0x720c then member +0x6c44,
//   then `strb #0` writes the guard byte (+0x43c08) back to 0 so a later
//   LoadContent will re-load.
void FruitFactZenPage::UnloadContent() {
    g_ZenTex720c.SetNull();   // binary: clears SmartPtr at base+0x720c (DAT_0017fb38) first
    g_ZenTex6c44.SetNull();   // binary: clears SmartPtr at base+0x6c44 (DAT_0017fb3c)
    g_ZenContentLoaded = false; // binary: guard byte at base+0x43c08 (DAT_0017fb40) = 0
}

// Binary @ 0x00180320 -- builds combo achievement list (hasCombo) or 'play more' message branch.
// DAT consts: head scale=68 (1806d0), spacing=40 (1806d4), maxW=220 (1806d8), clampW=140 (1806dc),
//   starX offset -72 (1806e0), starY=53 (1806e4), starStagger=42 (1806e8), iconY=37 (18070c),
//   fadeOut=1.33 (180710), fade2_zero=0 (180714).
void FruitFactZenPage::Init() {
    m_ZenComboCount = 0;
    m_ZenC8 = -0.5f;
    m_ZenStarResult = 0xff;

    CreateSenseisHead(68.0f);
    CreateHorizontalDivider();
    CreateSenseisFruitFactTitle();
    CreateSenseisFruitFactText();

    // Read combo count from controller's per-session combo state.
    // Binary: ctrl = m_pController; comboCount = *(int*)(*(ctrl+0x50)+0x210).
    // *(ctrl+0x50) is a session-state sub-object pointer embedded in HUDControl (+0x50).
    // TODO: 0x00180320 -- resolve *(m_pController+0x50)+0x210 for comboCount and +0x214
    //   for the fruit-info array. HUDControl at +0x50 falls in m_RemoveCallback area;
    //   the actual type of the pointer held there needs further RE of FruitFactPageControl
    //   binary layout. Leave a zero comboCount until resolved.
    int comboCount = 0;
    bool hasCombo = (comboCount > 2);
    m_ZenHasCombo = hasCombo ? 1 : 0;

    Mortar::FontCacheObjectTTF* font = GetZenTTFFont();

    if (hasCombo) {
        // --- combo-achievement branch ---
        char buf[128];
        snprintf(buf, sizeof(buf), Mortar::GETSTRING(LSTR_BEST_COMBO, 0), comboCount);
        m_ZenComboCount = comboCount;

        // Lay out comboCount fruit icons across the Y axis.
        // spacing=40 (DAT_1806d4), maxWidth=220 (DAT_1806d8).
        // If total > maxWidth, compress spacing to maxWidth/(n-1).
        float spacing = 40.0f;
        float totalW = spacing * (float)(comboCount - 1);
        if (totalW > 220.0f && comboCount > 1) {
            spacing = 220.0f / (float)(comboCount - 1);
            totalW = 220.0f;
        }

        for (int i = 0; i < comboCount; ++i) {
            // TODO: 0x00180320 -- read comboFruitInfo[i] from *(ctrl+0x50)+0x214+i*4
            //   and call Fruit::FruitInfo(fruitIdx) to resolve fruit icon texture.
            //   m_ZenFruitInfoIdx = fruitTypeArray[i]; Fruit::FruitInfo(m_ZenFruitInfoIdx).
            //   Copy TranisitionInfo default block (T_1022) into c+0x28 (m_PosTrans).
            float x = (-totalW * 0.5f - 8.0f) + (float)i * spacing;
            Vec3 ipos(x, 37.0f, 0.0f);
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            Vec3 sc(1.0f, 1.0f, 1.0f);
            Colour col(1.0f, 1.0f, 1.0f, 1.0f);
            float fadeIn  = (float)i * 0.25f;
            float fadeOut = (float)i * 0.25f + 0.25f;
            GenericHUDControl* c = new GenericHUDControl(fadeIn, fadeOut, emptyTex, NULL, ipos, sc, col, 8);
            // TODO: 0x00180320 -- AddSound(DAT_1806f4 sound name, min(i,8)/12.0f, 1.0f)
            AddGenericControl(c);
        }

        // Combo star result.
        // TODO: 0x00180320 -- pass &m_ZenFruitInfoIdx and comboCount from the real fruit array;
        //   currently uses placeholder zero array because the fruit-info reads are unresolved.
        int dummyFruitArr[1] = {0};
        int outDominant = 0;
        uint8_t star = FruitFact::CheckCombo(dummyFruitArr, comboCount > 0 ? comboCount : 1, &outDominant);
        m_ZenStarResult = star;
        Mortar::SmartPtr<Mortar::Texture> starTex = FruitFact::GetComboStarTexture(star);

        // Star icon GenericHUDControl.
        // pos.Y derived from totalW: if totalW < 140 use -72 offset else 42 (DAT_1806dc/1806e8).
        float starPosY = 37.0f;
        float starX;
        if (totalW < 140.0f) {
            starX = totalW * 0.5f + (-72.0f);   // DAT_1806e0 = -72
        } else {
            starX = 42.0f;                        // DAT_1806e8 = 42
        }
        Vec3 starPos(starX, 53.0f, 0.0f);        // DAT_1806e4 = 53
        float starFadeIn  = (float)comboCount * 0.25f;
        float starFadeOut = starFadeIn + 0.5f;
        Vec3 scStar(1.0f, 1.0f, 1.0f);
        Colour colStar(1.0f, 1.0f, 1.0f, 1.0f);
        GenericHUDControl* cStar = new GenericHUDControl(starFadeIn, starFadeOut, starTex, NULL, starPos, scStar, colStar, 0x400);
        // TODO: 0x00180320 -- copy TranisitionInfo default (T_1022) into cStar+0x28 (m_PosTrans)
        // TODO: 0x00180320 -- AddSound(DAT_180704 sound name, 1.0f, starFadeOut)
        AddGenericControl(cStar);

        // Star label: BakedStringBox with stroked text.
        if (font) {
            Vec3 labelPos(starX, starPosY, 0.0f);
            Mortar::SmartPtr<Mortar::Texture> emptyLabel;
            Vec3 scLabel(1.0f, 1.0f, 1.0f);
            Colour colLabel(1.0f, 1.0f, 1.0f, 1.0f);
            GenericHUDControl* cLabel = new GenericHUDControl(starFadeIn, starFadeOut, emptyLabel, NULL, labelPos, scLabel, colLabel, 0x400);

            Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
                font, 10.0f, 128.0f, 10.0f, 0xf, 3, 5.0f);

            // GetComboStarText returns a LSTR id; format it into buf.
            unsigned int starStrId = FruitFact::GetComboStarText(star);
            // TODO: 0x00180320 -- apply DAT_1806f8 format string via OS_SPrintf:
            //   snprintf(buf, sizeof(buf), GETSTRING(DAT_1806f8_lstr, 0), starTextStr)
            //   For now, use the star text directly without format wrapper.
            const char* starText = (starStrId > 0) ? Mortar::GETSTRING((LocalizedString)starStrId, 0) : "";
            box->SetText(starText ? starText : "");
            // SetStroke 1-colour form: binary @ 0x00245314
            box->SetStroke(2.0f, Colour(0x83, 0x40, 0x5e, 255));
            box->SetGradient(
                Colour(0xf8, 0xf3, 0xdf, 255),
                Colour(0xf5, 0xef, 100,  255),
                false);
            box->SetHorizontalLineSpacing(-1.0f);
            cLabel->SetText(box);
            cLabel->SetAngle(-20.0f, 0.0f);
            AddGenericControl(cLabel);
        }

    } else {
        // --- no-combo branch: single "play more" style message ---
        char buf[128];
        const char* noComboStr = Mortar::GETSTRING(LSTR_ZEN_NO_COMBO_LINE1, 0);
        strncpy(buf, noComboStr ? noComboStr : "", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        // ctrl 1 (icon): pos=Vec3(-8,37,0), scale unit, white, flags=8,
        // fade=1.0, fadeOut=1.33 (DAT_180710).
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            Vec3 c1pos(-8.0f, 37.0f, 0.0f);
            Vec3 sc(1.0f, 1.0f, 1.0f);
            Colour col(1.0f, 1.0f, 1.0f, 1.0f);
            GenericHUDControl* c1 = new GenericHUDControl(1.0f, 1.33f, emptyTex, NULL, c1pos, sc, col, 8);
            // TODO: 0x00180320 -- copy TranisitionInfo default (T_1022) into c1+0x28 (m_PosTrans)
            // TODO: 0x00180320 -- AddSound(DAT_180704 sound name, 1.0f, 1.0f+something)

            if (font) {
                // box1: width=160(0xa0), height=40(0x28), align=0xf, wrap=3, ls=7
                Mortar::BakedStringBox* box1 = new Mortar::BakedStringBox(
                    font, 10.0f, 160.0f, 40.0f, 0xf, 3, 7.0f);
                // SetStroke 3-colour form: binary @ 0x002453f0
                box1->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box1->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                box1->SetText(Mortar::GETSTRING(LSTR_ZEN_NO_COMBO_BODY, 0));
                box1->SetHorizontalLineSpacing(-1.0f);
                c1->SetText(box1);
                // Recenter c1 pos by -(box.w/2, box.h/2):
                // pos -= Vec3(box->field_0x24/2, box->field_0x28/2, 0)
                // Using the declared box width/height (160, 40) as the centering values.
                c1pos.x -= 80.0f;
                c1pos.y -= 20.0f;
                // TODO: 0x00180320 -- above centering uses declared dims; binary reads
                //   the actual baked box dimensions from box->field_0x24 / field_0x28
                //   which may differ from ctor args if FitIntoVerticalBounds ran.
            }
            AddGenericControl(c1);
        }

        // ctrl 2 + box2 (second message line).
        // TODO: 0x00180320 -- resolve DAT_180ed0 (pos X), DAT_180ed4 (pos Y or Z),
        //   DAT_180ed8 (fadeOut), DAT_180ee8 (text string); port skeleton below.
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            Vec3 c2pos(-8.0f, 0.0f, 0.0f);   // TODO: 0x00180320 -- pos from DAT_180ed0/DAT_180ed4
            Vec3 sc(1.0f, 1.0f, 1.0f);
            Colour col(1.0f, 1.0f, 1.0f, 1.0f);
            float c2FadeOut = 1.0f;            // TODO: 0x00180320 -- DAT_180ed8
            GenericHUDControl* c2 = new GenericHUDControl(1.0f, c2FadeOut, emptyTex, NULL, c2pos, sc, col, 8);

            if (font) {
                Mortar::BakedStringBox* box2 = new Mortar::BakedStringBox(
                    font, 10.0f, 173.0f, 10.0f, 0xf, 3, 7.0f);  // width=0xad=173
                box2->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box2->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                // TODO: 0x00180320 -- text from DAT_180ee8 string (unresolved); using empty for now
                box2->SetText("");
                box2->SetHorizontalLineSpacing(-1.0f);
                c2->SetText(box2);
            }
            AddGenericControl(c2);
        }

        // ctrl 3 + box3 (third message line).
        // TODO: 0x00180320 -- resolve pos, fadeOut, text for ctrl 3 (same DAT block as ctrl 2).
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            Vec3 c3pos(-8.0f, -30.0f, 0.0f);  // TODO: 0x00180320 -- pos from DAT block
            Vec3 sc(1.0f, 1.0f, 1.0f);
            Colour col(1.0f, 1.0f, 1.0f, 1.0f);
            float c3FadeOut = 1.0f;             // TODO: 0x00180320 -- DAT
            GenericHUDControl* c3 = new GenericHUDControl(1.0f, c3FadeOut, emptyTex, NULL, c3pos, sc, col, 8);

            if (font) {
                Mortar::BakedStringBox* box3 = new Mortar::BakedStringBox(
                    font, 10.0f, 173.0f, 10.0f, 0xf, 3, 7.0f);
                box3->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box3->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                // TODO: 0x00180320 -- text from DAT block (third line); using empty for now
                box3->SetText("");
                box3->SetHorizontalLineSpacing(-1.0f);
                c3->SetText(box3);
            }
            AddGenericControl(c3);
        }
    }

    // Page title (banner buf set above by combo/no-combo branch).
    // TODO: 0x00180320 -- DAT_00180eec is the page title string (fmt-printed buf from
    //   combo/no-combo snprintf); pass the filled buf here. For now the buf contains
    //   the correct string but DAT_180eec is unresolved (could be a wrapper format string).
    //   If combo, buf = GETSTRING(LSTR_BEST_COMBO, comboCount); if no-combo, buf = GETSTRING(LSTR_ZEN_NO_COMBO_LINE1).
    char titleBuf[128];
    if (hasCombo) {
        snprintf(titleBuf, sizeof(titleBuf), Mortar::GETSTRING(LSTR_BEST_COMBO, 0), comboCount);
    } else {
        const char* s = Mortar::GETSTRING(LSTR_ZEN_NO_COMBO_LINE1, 0);
        strncpy(titleBuf, s ? s : "", sizeof(titleBuf) - 1);
        titleBuf[sizeof(titleBuf) - 1] = '\0';
    }
    CreateTitleTextControl(titleBuf);
}

// Binary @ 0x0017fa04
void FruitFactZenPage::Update(float /*dt*/) {
    // TODO: 0x0017fa04 -- per-frame update for zen page
}

// Binary @ 0x00180ef0
void FruitFactZenPage::DrawOrder(const Vec3& /*hudScale*/, int /*layerMask*/) {
    // Binary @ 0x00180ef0: if the page's cached render-texture (HUDControl3d::m_Texture,
    // binary +0x74) is non-null, draw it as a full-screen-quad backing for the page.
    if (!m_Texture) {
        return;
    }

    // vtable slot +0xc -> Texture::SetUnCached() (bind for drawing); binary @ 0x00188da4.
    m_Texture->SetUnCached();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();  // binary MatrixStack::Reset on World stack (+0x1090)

    // Scale to (width+1, height+1, 0). Binary reads m_Width (+0x24) / m_Height (+0x28),
    // adds 1, converts unsigned->float, builds Vec3(w, h, 0.0f), then multiplies by 1.0f.
    Vec3 sz((float)(m_Texture->m_Width + 1),
            (float)(m_Texture->m_Height + 1),
            0.0f);                         // DAT_00181050 = 0.0f
    Vec3 scaled = sz * 1.0f;               // binary local_24 = 1.0f scalar multiply
    mm.GetWorldStack().Scale(scaled);

    // Translate to pos - (8, -8, 0). Binary: anchor Vec3(8.0f, -8.0f, 0.0f)
    // (0x41000000, 0xc1000000, DAT_00181050); t = (*(Vec3*)(this+8)) - anchor.
    Vec3 anchor(8.0f, -8.0f, 0.0f);
    Vec3 t = pos - anchor;                 // pos = HUDControl::pos (binary this+0x8)
    mm.GetWorldStack().Translate(t);

    mm.UploadModelViewOnly();              // binary _UploadCurrentMatrices(this, 1)

    // Draw the full texture quad in white. Binary copies the engine global white
    // Colour (GOT entry @ 0x002d81f8 -> Colour @ 0x002d0398, runtime-init 255,255,255,255 --
    // same global used by BaseScreen/AboutScreen/DojoScreen page draws).
    // DrawQuadUnCached(colour, u0=0.0, v0=1.0, u1=0.0, v1=1.0, fx=NULL) -> full texture.
    Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255),
                                   0.0f, 1.0f, 0.0f, 1.0f, NULL);

    // vtable slot +0x10 -> Texture::UnSetUnCached() (unbind); binary @ 0x00188d9c.
    m_Texture->UnSetUnCached();
}

// Binary @ 0x0017fb44
// add r0,r0,#0xd0 ; b 0x0017faf8  ->  mov r1,#0 ; b SmartPtr<Texture>::SetPtr (0x00104fb0)
// i.e. the whole body is: m_TexZen.SetPtr(NULL) on the SmartPtr<Texture> at +0xd0.
// DIFFERS: the prior stub called FruitFactPage::Release() first; the binary does NOT
// chain to any base Release (FruitFactPage/BaseScreen declare no Release vtable slot).
// The single observable action is releasing the zen-page texture reference.
void FruitFactZenPage::Release() {
    m_TexZen.SetPtr(nullptr);
}
