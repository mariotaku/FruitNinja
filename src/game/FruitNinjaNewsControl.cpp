// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a13d0 (FruitNinjaNewsControl).
// HasUnreadNews() always returns false so StartNewsRender/Update/Draw are dead paths.
// MainScreen calls IsDisplayingNews() / OnNewsFinished() / CancelNews().
// Engine base Mortar::OpenFeintNewsRenderer @ 0x233xxx.

#include "game/FruitNinjaNewsControl.h"
#include <cstring>

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a13d0
FruitNinjaNewsControl::FruitNinjaNewsControl() {
    memset(m_fnPad, 0, sizeof(m_fnPad));
}

// Defunct: online News -- no-op stub
FruitNinjaNewsControl::~FruitNinjaNewsControl() {
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2074
// Binary @ 0x1a2074 calls SetTouchReleasedCallback(TouchReleasedCallback) @ 0x001a385c
// to register the per-finger lift handler.  Port omits the call (SetTouchReleasedCallback
// not yet ported; Defunct: online news -- dead path in any case).
void FruitNinjaNewsControl::StartNewsRender(const Mortar::SmartPtr<Mortar::Texture>& tex, Mortar::Font* font) {
    (void)tex;
    (void)font;
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a1000
void FruitNinjaNewsControl::CancelNewsRender() {
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a0bc4
void FruitNinjaNewsControl::Update(float dt) {
    (void)dt;
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a27fc
void FruitNinjaNewsControl::Draw(float* hudScale) {
    (void)hudScale;
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a0fb4
void FruitNinjaNewsControl::Destroy() {
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2014
void FruitNinjaNewsControl::SetNewsString(const char* str) {
    (void)str;
}

// Defunct: online News -- no-op stub; v1.6.1 binary @ 0x1a2030 (returns empty)
const char* FruitNinjaNewsControl::GetNewsString() const {
    return "";
}

// Defunct: online News -- no-op stub (input sinks)
void FruitNinjaNewsControl::InputSinkDown(unsigned int touchId, float x, float y) {
    (void)touchId; (void)x; (void)y;
}

void FruitNinjaNewsControl::InputSinkReleased(unsigned int touchId, float x, float y) {
    (void)touchId; (void)x; (void)y;
}

void FruitNinjaNewsControl::InputSinkMoveX(unsigned int touchId, float x, float y) {
    (void)touchId; (void)x; (void)y;
}

void FruitNinjaNewsControl::InputSinkMoveY(unsigned int touchId, float x, float y) {
    (void)touchId; (void)x; (void)y;
}

// Defunct: online News -- no-op stub
void FruitNinjaNewsControl::ModalTouchDown(float x, float y) {
    (void)x; (void)y;
}

void FruitNinjaNewsControl::ModalTouchEnded(float x, float y) {
    (void)x; (void)y;
}

// Defunct: online News -- no-op stub
bool FruitNinjaNewsControl::IsDisplayingNews() const {
    return false;
}

// Defunct: online News -- no-op stub
void FruitNinjaNewsControl::TransitionOut() {
}

// Defunct: online News -- no-op stub
void FruitNinjaNewsControl::ParseUrl(const char* url) {
    (void)url;
}

// Defunct: online News -- no-op stub
void FruitNinjaNewsControl::ProcessNewsString() {
}

// Defunct: online News -- no-op stub
void FruitNinjaNewsControl::DrawLinkButton() {
}

// Defunct: online News -- no-op stub
bool FruitNinjaNewsControl::IsValidChar(char c) {
    (void)c;
    return false;
}
