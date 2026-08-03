// IngamePopup -- pre-baked text/texture overlay keyed by type id.
// Binary: v1.6.1 IngamePopup @0x0016dbac (ctor), @0x0016d3ec (Draw),
//         @0x0016db38 (DeleteAllPopups), @0x0016e578 (BuildAllPopups).

#include "IngamePopup.h"
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/render/MatrixManager.h"
#include "engine/math/Matrix44.h"
#include "engine/math/MathUtil.h"
#include "engine/math/_Vector2.h"
#include <cmath>
#include "engine/asset/TextureManager.h"
#include "engine/asset/Mesh.h"
#include "engine/util/StringTable.h"
#include "engine/util/SmartPtr.h"
#include "game/GameWork.h"

#include <cstddef>
#include <vector>

// Shared TTF face for IngamePopup BakedStringBox labels (NEW / SELECTED / NEW BEST! / combo).
// v1.6.1 IngamePopup ctor @0x0016dbac: every BakedStringBox reads game_work.m_pTTFFontMain
//   (GameWork+0x614 = locale face; arabic.ttf when languageFlag==0x14, else gangofchinese.ttf).
// Port specific: the binary reads +0x614 unconditionally. The null branch below is a
//   port-only safety net with no binary counterpart.
static Mortar::FontCacheObjectTTF* GetIngamePopupTTFFont() {
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    // Lazy fallback only if PreloadFontsTTF hasn't run yet.
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// pM_Popups storage: std::vector<IngamePopup*> mirroring game_work+0x618 (12 bytes ARM32).
// Port uses a plain std::vector here since the m_Popups field in GameWork is reserved as
// a byte array (cannot be used as a real vector on x64).
// Access pattern: pM_Popups[type] is the popup for that type id.
static std::vector<IngamePopup*> s_Popups;

// IngamePopup(int type) -- v1.6.1 @0x0016dbac
// Constructs the 5 vectors, m_Type=type, m_VerticalOffset=0 (overwritten per-type below).
IngamePopup::IngamePopup(int type)
    : m_TextBoxes()
    , m_TextPositions()
    , m_Textures()
    , m_TexturePositions()
    , m_TextureScales()
    , m_Type(type)
    , m_VerticalOffset(0.0f)
{
    Mortar::FontCacheObjectTTF* font = GetIngamePopupTTFFont();

    if (type == 0x0F) {
        // NEW BEST SCORE banner -- v1.6.1 IngamePopup ctor @0x0016dbac
        // ASM-spec v1.6.1 IngamePopup ctor @0x0016dbac type 0x0F branch @0x0016df64:
        //   BakedStringBox(font, 20.0f, boxW=0x40, boxH=0x2c, align=0xf, maxLines=2, lineSpacing=8)
        //   fill: SetGradient(top=(255,142,0), mid=(149,19,13), bottom=(94,11,0), false) -- 3-stop.
        //   shadow: SetShadow(5.0, (0,0,0), (0,0,0), 0) -- drop-shadow (flag=0), not inner-glow.
        m_VerticalOffset = 20.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            20.0f,        // pointSize
            0x40,         // boxW = 64
            0x2c,         // boxH = 44
            (Mortar::ALIGNMENT_TYPE)0x0f,         // align = center-H + center-V (bits 0-1=11, bits 2-3=11)
            2,            // maxLines = 2
            8             // lineSpacing = 8; step = (int)(20+8) = 28px
        );
        // SetGradient(top=(255,142,0), mid=(149,19,13), bottom=(94,11,0)) -- 3-stop, binary DAT values
        box->SetGradient(Colour(255, 142, 0), Colour(149, 19, 13), Colour(94, 11, 0), false);
        // SetStroke(1.0, (32,0,0))
        box->SetStroke(1.0f, Colour(32, 0, 0));
        // lineSpacing = -1 already set via ctor arg
        // SetShadow(5.0, (0,0,0), offset(0,0,0), flag=0) -- drop-shadow, not inner-glow
        box->SetShadow(5.0f, Colour(0, 0, 0), _Vector3<float>(0.0f, 0.0f, 0.0f), 0);
        // SetText(GetString(0x2DC)) = "NEW BEST!"
        const char* str = GETSTRING(LSTR_GAME_TEXTURE_02, 0);
        box->SetText(str ? str : "NEW BEST!");
        box->Update();

        m_TextBoxes.push_back(box);
        m_TextPositions.push_back(_Vector3<float>(0.0f, 33.0f, 0.0f));

    } else if (type == 0x10) {
        // shop NEW badge -- v1.6.1 IngamePopup ctor @0x0016dbac
        // ASM-spec v1.6.1 IngamePopup ctor @0x0016dbac type 0x10:
        //   BakedStringBox(font, 16.0f, boxW=0x2c, boxH=0xe, align=0xf, maxLines=1, lineSpacing=0)
        //   align=0xf: bits 0-1=11 (center-H), bits 2-3=11 (center-V).
        //   text position: always (0,0,0); Arabic -4 shift is on TEXTURE position only.
        m_VerticalOffset = -7.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            16.0f,        // pointSize
            0x2c,         // boxW = 44
            0x0e,         // boxH = 14
            (Mortar::ALIGNMENT_TYPE)0x0f,         // align = center-H + center-V (bits 0-1=11, bits 2-3=11)
            1,            // maxLines = 1 (binary arg6)
            0             // lineSpacing (binary 7th arg = 0; step = (int)(16+0) = 16px)
        );
        // SetMetallicGradient((255,253,88),(255,255,255),(152,123,10),(255,253,88))
        box->SetMetallicGradient(
            Colour(255, 253, 88),
            Colour(255, 255, 255),
            Colour(152, 123, 10),
            Colour(255, 253, 88),
            false
        );
        // SetText(GetString(0x399)) = "NEW"
        const char* str = GETSTRING(LSTR_MENU_TEXTURE_09, 0);
        box->SetText(str ? str : "NEW");
        // SetHorizontalLineSpacing(-1) already via ctor -> m_AlignMode=-1
        box->Update();

        m_TextBoxes.push_back(box);

        // Binary _Stack_e4 = (0,0,0) always -- text position is NEVER shifted for Arabic.
        // The -4 Arabic shift applies only to the TEXTURE position (_Stack_cc).
        m_TextPositions.push_back(_Vector3<float>(0.0f, 0.0f, 0.0f));

        // Localised texture -- v1.6.1 IngamePopup ctor @0x0016dbac type 0x10,
        // GOT [0x16dfcc] -> "new_outline.tex" (the bordered badge; "new_sml.tex"
        // does not exist -> invalid SmartPtr -> border never drawn).
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("new_outline.tex");
        m_Textures.push_back(tex);

        // Texture position: Arabic lang shifts texture y by -4; text is not shifted.
        float texY = (game_work.languageFlag == 0x14) ? -4.0f : 0.0f;
        m_TexturePositions.push_back(_Vector3<float>(0.0f, texY, 0.0f));
        m_TextureScales.push_back(_Vector3<float>(0.8f, 0.8f, 0.0f));

    } else if (type == 0x11) {
        // shop SELECTED badge -- v1.6.1 IngamePopup ctor @0x0016dbac
        // ASM-spec v1.6.1 IngamePopup ctor @0x0016dbac type 0x11:
        //   BakedStringBox(font, 17.0f, boxW=0x76, boxH=0x12, align=0xf, maxLines=1, lineSpacing=0)
        //   align=0xf: bits 0-1=11 (center-H), bits 2-3=11 (center-V).
        //   text position: always (0,0,0); Arabic -4 shift is on TEXTURE position only.
        m_VerticalOffset = 20.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            17.0f,        // pointSize
            0x76,         // boxW = 118
            0x12,         // boxH = 18
            (Mortar::ALIGNMENT_TYPE)0x0f,         // align = center-H + center-V (bits 0-1=11, bits 2-3=11)
            1,            // maxLines = 1 (binary arg6)
            0             // lineSpacing (binary 7th arg = 0; step = (int)(17+0) = 17px)
        );
        // SetColour((43,176,5))
        box->SetColour(Colour(43, 176, 5), 0);
        // SetText(GetString(0x3C5)) = "SELECTED"
        const char* str = GETSTRING(LSTR_MENU_TEXTURE_53, 0);
        box->SetText(str ? str : "SELECTED");
        // SetHorizontalLineSpacing(-1) already via ctor -> m_AlignMode=-1
        box->Update();

        m_TextBoxes.push_back(box);
        // Binary _Stack_114 = (0,0,0) always -- text position never shifted for Arabic.
        m_TextPositions.push_back(_Vector3<float>(0.0f, 0.0f, 0.0f));

        // Localised texture -- v1.6.1 IngamePopup ctor @0x0016dbac type 0x11,
        // GOT [0x16e574] -> "selected_outline.tex" (bordered; "selected_sml.tex"
        // is the wrong, borderless small variant).
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("selected_outline.tex");
        m_Textures.push_back(tex);
        // Texture position: Arabic lang shifts texture y by -4 (same pattern as type 0x10).
        float texY = (game_work.languageFlag == 0x14) ? -4.0f : 0.0f;
        m_TexturePositions.push_back(_Vector3<float>(0.0f, texY, 0.0f));
        m_TextureScales.push_back(_Vector3<float>(1.0f, 1.0f, 0.0f));

    }
    // TODO: type 0x00 (combo popup) -- created on-demand elsewhere, skip this pass.
    // v1.6.1 IngamePopup ctor @0x0016dbac type 0 branch.
}

IngamePopup::~IngamePopup() {
    for (size_t i = 0; i < m_TextBoxes.size(); ++i) {
        delete m_TextBoxes[i];
    }
    m_TextBoxes.clear();
}

// Draw(Vec3 pos, float scale) -- v1.6.1 @0x0016d3ec
// Two loops: (A) text boxes, (B) textures.
void IngamePopup::Draw(_Vector3<float> pos, float scale) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // Convert m_VerticalOffset from degrees to radians for cos/sin.
    // Binary Draw @0x0016d3ec: uses cosf/sinf directly (not SinIdx table).
    // 0.01745329f = pi/180 (deg->rad).
    float rad = m_VerticalOffset * 0.01745329f;
    float cosA = cosf(rad);
    float sinA = sinf(rad);

    // (A) Text boxes
    for (size_t i = 0; i < m_TextBoxes.size(); ++i) {
        Mortar::BakedStringBox* box = m_TextBoxes[i];
        if (!box) continue;

        // ASM-spec v1.6.1 IngamePopup::Draw @0x0016d41c: text anchor uses BakedStringBox box dims
        // (+0x24 boxW / +0x28 boxH, signed int) for a rotation-aware corner correction:
        //   delta = (hw,-hh) - Rotate(hw,-hh,angle), so at angle=0 there is zero displacement.
        int boxW = box->GetBoxWidth();
        int boxH = box->GetBoxHeight();
        // integer half-extents, sign-aware arithmetic shift (mirrors binary vcvt + (x+(x>>31))>>1)
        int hwi = (int)(boxW * scale);  hwi = (hwi + (hwi >> 31)) >> 1;
        int hhi = (int)(boxH * scale);  hhi = (hhi + (hhi >> 31)) >> 1;
        float hw = (float)hwi;
        float hh = (float)hhi;
        float rx =  hw * (1.0f - cosA) - hh * sinA;
        float ry = -hh * (1.0f - cosA) - hw * sinA;

        const _Vector3<float>& textPos = m_TextPositions[i];
        _Vector3<float> finalPos(
            pos.x + scale * textPos.x + rx,
            pos.y + scale * textPos.y + ry,
            pos.z + scale * textPos.z
        );

        box->SetTranslation(finalPos, 1);
        // Draw((scale,scale), rotDeg, center=1)
        _Vector2<float> sc(scale, scale);
        box->Draw(sc, m_VerticalOffset, 1);
    }

    // (B) Textures
    for (size_t i = 0; i < m_Textures.size(); ++i) {
        if (!m_Textures[i].IsValid()) continue;
        Mortar::Texture* tex = m_Textures[i].Get();
        if (!tex) continue;

        float texW = (tex->GetWidth()  > 0) ? (float)tex->GetWidth()  : 1.0f;
        float texH = (tex->GetHeight() > 0) ? (float)tex->GetHeight() : 1.0f;

        const _Vector3<float>& texPos = (i < m_TexturePositions.size())
                                            ? m_TexturePositions[i]
                                            : _Vector3<float>(0, 0, 0);
        const _Vector3<float>& texScale = (i < m_TextureScales.size()) ? m_TextureScales[i] : _Vector3<float>(1, 1, 0);

        // pos2 = pos + texPos[i] * scale * texScale[i]
        _Vector3<float> pos2(
            pos.x + texPos.x * scale * texScale.x,
            pos.y + texPos.y * scale * texScale.y,
            pos.z + texPos.z * scale * texScale.z
        );

        tex->Set();
        mm.GetWorldStack().Reset();

        // scale matrix: m[0] = texW * texScale.x * scale; m[5] = texH * texScale.y * scale
        float sx = texW * texScale.x * scale;
        float sy = texH * texScale.y * scale;

        // ASM-spec v1.6.1 IngamePopup::Draw @0x0016d6ec: border xform equivalent to T*(R*S), z-scale 0.
        // The binary's Mul44 @0x0016f5a0 is REVERSED (output = param_1 * this), so binary op*(this=S,
        // rhs=R) stores R*S. The port's operator* is standard (this*b), so the port must use
        // matR * matS (= R*S) to match -- matS * matR gave S*R, which shears the non-square SELECTED
        // quad into a parallelogram (and made the border tilt differently from the text). Same root for
        // both symptoms. SinIdx/CosIdx (border) and cosf/sinf (text) both use m_VerticalOffset, same sign.
        uint16_t rotIdx = (uint16_t)(int)(m_VerticalOffset * 182.0f);
        Matrix44 matR;
        matR.RotZ44(SinIdx(rotIdx), CosIdx(rotIdx));
        Matrix44 matS = Matrix44::MakeScale(sx, sy, 0.0f);
        Matrix44 mat = matR * matS;
        mat.GlobalTranslate44(pos2);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        Mortar::Mesh::DrawQuadUnCached(white, 0.0f, 1.0f, 0.0f, 1.0f, nullptr);

        tex->UnSet(1);
    }
}

// BuildAllPopups -- v1.6.1 @0x0016e578
// Creates types 0x11, 0x10, 0x0F via new+ctor, stores at s_Popups[type].
// Ensures at least 0x12 null slots exist first.
void BuildAllPopups() {
    if (s_Popups.size() < 0x12) {
        s_Popups.resize(0x12, static_cast<IngamePopup*>(0));
    }

    // Binary order: 0x11, 0x10, 0x0F
    if (!s_Popups[0x11]) {
        s_Popups[0x11] = new IngamePopup(0x11);
    }
    if (!s_Popups[0x10]) {
        s_Popups[0x10] = new IngamePopup(0x10);
    }
    if (!s_Popups[0x0F]) {
        s_Popups[0x0F] = new IngamePopup(0x0F);
    }
}

// DeleteAllPopups -- v1.6.1 @0x0016db38
// Deletes every non-null s_Popups[i].
void DeleteAllPopups() {
    for (size_t i = 0; i < s_Popups.size(); ++i) {
        delete s_Popups[i];
        s_Popups[i] = static_cast<IngamePopup*>(0);
    }
    s_Popups.clear();
}

// Accessor for consumers (ScoreControl, ShopListItem).
IngamePopup* GetIngamePopup(int type) {
    if (type < 0 || (size_t)type >= s_Popups.size()) return 0;
    return s_Popups[type];
}
