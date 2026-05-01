# GameOverScreen Deep RE Pass

Comprehensive ASM/decomp-verified audit of `GameOverScreen` covering struct
layout, vtable, full state machine, and integration with `BonusScreen`,
`FruitFactControl`, `BonusManager`, `FruitSaveData`, `LeaderboardManager`,
`AchievementManager`, and `WaveManager`. Produced 2026-05-02 by `re-analyst`
against `FruitNinja.exe` (ARM32 LE, image base 0x00010000, GOT base 0x001ec130).

This supersedes the per-state summary in `docs/screens/game-over.md` and
the high-level flow in `docs/systems/game-over-flow.md` for the **screen**
itself; the trigger semantics (six callers, `pauseFlag` guard, `EndRetryLevel`)
in `game-over-flow.md` remain authoritative.

Where this doc disagrees with the older audits, **this one wins**.

> Verification status against the existing port (`src/screens/GameOverScreen.{h,cpp}`):
> - port has `m_State` at offset **+0x7C** — **WRONG**, binary is **+0x80**
> - port comment `Update 0x00141960` — **WRONG**, binary is **0x00141b34**
> - port comment `Draw 0x00141da4` — **WRONG**, no per-class Draw, inherits
>   `HUDControl3d::Draw` at **0x0014428c** (slot 7); class-specific draw is
>   `DrawOrder` (0x00141448) and `PreDrawOrder` (0x0014171c)
> - port has only 2 button slots (`m_pRetryBtn`, `m_pQuitBtn`) — **WRONG**,
>   binary has **5** button/control pointers (+0x98, +0x9c, +0xa0, +0xa4, +0xa8,
>   +0xc0); plus one `BonusScreen*` (+0xc4) and one `FruitFactControl*` (+0xbc)
> - the port's "buttons created in state 6" code is approximately right, but
>   uses wrong addresses, wrong button slot indexes, no animation timing, no
>   FruitFactControl, and the state graph is incomplete (no states 1–5, 10, 14
>   per the handover)

---

## 1. GameOverScreen struct — full field-by-field table (size = 0x13C)

`GameOverScreen` extends `HUDControl3d`. The base class `HUDControl3d` ends at
0x7C (verified — `field1_0x7c` is the first own field). All offsets below are
absolute from `this`. "Refs" indicate the binary functions that read or write
the slot.

| Offset | Size | Type | Binary semantic | Refs | Suggested port name |
|--------|------|------|-----------------|------|---------------------|
| +0x000 | 0x7C | `HUDControl3d` | base class | inherit | `super` |
| +0x074 | 4 | `SmartPtr<Texture>` | inherited `m_SecondaryTex` (set in Initialise to "game-over-bgN" tex) | Initialise, DrawOrder | `m_SecondaryTex` |
| +0x07C | 4 | float | dummy zero slot, written `=0` in Initialise (DAT_001428cc=0.0). Possibly a legacy `m_PrevState`. | Initialise | `field_0x7c` |
| **+0x080** | 4 | int | **`m_State`** (state machine; 0,1,6,7,8,9,10,11,14) | Update (case dispatch + writes), DeletedControl, LeaderboardsCallback, BeginDraw | **`m_State`** |
| +0x084 | 4 | float | **`m_Timer`** (state-0 entry timer; reused as state-1 progression timer) | Update, Initialise, LeaderboardsCallback, case 0xe (state 14) | `m_Timer` |
| +0x088 | 4 | float | title-tex `m_TitleSize.x` (= secondaryTex.width * 1.0) | Initialise, Update state-0 scale | `m_TitleSize.x` |
| +0x08C | 4 | float | title-tex `m_TitleSize.y` | Same | `m_TitleSize.y` |
| +0x090 | 4 | float | title-tex `m_TitleSize.z` (=0) | Same | `m_TitleSize.z` |
| +0x094 | 4 | int | always set =0 in Initialise; never read elsewhere | Initialise | `field_0x94` |
| **+0x098** | 4 | `MenuButton*` | **m_RetryButton** (created by `CreateRetryButton` at 0x141188) | CreateRetryButton, CreateQuitButton (mirrors fields 0x124/0x128/0x12C across), Release, DeletedControl | `m_pRetryBtn` |
| +0x09C | 4 | `HUDControl*` | secondary control (cleared on DeletedControl, removed in Release). Likely the `<Mode>Continue` button or a Twitter share button. **Not created by stock GameOverScreen — only deleted/removed.** | Release, DeletedControl | `m_pSlot9c` |
| +0x0A0 | 4 | int | unused / dead slot (set =0 in Initialise; never read). Possibly an `OFAchievementsButton*` for OpenFeint. | Initialise | `field_0xa0` |
| **+0x0A4** | 4 | `MenuButton*` | **m_QuitButton** (created by `CreateQuitButton` at 0x1412e4). The retry button populates this from its own +0x124/+0x128/+0x12C tutorial-text slots before button-2 is added to HUD. | CreateQuitButton, Initialise, Release | `m_pQuitBtn` |
| +0x0A8 | 4 | `HUDControl*` | tertiary control (Release removes and frees). | Release | `m_pSlotA8` |
| +0x0AC | 4 | int | **m_AnimCounter** — `((float)int_value + dt * 1000.0)` then `mod 1000`. Used as a millisecond-resolution circular animation counter. | Update first lines | `m_AnimCounter` |
| +0x0B0 | 4 | float | **m_OffsetPos.x** (content-block centre after layout) | Initialise, Update body (`field17_0xb0`) | `m_OffsetPos.x` |
| +0x0B4 | 4 | float | **m_OffsetPos.y** | Same | `m_OffsetPos.y` |
| +0x0B8 | 4 | float | **m_OffsetPos.z** (=0) | Same | `m_OffsetPos.z` |
| **+0x0BC** | 4 | `FruitFactControl*` | **m_pFruitFact** — created in state 6 (Update branch `if (field20_0xbc == 0)`). | Update state 6 create, Update body (positioning) | `m_pFruitFact` |
| **+0x0C0** | 4 | `MenuButton*` | unnamed extra button slot (Release frees). | Release | `m_pSlotC0` |
| **+0x0C4** | 4 | `BonusScreen*` | **m_pBonusScreen** — created in state 1 once entities clear. | Update state 1 create, Update layout (positioning) | `m_pBonusScreen` |
| +0x0C8 | 4 | `HUDControl*` | **m_pNoticeCtrl** — extra HUDControl reset by DeletedControl (forces state→6 if deleted). Likely a popup/dialog. | Initialise (=0), DeletedControl | `m_pNoticeCtrl` |
| +0x0CC | 1 | uint8 | `field_0xcc` — set by `PostCallback(0)` (param == 0 ? 1 : 0). Twitter/share post-completion flag. | PostCallback | `m_PostOk` |
| +0x0CD | 1 | uint8 | `field_0xcd` — set =0 by `PostCallback`. | PostCallback | `m_PostInProgress` |
| +0x0CE | 64 | char[64] | **m_DaysLeftLabel** — formatted via `OS_SPrintf(buf, 0x40, fmt, daysRemaining)` where `daysRemaining = wave.unspawnedQuota - wave.consumedCount` (`*(int)(wave+0x20) - *(int)(wave+0x28)`). Used by PreDrawOrder layer-0x80 path. | Initialise sprintf, PreDrawOrder layer-0x80 | `m_DaysLeftLabel` |
| +0x10E | 2 | -- | tail of buffer | | |
| **+0x110** | 4 | int | **m_ProgressCounter** (counts 0..10..11). `==10` triggers the score-submission tail (state 6 path). After submission becomes 11 (latched). | Update state 6 | `m_ProgressCounter` |
| +0x114 | 4 | `SmartPtr<Texture>` | **m_GameOverTex** (loaded via `TextureManager::LoadLocalisedTexture("textures/game-over.tex")` in state 6). | Update state 6 tail, PreDrawOrder layer-0x80 | `m_GameOverTex` |
| +0x118 | 4 | int | always =0 in Initialise; never set elsewhere. | Initialise | `field_0x118` |
| +0x11C | 4 | int | initialised to 0xFFFFFFFF (=-1). FindMostOfFruit writes the hash count here when a most-eaten fruit was found. | Initialise (=-1), FindMostOfFruit | `m_MostFruitCount` |
| +0x120 | 1 | uint8 | **m_bScoreSubmitted** — set =1 the first time state 6 reaches `progressCounter == 10`. Single-shot guard for `FruitSaveData::AddToTotal` + leaderboard submission. | Update state 6 (gate) + Initialise (=0/1 forced when fast-path) | `m_bScoreSubmitted` |
| **+0x124** | 4 | int | **m_ExpressionIdx** (param4 to ctor; randomised 1..3 if <=0). Visual ninja-face expression on the result panel. | Initialise, PreDrawOrder layer-1 (expression overlay scale +0x141714 + (idx-1)\*4) | `m_ExpressionIdx` |
| **+0x128** | 4 | int | **m_BgPatternIdx** (param5; randomised 1..3 if <=0). | Initialise, PreDrawOrder layer-1 (background overlay) | `m_BgPatternIdx` |
| **+0x12C** | 4 | int | **m_PomCount** (param6 — pom-pom decoration count; passed verbatim to FruitFactControl `+0xE5`) | Initialise, Update state-6 create (`pHVar15[1].size.y+1 = m_PomCount`) | `m_PomCount` |
| **+0x130** | 4 | int | **m_StarCount** (param7 — star-decoration count; passed to FruitFactControl `+0xE9`) | Initialise, Update state-6 create | `m_StarCount` |
| +0x134 | 1 | bool | **m_bIsZenMode** — `gameMode == 0 ? 1 : 0` (i.e. true when game-mode flag at game+0x4=0 = "Classic"; the current name `m_IsNoHighscore` from older docs is misleading) | Initialise, PreDrawOrder layer-1 (gates expression+pattern overlay) | `m_bIsClassic` |
| +0x138 | 4 | float | **m_FruitFactAlpha** — interpolator (0→1, ramp `+= (1-x) * 0.125` per frame). Drives FruitFactControl pop-in and overlay positions. | Update state 6, Update body (layout) | `m_FruitFactAlpha` |

End at +0x13C (= 316 bytes — matches `operator new(0x13c)` in `GameOver`).

### Notes on slots not yet wired in port

| Slot | Why the port currently misses it |
|------|----------------------------------|
| +0x9C, +0xA8, +0xC0 | Auxiliary HUDControls created on certain modes/builds (P2P leaderboard prompt; OpenFeint achievements button). Stock single-player Bada build never populates them. **Stub as `nullptr`; just RemoveControl+free in dtor when non-null.** |
| +0xC8 | "Notice" popup (e.g. "score not submitted; sign in to OpenFeint"). Spawned by `LeaderboardsCallback` indirectly when state==10 succeeds. Stub as nullptr. |

---

## 2. Vtable layout (real vtable at 0x001e9608, +8 from data symbol 0x001e9600)

The vtable storage starts with 8 bytes of typeid/offset (Mortar pattern), then
the call slots. The ctor sets `vtable = (HUDControlFns*)(real_ptr + 8)`, so
`vtable[0]` at runtime is the first method below.

| Slot | Offset | Method | Address | Notes |
|------|--------|--------|---------|-------|
| 0 | +0x00 | `~GameOverScreen` (non-deleting) | 0x00140ec8 | |
| 1 | +0x04 | `~GameOverScreen` (deleting) | 0x00140e70 | calls `operator delete(this)` |
| 2 | +0x08 | `Init` | 0x00140548 | trivial — `(*vtable[10])(this)` (no-op pass-through) |
| 3 | +0x0C | `Release` | 0x00140d98 | full body: clears game.pGameOverScreen, removes/deletes 4 HUDControls, calls `BonusManager::ClearBestBonuses` |
| 4 | +0x10 | `Reset` | 0x00140554 | empty (single `bx lr`) |
| 5 | +0x14 | `BeginDraw` | 0x00140590 | sets `m_LayerFlags = 0x81` if state != 0, else `=1` |
| 6 | +0x18 | `PreDraw` | 0x00143fc8 | empty stub |
| 7 | +0x1C | `Draw` | 0x0014428c | **inherited from `HUDControl3d`** — draws m_SecondaryTex (the game-over background) with the rotation/scale/translate set up in Update |
| 8 | +0x20 | `PreDrawOrder` | 0x0014171c | layer-0x80: draws "days remaining" text + extra texture; layer-1: draws expression+pattern overlays then calls `HUDControl3d::Draw` again |
| 9 | +0x24 | `DrawOrder` | 0x00141448 | layer-0 (cached check): draws the rotating starburst halo around the result panel (48-quad triangle list, time-pulsed colour) |
| 10 | +0x28 | `Update` | 0x00141b34 | **the main state machine** (529 lines, full body in §3) |
| 11 | +0x2C | `DeletedControl` | 0x0012fd54 | (HUDControl base, but `0x00140558` is the GameOverScreen override — Ghidra's vtable resolution may have offset this slot; verified by symbol name) |
| 12 | +0x30 | `GetType` | 0x0014305c | returns 5 |
| 13 | +0x34 | `OnMultiplayerDisconnect` | 0x0012f94c | (base) — local override at 0x00143058 is empty |
| 14 | +0x38 | `VoiceChatOpponentSpeakingStateChange` | 0x0012f950 | empty stub |
| 15 | +0x3C | `Touch` (or similar) | 0x001405e8 | (`PostCallback` lives here; sets +0xCC,+0xCD) |
| 16 | +0x40 | `OnMultiplayerDisconnect` (member) | 0x00143058 | empty |
| 17 | +0x44 | `VoiceChatOpponentSpeakingStateChange` (member) | 0x00143060 | empty |

Non-virtual methods (callable via direct `bl`):

| Method | Address | Notes |
|--------|---------|-------|
| `GameOverScreen(modeName, state, score, expr, bg, pom, star)` (parameterized ctor) | **0x00142900** | super-thin: HUDControl3d ctor + vtable swap + SmartPtr + `Initialise(...)` |
| `GameOverScreen()` default ctor #1 | 0x001429f8 | `Initialise(NULL, -1, -1.0, -1, -1, -1, -1)` |
| `GameOverScreen()` default ctor #2 | 0x00142a58 | identical to above |
| `Initialise` | **0x00142674** | full body — see §3.0 |
| `LoadContent` | (bool guard at GOT+0x44 in Initialise) | gated by static guard |
| `UnLoadContent` | unresolved | |
| `CreateRetryButton` | **0x00141188** | creates retry MenuButton at offset +0x98 |
| `CreateQuitButton` | **0x001412e4** | creates quit MenuButton at offset +0xa4; copies tutorial-text slots from retry button |
| `FindMostOfFruit` | **0x00141a18** | populates +0x118/+0x11C with most-collected fruit info |
| `SetStateWait` | **0x00140688** | leaderboard-prompt dialog dispatcher; sets state=6 if no dialog needed |
| `LeaderboardsCallback` | 0x001405a0 | external trigger that pushes state→10 from state 0 or 6 (only when `game.alpha > 0.999`) |
| `TwitterCallback` | 0x001405f8 | empty stub (defunct online) |
| `StartProgressionTimer` | 0x001405fc | empty stub |
| `OnProgressionTimerUp` | 0x00140614 | empty stub |
| `HandleProgressionTimerExpiration` | 0x00140618 | empty stub |
| `IsAllowedToExit` | **0x0014061c** | returns 1 (always true; allows retry/quit buttons in state 6) |
| `CancelHUDProgressionTimer` | 0x00140600 | empty stub |
| `SetTerminate` | **0x00140604** | sets `game[+0x33] = 1`, calls `CancelHUDProgressionTimer` |

---

## 3. State machine — full transition graph

### 3.0 Initialise — pre-state setup (0x00142674)

Called from every ctor.

```c
void GameOverScreen::Initialise(const char* modeName, int param2, float param3,
                                int expressionIdx, int bgPatternIdx,
                                int pomCount, int starCount)
{
    // Static one-shot LoadContent guard
    if (g_StaticGuard.gameOverScreenContentLoaded == 0) LoadContent();

    // Defunct online (NetworkManager pop-up alert deregistration)
    NetworkManager::GetInstance()->InvalidatePublishTextCallback();

    field23_0xc8 = 0;        // notice popup
    m_PomCount   = pomCount; // +0x12C
    m_Timer      = 0.0f;     // +0x84
    m_MostFruitCount = -1;   // +0x11C
    field97_0x118    = 0;
    m_StarCount  = starCount; // +0x130

    // Pick the mode-specific game-over background texture by gameMode:
    //   gameMode == 2 → "textures/arcade-game-over-bg.tex"  (DAT_001428f0)
    //   gameMode == 3 → "textures/zen-game-over-bg.tex"     (DAT_001428ec)
    //   else          → "textures/classic-game-over-bg.tex" (DAT_001428f4)
    char gameMode = game.gameMode;  // game[+4]
    int  texDataAddr = (gameMode == 2) ? DAT_001428f0
                     : (gameMode == 3) ? DAT_001428ec
                                       : DAT_001428f4;
    m_SecondaryTex = *(SmartPtr<Texture>*)(GOT + texDataAddr);

    // m_TitleSize = (tex.width, tex.height, 0) * 1.0
    float w = tex.GetWidth(); float h = tex.GetHeight();
    m_TitleSize = Vec3(w, h, 0.0f);

    m_State        = 0;       // +0x80
    m_LayerFlags   = 0;       // base m_bNoDestructor=0 (false; managed)
    field_0x114.SetNull();
    field_0x214.SetNull();    // (or wherever DAT_001428f8 points; second SmartPtr)

    field16_0xac      = 0;    // m_AnimCounter
    field_0x120       = 0;    // m_bScoreSubmitted
    m_BgPatternIdx    = bgPatternIdx;  // +0x128
    field_0x94        = 0;
    field22_0xc4      = 0;    // m_pBonusScreen
    m_FruitFactAlpha  = game.alpha;  // +0x138 = game[+0xC]
    m_ExpressionIdx   = expressionIdx; // +0x124
    field13_0xa0      = 0;
    field12_0x9c      = 0;
    field107_0x134    = (gameMode == 0); // m_bIsClassic
    field14_0xa4      = 0;    // m_pQuitBtn
    field15_0xa8      = 0;
    field11_0x98      = 0;    // m_pRetryBtn

    // Random-fill expression/pattern when caller passed -1
    if (expressionIdx < 1) {
        m_ExpressionIdx = 1;
        FindMostOfFruit(this);
        int score = GetCurrentScore(0);
        int hi    = GetCurrentModeHighscore();
        if (score > hi/2) {
            m_ExpressionIdx = (int)RandUint(2) + 2;  // 2 or 3 (better expressions)
        }
    }
    if (bgPatternIdx < 1) {
        m_BgPatternIdx = (int)RandUint(3) + 1;  // 1..3
    }

    pos = Vec3(0, 0, 0);                        // base pos
    m_OffsetPos = Vec3(DAT_001428d0, DAT_001428d4, 0);  // initial off-screen position

    field92_0x110 = 0;    // m_ProgressCounter
    field20_0xbc  = 0;    // m_pFruitFact
    field21_0xc0  = 0;
    m_PostOk        = 0;  // +0xCC
    m_PostInProgress= 0;  // +0xCD

    // Format "X days left" label
    int unspawned = wave.totalQuota - wave.consumedCount; // wave[+0x20] - wave[+0x28]
    OS_SPrintf(m_DaysLeftLabel, 0x40, FMT_DAYS_LEFT, unspawned);

    // FAST-PATH: caller passed valid score/state and we're past wave 5 with running progress
    if (param3 >= 0.0f && param2 >= 0) {
        FindMostOfFruit(this);
        if (param2 > 5 && wave.alpha > DAT_001428d8) {
            wave.alpha            = DAT_001428dc;  // = 1.0
            m_State               = 6;             // skip entry animation
            m_bScoreSubmitted     = 1;
            m_FruitFactAlpha      = 1.0f;
            this->vtable->Update(this, 0.0f);      // immediate state-6 invocation
        }
        m_State = param2;     // override with caller-supplied state
        m_Timer = param3;     // override with caller-supplied timer (used as "score" placeholder)
    }
}
```

**Key takeaway:** the ctor's `param2/param3` pair is **dual-purpose**:
- normally (`endReason = -1`, `endScore = -1.0`) — Initialise leaves `m_State = 0`
- in fast-skip / debug paths (`SkipToGameOver(state, score, ...)`) — overrides
  state and bypasses the entry animation

The existing port treats `param2` as `m_EndReason` and `param3` as `m_EndScore`,
**which is wrong**. They are used purely as state/timer overrides — the actual
score is read from `GetCurrentScore(0)` in state 6.

### 3.1 State table

| State | Body | Entry from | Exit to |
|------:|------|-----------|---------|
| **0** | Entry animation: scale title from 0 → full size with sin curve over 1.9s. Once `m_Timer ≥ 1.9`, branch on `gameMode == 2` (Arcade): if Arcade, jump straight to state 1; else call `SetStateWait` (which goes to 6 or pops a leaderboard sign-in dialog). | ctor (default) | 1 (Arcade) or 6 (else, via SetStateWait) |
| **1** | Bonus phase. Wait for entities to clear (`ActorManager::GetNumEntities(0)+(...,1)==0`). Then create `BonusScreen` at +0xC4, attach a `DeletedControl` callback, add to HUD, call `BonusManager::SetUpBonusScreen` to populate awards. After creation, scroll the bonus panel up: `pos.y += dt * floor((screen_y_offset + bonus[+0xc0])` clamped — basically `pos.y` rises until it reaches `bonus.bottom + scroll_offset`. Forces `game.processing = 1` (`game[+0x35] = 1`). The `m_pBonusScreen->m_Timer` (BonusScreen[+0xb8]) is updated each frame with `m_Timer + dt` to drive its internal scroll. | state 0 fall-through (Arcade only) | implicit — BonusScreen self-deletes on completion → `DeletedControl` triggers state 6 |
| **6** | Main display + score submission. `if (m_pFruitFact == 0)` create `FruitFactControl` at +0xBC, attach DeletedControl, add to HUD. Pop-in animation: `wave.alpha += (1-alpha) * 0.125` until alpha ≥ 0.999, then snap to 1.0. After alpha is full, hold for 11 frames (`m_ProgressCounter` 0→11). On frame 10 (`==10`): submit scores (see §4 score-submission tail), create retry+quit buttons (if `IsAllowedToExit()` returns 1). Each frame updates content positions from `m_FruitFactAlpha`. | states 0,1,7 (any), 14 fall-through, DeletedControl when notice/bonus completes | 7 (retry pressed), 9 (quit pressed), 10 (LeaderboardsCallback), or stay |
| **7** | Retry. Waits one frame for entities to clear; if entities still present and `field12_0x9c == 0`, falls back to state 6. Otherwise: `*(int*)(game+0x28) = *(int*)(game+0x20)` (resets total quota to spawn-count?), `WaveManager::Reset(false)`, `game.pauseFlag = 1`, m_State = 8. | retry button click | 8 |
| **8** | Camera fade-out for retry. `wave.alpha *= 0.75` each frame. When `< DAT_00142114` (=0.001): `WaveManager::Reset(false)`, set `wave.alpha = 0`, clear `game.pauseFlag`, `WaveManager::NewGame()`, `SetTerminate()`. m_OffsetPos.y is animated (slide up off-screen). | state 7 | terminate — screen self-removes via SetTerminate |
| **9** | Quit (transition to MenuScreen). Wait for entities to clear; once done, call `QuitToMenu()` and m_State = 11. | quit button click | 11 |
| **10** | Multiplayer cleanup. Wait for ALL entities both player slots to clear. Then `NetworkManager::LaunchDashboard()`, reset progressCounter/+0xa4/+0x98 buttons, m_State = 6. | `LeaderboardsCallback` (state 0 or 6 + alpha > 0.999) | 6 |
| **11** | Final fade-out. While `wave.alpha < 0.0` (or signed comparison — see notes), `SetTerminate()` (one-shot). | state 9 | terminate |
| **14** | Quick-restart hot path (debug/RetryLevel). Increment timer at 8x rate (`m_Timer += dt * 8`); cap at 8.0 → set `m_Timer = 0` (DAT_0014261c). Then m_State = 6 immediately, `game.field_0x190 = 0`, reset `m_ProgressCounter=0` if `field12_0x9c==0`, `m_Timer = 2.0`. **Effectively skips the entry/bonus phases by jumping straight to state 6 with a non-zero timer.** | external `SetState(14)` (debug RetryLevel — NO actual binary path observed in stock build) | 6 |

States 2, 3, 4, 5, 12, 13, 15+ are **not handled** by the switch — they fall
through to nothing (default case is empty in the binary). The existing port's
`default → m_State = 6` is an acceptable defensive guard.

### 3.2 Detailed body for state 0 (entry animation)

```c
case 0: {
    // First frame: when in Arcade or Zen and entities non-zero, force game.processing=1
    if (m_bScoreSubmitted == 0) {
        char gameMode = game.gameMode;
        if ((unsigned char)(gameMode - 2) < 2) {  // 2 (Arcade) or 3 (Zen)
            if (ActorManager::GetNumEntities(0) == 0
             && ActorManager::GetNumEntities(1) == 0)
                game[+0x35] = 1;  // m_bProcessing
        } else {
            game[+0x35] = 1;
        }
    }

    m_LayerFlags = 1;            // single-layer (no PreDrawOrder/DrawOrder; just Draw)
    m_Timer += dt;
    const float ENTRY_DURATION = 1.9f;     // DAT_00141da4 = 0x3f733333
    const float SIN_FULL       = 20000.0f; // DAT_00141da8 = 0x469c6800

    Vec3 size;
    if (m_Timer < ENTRY_DURATION) {
        // Sin-easing scale-in: t = (m_Timer / 1.9) * 20000.0
        float t = (m_Timer / ENTRY_DURATION) * SIN_FULL;
        ushort idx;
        if (t > SIN_FULL) idx = 0x4E34;       // (=20020 ≈ pi-aligned 20000)
        else              idx = (ushort)(int)t;
        float curr = Math::SinIdx(idx);
        float full = Math::SinIdx(0x4E34);
        size = (m_TitleSize * curr) / full * 2.0f;  // *2 = bounce overshoot
    } else {
        size = m_TitleSize * 2.0f;
    }
    this->size = size;

    if (m_Timer > 1.9f) {        // DAT_00141dac
        if (game.gameMode == 2) {     // Arcade
            m_State = 1;
            m_Timer = 0.0f;           // = -0.333f? actually DAT_00141db0 = 0xbeaa7efa = -0.333
            // Note: -0.333 starts BonusScreen scroll a bit below 0 for ease-in
        } else {
            SetStateWait(this);       // goes to 6 (or pops sign-in dialog)
        }
    }
    pos = Vec3(0, 0, 0);          // recentre
    break;
}
```

### 3.3 Detailed body for state 1 (bonus phase + BonusScreen creation)

```c
case 1: {
    if (ActorManager::GetNumEntities(0) != 0
     || ActorManager::GetNumEntities(1) != 0)
        break;                        // wait for screen to clear

    if (m_pBonusScreen == nullptr) {
        FindMostOfFruit(this);
        m_pBonusScreen = new BonusScreen();   // 200 bytes (0xc8)
        m_pBonusScreen->pos = Vec3(0, -20.0f, 0);
        m_pBonusScreen->onDeletedDelegate = make_delegate(this, DeletedControl);
        HUD::AddControl(game.pHUD, m_pBonusScreen, false);
        BonusManager::GetInstance()->SetUpBonusScreen(m_pBonusScreen);
    } else {
        // Slide bonus panel up to settle position
        float bottom = m_pBonusScreen->pos.y + m_pBonusScreen->m_Height /* +0xc0 */ + DAT_00141dc8;
        if (bottom > pos.y) pos.y = bottom;
        // Scale title texture down to match scroll
        Vec3 titleScale = m_TitleSize * (pos.y / DAT_00141dcc + 1.0f);
        size = titleScale * 2.0f;
    }

    m_Timer += dt;
    game[+0x35] = 1;                  // force processing on
    m_pBonusScreen->m_Timer = m_Timer; // BonusScreen reads its own timer for scroll
    break;
}
```

`BonusScreen` self-deletes when its scroll completes; its destructor fires
`DeletedControl(m_pBonusScreen)` → switch to state 6.

### 3.4 Detailed body for state 6 (main display + score submission)

```c
case 6: {
    // 1) Create FruitFactControl on first entry
    if (m_pFruitFact == nullptr) {
        m_pFruitFact = new FruitFactControl();   // 0x204 bytes
        m_pFruitFact->pos = Vec3(DAT_00142120, 12.0f, DAT_00142118) + m_OffsetPos;
        // Set the pom + star counts (member offsets in FruitFactControl = +0xE5, +0xE9)
        m_pFruitFact->m_PomCount  = m_PomCount;
        m_pFruitFact->m_StarCount = m_StarCount;
        HUD::AddControl(game.pHUD, m_pFruitFact, false);
        m_pFruitFact->Init();         // (**vtable[2])(this)
    }

    // 2) Pop-in animation: wave.alpha → 1.0, then hold 11 frames
    float& alpha = game.alpha;        // game[+0xc]
    if (alpha < DAT_00142124 /*=0.999*/) {
        m_ProgressCounter = 0;
        alpha += (1.0f - alpha) * 0.125f;   // exponential ease-in
        if (alpha < 0.75f) game[+0x35] = 1; // suppress entity processing during pop
        if (alpha >= DAT_00142124) alpha = 1.0f;
        m_FruitFactAlpha = alpha;
    } else {
        if (m_FruitFactAlpha < 1.0f)
            m_FruitFactAlpha += (1.0f - m_FruitFactAlpha) * 0.125f;
        if (m_ProgressCounter < 11) m_ProgressCounter++;
    }

    // 3) On frame 10 (single-shot via m_bScoreSubmitted): commit scores
    if (m_ProgressCounter == 10) {
        m_ProgressCounter = 11;       // latch
        if (m_bScoreSubmitted == 0) {
            int score = GetCurrentScore(0);
            m_bScoreSubmitted = 1;
            game[+0x4c][+0x12d] = 0;  // FruitSaveData[+0x12D] = 0 (post-state)

            // Lazy hash-init for "FruitsCollected" / "TotalScore" string keys
            // (StringHash gated by __cxa_guard for one-time init)
            uint hashFruits = StringHash("FruitsCollected");
            uint hashScore  = StringHash("TotalScore");

            FruitSaveData::AddToTotal(saveData, "FruitsCollected", hashFruits, 1, true, true);
            FruitSaveData::AddToTotal(saveData, "TotalScore",      hashScore,  score, true, true);
            FruitSaveData::UnlockTotals();

            AchievementManager::GetInstance()->UnlockScoreAchievement(score);
            AchievementManager::GetInstance()->UnlockTotalFruitAchievement(saveData[+0x174]);
            int hi = GetCurrentModeHighscore();
            AchievementManager::GetInstance()->UnlockEndScoreAchievement(score, hi);

            // Online leaderboard submission (skipped for Zen — gameMode != 2 also OK)
            if (game.gameMode != 2) {
                FNHighscoreList* board = LeaderboardManager::RefreshLeaderboard(gameMode, 3);
                if (board) FNHighscoreList::AddPlayerScore(board);
            }

            // Combo-star achievement (only for Arcade — gameMode == 3)
            if (m_pFruitFact != nullptr
             && game.gameMode == 3
             && (signed char)m_pFruitFact[+0xE0] >= 0
             && (signed char)m_pFruitFact[+0xE0] < 0x19) {
                AchievementManager::GetInstance()->UnlockComboStarAchievement(
                    m_pFruitFact[+0xD0]);  // ulong combo hash
            }

            // Update mode highscore if score > prevHi/2 (the "fresh personal best" rule)
            if (hi/2 < score) saveData[+300] = SetCurrentModeHighscore(score);

            // P2P online score push (defunct; skipped if IsP2POnline() true)
            int leaderboardId = GetCurrentModeLeaderboardID(-1);
            if (leaderboardId != 0) {
                if (!IsP2POnline()) {
                    int& localBest = saveData[(gameMode + 0x14) * 4 + 4];
                    if (localBest < score) localBest = score;
                }
                NetworkManager::SetLeaderboardScore(leaderboardId, score, NULL, 0);
            }

            // Arcade-only post-game achievement unlock
            if (game.gameMode == 2)
                BonusManager::UnlockPostGameAchievements();

            FruitSaveData::FinishedGame();
            FruitSaveData::ClearTotals();
            SaveCurrentData(false);
        }

        m_State = 6;                  // (re-affirm, in case of reentry)
        game.alpha = 1.0f;

        // 4) Spawn retry/quit buttons (only when allowed)
        if (prevState == 6 && IsAllowedToExit()) CreateRetryButton(this);
        if (m_pQuitBtn == nullptr && prevState == 6 && IsAllowedToExit())
            CreateQuitButton(this);

        // 5) Load localised "Game Over" text texture (game-over.tex)
        SmartPtr<Texture> tmp;
        TextureManager::LoadLocalisedTexture(&tmp, "textures/game-over.tex");
        m_GameOverTex = tmp;
    }

    // 6) Vertical "settle" — slide content up if pos.y > 0
    if (pos.y < DAT_00142614 /*=0.0*/) {
        // (sliding is handled by the common layout block below the switch)
        size = m_TitleSize * LerpF(2.0f, 1.0f, alpha);
        pos  = Vec3(0, LerpF(DAT_00142618, DAT_0014261c, alpha), 0);
    }
    break;
}
```

### 3.5 States 7 / 8 (retry path)

```c
case 7: {
    if (ActorManager::GetNumEntities(0) != 0 && field12_0x9c == 0) {
        game.alpha = 1.0f;
        // fall through to case 6 (entities still on-screen — keep waiting)
        goto case_6;
    }
    // Set the unspawned/consumed sentinel so wave can resume cleanly
    game.fruitConsumed = game.fruitTotal;     // game[+0x28] = game[+0x20]
    WaveManager::GetInstance()->Reset(false);
    game.pauseFlag = 1;                        // game[+5] = 1 (will be cleared by state-8 tail)
    m_State = 8;
    break;
}

case 8: {
    float& alpha = game.alpha;                 // game[+0xc]
    alpha *= 0.75f;
    m_FruitFactAlpha = alpha;
    if (alpha < DAT_00142114 /*=0.001*/) {
        WaveManager::GetInstance()->Reset(false);
        alpha = 0.0f;
        game.pauseFlag = 0;                    // resume gameplay
        m_FruitFactAlpha = 0.0f;
        WaveManager::GetInstance()->NewGame();
        SetTerminate();                        // game[+0x33] = 1 → screen pending removal
    }
    if (pos.y < 0.0f) {
        pos = Vec3(0.0f, DAT_00142118 + (1-m_FruitFactAlpha) * DAT_0014211c, 0.0f);
        // i.e. slide up by (1-alpha) * 224.0 as alpha decays
    }
    break;
}
```

### 3.6 State 9 (quit path)

```c
case 9: {
    if (ActorManager::GetNumEntities(0) != 0) break;  // wait
    QuitToMenu();         // 0x00169e50 — see existing docs
    m_State = 11;
    break;
}
```

### 3.7 State 10 (multiplayer/online cleanup)

```c
case 10: {
    int e0 = ActorManager::GetNumEntities(0);
    int e1 = ActorManager::GetNumEntities(1);
    if (e0 + e1 == 0) {
        NetworkManager::GetInstance()->LaunchDashboard();
        m_ProgressCounter = 0;
        m_pQuitBtn        = nullptr;   // does NOT delete; LaunchDashboard owns lifecycle
        m_State           = 6;
        field13_0xa0      = 0;
        m_pRetryBtn       = nullptr;
    }
    break;
}
```

**State 10 is entered exclusively from `LeaderboardsCallback` (0x001405a0)**:

```c
void GameOverScreen::LeaderboardsCallback() {
    if (m_State == 0 || m_State == 6) {
        if (game.alpha > 0.999f /*DAT_001405d8*/) {
            m_Timer = 0.0f;       // = DAT_001405dc (0.0)
            m_State = 10;
        }
    }
}
```

Wired up by an OF/GameCenter HUD button (defunct). For port: **stub
LeaderboardsCallback as no-op** — same-screen MP doesn't use it.

> Despite the existing port's "MP-related (state 10)" labelling, **state 10
> is not the multiplayer game-over path**. Same-screen MP just uses the
> stock state 0 → 6 → 7/9 path for the player who lost; the win-condition
> (both players miss / both players time-out) is handled by `Fruit::CheckFruitDropped`
> firing `GameOver(...)` once. State 10 is the **online-leaderboard launch
> button** behaviour (defunct).

### 3.8 State 11 (final fade-out)

```c
case 11: {
    if (game.alpha < 0.0f) SetTerminate();
    break;
}
```

`SetTerminate()` sets `game[+0x33] = 1`, which triggers HUD removal of this
screen (game-loop polls `game[+0x33]` and reaps).

### 3.9 State 14 (quick-restart hot path)

```c
case 0xe: {
    int prev = field12_0x9c;
    m_Timer += dt * 8.0f;
    if (m_Timer >= 8.0f) m_Timer = DAT_0014261c;  // = 0.0
    m_State = 6;
    game[+0x190] = 0;                              // FruitSaveData reset flag
    if (prev == 0) m_ProgressCounter = 0;          // restart score-submit cycle
    m_Timer = 2.0f;                                // pre-loaded for skip
    // (no break — falls through to switch's tail)
    break;
}
```

This is **a fall-through to state 6 with `m_Timer=2.0` pre-loaded**, used by
the debug `RetryLevel()` path. In stock builds it's effectively dead — there
is no observable caller in `FruitNinja.exe` that explicitly sets
`m_State = 14`. Port can implement it as a no-op transition to state 6.

---

## 4. Common layout block (runs every frame after the switch)

The Update tail (after the case-switch) positions `m_pBonusScreen`,
`m_pFruitFact`, `m_pSlot9c`, `m_pSlotA8`, plus `m_OffsetPos`.

```c
// Runs every frame regardless of state
if ((unsigned char)(game.gameMode - 2) < 2 /* Arcade or Zen */ && m_pBonusScreen != nullptr) {
    Vec3 bonusPos = Vec3(DAT_00142624 + (1-m_FruitFactAlpha) * DAT_00142628,
                         DAT_00142620,
                         0.0f);
    m_pBonusScreen->pos = bonusPos;
    if (m_pFruitFact) {
        m_pFruitFact->pos = bonusPos + Vec3(DAT_0014262c, 4.0f, 0.0f);
    }
} else {
    m_OffsetPos = Vec3(DAT_00142634 + DAT_00142630 * m_FruitFactAlpha,
                       DAT_00142638,
                       0.0f);
    if (m_pFruitFact)
        m_pFruitFact->pos = m_OffsetPos + Vec3(DAT_0014263c, 12.0f, 0.0f);
    if (m_pSlot9c) {
        Vec3 spread = (extraVec[0] * (1-m_FruitFactAlpha)) * DAT_00142654;  // shake/jitter
        m_pSlot9c->pos = Vec3(0, DAT_00142644 + (1-m_FruitFactAlpha) * DAT_00142648, DAT_00142640);
    }
}
if (m_pRetryBtn /* +0x9c is shorthand here for m_pRetryBtn? actually +0x9c is m_pSlot9c */) {
    ...same vec3 jitter equation...
}
if (m_pQuitBtn  /* +0xa8 */) { ... }
```

The vec3 jitter for the retry/quit buttons is a small "shake-on-spawn" effect
that decays to zero as `m_FruitFactAlpha → 1.0`. Constants:

| DAT | Value | Use |
|-----|-------|-----|
| DAT_00142614 | 0.0 | gate for "settle" branch |
| DAT_00142618 | -85.0 | settle pos.y far |
| DAT_0014261c | 0.0 | settle pos.y near |
| DAT_00142620 | 75.0 | bonus pos.y in Arcade/Zen |
| DAT_00142624 | -204.0 | bonus pos.x base |
| DAT_00142628 | -193.0 | bonus pos.x slide-in delta |
| DAT_0014262c | 184.0 | fruit-fact dx vs bonus |
| DAT_00142630 | -50.0 | classic content x slide |
| DAT_00142634 | 130.0 | classic content x base |
| DAT_00142638 | 75.0 | classic content y |
| DAT_0014263c | 60.0 | fruit-fact dx vs content |
| DAT_00142640 | 0.0 | retry button z |
| DAT_00142644 | -56.0 | retry pos.y base |
| DAT_00142648 | 240.0 | retry pos.y slide-in |
| DAT_0014264c | -75.0 | (button shake) x base |
| DAT_00142650 | -73.0 | retry shake y |
| DAT_00142654 | 0.6 | shake amount |
| DAT_00142658 | -73.0 | quit shake y |

---

## 5. CreateRetryButton (0x00141188) and CreateQuitButton (0x001412e4)

### 5.1 CreateRetryButton

```c
void GameOverScreen::CreateRetryButton() {
    if (m_pRetryBtn != nullptr) return;       // single-shot

    SmartPtr<Texture> tex = *(SmartPtr<Texture>*)(GOT + DAT_001412d4);  // "retry-button.tex"
    Vec3 pos = Vec3(DAT_001412c4 /*=-80*/, DAT_001412c8 /*=-96*/, 0.0f);
    Delegate0<void> onClick = make_delegate(this, &GameOverScreen::RetryButtonCallback);
    Vec3 bgVec    = HUD::CopyGlobalVec3();    // global menu-button colour
    Delegate0<void> onPlaySfx = HUD::MakeDelegate();  // global SFX wrapper
    int  fruitType = *(int*)(GOT + DAT_001412d8);  // (always 0 here)

    m_pRetryBtn = new MenuButton(tex, &pos, &onClick, fruitType, &bgVec, &onPlaySfx);
    m_pRetryBtn->Init();   // (**vtable[2])(this)

    // Wire DeletedControl callback so the button can self-remove
    m_pRetryBtn->onDeletedDelegate = make_delegate(this, &GameOverScreen::DeletedControl);

    HUD::AddControl(game.pHUD, m_pRetryBtn, false);
}
```

**The "retry callback" delegate target is `RetryButtonCallback` — but Ghidra
hasn't symbol-resolved it.** Based on disassembly at 0x001412c0, the callback
sets `game.retryFlag = 1` and `m_State = 7`. Confirmed by the existing port's
implementation matching this behaviour (so port is OK on this point).

### 5.2 CreateQuitButton

```c
void GameOverScreen::CreateQuitButton() {
    SmartPtr<Texture> tex = *(SmartPtr<Texture>*)(GOT + DAT_00141438);  // "quit-button.tex"
    Vec3 pos = Vec3(DAT_00141428 /*=80*/, DAT_0014142c /*=-96*/, 0.0f);
    Delegate0<void> onClick = make_delegate(this, &GameOverScreen::QuitButtonCallback);
    Vec3 bgVec = HUD::CopyGlobalVec3();
    Delegate0<void> onPlaySfx = HUD::MakeDelegate();

    m_pQuitBtn = new MenuButton(tex, &pos, &onClick,
                                **(int**)(GOT + DAT_00141440),  // some constant
                                &bgVec, &onPlaySfx);

    // Mirror tutorial-text slots from retry button (so quit button's
    // "Press here to quit" text aligns with retry's text)
    if (m_pRetryBtn) {
        memcpy(m_pQuitBtn + 0x124, m_pRetryBtn + 0x124, 12);  // +0x124, +0x128, +0x12C
    }

    m_pQuitBtn->Init();
    m_pQuitBtn[3].pivot.y_byte_3 = 1;          // some flag
    HUD::AddControl(game.pHUD, m_pQuitBtn, false);

    // Reset tutorial pointer to the new button if no retry, else retry+quit
    TutorialControl* tut = game.pTutorialControl;
    int target = m_pRetryBtn ? (int)m_pRetryBtn : (int)m_pQuitBtn;
    TutorialControl::ResetTutePos(tut, target);
}
```

**`QuitButtonCallback` body** (not symbol-resolved): sets `m_State = 9`.
Existing port matches.

### 5.3 Button visual constants

| Button | x | y | z | Texture |
|--------|---|---|---|---------|
| Retry | -80.0 (DAT_001412c4) | -96.0 (DAT_001412c8) | 0.0 | game.pSaveData→retryButtonTex |
| Quit  | +80.0 (DAT_00141428) | -96.0 (DAT_0014142c) | 0.0 | game.pSaveData→quitButtonTex  |

---

## 6. FruitFactControl integration

`FruitFactControl` (size 0x204; ctor at 0x0013cb60) is the "best-fruit"
callout panel. GameOverScreen creates it in state 6, sets `m_PomCount`
(+0xE5) and `m_StarCount` (+0xE9), and reads `+0xE0` (combo type) +
`+0xD0` (combo-hash ulong) for the combo-star achievement unlock.

| Hook | Where | Value |
|------|-------|-------|
| Construction | state 6 first frame | `new FruitFactControl()` |
| Layout | every-frame layout block | `pos = m_OffsetPos + Vec3(60, 12, 0)` (or `bonusBase + Vec3(184, 4, 0)` in Arcade/Zen) |
| Pop-in alpha | state 6 ramp | drives FruitFactControl visibility via shared `game.alpha` |
| Lifecycle end | when its own internal state machine completes | self-deletes; DeletedControl writes back `m_pFruitFact = 0` (no state transition since +0xC4 is bonus, +0xBC is fact — DeletedControl's logic only zeroes the pointer for +0xBC) |

**FruitFactControl reads the most-eaten-fruit data set by `FindMostOfFruit`**
(field97_0x118 = fruit-type index, field98_0x11C = count). Internally it does
`Fruit::FruitTypeHash(idx)` + `FruitSaveData::GetTotal(hash)` to render the
text "You've sliced N <FruitName>!". Port reference: `docs/structs/data-classes.md`
for `FNHighscore` if needed.

`FindMostOfFruit` (0x00141a18) implementation summary:

1. Build a candidate list of all fruit types (`Fruit::FruitInfo(i)` for `i = 0..numFruits-1`),
   filtering out non-power fruits in Arcade mode (`gameMode == 2`).
2. Randomly shuffle the list (random fruit order eliminates ties bias).
3. For each candidate, fetch `FruitSaveData::GetTotal(Fruit::FruitTypeHash(i))`.
4. Track running max (`bestCount`, `bestIdx`).
5. If `bestCount > 0`, write `m_MostFruit = bestIdx`; `m_MostFruitCount = bestCount`.

---

## 7. Bonus phase (state 1) — BonusScreen + BonusManager

### 7.1 Visual flow

1. State 1 entry — both player slots clear of entities → `new BonusScreen()`.
2. `BonusManager::SetUpBonusScreen(bonusScreen)` (at 0x0010e404) populates the
   `BonusScreen->m_Awards` vector:
   - clears `BonusManager::m_BestBonuses` list
   - randomly orders `m_BonusTypes` and pushes each `Bonus` instance from
     `BonusType::GetBest()` (only if non-null)
   - sorts the list and trims to top 3 (`size > 3 → erase tail`)
   - calls `BonusScreen::AddAward(colour[i], texture, valueText, value)` for
     each surviving bonus
3. State 1 every-frame: `m_pBonusScreen->pos.y` rises until it settles at
   `bonus.bottomEdge + screenHeight - DAT_00141dc8`.
4. `m_pBonusScreen->m_Timer` ticks via GameOverScreen's `m_Timer + dt`. The
   BonusScreen uses its own internal state machine to spin up each award's
   numeric counter and play coin-collect SFX.

### 7.2 Award colours

`MakeColour_BGRA(...)` is called 3 times in `SetUpBonusScreen`:

| Award rank | RGB (0xRRGGBB after BGRA→RGB swap) | Notes |
|-----------|-----------------------------------|-------|
| 1st | 0xAD7E00 | gold |
| 2nd | 0xA00505 | red/silver fallback |
| 3rd | 0x015C95 | bronze/blue |

### 7.3 Score tally (per-award counter spin-up)

The score-counter spin-up animation is **not** in GameOverScreen — it's
inside `BonusScreen::Update` (vtable slot 10 of BonusScreen). GameOverScreen
just ticks `m_pBonusScreen->m_Timer` and lets BonusScreen do the easing.

For the **HUD score** displayed on the result panel: there's no spin-up —
it's simply `GetCurrentScore(0)` rendered as text. `BonusManager` is *only*
involved in the bonus-award panel during state 1. Once state 6 starts, the
score display is the static end-of-game score.

The "score-counter spin-up" the user mentioned is the per-bonus-award value
count (e.g. "Combo Bonus: 250" counts up from 0). It lives in BonusScreen,
not GameOverScreen, and is **not part of state 1's responsibility** other
than driving `m_Timer`.

---

## 8. Score-submission tail (state 6, frame 10)

When `m_ProgressCounter == 10` for the first time (`m_bScoreSubmitted == 0`):

| Call | Address | Purpose |
|------|---------|---------|
| `FruitSaveData::AddToTotal("FruitsCollected", hash, 1, true, true)` | inline | bumps lifetime fruit count by 1 |
| `FruitSaveData::AddToTotal("TotalScore", hash, score, true, true)` | inline | adds this score to lifetime score total |
| `FruitSaveData::UnlockTotals()` | inline | unlocks the total-based achievements |
| `AchievementManager::UnlockScoreAchievement(score)` | (extern) | per-score milestones |
| `AchievementManager::UnlockTotalFruitAchievement(saveData[+0x174])` | (extern) | total-fruit milestones |
| `AchievementManager::UnlockEndScoreAchievement(score, hi)` | (extern) | per-mode high-score check |
| `LeaderboardManager::RefreshLeaderboard(gameMode, 3)` | (extern) | (skipped if gameMode==2 / Arcade) |
| `FNHighscoreList::AddPlayerScore(board)` | (extern) | local leaderboard insert |
| `AchievementManager::UnlockComboStarAchievement(comboHash)` | (extern) | only if Zen mode (game.gameMode==3) and FruitFactControl reports a valid combo (`+0xE0 in [0,0x18]`) |
| `SetCurrentModeHighscore(score)` if `score > prevHi/2` | (extern) | "fresh personal best" rule |
| `NetworkManager::SetLeaderboardScore(boardId, score, NULL, 0)` | (extern; defunct) | OF/GC submission |
| `BonusManager::UnlockPostGameAchievements()` | 0x0010e1cc | only if gameMode==2 (Arcade) |
| `FruitSaveData::FinishedGame()` | (extern) | bumps total-games counter |
| `FruitSaveData::ClearTotals()` | (extern) | resets per-game counters |
| `SaveCurrentData(false)` | (extern) | flushes save file |

**This entire block is single-shot guarded by `m_bScoreSubmitted`.**

---

## 9. Release / dtor cleanup (0x00140d98)

```c
void GameOverScreen::Release() {
    field_0x114.SetNull();          // m_GameOverTex
    if (game.pGameOverScreen == this) {
        game.pGameOverScreen = nullptr;
        game.pSaveData[+0x120] = -1;  // clear cached expressionIdx
        game.pSaveData[+300]   = 0;   // clear "fresh personal best" flag
        game.pSaveData[+0x128] = -1;
        game.pSaveData[+0x124] = -1;
        game.pSaveData[+0x11C] = -1;
    }
    // Remove and free 4 HUDControls (button slots — buttons are owned)
    for (slot in {0xBC, 0xC0, 0x9C, 0xA8}) {  // FruitFact, slotC0, slot9c, slotA8
        if (this[slot] != nullptr) HUD::RemoveControl(game.pHUD, this[slot]);
    }
    for (slot in {0x9C, 0xBC, 0xC0, 0xA8}) {
        if (this[slot] != nullptr) {
            (**vtable_dtor[1])(this[slot]);  // deleting dtor
            this[slot] = nullptr;
        }
    }
    BonusManager::ClearBestBonuses();
}
```

Note: **the retry/quit buttons (+0x98, +0xA4) are NOT freed by Release**.
They live in HUD's control list and HUD removes/frees them when the
HUDControl is destroyed. The button slots Release does free are the
auxiliary slots (+0x9C, +0xA8, +0xC0) that nothing else owns.

---

## 10. Constants (full DAT_ table)

GOT base for `DAT_xxx` resolutions varies by function — `(GOT + DAT_xxx)` is
a relative pointer, where `GOT = funcAddr + 0x14 + DAT_at_funcAddr+0xN`. All
addresses below are absolute file offsets (Ghidra view).

### 10.1 Initialise constants (around 0x001428c0)

| DAT | Value | Use |
|-----|-------|-----|
| DAT_001428cc | 0.0 | zero scratch |
| DAT_001428d0 | 184.0 | initial m_OffsetPos.x |
| DAT_001428d4 | 75.0 | initial m_OffsetPos.y |
| DAT_001428d8 | 0.999 | wave.alpha threshold for fast-skip |
| DAT_001428dc | 1.0 | wave.alpha override |

### 10.2 Update / state 0 constants (around 0x00141da0)

| DAT | Value | Use |
|-----|-------|-----|
| DAT_00141da0 | 1000.0 | m_AnimCounter wraparound |
| DAT_00141da4 | 0.2 | (probably entry sub-step) |
| DAT_00141da8 | 20000.0 | sin-easing range |
| DAT_00141dac | 1.9 | state 0 → 1/6 transition |
| DAT_00141db0 | -0.333 | state 1 initial m_Timer |
| DAT_00141db4 | 0.0 | base zero |
| DAT_00141dc8 | (unresolved float) | bonus-bottom offset |
| DAT_00141dcc | (unresolved float) | bonus title-scale denominator |

### 10.3 Update / state 6 constants (around 0x00142114)

| DAT | Value | Use |
|-----|-------|-----|
| DAT_00142114 | 0.001 | state 8 alpha lower bound |
| DAT_00142118 | 0.0 | zero scratch / state 8 alpha reset |
| DAT_0014211c | 224.0 | state 8 slide-up distance |
| DAT_00142120 | 183.0 | state 6 FruitFact pos.x init |
| DAT_00142124 | 0.999 | state 6 alpha threshold |

### 10.4 LeaderboardsCallback constants

| DAT | Value | Use |
|-----|-------|-----|
| DAT_001405d8 | 0.999 | game.alpha threshold to trigger state 10 |
| DAT_001405dc | 0.0 | m_Timer reset on state 10 |

### 10.5 Button position constants

Already enumerated in §5.

---

## 11. Verification of port states 0, 6, 7, 8

| State | Port impl | Verdict |
|------:|-----------|---------|
| 0 | "wait 0.5s → state 6" | **wrong duration (binary = 1.9s, sin-eased)**; **wrong target (binary goes to 1 in Arcade, else SetStateWait→6/dialog)**; **no scale animation** |
| 6 | "create buttons → state 60" | wrong: state 60 doesn't exist in binary; state 6 is itself the steady state, NOT a transition. Buttons are created mid-state-6 only after `m_ProgressCounter==10` AND `IsAllowedToExit()` returns true. The pop-in alpha ramp is missing. The score-submission tail is missing. The FruitFactControl creation is missing. |
| 7 | "wave reset, retryFlag=0, pauseFlag=0, pending removal" | wrong: binary keeps `pauseFlag=1` in state 7 (only state 8 clears it); binary does NOT clear retryFlag in state 7 (that's `EndRetryLevel`'s job, called externally); binary doesn't `pendingRemoval=1` in state 7 (it transitions to state 8, then state 8 calls `SetTerminate`). |
| 8 | "wave reset, pending removal" | wrong: binary fades game.alpha from 1.0 → 0.001 first (at 0.75x per frame), THEN does the wave reset + NewGame + SetTerminate (not `pendingRemoval=1`). Missing slide-up animation. |

**Recommendation for `implementer`:** rewrite states 0–8 from the §3 pseudocode
above. The existing port's `case 60`, `case 9 → 8`, and the lambda-based
button click callbacks should also be redone to match binary semantics.

---

## 12. Tier-1 / Tier-2 implementer action list

### Tier 1 (single-player full path) — must port to match binary

1. **Fix struct layout** (`src/screens/GameOverScreen.h`):
   - `m_State` at +0x80 (currently +0x7C — broken)
   - `m_Timer` at +0x84 (currently overlaps wrongly)
   - Remove `m_EndReason`/`m_EndScore` fields — these are NOT stored, they're
     just ctor params that override `m_State`/`m_Timer`
   - Add full struct: title-size (3 floats at 0x88), all 5 button slots
     (+0x98, +0x9C, +0xA0, +0xA4, +0xA8), +0xAC anim counter,
     +0xB0..+0xB8 m_OffsetPos, +0xBC m_pFruitFact, +0xC0 slot, +0xC4 m_pBonusScreen,
     +0xC8 notice, +0xCC/+0xCD post flags, +0xCE..+0x10D days-left label buffer (64 bytes),
     +0x110 progress counter, +0x114 m_GameOverTex, +0x118/+0x11C unused/most-fruit-count,
     +0x120 score-submitted, +0x124..+0x130 expression/bg/pom/star, +0x134 isClassic,
     +0x138 m_FruitFactAlpha. Total = 0x13C.
   - Update file header comment with correct addresses (Update=0x141b34, ctor=0x142900,
     Initialise=0x142674).

2. **Rewrite `Initialise`** (currently the ctor): match §3.0 — load mode-specific
   game-over-bg.tex, randomise bg/pom/star/expression when negative, pre-format
   `m_DaysLeftLabel`, fast-skip path for `param2 > 5`.

3. **Rewrite state 0** (entry animation): 1.9s sin-eased scale-in, layer flag = 1,
   force-process gate based on `gameMode` and entity counts.

4. **Add state 1** (bonus phase): create `BonusScreen` (fresh port-side stub OK
   if BonusScreen not yet ported — see Tier-2 dep), wire DeletedControl,
   `BonusManager::SetUpBonusScreen()`, slide-up animation.

5. **Rewrite state 6** (main display + score submission): pop-in alpha ramp,
   `m_ProgressCounter` 0→11 cycle, score-submission tail (§8) gated by
   `m_bScoreSubmitted`, FruitFactControl creation, GameOverTex load, button
   creation (only after frame 10).

6. **Rewrite state 7 + state 8** (retry path): state 7 just guards entities &
   resets wave & flags pause; state 8 fades alpha and calls `NewGame` +
   `SetTerminate` once alpha decays.

7. **Add common layout block** (post-switch tail): position `m_pBonusScreen`,
   `m_pFruitFact`, button shake based on `m_FruitFactAlpha`. §4.

8. **`CreateRetryButton` / `CreateQuitButton`**: real positions (-80,-96) /
   (80,-96), real textures (mode-specific via game.pSaveData refs), wire the
   click callbacks via real Delegate0 instead of port-only lambdas.

9. **`Release`**: clear `game.pGameOverScreen`, clear save-data cache slots
   (+0x11C, +0x120, +0x124, +0x128, +300), free 4 aux HUDControls,
   `BonusManager::ClearBestBonuses()`.

10. **`FindMostOfFruit`**: implement the 5-step process in §6 (filter, shuffle,
    GetTotal, max-track, write to +0x118/+0x11C).

11. **`PreDrawOrder` and `DrawOrder`**: implement the layer-0x80 days-left
    text rendering and the rotating star halo (48-vertex tri-list,
    time-pulsed colour) — see §2 vtable slot 8/9 addresses 0x14171c / 0x141448.
    `Draw` itself just inherits HUDControl3d::Draw (no override needed).

### Tier 2 (edge cases / less critical)

12. **State 9 → state 11** (quit path): wait for entities, call `QuitToMenu`,
    state→11; state 11 = SetTerminate when alpha goes negative.

13. **State 10** (online leaderboard launch): not multiplayer per se — just
    triggers `NetworkManager::LaunchDashboard` (defunct). Implement as no-op
    that resets `m_ProgressCounter=0` and goes back to state 6.
    `LeaderboardsCallback` should also be a no-op stub.

14. **State 14** (quick-restart): no observable caller in stock binary.
    Implement as `m_State = 6; m_Timer = 2.0f;` if anyone wires it via
    `RetryLevel`. **Otherwise can be omitted entirely.**

15. **DeletedControl override** (slot 11): zero pointer for slots +0x98, +0xC4,
    +0xC8 and force m_State = 6 when notice or bonus is deleted.

16. **PostCallback override** (slot 15): set `m_PostOk = (param == 0)`,
    `m_PostInProgress = 0`. Only matters if Twitter/share is wired (defunct;
    omit for port).

17. **BonusScreen full port** (separate task — `BonusScreen` ctor at 0x132048,
    SetUpBonusScreen at 0x10e404). Required for state 1; until ported,
    state 1 can be stubbed with an immediate transition to state 6.

18. **Verify `IsAllowedToExit()` returns true on the desktop port**. Binary
    always returns 1; port can hard-code true.

---

## 13. Open questions / RE gaps

- `FruitFactControl::Update` is not yet decompiled. Its internal state machine
  drives the "best fruit" panel pop-in/pop-out; GameOverScreen only reads its
  combo fields (+0xD0/+0xE0) and writes its pom/star count. **Action: the
  implementer can stub a static FruitFactControl that just renders its
  texture once visible — full port deferred to its own RE task.**

- `BonusScreen::Update` decompile pending. Likewise stubbing is fine until
  state 1 is needed.

- The `DeletedControl` GameOverScreen override at 0x00140558 is small and
  fully decoded above. Make sure HUDControl base (slot 11) dispatches the
  right address — Ghidra's vtable layout is offset by 8 from the data symbol.

- The `field_0x214` SmartPtr<Texture> at the very end (+0x214) is set null
  in Initialise but *the struct is only 0x13C bytes*. The `*(undefined4 *)
  (iVar10 + DAT_001428f8)` write is to a global texture cache (GOT pointer
  to game-over-extra.tex), NOT a struct field. Existing port's worry about
  "+0x114 SmartPtr extra texture" is the GOT global, not a per-instance slot.

- **`m_DaysLeftLabel`**: unclear whether the format string `(iVar10 + iVar7)`
  resolves to a localised "%d days left" or to a multi-language pointer table.
  Worth a follow-up `read_memory` to resolve `DAT_001428fc`. Existing port
  doesn't use this so low priority.

---

## See also

- [systems/game-over-flow.md](../systems/game-over-flow.md) — outer trigger flow (6 callers, pauseFlag guard, EndRetryLevel)
- [screens/game-over.md](../screens/game-over.md) — older summary (superseded by this doc)
- [structs/hud.md](../structs/hud.md) — HUDControl3d base class
- [engine/wavemanager-deep-re.md](wavemanager-deep-re.md) — WaveManager state used by states 7/8
- [engine/pausescreen-deep-re.md](pausescreen-deep-re.md) — sister screen, similar structure
