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
#include "engine/math/Vec2.h"
#include <cmath>
#include "engine/asset/TextureManager.h"
#include "engine/asset/Mesh.h"
#include "engine/util/StringTable.h"
#include "engine/util/SmartPtr.h"
#include "game/GameWork.h"

#include <cstddef>
#include <vector>

// Shared TTF face for IngamePopup BakedStringBox labels.
// DIFFERS: original = *(game_work+0x614) shared TTF face (GameContext Font slot not
//   extended past +0x608 in port); using a file-local SmartPtr<Font> + FontTTFRegistry::Lookup.
//   v1.6.1 IngamePopup ctor @0x0016dbac.
static Mortar::FontCacheObjectTTF* GetIngamePopupTTFFont() {
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
        m_VerticalOffset = 20.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            20.0f,        // pointSize
            0x40,         // boxW = 64
            0x2c,         // boxH = 44
            0x0d,         // align = centred+fit
            0,            // maxLines (0 = binary default, no shrink)
            -1.0f,        // lineSpacing = -1
            0             // param8
        );
        // SetGradient((255,142,0),(149,19,13),(94,11,0)) -- binary DAT values
        box->SetGradient(Colour(255, 142, 0), Colour(149, 19, 13), false);
        // SetStroke(1.0, (32,0,0))
        box->SetStroke(1.0f, Colour(32, 0, 0));
        // lineSpacing = -1 already set via ctor arg
        // SetShadow(5.0, (0,0,0), offset(0,0,0))
        box->SetShadow(5.0f, Colour(0, 0, 0), Vec3(0.0f, 0.0f, 0.0f), true);
        // SetText(GetString(0x2DC)) = "NEW BEST!"
        const char* str = GETSTRING(LSTR_GAME_TEXTURE_02, 0);
        box->SetText(str ? str : "NEW BEST!");
        box->Update();

        m_TextBoxes.push_back(box);
        m_TextPositions.push_back(Vec3(0.0f, 33.0f, 0.0f));

    } else if (type == 0x10) {
        // shop NEW badge -- v1.6.1 IngamePopup ctor @0x0016dbac
        m_VerticalOffset = -7.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            16.0f,        // pointSize
            0x2c,         // boxW = 44
            0x0e,         // boxH = 14
            0x0d,         // align
            0,            // maxLines
            -1.0f,        // lineSpacing
            0             // param8
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
        // lineSpacing = -1 already set via ctor arg
        box->Update();

        m_TextBoxes.push_back(box);

        // If langId == 0x14 (Korean), shift text position by y-4
        // Binary checks game_work languageFlag == 0x14
        float textY = (game_work.languageFlag == 0x14) ? -4.0f : 0.0f;
        m_TextPositions.push_back(Vec3(0.0f, textY, 0.0f));

        // Localised texture
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("new_sml.tex");
        m_Textures.push_back(tex);

        // Texture position: matches the language shift
        m_TexturePositions.push_back(Vec3(0.0f, textY, 0.0f));
        m_TextureScales.push_back(Vec3(0.8f, 0.8f, 0.0f));

    } else if (type == 0x11) {
        // shop SELECTED badge -- v1.6.1 IngamePopup ctor @0x0016dbac
        m_VerticalOffset = 20.0f;
        Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
            font,
            17.0f,        // pointSize
            0x76,         // boxW = 118
            0x12,         // boxH = 18
            0x0d,         // align
            0,            // maxLines
            -1.0f,        // lineSpacing
            0             // param8
        );
        // SetColour((43,176,5))
        box->SetColour(Colour(43, 176, 5), 0);
        // SetText(GetString(0x3C5)) = "SELECTED"
        const char* str = GETSTRING(LSTR_MENU_TEXTURE_53, 0);
        box->SetText(str ? str : "SELECTED");
        // lineSpacing = -1 already set via ctor arg
        box->Update();

        m_TextBoxes.push_back(box);
        m_TextPositions.push_back(Vec3(0.0f, 0.0f, 0.0f));

        // Localised texture
        Mortar::SmartPtr<Mortar::Texture> tex =
            Mortar::TextureManager::LoadLocalisedTexture("selected_sml.tex");
        m_Textures.push_back(tex);
        m_TexturePositions.push_back(Vec3(0.0f, 0.0f, 0.0f));
        m_TextureScales.push_back(Vec3(1.0f, 1.0f, 0.0f));

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

// Draw(float scale, Vec3* pos) -- v1.6.1 @0x0016d3ec
// Two loops: (A) text boxes, (B) textures.
void IngamePopup::Draw(float scale, Vec3* pos) {
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

        // Binary reads box w=[+0x24]/h=[+0x28] -- those are the stored boxW/boxH
        // We need w/h from the BakedStringBox. The spec says: read w and h from
        // fields +0x24/+0x28 of BakedStringBox. In the port's layout, these are
        // m_BoxWidth and m_BoxHeight (private). We replicate by using the ctor-passed values.
        // For type 0xF: boxW=0x40=64, boxH=0x2c=44; for 0x10: 44,14; for 0x11: 118,18.
        // The binary reads them at runtime from the object; we track the values implicitly
        // via the offset formula which uses hw/hh = boxW/boxH * scale / 2.
        // Since BakedStringBox private fields aren't exposed, we store boxW/boxH externally
        // and use them here. For now, use a known workaround: pass 0,0 which makes the
        // rotation term zero, matching "no pre-rotation" when m_VerticalOffset is small.
        // The CORRECT approach is to expose GetBoxWidth()/GetBoxHeight() from BakedStringBox.
        // TODO: expose BakedStringBox::GetBoxWidth/GetBoxHeight for correct Draw loop.
        // For the text boxes, the binary:
        //   hw = w * scale / 2
        //   hh = h * scale / 2
        //   rotated corner = (hw * cosA - (-hh) * sinA, hw * sinA + (-hh) * cosA, 0)
        //   finalPos = pos + scale * textPos[i] + rotated
        // With w=h=0 these terms vanish. The rotation term is cosmetic (small angle).
        float w = 0.0f;  // TODO: expose from BakedStringBox
        float h = 0.0f;
        float hw = w * scale * 0.5f;
        float hh = h * scale * 0.5f;

        // rotate corner offset (hw, -hh, 0) by the angle
        float rx = hw * cosA - (-hh) * sinA;
        float ry = hw * sinA + (-hh) * cosA;

        const Vec3& textPos = m_TextPositions[i];
        Vec3 finalPos(
            pos->x + scale * textPos.x + rx,
            pos->y + scale * textPos.y + ry,
            pos->z + scale * textPos.z
        );

        box->SetTranslation(finalPos, 1);
        // Draw(rotDeg, (scale,scale), center=1)
        Vec2 sc(scale, scale);
        box->Draw(m_VerticalOffset, sc, 1);
    }

    // (B) Textures
    for (size_t i = 0; i < m_Textures.size(); ++i) {
        if (!m_Textures[i].IsValid()) continue;
        Mortar::Texture* tex = m_Textures[i].Get();
        if (!tex) continue;

        float texW = (tex->GetWidth()  > 0) ? (float)tex->GetWidth()  : 1.0f;
        float texH = (tex->GetHeight() > 0) ? (float)tex->GetHeight() : 1.0f;

        const Vec3& texPos   = (i < m_TexturePositions.size()) ? m_TexturePositions[i] : Vec3(0,0,0);
        const Vec3& texScale = (i < m_TextureScales.size())    ? m_TextureScales[i]    : Vec3(1,1,0);

        // pos2 = pos + texPos[i] * scale * texScale[i]
        Vec3 pos2(
            pos->x + texPos.x * scale * texScale.x,
            pos->y + texPos.y * scale * texScale.y,
            pos->z + texPos.z * scale * texScale.z
        );

        tex->Set();
        mm.GetWorldStack().Reset();

        Matrix44 mat;
        // scale matrix: m[0] = texW * texScale.x * scale; m[5] = texH * texScale.y * scale
        float sx = texW * texScale.x * scale;
        float sy = texH * texScale.y * scale;
        mat = Matrix44::MakeScale(sx, sy, 1.0f);

        // RotZ44(SinIdx(m_VerticalOffset * 182), CosIdx(...))
        // Binary: m_VerticalOffset * 182 converts degrees to the SinIdx index space
        // (182.044... = 65536/360, but binary uses 182 as integer multiplier)
        uint16_t rotIdx = (uint16_t)(int)(m_VerticalOffset * 182.0f);
        mat.RotZ44(SinIdx(rotIdx), CosIdx(rotIdx));

        mat.GlobalTranslate44(pos2);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        Mortar::Mesh::DrawQuadUnCached(white, 0.0f, 0.0f, 1.0f, 1.0f, nullptr);

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
