#ifndef FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H
#define FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H

// FruitNinjaNewsControl -- live news overlay UI control.
// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a13d0 (FruitNinjaNewsControl ctor).
// Engine base: Mortar::OpenFeintNewsRenderer @ 0x233xxx.
//
// Since HasUnreadNews() always returns false, StartNewsRender/Update/Draw are dead paths.
// MainScreen drives it: IsDisplayingNews(), OnNewsFinished(), CancelNews().

#include "engine/network/OpenFeintNewsRenderer.h"
#include "engine/asset/Texture.h"
#include "engine/render/Font.h"
#include "engine/util/SmartPtr.h"
#include "engine/math/_Vector3.h"
#include "engine/core/MortarTypes.h"
#include <cstdint>

struct InputEvent;

class FruitNinjaNewsControl : public Mortar::OpenFeintNewsRenderer {
public:
    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a13d0
    FruitNinjaNewsControl();

    // Defunct: online News -- no-op stub
    virtual ~FruitNinjaNewsControl();

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2074
    void StartNewsRender(const Mortar::SmartPtr<Mortar::Texture>& tex, Mortar::Font* font);

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a1000
    void CancelNewsRender();

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a0bc4
    void Update(float dt);

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a27fc
    void Draw(float* hudScale);

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a0fb4
    void Destroy();

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2014
    void SetNewsString(const char* str);

    // Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2030 (returns empty)
    const char* GetNewsString();

    // Defunct: online News -- no-op stub (input sinks); NOT vtable overrides --
    // installed as input-callback function pointers via StartNewsRender, not
    // FruitNinjaNewsControl's own vtable slots.
    // v1.6.1 FruitNinjaNewsControl::InputSinkDown @0x001a1ce4
    int InputSinkDown(InputEvent* evt, const _Vector3<float>& pos);
    // v1.6.1 FruitNinjaNewsControl::InputSinkReleased @0x001a0a40
    int InputSinkReleased(InputEvent* evt, const _Vector3<float>& pos);
    // v1.6.1 FruitNinjaNewsControl::InputSinkMoveX @0x001a03b0
    int InputSinkMoveX(InputEvent* evt, const _Vector3<float>& pos);
    // v1.6.1 FruitNinjaNewsControl::InputSinkMoveY @0x001a03b8
    int InputSinkMoveY(InputEvent* evt, const _Vector3<float>& pos);

    // Defunct: online News -- no-op stub
    void ModalTouchDown(float x, float y);
    void ModalTouchEnded(float x, float y);

    // Defunct: online News -- no-op stub
    bool IsDisplayingNews() const;

    // Defunct: online News -- no-op stub
    void TransitionOut();

private:
    // Defunct: online News -- no-op stub; v1.6.1 FruitNinjaNewsControl::ParseUrl @0x001a0438
    // startIdx/endIdx index into m_NewsString: scans [startIdx, endIdx) for
    // [url]...[/url] tags; returns matched end-tag index, 0 if none.
    int ParseUrl(int startIdx, int endIdx);

    // Defunct: online News -- no-op stub
    void ProcessNewsString();

    // Defunct: online News -- no-op stub; v1.6.1 FruitNinjaNewsControl::DrawLinkButton @0x001a23cc
    void DrawLinkButton(Mortar::MortarRectangleT<float>* clipRect);

    // Defunct: online News -- no-op stub
    bool IsValidChar(char c);

    // Binary size not precisely RE'd; use small opaque pad.
    // OpenFeintNewsRenderer already carries 0x10D4 bytes of pad.
    // FruitNinjaNewsControl own fields (font ptr, string buffer, flags) are not accessed by port.
    uint8_t m_fnPad[64];
};

#endif // FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H
