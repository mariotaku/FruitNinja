#ifndef FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H
#define FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H

// FruitNinjaNewsControl -- live news overlay UI control.
// Defunct: online News -- no-op stub; binary @ 0x1a13d0 (FruitNinjaNewsControl ctor).
// Engine base: Mortar::OpenFeintNewsRenderer @ 0x233xxx.
//
// Since HasUnreadNews() always returns false, StartNewsRender/Update/Draw are dead paths.
// MainScreen drives it: IsDisplayingNews(), OnNewsFinished(), CancelNews().

#include "engine/network/OpenFeintNewsRenderer.h"
#include "engine/asset/Texture.h"
#include "engine/render/Font.h"
#include "engine/util/SmartPtr.h"
#include <cstdint>

class FruitNinjaNewsControl : public Mortar::OpenFeintNewsRenderer {
public:
    // Defunct: online News -- no-op stub; binary @ 0x1a13d0
    FruitNinjaNewsControl();

    // Defunct: online News -- no-op stub
    virtual ~FruitNinjaNewsControl();

    // Defunct: online News -- no-op stub; binary @ 0x1a2074
    void StartNewsRender(const Mortar::SmartPtr<Mortar::Texture>& tex, Mortar::Font* font);

    // Defunct: online News -- no-op stub; binary @ 0x1a1000
    void CancelNewsRender();

    // Defunct: online News -- no-op stub; binary @ 0x1a0bc4
    void Update(float dt);

    // Defunct: online News -- no-op stub; binary @ 0x1a27fc
    void Draw(float* hudScale);

    // Defunct: online News -- no-op stub; binary @ 0x1a0fb4
    void Destroy();

    // Defunct: online News -- no-op stub; binary @ 0x1a2014
    void SetNewsString(const char* str);

    // Defunct: online News -- no-op stub; binary @ 0x1a2030 (returns empty)
    const char* GetNewsString() const;

    // Defunct: online News -- no-op stub (input sinks)
    void InputSinkDown(unsigned int touchId, float x, float y);
    void InputSinkReleased(unsigned int touchId, float x, float y);
    void InputSinkMoveX(unsigned int touchId, float x, float y);
    void InputSinkMoveY(unsigned int touchId, float x, float y);

    // Defunct: online News -- no-op stub
    void ModalTouchDown(float x, float y);
    void ModalTouchEnded(float x, float y);

    // Defunct: online News -- no-op stub
    bool IsDisplayingNews() const;

    // Defunct: online News -- no-op stub
    void TransitionOut();

private:
    // Defunct: online News -- no-op stub; binary @ 0x1a2074 helper
    void ParseUrl(const char* url);

    // Defunct: online News -- no-op stub
    void ProcessNewsString();

    // Defunct: online News -- no-op stub
    void DrawLinkButton();

    // Defunct: online News -- no-op stub
    bool IsValidChar(char c);

    // Binary size not precisely RE'd; use small opaque pad.
    // OpenFeintNewsRenderer already carries 0x10D4 bytes of pad.
    // FruitNinjaNewsControl own fields (font ptr, string buffer, flags) are not accessed by port.
    uint8_t m_fnPad[64];
};

#endif // FN_GAME_FRUIT_NINJA_NEWS_CONTROL_H
