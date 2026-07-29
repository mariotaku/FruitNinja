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
#include "engine/math/_Vector3.h"
#include "engine/math/_Vector2.h"
#include "engine/math/Colour.h"
#include "engine/util/StringTable.h"
#include "hud/GenericHUDControl.h"
#include "hud/FruitFactCombo.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "entities/Fruit.h"
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
// v1.6.1: reads game_work.m_pTTFFontMain (GameWork+0x614, the locale face
//   PreloadFontsTTF @0x0011c1fc sets to arabic.ttf when languageFlag==0x14,
//   else gangofchinese.ttf). Falls back to a lazily-created gangofchinese.ttf
//   only if PreloadFontsTTF hasn't run yet.
static Mortar::FontCacheObjectTTF* GetZenTTFFont() {
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Binary @ 0x0017fcd4
FruitFactZenPage::FruitFactZenPage(FruitFactControl* pCtrl)
    : FruitFactPage(pCtrl)
    , m_HasUnlockedFacts(0)
    , m_NumFacts(0)
    , m_StarBias(-0.5f)
    , m_ComboLevel(0xff)
{
    _pad99[0] = 0; _pad99[1] = 0; _pad99[2] = 0;
    memset(m_Facts, 0, sizeof(m_Facts));
    // ASM-spec v1.6.1 FruitFactZenPage::FruitFactZenPage @0x0017fcd4: final call is LoadContent(this)
    LoadContent();
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
// DAT consts: head scale=68 (1806d0), spacing=40.0 (1806d4), maxW=220.0 (1806d8), clampW=140.0 (1806dc),
//   starXoff=-72.0 (1806e0), starY=53.0 (1806e4), starStagger=42.0 (1806e8), iconY=37.0 (18070c),
//   fadeOut=1.33 (180710), fade2_zero=0.0 (180714).
// Session state @ binary Game+0x50 = FruitSaveData (port: game_work.m_SaveData).
// +0x210 = m_BestComboLength, +0x214 = m_BestComboFruits[].
void FruitFactZenPage::Init() {
    m_NumFacts = 0;
    m_StarBias = -0.5f;
    m_ComboLevel = 0xff;

    CreateSenseisHead(68.0f);
    CreateHorizontalDivider();
    CreateSenseisFruitFactTitle();
    CreateSenseisFruitFactText();

    // Read best-combo session data from the Game singleton's save-data object.
    // Binary: *(*(GOT+0x77F4) + 0x50) = the active FruitSaveData.
    // +0x210 = m_BestComboLength, +0x214 = m_BestComboFruits int array.
    int comboCount = 0;
    int* comboArr = NULL;
    if (game_work.m_SaveData) {
        comboCount = game_work.m_SaveData->m_BestComboLength;
        comboArr   = game_work.m_SaveData->m_BestComboFruits;
    }
    bool hasCombo = (comboCount > 2);
    m_HasUnlockedFacts = hasCombo ? 1 : 0;

    Mortar::FontCacheObjectTTF* font = GetZenTTFFont();

    // Shared banner buffer; combo/no-combo branch writes into it; CreateTitleTextControl reads it.
    // Binary: *(GOT+0x71E0) is the same static char* used for both the OS_SPrintf and the title.
    char banner[128];
    banner[0] = '\0';

    if (hasCombo) {
        // --- combo-achievement branch ---
        snprintf(banner, sizeof(banner), GETSTRING(LSTR_BEST_COMBO, 0), comboCount);
        m_NumFacts = comboCount;

        // Lay out comboCount fruit icons.
        // spacing=40.0 (DAT_1806d4), maxW=220.0 (DAT_1806d8).
        // Binary ARM comparison: span < maxW is FALSE when span >= maxW -> compress.
        float spacing = 40.0f;
        float span    = spacing * (float)(comboCount - 1);
        if (span >= 220.0f) {
            spacing = 220.0f / (float)(comboCount - 1);
            span    = 220.0f;
        }

        float fade = 0.25f;
        for (int i = 0; i < comboCount; ++i) {
            // Fill m_Facts from the session combo array (Game+0x50+0x214+i*4).
            if (comboArr != NULL) {
                m_Facts[i] = comboArr[i];
            } else {
                m_Facts[i] = 0;
            }
            // ASM-spec v1.6.1 FruitFactZenPage::Init @0x00180320: icon tex = FruitInfo(m_Facts[i])->m_ZenTexture (+0x304); scale arg = Vec3::Zero (auto-size).
            // m_Facts[i] defaults to -1 (FruitSaveData::m_BestComboFruits sentinel, see fill loop
            // above) and otherwise holds a live m_FruitType (always in range) -- only the negative
            // sentinel needs guarding; FruitInfo_Get no longer bounds-checks.
            Mortar::SmartPtr<Mortar::Texture> iconTex;
            if (m_Facts[i] >= 0) {
                iconTex = Fruit::FruitInfo(m_Facts[i])->m_ZenTexture;
            }

            float x = (span * -0.5f - 8.0f) + (float)i * spacing;
            _Vector3<float> ipos(x, 37.0f, 0.0f);             // iconY=37.0 (DAT_18070c)
            _Vector3<float> sc(0.0f, 0.0f, 0.0f);             // Vec3::Zero -> auto-size from texture dims
            Colour col(255, 255, 255, 255);
            GenericHUDControl* c = new GenericHUDControl(fade, fade + 0.25f, iconTex, NULL, ipos, sc, col, 8);
            // Binary: T_1022 default block (all zeros) memcpy'd to c+0xa4 (m_ScaleTrans, 5 words).
            // GenericHUDControl ctor already zero-initializes m_ScaleTrans; no-op here.
            int si = (i < 8) ? i : 8;
            c->AddSound("popup-1", 1.0f, (float)si / 12.0f);  // DAT_001806f4 @ 0x00282606
            AddGenericControl(c);
            fade += 0.25f;
        }

        // Combo star classification from the just-filled m_Facts array.
        int outDominant = 0;
        m_ComboLevel = FruitFact::CheckCombo(m_Facts, comboCount, &outDominant);
        // Binary stores GetComboStarTexture's result in the write-only member at +0xd0
        // (m_pComboStarTexture) and NEVER draws it -- the star visual is the "* {name}"
        // font text below, not this sprite. Keeping the call faithful holds the ref.
        // (On Wii GetComboStarTexture skips the actual disk load -- see its body --
        // since the result is never drawn; the call/RNG side effect is preserved.)
        m_pComboStarTexture = GetComboStarTexture((COMBO_TYPE)m_ComboLevel);

        // Star position: if span < 140.0 (DAT_1806dc), use compressed X; else stagger=42.0 (DAT_1806e8).
        float starX;
        bool narrow = (span < 140.0f);
        starX = 42.0f;                                // DAT_1806e8
        if (narrow) {
            starX = 28.0f;                            // literal 28.0 in binary
        }
        if (narrow) {
            starX = span * 0.5f - starX;
        }
        _Vector3<float> starPos(starX, 53.0f, 0.0f);             // starY=53.0 (DAT_1806e4)
        float starFadeIn  = fade;
        float starFadeOut = starFadeIn + 0.5f;
        _Vector3<float> scStar(0.0f, 0.0f, 0.0f);  // auto-size from texture dims (binary: callers pass zero scale)
        Colour colStar(255, 255, 255, 255);
        // Star + label are the same GenericHUDControl in the binary (pGVar5). The binary
        // builds this control with NO texture -- the star sprite is never drawn; only the
        // "* {localized name}" BakedStringBox text below renders. Pass an empty texture.
        Mortar::SmartPtr<Mortar::Texture> emptyStarTex;
        GenericHUDControl* cStar = new GenericHUDControl(starFadeIn, starFadeOut, emptyStarTex, NULL, starPos, scStar, colStar, 0x400);
        // Binary: T_1022 default block memcpy'd to cStar+0x28 (6 words = 0x18 bytes).
        // GenericHUDControl ctor already zero-initializes all TranisitionInfo/PulseInfo fields; no-op.
        cStar->AddSound("achievement", 1.0f, 0.0f);  // DAT_00180704 @ 0x0027F593

        if (font) {
            Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
                font, 10.0f, 128.0f, 10.0f, (Mortar::ALIGNMENT_TYPE)0xf, 3, 5);
            // Binary: OS_SPrintf(buf, 0x200, "* %s", GetComboStarText(m_ComboLevel))
            // GetComboStarText returns a LSTR id; GETSTRING converts to a C string.
            unsigned int starStrId = GetComboStarText((COMBO_TYPE)m_ComboLevel);
            const char* starText = (starStrId > 0) ? GETSTRING((LocalizedString)starStrId, 0) : "";
            char starBuf[512];
            snprintf(starBuf, sizeof(starBuf), "* %s", starText ? starText : "");  // DAT_001806f8 @ 0x0028260E
            box->SetText(starBuf);
            box->SetStroke(2.0f, Colour(0x83, 0x40, 0x5e, 255));
            box->SetGradient(
                Colour(0xf8, 0xf3, 0xdf, 255),
                Colour(0xf5, 0xef, 0x64, 255),
                false);
            box->SetHorizontalLineSpacing(-1);
            cStar->SetText(box);
            cStar->SetAngle(-20.0f, 0.0f);
        }
        AddGenericControl(cStar);

    } else {
        // --- no-combo branch: "play more" style message ---
        // Binary: strcpy(*(GOT+0x71E0), GETSTRING(LSTR_ZEN_NO_COMBO_LINE1)) sets shared banner.
        const char* noComboStr = GETSTRING(LSTR_ZEN_NO_COMBO_LINE1, 0);
        strncpy(banner, noComboStr ? noComboStr : "", sizeof(banner) - 1);
        banner[sizeof(banner) - 1] = '\0';

        // ctrl 1: pos=(-8, 37, 0) (DAT_180ed0=-8, DAT_180ed4=37/0), fadeOut=1.33 (DAT_180710).
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            _Vector3<float> c1pos(-8.0f, 37.0f, 0.0f);
            _Vector3<float> sc(1.0f, 1.0f, 1.0f);
            Colour col(255, 255, 255, 255);
            GenericHUDControl* c1 = new GenericHUDControl(1.0f, 1.33f, emptyTex, NULL, c1pos, sc, col, 8);
            // Binary: T_1022 all-zeros block; already zero-initialized by ctor; no-op.
            c1->AddSound("achievement", 1.0f, 0.0f);  // DAT_00180704 @ 0x0027F593

            if (font) {
                // box1: width=0xa0=160, height=0x28=40, align=0xf, wrap=3, ls=7
                Mortar::BakedStringBox* box1 = new Mortar::BakedStringBox(
                    font, 10.0f, 160.0f, 40.0f, (Mortar::ALIGNMENT_TYPE)0xf, 3, 7);
                box1->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box1->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                box1->SetText(GETSTRING(LSTR_ZEN_NO_COMBO_BODY, 0));
                box1->SetHorizontalLineSpacing(-1);
                c1->SetText(box1);
                // Binary reads actual baked dims box->+0x24 / +0x28 to recenter.
                // TODO: v1.6.1 FruitFactZenPage::Init @0x00180320 -- c1pos.x -= box1->m_BakedWidth/2; c1pos.y -= box1->m_BakedHeight/2
                //   (field_0x24 / field_0x28 in BakedStringBox); using declared ctor dims (160, 40) as
                //   approximation until BakedStringBox layout is confirmed.
                c1->m_BasePos.x -= 80.0f;
                c1->m_BasePos.y += 20.0f;   // binary subtracts (w/2, -h/2, 0) => +h/2 on Y
            }
            AddGenericControl(c1);
        }

        // ctrl 2: pos=(-8, 37, 0), fadeOut=1.33 (DAT_180ed8). Text=DAT_180ee8="_ " (separator line).
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            _Vector3<float> c2pos(-8.0f, 37.0f, 0.0f);          // DAT_180ed0=-8.0, DAT_180ed4=37.0
            _Vector3<float> sc(1.0f, 1.0f, 1.0f);
            Colour col(255, 255, 255, 255);
            GenericHUDControl* c2 = new GenericHUDControl(1.0f, 1.33f, emptyTex, NULL, c2pos, sc, col, 8);

            if (font) {
                // box2: width=0xad=173, height=0x28=40, align=0xf, wrap=3, ls=7
                Mortar::BakedStringBox* box2 = new Mortar::BakedStringBox(
                    font, 10.0f, 173.0f, 40.0f, (Mortar::ALIGNMENT_TYPE)0xf, 3, 7);
                box2->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box2->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                box2->SetText("_");                   // DAT_00180ee8 @ 0x00281E11 = "_"
                box2->SetHorizontalLineSpacing(-1);
                c2->SetText(box2);
                c2->m_BasePos.x -= 173.0f;
                c2->m_BasePos.y += 20.0f;
            }
            AddGenericControl(c2);
        }

        // ctrl 3: same layout as ctrl 2 per binary (same DAT block, same text "_").
        {
            Mortar::SmartPtr<Mortar::Texture> emptyTex;
            _Vector3<float> c3pos(-8.0f, 37.0f, 0.0f);          // same pos as ctrl 2 per binary
            _Vector3<float> sc(1.0f, 1.0f, 1.0f);
            Colour col(255, 255, 255, 255);
            GenericHUDControl* c3 = new GenericHUDControl(1.0f, 1.33f, emptyTex, NULL, c3pos, sc, col, 8);

            if (font) {
                // box3: same dims as box2
                Mortar::BakedStringBox* box3 = new Mortar::BakedStringBox(
                    font, 10.0f, 173.0f, 40.0f, (Mortar::ALIGNMENT_TYPE)0xf, 3, 7);
                box3->SetStroke(2.0f,
                    Colour(0xff, 0xff, 0xf4, 255),
                    Colour(0xff, 0xfc, 0x14, 255),
                    Colour(0xc8, 0x82, 0x00, 255));
                box3->SetColour(Colour(0x97, 0x51, 0x1e, 255), 0);
                box3->SetText("_");                   // DAT_00180ee8 @ 0x00281E11 = "_"
                box3->SetHorizontalLineSpacing(-1);
                c3->SetText(box3);
            }
            AddGenericControl(c3);
        }
    }

    // Page title from the shared banner buf written above by either branch.
    // Binary: DAT_00180eec = DAT_001806f0 = same GOT ptr as the OS_SPrintf/strcpy target.
    CreateTitleTextControl(banner);
}

// Binary @ 0x0017fa04
// ASM-spec v1.6.1 FruitFactZenPage::Update @ 0x0017fa04:
//   FruitFactPage::Update(dt); then m_Texture = g_ZenTex720c (SmartPtr at GOT+0x720c =
//   "blank_dialog_box.tex" loaded in LoadContent). Without this, m_Texture stays NULL and
//   DrawOrder (@0x00180ef0) skips the board quad -> invisible Zen board.
void FruitFactZenPage::Update(float dt) {
    FruitFactPage::Update(dt);
    m_Texture = g_ZenTex720c;
}

// Binary @ 0x00180ef0
void FruitFactZenPage::DrawOrder(float* /*hudScaleRaw*/, int /*layerMask*/) {
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
    _Vector3<float> sz((float)(m_Texture->GetWidth() + 1),
                       (float)(m_Texture->GetHeight() + 1),
                       0.0f);                         // DAT_00181050 = 0.0f
    _Vector3<float> scaled = sz * 1.0f;               // binary local_24 = 1.0f scalar multiply
    mm.GetWorldStack().Scale(scaled);

    // Translate to pos - (8, -8, 0). Binary: anchor Vec3(8.0f, -8.0f, 0.0f)
    // (0x41000000, 0xc1000000, DAT_00181050); t = (*(Vec3*)(this+8)) - anchor.
    _Vector3<float> anchor(8.0f, -8.0f, 0.0f);
    _Vector3<float> t = pos - anchor;                 // pos = HUDControl::pos (binary this+0x8)
    mm.GetWorldStack().Translate(t);

    mm.UploadModelViewOnly();              // binary _UploadCurrentMatrices(this, 1)

    // Draw the full texture quad in white. Binary copies the engine global white
    // Colour (GOT entry @ 0x002d81f8 -> Colour @ 0x002d0398, runtime-init 255,255,255,255 --
    // same global used by BaseScreen/AboutScreen/DojoScreen page draws).
    // DrawQuadUnCached(colour, uMin=0.0, uMax=1.0, vMin=0.0, vMax=1.0, fx=NULL) -> full texture.
    Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255),
                                   0.0f, 1.0f, 0.0f, 1.0f, NULL);

    // vtable slot +0x10 -> Texture::UnSetUnCached() (unbind); binary @ 0x00188d9c.
    m_Texture->UnSetUnCached();
}

// Binary @ 0x0017fb44
// add r0,r0,#0xd0 ; b 0x0017faf8  ->  mov r1,#0 ; b SmartPtr<Texture>::SetPtr (0x00104fb0)
// i.e. the whole body is: m_pComboStarTexture.SetPtr(NULL) on the SmartPtr<Texture> at +0xd0.
// DIFFERS: the prior stub called FruitFactPage::Release() first; the binary does NOT
// chain to any base Release (FruitFactPage/BaseScreen declare no Release vtable slot).
// The single observable action is releasing the zen-page combo-star texture reference.
void FruitFactZenPage::Release() {
    m_pComboStarTexture.SetPtr(nullptr);
}
