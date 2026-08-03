// Analysed: 2026-05-04T00:00

#include "input/InputManager.h"
#include "input/InputDeviceBada.h"
#include "util/StringHash.h"
#include <cstddef>

namespace Mortar {

InputManager* InputManager::s_instance = nullptr;

// v1.6.1 Mortar::InputManager::InputManager @0x0024375c — ctor: both flags = 0, vector default-inits (all three pointers zeroed).
InputManager::InputManager()
    : m_loadingConfig(false)
    , m_inUpdate(false)
{
    s_instance = this;
}

// v1.6.1 Mortar::InputManager::~InputManager @0x0024371c — dtor: vector dtor only; does NOT call Destroy.
// Virtual to match binary isPolymorphic=true.
InputManager::~InputManager() {
    s_instance = nullptr;
    // Note: binary does NOT call Destroy() in dtor — vector goes out of scope only.
    // Devices leaked intentionally (matches binary behavior at 0x0024371c).
}

InputManager* InputManager::GetInstance() {
    return s_instance;
}

// v1.6.1 Mortar::InputManager::Init @0x002447d4 — Init: alloc InputDeviceBada via new, dev->Init(flags), push_back.
void InputManager::Init(unsigned long flags) {
    InputDeviceBada* dev = new InputDeviceBada();
    dev->Init(flags);
    m_inputDevices.push_back(dev);
}

// v1.6.1 Mortar::InputManager::Destroy @0x00243798 — Destroy: clear flags, ClearActions(all=true) on first
//   device only, then Destroy+dtor on each, list.clear().
void InputManager::Destroy() {
    m_loadingConfig = false;
    m_inUpdate = false;
    if (!m_inputDevices.empty()) {
        // v1.6.1 Mortar::InputManager::Destroy @0x00243798: ClearActions(hash=0, last=true) on the FIRST device only
        // (the loop calls it when its index counter is 0). hash=0 + last=true means
        // "clear every action binding" — the same 2nd-param ('last') used by the
        // ClearActions(hash) broadcast below, here forced true for the wholesale clear.
        InputDevice* first = m_inputDevices.front();
        first->ClearActions(0, true);
    }
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Destroy();
        delete *it;
    }
    m_inputDevices.clear();
}

// v1.6.1 Mortar::InputManager::Update @0x00243838 — Update: gate on m_loadingConfig, m_inUpdate=true,
//   broadcast Update(dt), m_inUpdate=false.
// NOTE: genuine v1.6.1 gate -- @0x00243838 is 'ldrb r3,[r0,#0x4]; cmp r3,#0; bne
// epilogue'. Not a port addition.
void InputManager::Update(float dt) {
    if (m_loadingConfig) return;
    m_inUpdate = true;
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Update(dt);
    }
    m_inUpdate = false;
}

// This stub BLOCKS removing the port's m_bindings substitute (InputDeviceBada).
// In the binary this function is the ONLY producer of InputActionMappers: it parses
// Input/Input.txt and, per action line, does operator new(0x44) + InputActionMapper
// ctor + InputManager::AddActionMapper (which broadcasts to every device's
// AddActionMapper, filling InputDevice::m_ActionMappers). With it stubbed,
// m_ActionMappers stays permanently empty, so the binary-faithful non-virtual
// RegisterInputCallback/ClearActions/CheckActions bodies would register and dispatch
// nothing — hence the port's InputDeviceBinding list stands in. Porting this also
// needs InputManager::ParseAction and InputManager::ParseKey.
//
// One-callback-per-hash constraint (satisfied as of the blade-input rework):
// InputDevice::RegisterInputCallback @0x002759f4 walks m_ActionMappers and calls
// InputActionMapper::SetCallback on every mapper whose m_ActionHash matches.
// SetCallback OVERWRITES the mapper's single 36-byte Delegate1 -- one mapper holds
// exactly ONE callback, there is no append, so a hash with two port-side handlers
// would silently lose one under the mapper path. The blade used to be such a case
// (GameTaskInitInput and SlashEntity::Init both bound TouchDown_<i> /
// TouchMove_X<i> / TouchMove_Y<i>). It no longer is: GameTaskInitInput is the sole
// registrar, and its TouchDownCallback @0x001cbf18 / PointerMoveCallback
// @0x001cbfcc dispatch into g_pSlashEntities[n] the way the binary does.
//
// What still blocks the swap:
//   * The action names. The port's translators emit "TouchUp_<i>", which
//     input.txt does not declare (it is "TouchReleased_<i>", and v1.6.1 binds no
//     callback to it at all). Rename translator-side when this lands.
//   * InputEvent's port-only side channel (actionHash, x, y) and the m_bindings
//     list itself -- see the DIFFERS in InputEvent.h and InputDevice.h.
//   * InputManager::ParseAction and InputManager::ParseKey must be live.
int InputManager::LoadConfigFile(const char* path) {
    (void)path;
    // Defunct: input config file — no-op stub; v1.6.1 Mortar::InputManager::LoadConfigFile @ 0x002442fc
    return 1;
}

// v1.6.1 Mortar::InputManager::AddActionMapper @0x00243894 — broadcast to devices.
void InputManager::AddActionMapper(InputActionMapper* mapper) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->AddActionMapper(mapper);
    }
}

// v1.6.1 Mortar::InputManager::ClearActions @0x002441e0 — broadcast
// ClearActions(hash, last=true on final).
void InputManager::ClearActions(unsigned long actionHash) {
    std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
    std::vector<InputDevice*>::iterator end = m_inputDevices.end();
    while (it != end) {
        std::vector<InputDevice*>::iterator next = it;
        ++next;
        bool last = (next == end);
        (*it)->ClearActions(actionHash, last);
        it = next;
    }
}

// Search devices by GetDeviceType.
// ASM-spec v1.6.1 Mortar::InputManager::HasInputDevice @ 0x00244298
// The `if (out)` on the match path is genuine: @0x002442c8
// `cmp r6,#0x0 / movne r0,#0x1 / ldrne r3,[r7] / strne r3,[r6]`, then @0x002442dc
// `b 0x002442f4` -> `mov r0,#1`. A match with out==NULL returns true without writing.
// The not-found path writes nothing: @0x002442ec `mov r0,#0x0 / ldmia sp!,{...,pc}`.
bool InputManager::HasInputDevice(InputDeviceTypes type, InputDevice** out) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        if ((*it)->GetDeviceType() == type) {
            if (out) *out = *it;
            return true;
        }
    }
    return false;
}

// v1.6.1 Mortar::InputManager::OnAxisExtentsChanged @0x00244238 — broadcast.
void InputManager::OnAxisExtentsChanged() {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->OnAxisExtentsChanged();
    }
}

// v1.6.1 Mortar::InputManager::ParseAction @0x00244060 — lookup table of 7
// action-name hashes -> event-type flag.
// Lazily builds a static {StringHash(name), flag} table (the binary guards it with
// __cxa_guard; a function-local static gives the identical once-only init), then
// linear-searches for `hash` and returns the matching flag, or 0 on no match.
// Dead unless config-file parsing enabled (LoadConfigFile is a Defunct no-op stub).
// Table source: ONE interleaved array @0x002d8fbc, stride 8 -- name hash at +0
// (filled by the __cxa_guard'ed init), flag at +4 (pre-initialised in .data),
// 7 entries. It sits immediately after ParseKey's 61-entry table
// (0x002d8dd4 + 61*8 == 0x002d8fbc). Strings @ 0x001c25dc/0x001b9730:
//   "pressed"=0x01, "released"=0x04, "down"=0x02, "up"=0x08,
//   "active"=0x10, "move"=0x20, "dead"=0x40
unsigned long InputManager::ParseAction(unsigned long hash) const {
    struct ActionEntry { unsigned long nameHash; unsigned long flag; };
    static bool initialised = false;
    static ActionEntry table[7];
    if (!initialised) {
        table[0].nameHash = StringHash("pressed");  table[0].flag = 0x01;
        table[1].nameHash = StringHash("released"); table[1].flag = 0x04;
        table[2].nameHash = StringHash("down");     table[2].flag = 0x02;
        table[3].nameHash = StringHash("up");       table[3].flag = 0x08;
        table[4].nameHash = StringHash("active");   table[4].flag = 0x10;
        table[5].nameHash = StringHash("move");     table[5].flag = 0x20;
        table[6].nameHash = StringHash("dead");     table[6].flag = 0x40;
        initialised = true;
    }
    for (int i = 0; i < 7; ++i) {
        if (hash == table[i].nameHash) {
            return table[i].flag;
        }
    }
    return 0;
}

// v1.6.1 Mortar::InputManager::ParseKey @0x002438c8 — lookup of 61 (0x3d) key-name
// hashes -> key code. The binary guards the table with __cxa_guard_acquire and scans
// it linearly (`while (i != 0x3d)`), returning the paired value or 0.
// Same lazily-built guarded static table pattern as ParseAction; linear-searches
// for `hash`, returns the matching key code (InputKeys value), or 0 on no match.
// Dead unless config-file parsing enabled (LoadConfigFile is a Defunct no-op stub).
// Table source: ONE interleaved array @0x002d8dd4, stride 8 -- name hash at +0, key
// code at +4 (it is NOT a separate name array plus value array). ParseAction's
// 7-entry table is contiguous with it at 0x002d8fbc. Key codes:
//   MouseButton1..8        -> 0x6c..0x73
//   MouseAxisX, MouseAxisY -> 0x74, 0x75
//   Touch1..16             -> 0x89..0x98
//   TouchAxisX1..16        -> 0x99..0xa8
//   TouchAxisY1..16        -> 0xa9..0xb8
//   AccelAxisX/Y/Z         -> 0xb9, 0xba, 0xbb
unsigned long InputManager::ParseKey(unsigned long hash) const {
    struct KeyEntry { unsigned long nameHash; unsigned long code; };
    static bool initialised = false;
    static KeyEntry table[61];
    if (!initialised) {
        static const char* const names[61] = {
            "MouseButton1", "MouseButton2", "MouseButton3", "MouseButton4",
            "MouseButton5", "MouseButton6", "MouseButton7", "MouseButton8",
            "MouseAxisX", "MouseAxisY",
            "Touch1", "Touch2", "Touch3", "Touch4", "Touch5", "Touch6",
            "Touch7", "Touch8", "Touch9", "Touch10", "Touch11", "Touch12",
            "Touch13", "Touch14", "Touch15", "Touch16",
            "TouchAxisX1", "TouchAxisX2", "TouchAxisX3", "TouchAxisX4",
            "TouchAxisX5", "TouchAxisX6", "TouchAxisX7", "TouchAxisX8",
            "TouchAxisX9", "TouchAxisX10", "TouchAxisX11", "TouchAxisX12",
            "TouchAxisX13", "TouchAxisX14", "TouchAxisX15", "TouchAxisX16",
            "TouchAxisY1", "TouchAxisY2", "TouchAxisY3", "TouchAxisY4",
            "TouchAxisY5", "TouchAxisY6", "TouchAxisY7", "TouchAxisY8",
            "TouchAxisY9", "TouchAxisY10", "TouchAxisY11", "TouchAxisY12",
            "TouchAxisY13", "TouchAxisY14", "TouchAxisY15", "TouchAxisY16",
            "AccelAxisX", "AccelAxisY", "AccelAxisZ"
        };
        // Key codes: 0x6c..0x75 for the first 10 entries, then 0x89..0xbb
        // for the remaining 51 (contiguous in the binary value table).
        int i = 0;
        for (; i < 10; ++i) {
            table[i].nameHash = StringHash(names[i]);
            table[i].code = 0x6cUL + (unsigned long)i;
        }
        for (; i < 61; ++i) {
            table[i].nameHash = StringHash(names[i]);
            table[i].code = 0x89UL + (unsigned long)(i - 10);
        }
        initialised = true;
    }
    for (int i = 0; i < 61; ++i) {
        if (hash == table[i].nameHash) {
            return table[i].code;
        }
    }
    return 0;
}

// v1.6.1 Mortar::InputManager::RegisterInputCallback @0x0024475c — broadcast to
// devices (2-param; bindings live on device).
// DIFFERS: original = per-device binding store, see v1.6.1 Mortar::InputManager::RegisterInputCallback @0x0024475c
void InputManager::RegisterInputCallback(unsigned long actionHash, InputCallback cb) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        InputDeviceCallback devcb;
        devcb = cb;
        (*it)->RegisterInputCallback(actionHash, devcb);
    }
}

// v1.6.1 Mortar::InputManager::ResetDevices @0x0024380c — broadcast Reset().
void InputManager::ResetDevices() {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->Reset();
    }
}

// v1.6.1 Mortar::InputManager::SetQueueEventsUntilUpdate @0x002436e8 — broadcast.
void InputManager::SetQueueEventsUntilUpdate(bool v) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->SetQueueEventsUntilUpdate(v);
    }
}

// v1.6.1 Mortar::InputManager::SetSendDownCallbacksEachUpdate @0x00244264 — broadcast.
void InputManager::SetSendDownCallbacksEachUpdate(bool v) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->SetSendDownCallbacksEachUpdate(v);
    }
}

// Binary @ 0x00195fd8 — return (c - 0x20) < 0x90.
bool InputManager::ValidCharacter(unsigned char c) const {
    return (unsigned char)(c - 0x20u) < 0x90u;
}

// Port-side: dispatch through all devices.
void InputManager::DispatchEvent(InputEvent* event) {
    for (std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
         it != m_inputDevices.end(); ++it) {
        (*it)->DispatchEvent(event);
    }
}

// Port-side: global dispatch (no hash filter — all bindings on all devices).
void InputManager::DispatchGlobal(InputEvent* event) {
    // Route same as DispatchEvent; device-side DispatchEvent filters by hash.
    // For global events (no specific hash), callers should set event->actionHash = 0
    // or use a dedicated broadcast path.
    // TODO: refine global dispatch semantics when full binary dispatch path is ported.
    DispatchEvent(event);
}

} // namespace Mortar

// ASM-spec v1.6.1 DefaultKeyCallback @0x18cd6c: identity; returns its argument unchanged.
const char* DefaultKeyCallback(const char* key) {
    return key;
}

// ASM-spec v1.6.1 TransformInput @0x1a03ac: identity no-op (binary body is `bx lr`).
// Raw-touch to game-coord rotate/scale lives in InputDeviceBada, NOT here.
InputEvent* TransformInput(InputEvent* ev, float& /*x*/, float& /*y*/) {
    return ev;
}
