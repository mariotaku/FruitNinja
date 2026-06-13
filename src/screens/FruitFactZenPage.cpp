// FruitFactZenPage -- v1.6.1 Zen-mode fruit-fact page.
// Binary refs: ctor 0x0017fcd4, Init 0x00180320, etc.

#include "FruitFactZenPage.h"
#include "engine/asset/TextureManager.h"
#include "engine/render/MatrixManager.h"
#include "engine/asset/Mesh.h"

// Zen-page shared content: two localised textures + a one-shot loaded guard.
// Binary: PC-relative file-static globals (GOT object @ 0x002C1130); members at
// +0x720c (DAT_0017fb38), +0x6c44 (DAT_0017fb3c), guard byte +0x43c08 (DAT_0017fb40).
// LoadContent (@0x0017fa34) sets the guard and fills both via LoadLocalisedTexture;
// UnloadContent (@0x0017fb00) nulls both and clears the guard.
static bool g_ZenContentLoaded = false;
static Mortar::SmartPtr<Mortar::Texture> g_ZenTex720c;  // "blank_dialog_box.tex"
static Mortar::SmartPtr<Mortar::Texture> g_ZenTex6c44;  // "combo_description.tex"

// Binary @ 0x0017fcd4
FruitFactZenPage::FruitFactZenPage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
{
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

// Binary @ 0x00180320 -- builds achievement list / 'play to unlock' branch
void FruitFactZenPage::Init() {
    // TODO: 0x00180320 -- build BakedStringBox + GenericHUDControl children
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
