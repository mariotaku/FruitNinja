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
//   +0x7C        : const char* m_pCurFactString (p_pad+0x00; ctor inits 0)
//   +0x80        : int m_FruitIdx               (p_pad+0x04; ctor inits -1)
//   +0x84        : int m_FactIdx                (p_pad+0x08; ctor inits -1)
//   +0x88        : SmartPtr<Texture> m_Tex88    (p_pad+0x0C; fact page texture)
//   +0x8C..+0x97 : unnamed SmartPtr<Texture> slots (p_pad+0x10..0x1B)
//   +0x98        : Colour m_FactColour          (p_pad+0x1C)
//   +0x9C        : int m_curPage  (init -1)
//   +0xA0        : MenuButton* m_pLeftArrow  (lazily new'd in Update)
//   +0xA4        : MenuButton* m_pRightArrow (lazily new'd in Update)
//   +0xA8        : flags byte = 1
//   +0xAC        : byte field (p_pad+0x2C, from FruitInfo+4)
//   +0xB0        : std::vector<FruitFactPage*> m_pages  (p_pad+0x30)
//
// Singleton: static instance @ DATA 0x002d7520 (v1.6.1); constructed at
// static-init time (entry-point xref). Not reached via a menu button.
//

#include "hud/HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/Colour.h"
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

    // Shared paging-arrow texture (loaded by LoadContent @ 0x00170b1c).
    static Mortar::SmartPtr<Mortar::Texture> s_TexArrow;

private:
    // +0x7C: current fact string (result of Fruit::GetFact; ctor inits NULL)
    const char* m_pCurFactString;                        // @+0x7C
    // +0x80: current fruit-type index (ctor inits -1)
    int         m_FruitIdx;                              // @+0x80
    // +0x84: current fact-within-fruit index (ctor inits -1)
    int         m_FactIdx;                               // @+0x84
    // +0x88: fact page texture (ctor builds SmartPtr at p_pad+0xc)
    Mortar::SmartPtr<Mortar::Texture> m_Tex88;          // @+0x88
    // +0x8C..+0x97: unnamed SmartPtr slots (binary p_pad+0x10..+0x1b)
    Mortar::SmartPtr<Mortar::Texture> m_Tex8C;          // @+0x8C
    Mortar::SmartPtr<Mortar::Texture> m_Tex90;          // @+0x90
    Mortar::SmartPtr<Mortar::Texture> m_Tex94;          // @+0x94
    // +0x98: fact colour (ctor builds Colour at p_pad+0x1c)
    Colour m_FactColour;                                 // @+0x98
    // +0x9C: current page index (init -1)
    int m_curPage;                                       // @+0x9C (p_pad+0x20)
    // +0xA0: left arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_pLeftArrow;                            // @+0xA0
    // +0xA4: right arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_pRightArrow;                           // @+0xA4
    // +0xA8: opaque byte (flags, init 1 in binary ctor)
    uint8_t m_flags;                                     // @+0xA8
    uint8_t _pad_A9[3];                                  // padding
    // +0xAC: additional byte field from binary ctor (p_pad[0x2c])
    uint8_t m_flagsB;                                    // @+0xAC
    uint8_t _pad_AD[3];                                  // padding to align pages vector
    // +0xB0: pages vector (binary p_pad+0x30, i.e. class +0xB0)
    std::vector<FruitFactPage*> m_pages;                 // @+0xB0
};

#endif // FN_HUD_FRUIT_FACT_PAGE_CONTROL_H
