#ifndef FN_PLATFORM_PS3_INPUT_TRANSLATOR_PS3_H
#define FN_PLATFORM_PS3_INPUT_TRANSLATOR_PS3_H

//
// InputTranslatorPS3 -- DualShock 3 / Sixaxis (PSL1GHT ioPad) -> Mortar::Touch.
//
// Player-requested mapping:
//
//   LEFT  STICK  -> blade / cut aim (where you slash)
//   CROSS (X)    -> cut button (hold to slash, or click while aiming)
//   RIGHT STICK  -> UI pointer for menus / buttons / shop
//
// Channel model (same 16-channel space as SDL / Wii):
//
//   Role BLADE (channels 0..3, one per pad):
//     Driven by left stick + Cross.
//     Motion mode OFF: press Cross + move left stick = cut.
//     Motion mode ON:  blade follows left stick continuously; Cross is the
//                      explicit cut / speed is still gated by SlashEntity.
//
//   Role UI (channels 12..15):
//     Driven by right stick. Always a normal press finger so widgets,
//     scrollers and menu buttons receive clicks. Independent of the blade
//     so you can aim and navigate UI without fighting the same stick.
//
// Only compiled when FRUIT_PLATFORM_PS3 is set.

#ifdef FRUIT_PLATFORM_PS3

#include <cstdint>

class InputTranslatorPS3 {
public:
    static const int MAX_PADS      = 4;
    static const int CHANNEL_COUNT = 16;

    // UI pointer channels (right stick) -- same range as Wii hover channels.
    static const int UI_CHANNEL_FIRST = 12;

    InputTranslatorPS3();

    void Init();
    void Poll();
    void DispatchForSimTick();
    void ReleaseAllFingers();
    void SetMotionMode(bool enabled);

private:
    float fingerX[CHANNEL_COUNT];
    float fingerY[CHANNEL_COUNT];
    bool  fingerActive[CHANNEL_COUNT];

    bool  prevCrossHeld[MAX_PADS];
    bool  padPresent[MAX_PADS];

    // Last aim positions (game-space) for optional on-screen cursors later.
    float m_BladeGX[MAX_PADS];
    float m_BladeGY[MAX_PADS];
    float m_UiGX[MAX_PADS];
    float m_UiGY[MAX_PADS];
    bool  m_BladeValid[MAX_PADS];
    bool  m_UiValid[MAX_PADS];

    bool motionModeWasOn_;

    void TransformNormalized(float nx, float ny, float& gx, float& gy);
    void PointerPressChannel(int ch, float gx, float gy);
    void PointerReleaseChannel(int ch);
    void PointerMoveChannel(int ch, float gx, float gy);

    void DrainBlade(int port, float nx, float ny, bool crossHeld, bool connected);
    void DrainUI(int port, float nx, float ny, bool connected);
};

#endif // FRUIT_PLATFORM_PS3

#endif // FN_PLATFORM_PS3_INPUT_TRANSLATOR_PS3_H
