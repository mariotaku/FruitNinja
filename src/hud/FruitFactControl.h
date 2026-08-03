#ifndef FN_HUD_FRUIT_FACT_CONTROL_H
#define FN_HUD_FRUIT_FACT_CONTROL_H

//
// FruitFactControl : HUDControl3d  (v1.6.1 binary page-book controller)
//
// Binary class name: FruitFactControl (v1.6.1 binary @ 0x00170c78)
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
//   BeginDrawing 0x00170804  (mov r3,#0x80; str r3,[r0,#0x34] -- sets m_LayerFlags)
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
//   +0x7C        : const char* m_FactText       (ctor inits 0)
//   +0x80        : uint m_ComboA                (ctor inits 0xFFFFFFFF)
//   +0x84        : uint m_ComboB                (ctor inits 0xFFFFFFFF)
//   +0x88        : SmartPtr<Texture> m_FactTexture
//   +0x8C        : Vec3 m_FactOffset            (Init sets (-69,53,0))
//   +0x98        : Colour m_FactColour          (ctor inits (0x74,0x5D,0x3B))
//   +0x9C        : int m_PageFlag               (ctor inits 0)
//   +0xA0        : MenuButton* m_NextButton     (lazily new'd in Update)
//   +0xA4        : MenuButton* m_PrevButton     (lazily new'd in Update)
//   +0xA8        : uint8_t m_GameStateSnapshot  (= game_work.gameMode byte)
//   +0xAC        : std::vector<FruitFactPage*> m_Pages
//   total 0xB8
//
// ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: the field layout above.
// UNVERIFIED: the layout carried an ASM-verified stamp that no asm-inspector run
// backs. Supporting evidence is circumstantial but consistent -- the __bada__
// offset asserts below pass, and the binary ctor's sub-object ctor calls land on
// +0x88 (m_FactTexture), +0x98 (m_FactColour) and +0xAC (m_Pages), exactly where
// this layout puts them. The ctor's instruction diff (port 43 vs binary 63) is a
// separate open question about inline-vs-out-of-line field init, not about these
// offsets.
//
// Singleton: static instance @ DATA 0x002d7520 (v1.6.1); constructed at
// static-init time (entry-point xref). Not reached via a menu button.
//

#include "hud/HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/Colour.h"
#include "engine/math/_Vector3.h"
#include <vector>
#include <cstdint>

struct InputEvent;
class MenuButton;
class FruitFactPage;

class FruitFactControl : public HUDControl3d {
    // FruitFactPage builder helpers access m_FactText and m_FactColour
    // directly (binary offset reads: ctrl+0x7c, ctrl+0x98).
    friend class FruitFactPage;
public:
    // Binary @ 0x00170c78
    FruitFactControl();
    // Binary @ 0x001718ac (D0) / 0x0017193c (D1) / 0x001719c4 (D2)
    virtual ~FruitFactControl();

    // HUDControl3d vtable overrides
    void Init() override;            // Binary @ 0x0017160c
    void Release() override;         // Binary @ 0x00171808
    void Reset() override;           // Binary @ 0x00170800 (no-op)
    void Update(float dt) override;  // Binary @ 0x00170eb4

    // Binary @ 0x00170814 -- SetPos(_Vector3)
    void SetPos(_Vector3<float> p);

    // Binary @ 0x00170804 -- BeginDrawing: sets m_LayerFlags(+0x34)=0x80 every draw
    void BeginDraw(float dt) override;

    // Binary @ 0x00170810 -- DrawOrder (no-op in binary)
    void DrawOrder(float* hudScaleRaw, int layerMask) override;

    // Binary @ 0x001720dc -- returns 0xc (12)
    int GetType() override { return 0xc; }

    // Binary @ 0x0017132c -- SetPage(index, playSound)
    // Calls cur->HidePage(), new->ShowPage(); optionally plays sfx.
    void SetPage(int idx, bool playSound);

    // Binary @ 0x00171ab4 -- push_back page into m_Pages vector,
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

    // Binary struct fields -- public to allow offsetof() in layout static_asserts
    // (GCC 4.4 __bada__ cross-build: offsetof on private members is an error).
    // +0x7C: current fact string (result of Fruit::GetFact; ctor inits NULL)
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    const char* m_FactText;                                // @+0x7C
    // +0x80: combo index A (ctor inits 0xFFFFFFFF)
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    unsigned int m_ComboA;                                 // @+0x80
    // +0x84: combo index B (ctor inits 0xFFFFFFFF)
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    unsigned int m_ComboB;                                 // @+0x84
    // +0x88: fact page texture
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    Mortar::SmartPtr<Mortar::Texture> m_FactTexture;       // @+0x88
    // +0x8C: fact offset Vec3 (Init sets (-69,53,0)); replaces old 3x SmartPtr slots
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    _Vector3<float> m_FactOffset;                                     // @+0x8C..+0x97
    // +0x98: fact colour (ctor inits (0x74,0x5D,0x3B))
    Colour m_FactColour;                                   // @+0x98
    // +0x9C: current page index (ctor inits 0)
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    int m_PageFlag;                                        // @+0x9C
    // +0xA0: next arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_NextButton;                              // @+0xA0
    // +0xA4: prev arrow MenuButton (lazily new'd in Update when pages>1)
    MenuButton* m_PrevButton;                              // @+0xA4
    // +0xA8: game-mode snapshot byte (= game_work.gameMode at Init time)
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    uint8_t m_GameStateSnapshot;                           // @+0xA8
    uint8_t _pad_A9[3];                                    // padding to align m_Pages
    // +0xAC: pages vector
    // ASM-spec v1.6.1 FruitFactControl @ 0x00170c78: layout
    std::vector<FruitFactPage*> m_Pages;                   // @+0xAC
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(FruitFactControl) == 0xB8,
    "FruitFactControl sizeof must be 0xB8");
static_assert(offsetof(FruitFactControl, m_FactOffset) == 0x8C,
    "FruitFactControl::m_FactOffset must be at +0x8C");
static_assert(offsetof(FruitFactControl, m_GameStateSnapshot) == 0xA8,
    "FruitFactControl::m_GameStateSnapshot must be at +0xA8");
static_assert(offsetof(FruitFactControl, m_Pages) == 0xAC,
    "FruitFactControl::m_Pages must be at +0xAC");
#endif

#endif // FN_HUD_FRUIT_FACT_CONTROL_H
