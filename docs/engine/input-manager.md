# Input Manager System

## Architecture

```
GlesForm::OnTouchPressed/Moved/Released (Bada OS events)
  → GlesForm::TransformTouchPos (portrait → landscape, axes swapped)
  → Mortar::Touch::__UpdateInternal (ring buffer TEvnt)
  → Mortar::Touch::Update → dispatches InputEvents via InputManager
    → InputManager::RegisterInputCallback targets:
        → SlashEntity::TouchDown / TouchMoveX / TouchMoveY
        → SplashInputEvent, ParticleInputEvent, etc.
```

## InputManager (singleton)

| Field | Type | Notes |
|-------|------|-------|
| +0x00 | InputManagerFns* | vtable |
| +0x04 | byte | m_bLoading (1 during LoadConfigFile) |
| +0x05 | byte | m_bWaiting |
| +0x08 | list\<InputDevice*\> | m_inputDevices |

### Key Methods

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| InputManager::InputManager | 0x0019694c | 15 | Init empty device list |
| LoadConfigFile | 0x001969d8 | 198 | Parse .txt config → InputActionMappers |
| RegisterInputCallback | 0x0019683c | 32 | Register hash → Delegate1 callback on all devices |
| ClearActions | 0x001961d0 | 37 | Clear all registered actions on all devices |
| GetInstance | (GOT thunk) | — | Singleton access |

### LoadConfigFile (0x001969d8, 198 lines)

Parses text input config files (e.g., `Data/input/game_input.txt`). Format:

```
ActionName:  KeyName;   eventType
```

Parser state machine:
- Reads byte-by-byte from loaded file
- `:` advances state (name → key section)
- `;` advances state (key → action section)
- `,` separates multiple keys/actions
- `\n` finalizes entry → creates InputActionMapper

For each complete entry:
1. `StringHash(actionName)` → action hash
2. `ParseKey(keyHash)` → key bitmask
3. `ParseAction(actionHash)` → action flags (down=0x10000, move=0x20000, up=0x80000)
4. Creates `InputActionMapper(0x44 bytes)` with event+delegate+hash
5. `AddActionMapper` → adds to device's action list

### RegisterInputCallback (0x0019683c, 32 lines)

```c
void InputManager::RegisterInputCallback(ulong actionHash, Delegate1<bool,InputEvent*> callback) {
    for (InputDevice* device : m_inputDevices) {
        InputDevice::RegisterInputCallback(device, actionHash, callback);
    }
}
```

Registers a callback for a named action (identified by StringHash). When the action fires, the callback is invoked with an InputEvent*.

## InputDevice (base class)

| Field | Type | Notes |
|-------|------|-------|
| +0x00 | vtable* | |
| +0x04 | list\<InputActionMapper*\> | actionMappers |

### CheckActions (0x001b36b0, 28 lines)

Iterates all InputActionMappers and calls `ProcessEvent(event)` on each.

## InputActionMapper (0x44 bytes)

Created by LoadConfigFile for each action binding.

| Field | Type | Notes |
|-------|------|-------|
| +0x00..+0x0b | InputEvent | Event template (action flags + key) |
| +0x0c | uint | m_ActionMask | Event type mask (down/move/up flags) |
| +0x10 | ushort+ushort | m_KeyData | Key identifier |
| +0x14 | int | m_MapperRef | For chained events |
| +0x20 | Delegate1\<bool,InputEvent*\> | m_Callback | Action callback |
| +0x40 | uint | m_ActionHash | StringHash of action name |

### ProcessEvent (0x001b3508, 48 lines)

```c
bool InputActionMapper::ProcessEvent(InputEvent* event) {
    uint eventType = event->flags & 0xFFFF0000;
    if ((eventType & this->m_ActionMask) == 0) return false;
    if ((event->flags & this->m_ActionMask & 0xFFFF) == 0) return false;

    if (eventType == 0x20000) {         // move event
        if (event->key matches this->key)
            return m_Callback(event);
    } else if (eventType == 0x80000) {  // up event
        if (event->key == this->key)
            return m_Callback(event);
    } else if (eventType == 0x10000) {  // down event
        if (event->mapper == this->m_MapperRef)
            return m_Callback(event);
    }
    return false;
}
```

## InputDeviceBada (0x1C bytes)

Platform-specific device that receives Bada touch events.

| Field | Type | Notes |
|-------|------|-------|
| +0x00 | vtable* | InputDeviceBada vtable |
| +0x04 | (InputDevice base) | actionMappers list |
| +0x0c | int | field_0x0c = 0 |
| +0x10 | int | field_0x10 = 0 |
| +0x14 | int | field_0x14 = 0 |
| +0x18 | int | field_0x18 = 0 |

## GameTaskInitInput (0x00169670)

Called during GameInit. Sets up 16 touch input channels (for 16-finger multitouch):

```c
void GameTaskInitInput() {
    InputManager* mgr = InputManager::GetInstance();
    mgr->LoadConfigFile("Data/input/game_input.txt");

    for (int i = 0; i < 16; i++) {
        // Create SlashEntity (type 3) for each touch channel
        SlashEntity* slash = ActorManager::Add(3, true);
        taskState->slashEntities[i] = slash;

        // Register 3 callbacks per touch channel:
        sprintf(name, "TouchDown_%d", i);
        mgr->RegisterInputCallback(StringHash(name), TouchDownCallback);

        sprintf(name, "TouchMove_X%d", i);
        mgr->RegisterInputCallback(StringHash(name), TouchMoveXCallback);

        sprintf(name, "TouchMove_Y%d", i);  // (same callback registered twice — once for up?)
        mgr->RegisterInputCallback(StringHash(name), TouchMoveYCallback);
    }

    // Additional global callbacks:
    mgr->RegisterInputCallback(StringHash("TouchScreen"), ...);
    mgr->RegisterInputCallback(StringHash("Particles"), ...);
    // + 5 more (pause, menu, back, etc.)
}
```

## Input Config File Format

Located at `Data/input/game_input.txt` and `Data/input/menu_input.txt`.

```
ActionName:   KeyBinding;   EventType
```

Where:
- **ActionName**: String hashed via `StringHash()` to match `RegisterInputCallback`
- **KeyBinding**: Key/axis name (e.g., `MouseButton1`, `TouchAxisX1`, `Touch1`)
- **EventType**: `down` (0x10000), `move` (0x20000), or `up` (0x80000)

### Observed bindings (game_input.txt):

| Action | Key | Type | Purpose |
|--------|-----|------|---------|
| TouchScreen | MouseButton1 | down | Any touch start |
| Particles | MouseButton2 | down | Particle debug |
| TouchMove_X0..X15 | TouchAxisX1..X16 | move | Per-finger X movement |
| TouchMove_Y0..Y15 | TouchAxisY1..Y16 | move | Per-finger Y movement |
| TouchDown_0..15 | Touch1..Touch16 | down | Per-finger touch start |
| TouchUp_0..15 | Touch1..Touch16 | up | Per-finger touch end |

## For Porting

The InputManager system is a Mortar engine abstraction over the Bada touch API. For the SDL2 port:

1. **Replace entirely** — SDL2 touch/mouse events map directly to game actions
2. **Keep the StringHash action names** — they're used by GameTaskInitInput callbacks
3. **16-touch channels** map to SDL2 finger IDs (SDL_FINGERDOWN/UP/MOTION)
4. The config files can be skipped — hardcode the action bindings in code
5. Key concept: each touch finger creates/drives a SlashEntity for blade trails

```
SDL2 Port Pipeline:
  SDL_FINGERDOWN → find free SlashEntity → SlashEntity::TouchDown(x, y)
  SDL_FINGERMOTION → SlashEntity::TouchMoveX(x) + TouchMoveY(y)
  SDL_FINGERUP → SlashEntity release
  SDL_MOUSEBUTTONDOWN → same as finger (for desktop testing)
```
