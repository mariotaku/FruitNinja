// Analysed: 2026-05-04T00:00

#include "input/InputManager.h"
#include "input/InputDeviceBada.h"
#include "asset/File.h"
#include "system/PowerManager.h"
#include "util/StringHash.h"
#include <cstddef>
#include <cstring>

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

namespace {

// The per-action-line mapper emit that v1.6.1 InputManager::LoadConfigFile
// @0x002442fc inlines twice (once in the '\n' case, once in the post-loop
// section==2 tail). Kept as one static helper so both sites stay identical.
//
// `tmpl` is the caller's single InputEvent, zero-initialised ONCE before the
// parse loop and never reset between lines -- a field written by one line is
// still there for the next, which is why the mapper ctor copies whatever is in
// it. Do not "clean" that up.
void EmitConfigMapper(InputManager* mgr, InputEvent& tmpl,
                      unsigned long cfgHash, unsigned long nameHash,
                      unsigned long key, unsigned long action) {
    unsigned long flags;
    if (key - 0xd0u < 3u) {
        // UNREACHABLE in v1.6.1: the highest key code ParseKey @0x002438c8 can
        // return is AccelAxisZ = 0xbb, so nothing lands in 0xd0..0xd2. Ported
        // anyway because the binary branch is there (stub-don't-skip).
        flags = action | 0x80000;
        tmpl.m_KeyCode = (uint16_t)key;          // event word 1, high half
    } else if (action <= 0xf) {
        // Button actions (pressed/down/released/up): the key code is compared
        // against the event's key-id word.
        flags = action | 0x10000;
        tmpl.m_KeyId = (uint32_t)key;            // event word 2
        tmpl.m_Delta = 0.0f;                     // event word 3
    } else {
        // Axis actions (active/move/dead): the key code goes in word 1's high
        // half. ProcessEvent treats codes < 0x89 there as a BITMASK, which is
        // what makes `MouseAxisX,MouseAxisY` (OR'd by the ',' case) match both.
        flags = action | 0x20000;
        tmpl.m_KeyCode = (uint16_t)key;          // event word 1, high half
    }

    // The guard is AFTER the field writes, exactly as the binary orders it.
    // key == 0 is the normal outcome for every keyboard/X360 line in
    // Input/Input.txt -- ParseKey knows 61 names and none of them is a key or
    // gamepad button, so those lines create no mapper at all.
    if (key != 0 && nameHash != 0 && action != 0) {
        InputDeviceCallback cb;   // EMPTY -- bound later by RegisterInputCallback, or never
        tmpl.m_Flags = (uint32_t)flags;
        InputActionMapper* mapper = new InputActionMapper(tmpl, cb, nameHash, cfgHash);
        mgr->AddActionMapper(mapper);   // broadcasts to EVERY device
    }
}

}  // namespace

// v1.6.1 Mortar::InputManager::LoadConfigFile @0x002442fc.
//
// THE only producer of InputActionMappers in the whole engine. Parses
// Input/Input.txt into one mapper per action line and broadcasts each to every
// registered device, filling InputDevice::m_ActionMappers. Everything downstream
// depends on it: InputDevice::RegisterInputCallback @0x002759f4 binds a callback
// by walking that list, and it never inserts on a miss -- so an action name this
// function did not create is unbindable. Call it BEFORE any RegisterInputCallback
// (GameTaskInitInput @0x001cae0c does exactly that, as its first statement).
//
// Line grammar: `Name: Key1,Key2; action1,action2` terminated by a newline.
//   ':'  ends the action NAME      -> nameHash, section 1
//   ','  ORs another key (section 1) or another action flag (section 2)
//   ';'  ends the key list         -> key ASSIGNED (not OR'd), action reset to 0,
//                                     section 2
//   '\n' ends the action list      -> emit the mapper, section back to 0
//   ' ' / '\t' are skipped WITHOUT resetting the token cursor.
//
// The v1.6.1 Input/Input.txt yields 67 mappers: 32 TouchMove_X/Y<i>, 16
// TouchDown_<i>, 16 TouchReleased_<i>, plus PointerMove / PointerPressed /
// PointerReleased. The other lines name keyboard or X360 inputs that ParseKey
// does not know, so they are dropped. Of the 67, the 16 TouchReleased_<i>
// mappers never get a callback -- v1.6.1 registers no per-finger release handler.
// That is expected; see the "unbound mappers are normal" note in InputDevice.h.
//
// Returns 1 on success, 0 if the file is missing or fails to load.
int InputManager::LoadConfigFile(const char* path) {
    m_loadingConfig = true;
    // Spin until any in-flight InputManager::Update broadcast has finished --
    // the parse mutates every device's mapper list.
    while (m_inUpdate) {
        PowerManager::GetInstance()->Update();
    }

    if (!File::Exists(path, 0)) {
        m_loadingConfig = false;
        return 0;
    }

    const unsigned long cfgHash = StringHash(path);   // m_ConfigSourceHash on every mapper

    File* f = new File(path, 0, 0);
    if (!f->Load(0, 0)) {
        PowerManager::GetInstance()->Update();
        delete f;
        m_loadingConfig = false;
        return 0;
    }

    char tok[256];
    int  t = 0;
    int  section = 0;                 // 0 = name, 1 = keys, 2 = actions
    unsigned long nameHash = 0;
    unsigned long key      = 0;
    unsigned long action   = 0;

    InputEvent tmpl;
    memset(&tmpl, 0, sizeof(tmpl));   // zero-init ONCE, never reset per line

    const unsigned char* data = static_cast<const unsigned char*>(f->Data());
    const unsigned long  size = f->Size();

    for (unsigned long i = 0; i < size; ++i) {
        const unsigned char c = data[i];
        switch (c) {
        case ',':
            tok[t] = 0;
            if (section == 1) {
                key |= ParseKey(StringHash(tok));
            } else {
                action |= ParseAction(StringHash(tok));
            }
            t = 0;
            break;
        case ':':
            tok[t] = 0;
            nameHash = StringHash(tok);
            ++section;
            t = 0;
            break;
        case ';':
            tok[t] = 0;
            action = 0;
            key    = ParseKey(StringHash(tok));   // ASSIGNS, does not OR
            ++section;
            t = 0;
            break;
        case '\n':
            tok[t] = 0;
            action |= ParseAction(StringHash(tok));
            EmitConfigMapper(this, tmpl, cfgHash, nameHash, key, action);
            section = 0;
            t = 0;
            break;
        case ' ':
        case '\t':
            break;                                 // skipped; t is NOT reset
        default:
            if (ValidCharacter(c)) {
                tok[t++] = (char)c;
            }
            break;
        }
    }

    // Trailing line with no newline terminator.
    if (section == 2) {
        tok[t] = 0;
        action |= ParseAction(StringHash(tok));
        EmitConfigMapper(this, tmpl, cfgHash, nameHash, key, action);
    }

    delete f;
    m_loadingConfig = false;
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
// ClearActions(configSourceHash, last=true on final). Only the final device
// frees the mapper objects; see InputDevice::ClearActions.
void InputManager::ClearActions(unsigned long configSourceHash) {
    std::vector<InputDevice*>::iterator it = m_inputDevices.begin();
    std::vector<InputDevice*>::iterator end = m_inputDevices.end();
    while (it != end) {
        std::vector<InputDevice*>::iterator next = it;
        ++next;
        bool last = (next == end);
        (*it)->ClearActions(configSourceHash, last);
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
