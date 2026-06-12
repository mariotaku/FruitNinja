#ifndef FN_HUD_FRUIT_FACT_PAGE_CONTROL_H
#define FN_HUD_FRUIT_FACT_PAGE_CONTROL_H

//
// FruitFactPageControl : HUDControl3d  (v1.6.1 binary page-book controller)
//
// Binary class name: FruitFactControl (v1.6.1 binary @ 0x00170c78)
// Port name: FruitFactPageControl  -- renamed to avoid collision with the
//   v1.5.1 FruitFactControl (game-over "best fruit fact" panel) already in
//   src/hud/FruitFactControl.{h,cpp}. The two binaries have different classes
//   with the same mangled name; the port can only have one.
//
// Binary refs (v1.6.1 FruitFactControl):
//   ctor       0x00170c78
//   dtor D0    0x001718ac
//   dtor D1    0x0017193c
//   dtor D2    0x001719c4
//   Init       0x0017160c
//   LoadContent  0x00170b1c
//   UnLoadContent 0x00171a4c
//   Update     0x00170eb4
//   Reset      0x00170800  (no-op)
//   Release    0x00171808
//   SetPos     0x00170814
//   BeginDrawing 0x00170804  (no-op)
//   DrawOrder  0x00170810  (no-op)
//   GetType    0x001720dc  -> returns 0xc (12)
//   SetPage(int,bool)        0x0017132c
//   RegisterPage(FruitFactPage*) 0x00171ab4
//   LeftButton               0x00171534
//   RightButton              0x00171458
//   LeftPressed(InputEvent*) 0x001708b8
//   RightPressed(InputEvent*) 0x0017086c
//   UpPressed(InputEvent*)   0x00170a20
//   DownPressed(InputEvent*) 0x00170924
//
// Binary layout (ARM32, 4-byte ptrs, v1.6.1):
//   +0x00..+0x7B : HUDControl3d base (0x7C bytes)
//   +0x7C..+0x87 : SmartPtr<Texture> title/header tex slots
//   +0x88        : SmartPtr<Texture> @+0x88 (0x22 words)
//   +0x98        : SmartPtr<Texture> @+0x98 (0x26 words)
//   +0x9C        : int m_curPage  (init -1)
//   +0xA0        : MenuButton* m_pLeftArrow  (lazily new'd in Update)
//   +0xA4        : MenuButton* m_pRightArrow (lazily new'd in Update)
//   +0xAC        : std::vector<FruitFactPage*> m_pages  (@+0x2b words)
//   +0x32 byte   : flags byte = 1
//
// Singleton: static instance @ DATA 0x002d7520 (v1.6.1); constructed at
// static-init time (entry-point xref). Not reached via a menu button.
//

#include "hud/HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include <vector>
#include <cstdint>

struct InputEvent;
class MenuButton;
class FruitFactPage;

class FruitFactPageControl : public HUDControl3d {
public:
    // Binary @ 0x00170c78
    FruitFactPageControl();
    // Binary @ 0x001718ac (D0) / 0x0017193c (D1) / 0x001719c4 (D2)
    virtual ~FruitFactPageControl();

    // HUDControl3d vtable overrides
    void Init() override;            // Binary @ 0x0017160c
    void Release() override;         // Binary @ 0x00171808
    void Reset() override;           // Binary @ 0x00170800 (no-op)
    void Update(float dt) override;  // Binary @ 0x00170eb4

    // Binary @ 0x00170814 -- SetPos(_Vector3)
    void SetPos(const Vec3& p);

    // Binary @ 0x00170804 -- BeginDrawing (no-op in binary)
    void BeginDraw(float dt) override;

    // Binary @ 0x00170810 -- DrawOrder (no-op in binary)
    void DrawOrder(const Vec3& hudScale, int layerMask) override;

    // Binary @ 0x001720dc -- returns 0xc (12)
    int GetType() override { return 0xc; }

    // Binary @ 0x0017132c -- SetPage(index, playSound)
    // Calls cur->HidePage(), new->ShowPage(); optionally plays sfx.
    void SetPage(int idx, bool playSound);

    // Binary @ 0x00171ab4 -- push_back page into m_pages vector,
    // hide if not current, copy control pos into page->pos.
    void RegisterPage(FruitFactPage* page);

    // Binary @ 0x00171534 / 0x00171458 -- arrow button callbacks
    void LeftButton();
    void RightButton();

    // Binary @ 0x001708b8 / 0x0017086c / 0x00170a20 / 0x00170924 -- input
    bool LeftPressed(InputEvent* ev);
    bool RightPressed(InputEvent* ev);
    bool UpPressed(InputEvent* ev);
    bool DownPressed(InputEvent* ev);

    // Binary @ 0x00170b1c / 0x00171a4c
    static void LoadContent();
    static void UnLoadContent();

private:
    // +0x7C: title/header texture slot A
    Mortar::SmartPtr<Mortar::Texture> m_TexTitle;       // @+0x7C
    // +0x80: title/header texture slot B
    Mortar::SmartPtr<Mortar::Texture> m_TexHeader;      // @+0x80
    // +0x84..+0x87: pad / reserved SmartPtr slot
    Mortar::SmartPtr<Mortar::Texture> m_TexReserved;    // @+0x84
    // +0x88: SmartPtr<Texture> (word 0x22)
    Mortar::SmartPtr<Mortar::Texture> m_Tex88;          // @+0x88
    // +0x8C: SmartPtr<Texture>
    Mortar::SmartPtr<Mortar::Texture> m_Tex8C;          // @+0x8C
    // +0x90..+0x97: SmartPtr<Texture> slots
    Mortar::SmartPtr<Mortar::Texture> m_Tex90;          // @+0x90
    Mortar::SmartPtr<Mortar::Texture> m_Tex94;          // @+0x94
    // +0x98: SmartPtr<Texture> (word 0x26)
    Mortar::SmartPtr<Mortar::Texture> m_Tex98;          // @+0x98
    // +0x9C: current page index (init -1)
    int m_curPage;                                       // @+0x9C
    // +0xA0: left arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_pLeftArrow;                            // @+0xA0
    // +0xA4: right arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_pRightArrow;                           // @+0xA4
    // +0xA8: opaque byte (flags, init 1 in binary ctor @+0x32 words)
    uint8_t m_flags;                                     // @+0xA8
    uint8_t _pad_A9[3];                                  // padding
    // +0xAC: pages vector (@+0x2b words in binary layout)
    std::vector<FruitFactPage*> m_pages;                 // @+0xAC
};

#endif // FN_HUD_FRUIT_FACT_PAGE_CONTROL_H
