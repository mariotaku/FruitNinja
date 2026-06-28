// FruitFactBonusFactPage -- v1.6.1 bonus-mode fact page.
// Binary refs: ctor 0x001743b8.

#include "FruitFactBonusFactPage.h"
#include "hud/GenericHUDControl.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Mesh.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/render/MatrixManager.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/util/StringTable.h"
#include "game/BonusManager.h"
#include "game/Bonus.h"
#include <cstdio>
#include <list>

// Shared TTF font pointer (mirrors FruitFactPage.cpp GetPageTTFFont()).
// DIFFERS: original = *(g_GameData+0x614) shared face owned by GameContext;
//   using a file-local SmartPtr<Font> + FontTTFRegistry::Lookup because
//   the port has not extended game_work past 0x608 to carry the +0x614 slot.
static Mortar::FontCacheObjectTTF* GetBonusTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// Binary @ 0x001743b8
FruitFactBonusFactPage::FruitFactBonusFactPage(FruitFactControl* pCtrl)
    : FruitFactPage(pCtrl)
{
}

FruitFactBonusFactPage::~FruitFactBonusFactPage() {
}

// Binary @ 0x001743b8 (ctor body mapped to Init() in the port).
// Builds the bonus fact page: bg tex, title, divider, sensei fact title/text,
// up to 3 bonus rows (icon + name + value), then sensei head.
// Row colours (BGRA, alpha=0xFF): gold=(R=0x00,G=0x7e,B=0xad), red=(0x05,0x05,0xa0),
// blue=(0x95,0x5c,0x01). GenericHUDControl sizeof=0x1d8, BakedStringBox sizeof=0xc8.
void FruitFactBonusFactPage::Init() {
    // Page background texture.
    // TODO: v1.6.1 FruitFactBonusFactPage @0x001743b8 -- resolve bg tex name from DAT_17484c string pool;
    //   sibling pages use "blank_dialog_box.tex" or similar -- reuse until confirmed.
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        Mortar::TextureManager::LoadLocalisedTexture("blank_dialog_box.tex");
    m_Texture = bgTex;

    // Title + layout helpers (all base helpers, binary-faithful call order).
    CreateTitleTextControl(GETSTRING(LSTR_BONUS_PAGE_TITLE, 0));
    CreateHorizontalDivider();
    CreateSenseisFruitFactTitle();
    CreateSenseisFruitFactText();

    // Row tint colours (T_1023 BGRA array, alpha forced 0xFF).
    // Ctor constant array at binary GOT offset; 3 entries.
    Colour rowCol[3];
    rowCol[0] = Colour(0x00, 0x7e, 0xad, 0xff); // gold
    rowCol[1] = Colour(0x05, 0x05, 0xa0, 0xff); // red
    rowCol[2] = Colour(0x95, 0x5c, 0x01, 0xff); // blue

    // Shared constants for all row controls (DAT_17485c, DAT_174860).
    // scZero triggers auto-size from texture dims for icon controls (binary: callers pass zero scale).
    Vec3 scZero(0.0f, 0.0f, 0.0f);
    Vec3 scUnit(1.0f, 1.0f, 1.0f);
    Colour white(1.0f, 1.0f, 1.0f, 1.0f);

    // Column anchor positions (DAT_174830..17483c, Z=0).
    Vec3 iconPos(95.0f, 58.0f, 0.0f);   // DAT_174830=95, DAT_174834=58
    Vec3 namePos(-100.0f, 62.0f, 0.0f); // DAT_174838=-100, DAT_17483c=62

    // DIFFERS: original reads g_GameData+3 (a languageFlag/bool byte) and uses
    //   it to nudge the value column X by 8px (fVar10=8 if byte==0, else 0).
    //   Port defaults to the byte==0 branch (fVar10=0.0, no shift).
    //   Binary valPos.X = -118.0 - fVar10 (DAT_174840=-118).
    //   Value final position is namePos + Vec3(184,2,0) per binary pseudocode.

    Mortar::FontCacheObjectTTF* font = GetBonusTTFFont();

    std::list<Bonus>::iterator it;
    Bonus* b = BonusManager::GetInstance()->GetFirstBestBonus(it);

    int row = 0;
    while (b != 0 && row < 3) {
        Colour* tint = &rowCol[row];

        // (a) Star / icon control (DAT_174834=58 Y, DAT_174830=95 X).
        // Binary ctor @0x001743b8: SmartPtr::SmartPtr(&dst, (SmartPtr*)(bonus + 0xD0))
        // Bonus::m_StarTexture is at +0xD0 (confirmed from Bonus.h layout assert).
        Mortar::SmartPtr<Mortar::Texture> iconTex = b->m_StarTexture;
        GenericHUDControl* cIcon = new GenericHUDControl(
            0.0f, 0.0f, iconTex, NULL, iconPos, scZero, white, 8);
        AddGenericControl(cIcon);

        // (b) Format value string from bonus->m_Tier (+0x3c).
        // Binary: OS_SPrintf(buf, 0x40, DAT_174858 fmt, *(int*)(bonus+0x3c))
        char buf[64];
        snprintf(buf, sizeof(buf), "%i", b->m_Tier);

        if (font) {
            // (c) Name control: BakedStringBox carrying m_DisplayName (+0x80).
            // fontSize=10, w=160(0xa0), h=10, align=1, wrap=1, ls=0.
            Mortar::SmartPtr<Mortar::Texture> noTex1;
            Mortar::BakedStringBox* nameBox = new Mortar::BakedStringBox(
                font, 10.0f, 160.0f, 10.0f, 1, 1, 0.0f);
            nameBox->SetColour(*tint, 0);
            nameBox->SetText(b->m_DisplayName);
            GenericHUDControl* cName = new GenericHUDControl(
                0.0f, 0.0f, noTex1, NULL, namePos, scUnit, white, 8);
            cName->SetText(nameBox);
            AddGenericControl(cName);

            // (d) Value control: BakedStringBox carrying formatted tier string.
            // fontSize=10, w=20(0x14), h=10, align=0xf, wrap=1, ls=0.
            // valFinal = namePos + Vec3(184, 2, 0) (DAT_174864=184, 0x40000000=2).
            Mortar::SmartPtr<Mortar::Texture> noTex2;
            Vec3 valOff(184.0f, 2.0f, 0.0f);
            Vec3 valFinal = namePos + valOff;
            Mortar::BakedStringBox* valBox = new Mortar::BakedStringBox(
                font, 10.0f, 20.0f, 10.0f, 0xf, 1, 0.0f);
            valBox->SetColour(*tint, 0);
            valBox->SetText(buf);
            GenericHUDControl* cVal = new GenericHUDControl(
                0.0f, 0.0f, noTex2, NULL, valFinal, scUnit, white, 8);
            cVal->SetText(valBox);
            AddGenericControl(cVal);
        }

        // Advance to next bonus and step row down 20px (DAT column pitch).
        b = BonusManager::GetInstance()->GetNextBestBonus(it);
        row++;
        iconPos.y -= 20.0f;
        namePos.y -= 20.0f;
    }

    // Sensei head at scale 68.0 (DAT_17486c).
    CreateSenseisHead(68.0f);

    // BaseScreen page-state slot at this+0x30 = HUDControl::m_Active, set to 9.
    m_Active = 9;
}

// ASM-spec v1.6.1 FruitFactBonusFactPage::DrawOrder @0x001749e0: same as ZenPage DrawOrder + pass==1 guard.
// Scale(w+1,h+1,0); Translate(pos-Vec3(8,-8,0)); DrawQuadUnCached(White,0,1,0,1); SetUnCached/UnSetUnCached bracket.
void FruitFactBonusFactPage::DrawOrder(float* /*hudScaleRaw*/, int pass) {
    if (pass != 1) return;

    if (!m_Texture) {
        return;
    }

    m_Texture->SetUnCached();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Vec3 sz((float)(m_Texture->GetWidth() + 1),
            (float)(m_Texture->GetHeight() + 1),
            0.0f);
    Vec3 scaled = sz * 1.0f;
    mm.GetWorldStack().Scale(scaled);

    Vec3 anchor(8.0f, -8.0f, 0.0f);
    Vec3 t = pos - anchor;
    mm.GetWorldStack().Translate(t);

    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255),
                                   0.0f, 1.0f, 0.0f, 1.0f, NULL);

    m_Texture->UnSetUnCached();
}
