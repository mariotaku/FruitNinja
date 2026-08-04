#include "InputDevice.h"
#include <cstring>

namespace Mortar {

// v1.6.1 InputActionMapper::InputActionMapper @0x002756b0.
// ev is a by-value template event; the ctor copies its five words verbatim into
// this+0x0c..this+0x1c (`ldmia lr!,{r0-r3} / stmia r12!,{r0-r3}` then word 4).
// Word 1 is the packed m_Tag | m_KeyCode<<16 halfword pair, so it is rebuilt here
// rather than read as a single member.
InputActionMapper::InputActionMapper(InputEvent ev, InputDeviceCallback cb,
                                      unsigned long actionHash,
                                      unsigned long configSourceHash)
    : m_Enabled(true)
    , m_ActionHash(actionHash)
    , m_ConfigSourceHash(configSourceHash)
    , m_ActionMask(ev.m_Flags)                                        // event word 0
    , m_MatchValue(((uint32_t)ev.m_KeyCode << 16) | (uint32_t)ev.m_Tag) // event word 1
    , m_KeyMask(ev.m_KeyId)                                           // event word 2
    , m_Param4(0)                                                     // event word 3, set below
    , m_Param5(ev.m_Stamp)                                            // event word 4
    , m_Callback(cb)
{
    // Word 3 is a float in the event and an untyped word in the mapper; copy the
    // bits rather than converting.
    memcpy(&m_Param4, &ev.m_Delta, sizeof(m_Param4));
}

// ASM-verified: 2026-07-31T00:00Z v1.6.1 Mortar::InputActionMapper::ProcessEvent @ 0x00275728 (asm-inspector)
//   compile-one.sh (Sourcery 4.4.1, -marm -O2) vs Ghidra disasm: 44 port instrs
//   vs 43 binary, same branch skeleton throughout -- both tail-call the delegate
//   on the MOVE/UP arms (`add r0,r0,#32; pop; b <Call>`) and use a plain `bl` +
//   `b <ret 0>` on the DOWN arm. Residual deltas are all cosmetic: the three
//   InputEvent field offsets (see DIFFERS below), `tst` vs `ands` (the binary
//   keeps the AND result live in r5 for its return path, hence its 4-reg frame
//   and the `cpy r12,r5 / mov r12,#0 / cpy r0,r12` tail where the port folds to
//   `mov r0,#0`), one register-copy, and the callee name (`Delegate1::operator()`
//   in the port, `Delegate1::Call` in the binary -- same PLT-called shape).
//
// v1.6.1 InputActionMapper::ProcessEvent @0x00275728 (43 instructions, ARM).
// Filters the incoming event against this mapper's template event and fires
// m_callback(event) on a match. Control flow ported 1:1 from the binary:
//
//   eventWord = *(uint32*)event                      (InputEvent +0x00)
//   typeBits  = eventWord & 0xffff0000               (DOWN/MOVE/UP)
//   if ((typeBits & m_ActionMask) == 0) return 0;            // type must overlap
//   if ((eventWord & m_ActionMask & 0xffff) == 0) return 0;  // device mask must overlap
//   0x20000 (AXIS): kc = m_MatchValue >> 16;                 // ldrh [this,#0x12]
//                   if (kc < 0x89) { if ((kc & ev[+6]) == 0) return 0;   // bitmask finger set
//                                    return Call(); }                    // tail call
//                   /* else fall into the shared exact compare below */
//   0x80000 (?):    /* fall into the shared exact compare below */
//   shared:         if (ev[+6] != kc) return 0;
//                   return Call();                           // tail call, returns its bool
//   0x10000 (BUTTON): if (ev[+8] != m_KeyMask) return 0;
//                   Call(); return 0;                        // NOT a tail call: 0x0027577c
//                     `bl Call` then `b 0x002757c8` -> `mov r12,#0; cpy r0,r12`, so the
//                     DOWN arm discards the handler's result and always returns 0.
//   default:        return 0;
//
// The return value is the handler's bool on the MOVE/UP arms and 0 everywhere
// else. It is unobservable: the only caller, InputDevice::CheckActions
// @0x002757fc, is an unconditional walk of m_ActionMappers that discards it --
// the binary has NO chain-consume at this level (see CheckActions below).
//
// The three template words compared here are straight copies of the ctor's
// by-value InputEvent (ctor @0x002756b0 stores event words 0..4 into
// this+0x0c..+0x1c), so the compare is field-for-field template-vs-event:
//   m_ActionMask (+0x0c) <-> event word 0        m_MatchValue (+0x10) <-> event word 1
//   m_KeyMask    (+0x14) <-> event word 2
//
// The three event reads now land on the binary's own offsets: m_Flags +0x00,
// m_KeyCode +0x06, m_KeyId +0x08. (They used to land at +0x04 / +0x1c / +0x20 —
// the last remaining divergence in this function, closed by the layout-faithful
// InputEvent.) The ASM-verified stamp above predates that offset fix and is due a
// re-verify.
//
bool InputActionMapper::ProcessEvent(InputEvent* event) {
    uint32_t eventWord = event->m_Flags;
    uint32_t typeBits  = eventWord & 0xffff0000u;

    if ((typeBits & m_ActionMask) == 0) {
        return false;
    }
    if ((eventWord & m_ActionMask & 0xffffu) == 0) {
        return false;
    }

    uint16_t kc   = 0;   // this->m_MatchValue >> 16  -- ldrh [this,#0x12]
    uint16_t evKc = 0;   // binary event +0x06        -- ldrh [event,#0x06]

    if (typeBits == INPUT_ARM_AXIS) {               // 0x20000
        kc   = (uint16_t)(m_MatchValue >> 16);
        evKc = event->m_KeyCode;
        if (kc < 0x89) {
            // Finger-set bitmask, not a keycode: any overlapping bit matches.
            if ((kc & evKc) == 0) {
                return false;
            }
            return m_Callback(event);
        }
        // kc >= 0x89: falls through to the shared exact compare (binary 0x002757ac).
    } else if (typeBits == INPUT_ARM_UNKNOWN) {     // 0x80000
        evKc = event->m_KeyCode;
        kc   = (uint16_t)(m_MatchValue >> 16);
    } else if (typeBits == INPUT_ARM_BUTTON) {      // 0x10000
        if (event->m_KeyId != this->m_KeyMask) {
            return false;
        }
        m_Callback(event);
        return false;   // binary discards the handler result on this arm
    } else {
        return false;
    }

    if (evKc != kc) {
        return false;
    }
    return m_Callback(event);
}

// v1.6.1 InputDevice::InputDevice @0x002759a8 — set fns ptr, list ctor, list clear.
// Port: standard C++ ctor (vptr set by compiler, m_ActionMappers default-constructed).
InputDevice::InputDevice() {
}

// v1.6.1 InputDevice::~InputDevice @0x00275958 (deleting variant @0x0027598c).
InputDevice::~InputDevice() {
}

// ASM-spec v1.6.1 InputDevice::Destroy @0x00275938: m_ActionMappers.clear() (list nodes only, payloads borrowed).
void InputDevice::Destroy() {
    m_ActionMappers.clear();
}

// v1.6.1 InputActionMapper::SetCallback @0x00275690:
//   add r0,r0,#0x20 ; b StackAllocatedPointer<BaseDelegate,32>::operator=
// A straight overwrite of the delegate at +0x20 — no append, no boundness test.
void InputActionMapper::SetCallback(const InputDeviceCallback& cb) {
    m_Callback = cb;
}

// v1.6.1 InputDevice::ClearActions @0x002758b0 — non-virtual base method.
// Removes every mapper matching `configSourceHash` (0 = match all). The iterator
// is advanced BEFORE the erase because the node is freed by it. Only the last
// device in InputManager's broadcast deletes the mapper objects: AddActionMapper
// @0x00243894 pushed the SAME pointer onto every device's list, so an earlier
// device freeing them would leave the rest dangling.
void InputDevice::ClearActions(unsigned long configSourceHash, bool last) {
    std::list<InputActionMapper*>::iterator it = m_ActionMappers.begin();
    while (it != m_ActionMappers.end()) {
        InputActionMapper* mapper = *it;
        std::list<InputActionMapper*>::iterator cur = it;
        ++it;
        if (configSourceHash == 0 || mapper->m_ConfigSourceHash == configSourceHash) {
            m_ActionMappers.erase(cur);
            if (last) {
                delete mapper;
            }
        }
    }
}

// v1.6.1 InputDevice::RegisterInputCallback @0x002759f4 — non-virtual base method.
// Walk the mapper list, overwrite the callback on every hash match. NO INSERT ON
// MISS: an action name absent from Input/Input.txt binds nothing at all.
void InputDevice::RegisterInputCallback(unsigned long actionHash,
                                        InputDeviceCallback cb) {
    for (std::list<InputActionMapper*>::iterator it = m_ActionMappers.begin();
         it != m_ActionMappers.end(); ++it) {
        if ((*it)->m_ActionHash == actionHash) {
            (*it)->SetCallback(cb);
        }
    }
}

// v1.6.1 InputDevice::AxisEvent @0x0027582c — build the axis event on the stack
// and run it past every mapper. Param numbering is the binary's: 1-based
// EXCLUDING `this`, so the stamp is param 5 and the tag is param 6.
void InputDevice::AxisEvent(long axisId, unsigned long mask, float value,
                            float delta, unsigned long stamp, long tag) {
    InputEvent ev;
    ev.m_Flags   = (uint32_t)mask | INPUT_ARM_AXIS;
    ev.m_Tag     = (uint16_t)tag;
    ev.m_KeyCode = (uint16_t)(uint8_t)axisId;   // uxtb
    ev.m_Value   = value;
    ev.m_Delta   = delta;
    ev.m_Stamp   = (uint32_t)stamp;
    CheckActions(&ev);
}

// v1.6.1 InputDevice::ButtonPressed @0x00275864 — same shape as AxisEvent, but
// the key id goes in the value word and event word 1 is written whole from the
// last param (so the stamp is param 4 here, not param 5).
void InputDevice::ButtonPressed(unsigned long key, unsigned long mask, float value,
                                unsigned long stamp, long tagWord) {
    InputEvent ev;
    ev.m_Flags   = (uint32_t)mask | INPUT_ARM_BUTTON;
    ev.m_Tag     = (uint16_t)((uint32_t)tagWord & 0xffffu);
    ev.m_KeyCode = (uint16_t)((uint32_t)tagWord >> 16);
    ev.m_KeyId   = (uint32_t)key;
    ev.m_Delta   = value;
    ev.m_Stamp   = (uint32_t)stamp;
    CheckActions(&ev);
}

// v1.6.1 InputDevice::CheckActions @0x002757fc. Iterate m_ActionMappers list, call ProcessEvent.
// The walk is UNCONDITIONAL: the binary discards ProcessEvent's return value
// (`bl 0x001085e8` at 0x00275818 with no test of r0), so every mapper sees every
// event. There is no chain-consume in the binary at this level.
void InputDevice::CheckActions(InputEvent* event) {
    for (std::list<InputActionMapper*>::iterator it = m_ActionMappers.begin();
         it != m_ActionMappers.end(); ++it) {
        (*it)->ProcessEvent(event);
    }
}

} // namespace Mortar
