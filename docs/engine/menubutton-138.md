# MenuButton +0x138 — Hardware Back-Key Auto-Fire

RE'd: 2026-04-29.

## Summary

`MenuButton +0x138` is a **single-byte flag** that marks a button as the
**default action target for the hardware Back / Menu key** on the screen
that owns it. When the user presses the device's hardware Back key
(Bada `KEY_END_KEY`) or Menu key, every visible `MenuButton` whose
`m_bHighlighted == 1` and `+0x138 == 1` will auto-fire as if the user
clicked / sliced it that frame.

The field is **NOT** a "removal pending" / fade-out flag. The earlier
port name `m_bRemovalPending` is a misread.

Suggested name: **`m_bRespondsToBackKey`** (or `m_bIsDefaultMenuAction`).

## Binary references

### Initialiser (writes 0)

| Address | Function | Effect |
|---------|----------|--------|
| 0x0014ee96 | `MenuButton::Init` @ 0x0014ee40 | `this->+0x138 = 0` (default off) |

### Reader (single)

| Address | Function | Behaviour |
|---------|----------|-----------|
| 0x0014e9a8 | `MenuButton::Update` @ 0x0014e614 | Gates back-key auto-fire (see pseudocode below) |

### Writers (set to 1, opt-in by screen code after `Init`)

| Address | Function | Button |
|---------|----------|--------|
| 0x0012f2bc | `AboutScreen::Update` | Back button |
| 0x0013856c | `DojoScreen::Update`  | Play button (state 0 path) |
| 0x0013e86a | `GameModeScreen::CreateControls` | `m_BtnBack` |
| 0x00141400 | `GameOverScreen::CreateQuitButton` | Quit button |
| 0x00148c80 | `LeaderboardScreen::CreateQuitButton` | Quit button |
| 0x0014bbb4 | `MainScreen::Update` | `pLeaderboardBtn` (state 1 path) |
| 0x001545e8 | `PauseScreen::Update` | Pause/resume toggle |
| 0x0015e3c6 | `ShopScreen::Update` | Back button |
| 0x0016524a | `UpsellScreen::Update` | Buy-now ring |

(Hits at `0x0017c6fa`, `0x0017d72a`, `0x0017d758`, `0x0017d7ec` are
`SlashEntity::field_0x138`, unrelated to MenuButton.)

## Pseudocode of the reader

From `MenuButton::Update` decompile (binary @ 0x0014e9a2..0x0014e9da):

```c
// iVar6 holds this function's GOT base. The chain
//   *(int*)(GOT + DAT_0014ebbc)  ->  GameObj singleton (FruitNinjaApp/GameTask)
// reads byte +0x604 of that singleton.
//
// +0x604 = m_BackKeyPressed: edge-triggered byte cleared every
// frame at the top of GameTaskUpdate (0x0010a5d4) and set to 1 by:
//   - RegressMenuCallback   (0x00168e9c)  hardware Back key
//   - ShowPauseMenuCallback (0x00168e6c)  hardware Menu key (when
//                                          gameMode==0 && !paused)
if (this->m_bHighlighted != 0 &&
    GameObj.m_BackKeyPressed != 0 &&
    this->m_bRespondsToBackKey != 0)        // +0x138
{
    if (this->m_pEntity != NULL) {
        // Fire the entity's slice handler with bladeDir = (1, 0, 0).
        // For Fruit, this is Fruit::OnSliced (vtable slot +0x24);
        // for Bomb, Bomb::OnHit. Both fire the button's click delegate
        // via the normal slice-detection path further down in Update.
        Vec3 fakeBlade = { 1.0f, 0.0f, 0.0f };
        this->m_pEntity->vtable[0x24](this->m_pEntity, NULL, NULL, NULL, &fakeBlade);
    } else {
        // Toggle/flat button (no fruit entity) -- fire callback directly.
        TouchReleased();
    }
}
```

The same singleton+0x604 byte is referenced at `0x00168eac`
(`RegressMenuCallback`) and `0x00168e8c` (`ShowPauseMenuCallback`); both
write 1 unconditionally / under guard, and `GameTaskUpdate` clears it
back to 0 at the top of the next tick — making it a one-frame
edge-triggered flag.

## Why each screen sets this on the button it does

The "default action" for hardware Back varies by screen:

* **Most screens** (About, GameMode, GameOver, Leaderboard, Shop, Main
  in leaderboard state) wire it to the **back / quit button** — the
  natural "back out" action.
* **DojoScreen** wires it to the **play button** because Dojo's idle
  state has multiple buttons (Play, Shop, About) and Play is the
  designed default activity.
* **PauseScreen** wires it to the **pause/resume toggle** so the
  hardware Menu key un-pauses gameplay.
* **UpsellScreen** wires it to the **buy-now ring** — the screen's
  primary call-to-action.

In every case, the rule is "what should the hardware Back / Menu key do
on this screen?", not "is this button a back button?".

## Port discrepancy (informational — RE only, do not fix from this doc)

`src/hud/MenuButton.h` calls this field `m_bRemovalPending`; `src/hud/MenuButton.cpp`
uses it to drive a fictional alpha fade-out animation (decay
`m_DrawColour.a`, set `m_bPendingRemoval` when alpha hits 6). The
binary's `MenuButton::Update` does no such thing — there is no
`m_DrawColour.a` decay loop, no `+0x33` self-removal trigger from
this field. The port also exposes a `StartFadeOut()` helper that has no
binary counterpart. These are leftover misunderstandings that the
implementer should rip out and replace with the back-key behaviour
documented above.

The shrink-then-self-remove animation that *does* exist is driven by
`m_FadeCounter` (+0xD0) and uses a sin-curve scale on `size`, not alpha
fade. That part of the port code is correct.
