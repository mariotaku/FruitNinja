# SystemManager

## SystemManager (singleton, size = 0xD4 / 212 bytes)

Engine singleton for frame timing (FPS calculation) and quit lifecycle. Accessed via GOT-relative addressing.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | pointer | fns | vtable |
| +0x04 | 1 | byte | m_bRunning | 1 = running, QuitGame() sets to 0 |
| +0x06 | 2 | short | m_LastFrameTime | Last frame time in ticks |
| +0x08 | 2 | short | m_AvgFPS | Average FPS from ring buffer |
| +0x0A | 2 | short | m_MinFPS | Min FPS from ring buffer |
| +0x0C | 2 | short | m_MaxFPS | Max FPS from ring buffer |
| +0x0E | 1 | byte | m_RingMaxIdx | Ring buffer max valid index |
| +0x0F | 1 | byte | m_RingWriteIdx | Ring buffer write position (wraps at 30) |
| +0x10 | 60 | short[30] | m_FrameTimeRing | 30-entry frame time ring buffer |
| +0x4C | 1 | byte | m_QuitState | 3 = init, 2 = RequestQuit |
| +0x50 | 4 | int | m_field50 | From constructor |
| +0x54 | 128 | UniqueDeviceID | m_deviceId | Device identifier |

### Key Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| SystemManager() | 0x0018afa4 | `__thiscall (this)` | Constructor — inits FPS fields to 0x3C (60fps), m_QuitState=3 |
| ~SystemManager | 0x0018ada8 | `__thiscall (this)` | Destructor |
| Update | 0x0018ade0 | `__thiscall (this, float* dt)` | FPS ring buffer calculator, outputs dt |
| QuitGame | 0x0018ae88 | `__thiscall (this)` | Sets m_bRunning = 0 |
| RequestQuit | 0x0018ae90 | `__thiscall (this)` | Sets m_QuitState = 2 |
| _RetrieveDeviceID | 0x0018aea0 | `__thiscall (this)` | Populates m_deviceId |
| CurrentDate | 0x0018aeb8 | `__thiscall (MortarDate*, bool)` | Returns current date/time |
| GetInstance (thunk) | 0x000f3a44 | thunk | Dispatches to real GetInstance |

### Update (0x0018ade0) — Fixed Timestep + FPS Ring Buffer

<!-- Analysed: 2026-04-09T11:00 -->

`SystemManager::Update(float* dt)` is called once per frame from `FruitNinja::Draw` (0x1824e0). It:

1. **Outputs fixed dt**: `*param_1 = DAT_0018ae84` = **0x3C888889 = 1.0/60.0 ≈ 0.01667**
   - This is a **hardcoded constant** — never measures actual elapsed time
   - ALL game logic (physics, lerps, timers) is tuned for this fixed step
2. Writes hardcoded frame time `0x3B` (59) to `m_FrameTimeRing[m_RingWriteIdx]` and `m_LastFrameTime`
3. Advances `m_RingWriteIdx` (wraps at 30 via `idx < 0x1d ? +1 : -0x1d`)
4. Scans ring buffer for min → `m_MinFPS`
5. Scans ring buffer for max → `m_MaxFPS`
6. Computes average → `m_AvgFPS`
7. Returns `m_bRunning` (false = game should exit)

### Constructor Init Values

```c
SystemManager::SystemManager() {
    this->m_QuitState = 3;     // init state
    this->m_bRunning = 1;      // running
    this->m_MaxFPS = 0x3C;     // 60 fps
    this->m_LastFrameTime = 0x3C;
    this->m_MinFPS = 0x3C;
    this->m_AvgFPS = 0x3C;
    this->m_RingWriteIdx = 0;
    this->m_RingMaxIdx = 0;
}
```

### Port Notes

For the SDL2 port:
- `QuitGame()` sets `m_bRunning = 0` — the game loop checks this to exit
- `RequestQuit()` sets `m_QuitState = 2` — triggers graceful shutdown with save
- The FPS ring buffer (30 frames) is informational only — not used for game logic
- `Update` outputs **fixed dt = 1/60** matching `DAT_0018ae84` — do NOT compute dt from elapsed time
- The original Bada timer fires every **10ms** (100fps); combined with dt=1/60, the game runs at 100/60 = **1.667× game-speed**. Port uses `SDL_Delay(10ms)` frame pacing to match

---

## See Also

- [Game loop](../functions/game-loop.md) — SystemManager::Update called in main loop
- [State machine](../systems/state-machine.md) — GameTaskUpdate checks running flag
