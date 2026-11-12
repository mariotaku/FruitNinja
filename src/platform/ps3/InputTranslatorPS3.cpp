// PS3 DualShock 3 input translator (PSL1GHT ioPad).
//
// Mapping:
//   Left stick  = blade aim (cut)
//   Cross (X)   = cut button
//   Right stick = UI / menus / buttons

#ifdef FRUIT_PLATFORM_PS3

#include "platform/ps3/InputTranslatorPS3.h"
#include "input/Touch.h"
#include "render/Layout.h"
#include "debug/DebugFlags.h"

#include <io/pad.h>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace {

const int kStickDeadzone = 24;

float StickAxis(u8 raw)
{
    int v = static_cast<int>(raw) - 0x80;
    if (v > -kStickDeadzone && v < kStickDeadzone)
        return 0.0f;
    const float range = 128.0f - static_cast<float>(kStickDeadzone);
    float f = static_cast<float>(v);
    if (f > 0.0f)
        f = (f - static_cast<float>(kStickDeadzone)) / range;
    else
        f = (f + static_cast<float>(kStickDeadzone)) / range;
    if (f < -1.0f) f = -1.0f;
    if (f >  1.0f) f =  1.0f;
    return f;
}

// Stick delta [-1,1] -> normalized screen [0,1] (centre = 0.5).
void StickToNormalized(float sx, float sy, float& nx, float& ny)
{
    nx = 0.5f + sx * 0.5f;
    ny = 0.5f + sy * 0.5f;
    if (nx < 0.0f) nx = 0.0f;
    if (nx > 1.0f) nx = 1.0f;
    if (ny < 0.0f) ny = 0.0f;
    if (ny > 1.0f) ny = 1.0f;
}

}  // namespace

InputTranslatorPS3::InputTranslatorPS3()
    : motionModeWasOn_(false)
{
    memset(fingerX, 0, sizeof(fingerX));
    memset(fingerY, 0, sizeof(fingerY));
    memset(fingerActive, 0, sizeof(fingerActive));
    memset(prevCrossHeld, 0, sizeof(prevCrossHeld));
    memset(padPresent, 0, sizeof(padPresent));
    memset(m_BladeGX, 0, sizeof(m_BladeGX));
    memset(m_BladeGY, 0, sizeof(m_BladeGY));
    memset(m_UiGX, 0, sizeof(m_UiGX));
    memset(m_UiGY, 0, sizeof(m_UiGY));
    memset(m_BladeValid, 0, sizeof(m_BladeValid));
    memset(m_UiValid, 0, sizeof(m_UiValid));
}

void InputTranslatorPS3::Init()
{
    if (ioPadInit(7) != 0) {
        printf("[PS3] ioPadInit failed\n");
        return;
    }

    padInfo info;
    if (ioPadGetInfo(&info) == 0) {
        printf("[PS3] pads connected: %u / max %u\n", info.connected, info.max);
        for (int i = 0; i < MAX_PADS; ++i) {
            if (info.status[i]) {
                ioPadSetSensorMode(i, 1);
                padPresent[i] = true;
            }
        }
    }
}

void InputTranslatorPS3::SetMotionMode(bool enabled)
{
    FN::g_MotionMode = enabled;
}

void InputTranslatorPS3::TransformNormalized(float nx, float ny, float& gx, float& gy)
{
    Layout::TouchToGame(nx, ny, &gx, &gy);
}

void InputTranslatorPS3::PointerPressChannel(int ch, float gx, float gy)
{
    if (fingerActive[ch]) return;
    fingerActive[ch] = true;
    fingerX[ch] = gx;
    fingerY[ch] = gy;
    Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy);
}

void InputTranslatorPS3::PointerReleaseChannel(int ch)
{
    if (!fingerActive[ch]) return;
    fingerActive[ch] = false;
    Mortar::Touch::GetInstance().OnReleased(ch + 1);
}

void InputTranslatorPS3::PointerMoveChannel(int ch, float gx, float gy)
{
    if (!fingerActive[ch]) return;
    if (gx == fingerX[ch] && gy == fingerY[ch]) return;
    fingerX[ch] = gx;
    fingerY[ch] = gy;
    Mortar::Touch::GetInstance().OnMoved(ch + 1, gx, gy);
}

// ---------------------------------------------------------------------------
// BLADE = left stick + Cross (X)
// ---------------------------------------------------------------------------
void InputTranslatorPS3::DrainBlade(int port, float nx, float ny, bool crossHeld, bool connected)
{
    if (port < 0 || port >= MAX_PADS) return;

    const int bladeCh = port;  // channels 0..3

    if (!connected) {
        if (fingerActive[bladeCh]) PointerReleaseChannel(bladeCh);
        prevCrossHeld[port] = false;
        m_BladeValid[port] = false;
        return;
    }

    float gx = 0.0f, gy = 0.0f;
    TransformNormalized(nx, ny, gx, gy);

    m_BladeGX[port] = gx;
    m_BladeGY[port] = gy;
    m_BladeValid[port] = true;

    if (FN::g_MotionMode) {
        // Motion mode: blade always tracks left stick (hover-style).
        // Cross is the explicit "I'm cutting / confirming" edge; while held
        // the finger stays pressed so SlashEntity can speed-gate the slice.
        // When Cross is up the blade still follows (aim), matching Magic
        // Remote / Wii motion feel.
        if (!fingerActive[bladeCh]) {
            PointerPressChannel(bladeCh, gx, gy);
        } else {
            PointerMoveChannel(bladeCh, gx, gy);
        }

        // Optional: if you want Cross to *only* cut on press-edge and lift
        // on release while motion is on, uncomment the block below and
        // remove the always-pressed path above. Current behaviour keeps the
        // blade live so slow moves don't cut (speed gate) and fast moves do.
        (void)crossHeld;
        (void)prevCrossHeld[port];
    } else {
        // Classic mode: hold Cross + move left stick = cut.
        const bool down    = crossHeld;
        const bool wasDown = fingerActive[bladeCh];

        if (down && !wasDown) {
            PointerPressChannel(bladeCh, gx, gy);
        } else if (down && wasDown) {
            PointerMoveChannel(bladeCh, gx, gy);
        } else if (!down && wasDown) {
            PointerReleaseChannel(bladeCh);
        }
    }

    prevCrossHeld[port] = crossHeld;
}

// ---------------------------------------------------------------------------
// UI = right stick (menus, buttons, shop)
// ---------------------------------------------------------------------------
void InputTranslatorPS3::DrainUI(int port, float nx, float ny, bool connected)
{
    if (port < 0 || port >= MAX_PADS) return;

    const int uiCh = UI_CHANNEL_FIRST + port;  // channels 12..15

    if (!connected) {
        if (fingerActive[uiCh]) PointerReleaseChannel(uiCh);
        m_UiValid[port] = false;
        return;
    }

    float gx = 0.0f, gy = 0.0f;
    TransformNormalized(nx, ny, gx, gy);

    m_UiGX[port] = gx;
    m_UiGY[port] = gy;
    m_UiValid[port] = true;

    // Right stick always drives a normal UI finger so widgets see presses.
    // Stick at rest keeps the last position but does not force a permanent
    // press: we press on first valid read and keep it live so hover/click
    // helpers work; a dedicated "confirm" button can be added later (Circle).
    if (!fingerActive[uiCh]) {
        PointerPressChannel(uiCh, gx, gy);
    } else {
        PointerMoveChannel(uiCh, gx, gy);
    }
}

void InputTranslatorPS3::Poll()
{
    padInfo info;
    memset(&info, 0, sizeof(info));
    ioPadGetInfo(&info);

    for (int port = 0; port < MAX_PADS; ++port) {
        const bool connected = (info.status[port] != 0);
        padPresent[port] = connected;

        if (!connected) {
            DrainBlade(port, 0.5f, 0.5f, false, false);
            DrainUI(port, 0.5f, 0.5f, false);
            continue;
        }

        padData data;
        memset(&data, 0, sizeof(data));
        if (ioPadGetData(port, &data) != 0 || data.len == 0) {
            DrainBlade(port, 0.5f, 0.5f, false, true);
            DrainUI(port, 0.5f, 0.5f, true);
            continue;
        }

        // --- Left stick = BLADE ---
        float lnx, lny;
        StickToNormalized(StickAxis(data.ANA_L_H), StickAxis(data.ANA_L_V), lnx, lny);

        // --- Right stick = UI ---
        float rnx, rny;
        StickToNormalized(StickAxis(data.ANA_R_H), StickAxis(data.ANA_R_V), rnx, rny);

        // Cross (X) = cut button only
        const bool crossHeld = (data.BTN_CROSS != 0);

        DrainBlade(port, lnx, lny, crossHeld, true);
        DrainUI(port, rnx, rny, true);
    }
}

void InputTranslatorPS3::DispatchForSimTick()
{
    Mortar::Touch::GetInstance().Update(0.0f);

    if (motionModeWasOn_ && !FN::g_MotionMode) {
        // Leaving motion mode: blade channels go back to Cross-gated;
        // release any blade that is held without Cross so it doesn't stick.
        for (int port = 0; port < MAX_PADS; ++port) {
            if (!prevCrossHeld[port] && fingerActive[port]) {
                PointerReleaseChannel(port);
            }
        }
    }
    motionModeWasOn_ = FN::g_MotionMode;
}

void InputTranslatorPS3::ReleaseAllFingers()
{
    for (int ch = 0; ch < CHANNEL_COUNT; ++ch) {
        if (!fingerActive[ch]) continue;
        Mortar::Touch::GetInstance().OnReleased(ch + 1);
        fingerActive[ch] = false;
    }
    Mortar::Touch::GetInstance().Update(0.0f);
    memset(prevCrossHeld, 0, sizeof(prevCrossHeld));
}

#endif // FRUIT_PLATFORM_PS3
