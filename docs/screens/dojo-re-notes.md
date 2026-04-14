# DojoScreen — Port-Ready RE Notes
<!-- Analysed: 2026-04-14T12:15 -->

Functions: ctor `0x00137b90`, Update `0x00138414`, Draw `0x0013822c`,
LoadContent `0x00137a20`.

---

## 1. Button positions

Created lazily in state 0 once `m_TransitionAlpha > 0.9991`.

| Field | Callback | Position Vec3 | DAT sources |
|-------|----------|---------------|-------------|
| `field1_0x94` (Play/Quit) | `DojoScreen::QuitCallback` `0x001389f4` | `(185.0, -106.0, 0.0)` | x=`0x00138688`, y=`0x0013868c`, z=`0x00138690` |
| `field2_0x98` (Shop) | `DojoScreen::ShopCallback` `0x00137864` | `(-18.0, -15.0, 0.0)` | literals; z shared from `0x00138690` |
| `field3_0x9c` (About) | `DojoScreen::AboutCallback` `0x001378e0` | `(145.0, 42.0, 0.0)` | x=`0x001389d0`, y=`0x001389d4`, z=`0x001389d8` |

Post-creation scale applied to `field1_0x94` and `field3_0x9c` position and hit-rect via
`_Vector3::operator*=(pos, DAT_00138694)` where `DAT_00138694 = 0.825`.
Shop button (`field2_0x98`) is **not** scaled this way.

---

## 2. Button textures

Loaded by `DojoScreen::LoadContent` `0x00137a20` via `TextureManager::LoadLocalisedTexture`.
Five textures loaded in order into static offsets; raw string addresses in `.rodata`:

| # | String address | Filename | Notes |
|---|---------------|----------|-------|
| 1 | `0x001bb190` | `dojo.tex` | background board; used in Draw |
| 2 | `0x001bb199` | `dojo_sensei.tex` | 3D sensei — skip per task |
| 3 | `0x001bb1a9` | `senseis_swag.tex` | skip |
| 4 | `0x001bbc68` | `newgame.tex` | button 1 (`field1_0x94`) icon texture |
| 5 | `0x001bbc74` | `dojo_icon.tex` | button 3 (`field3_0x9c`) icon texture |

Shop button (`field2_0x98`) uses a fruit texture selected via `Fruit::FruitType(name, false)`;
the texture is read from the FruitInfo entry — not a direct `LoadLocalisedTexture` call.

---

## 3. State machine transitions

`field17_0x8c` = `m_TransitionAlpha`, `field18_0x90` = `m_State`.
Ctor initialises `m_TransitionAlpha = DAT_00137b80` = 0.0, state = 0.

| State | Alpha update | Threshold / condition | Next state |
|-------|--------------|-----------------------|------------|
| 0 | `alpha += (1.0 - alpha) * 0.25` | `alpha > 0.9991` (`DAT_001389dc`) | → 1 |
| 0 | same | `alpha <= 0.95` (`DAT_00138684`) | stay in 0, skip button creation |
| 1 | none | — | idle |
| 2 | `alpha *= 0.75` | `alpha <= 0.001` (`DAT_001389e0`) | clear buttons, create ShopScreen |
| 3 | `alpha *= 0.75` | `alpha <= 0.001` (`DAT_001389e0`) | clear buttons, create AboutScreen |
| 4 | `alpha *= 0.75` | `alpha <= 0.001` **and** `ActorManager::GetNumEntities == 0` | launch network, → 0 |
| 6 | `alpha *= 0.75` | `alpha < 0.001` (`DAT_001389e0`) | set `field_0x33=1` (pending removal), write GameState=8 |

`DAT_001389d8 = 0.0` — used as fVar12 seed value entering states 2/3/4; after fade the
`m_TransitionAlpha` is reset to `DAT_001389d8` (0.0) before child-screen creation.

---

## 4. "Wait for entities cleared" mechanism (state 4)

State 4 is reached via `DojoScreen::QuitCallback` → sets state to **6** (not 4).
State **4** is the network-dashboard path. Condition checked:

```
ActorManager* mgr = Mortar::ActorManager::GetInstance();
int n = Mortar::ActorManager::GetNumEntities(mgr, 0);
if (n == 0) { NetworkManager::LaunchDashboard(...); state = 0; }
```

There is **no separate threshold DAT** for this — the check is `n == 0` (integer equality).

---

## 5. Child screen creation sites

Both happen in the **cases 2/3** block, after `alpha *= 0.75` drops below `DAT_001389e0` (0.001).
The state is read *before* nulling buttons, then dispatched:

```c
// state 3 → AboutScreen
AboutScreen* s = (AboutScreen*)operator_new(0xa0);
AboutScreen::AboutScreen(s, this);             // this = DojoScreen*
(s->vtable[2])(s);                             // calls Init()
HUD::AddControl(hud, s, 0);
return;

// state 2 → ShopScreen
FruitSaveData::CheckDatesHaveChanged(...);
ShopScreen* s = (ShopScreen*)operator_new(0xbc);
ShopScreen::ShopScreen(s, this);
HUD::AddControl(hud, s, 0);
(s->vtable[2])(s);                             // calls Init()
return;
```

AboutScreen size = `0xa0`. ShopScreen size = `0xbc`.
Both are added to HUD at layer 0. Note Init() call order differs between the two.

---

## 6. Release / pending-removal flow

- `DojoScreen::QuitCallback` → sets `m_State = 6`.
- State 6 fade: `alpha *= 0.75`. ARM comparison `if (fVar12 < DAT_001389e0)` — fires when
  `alpha >= 0.001` is false, i.e., `alpha < 0.001`. Then:
  ```c
  this->field_0x33 = 1;                        // HUDControl::m_bPendingRemoval (offset 0x33)
  *(uint*)(HUD->field_0x160 + 0x10c) = 8;     // write GameTaskState = 8
  ```
- DojoScreen does **not** poll the child screen. The child (AboutScreen) calls
  `DojoScreen::QuitCallback` via the method pointer on the parent (`this->field120_0x90`)
  before marking itself pending-removal (see AboutScreen notes).

---

## 7. HUDControl field offsets used

Verified in decompile against `field_0x33`, `field_0x34`, `field18_0x90`:

| Offset | Field | Value set |
|--------|-------|-----------|
| `0x33` | `m_bPendingRemoval` | `1` (byte) — marks screen for HUD removal |
| `0x34` | `m_LayerFlags` | `0x80` (uint32) — written in ctor via `*(undefined4*)&field_0x34 = 0x80` |
| `0x32` | `field_0x32` | `0` (byte) — cleared in ctor, purpose unknown |
| `0x8c` | `m_TransitionAlpha` | float, init 0.0 |
| `0x90` | `m_State` | int, init 0 |

---

## 8. Draw function

`DojoScreen::Draw` `0x0013822c` — draws background board only; no per-button drawing here
(buttons are HUDControls drawn by the HUD traversal).

Draw sequence (only runs if `m_TransitionAlpha > 0.0`):
1. `MatrixStack::Reset`
2. Scale by texture dimensions `(w+1, h+1, DAT_001383c0)` = `(w+1, h+1, 0.0)` — queries
   from `field2_0x98` (Shop button texture stored in same GOT object).
3. Translate by `(-180.0, -47.0, 0.0)` from `(DAT_001383c4, DAT_001383c8, DAT_001383c0)`,
   further offset by `(1.0 - m_TransitionAlpha) * *(float*)(GOT + DAT_001383e0)` along X.
4. `MatrixManager::UploadCurrentMatrices`
5. `Texture::Set(board_tex)`, `Mesh::DrawQuadUnCached`, `Texture::UnSet`
6. `BaseScreen::DrawBorders(this, &board_tex_smart, Vec3(DAT_001383cc, DAT_001383d0, 0.0))`
   = `Vec3(-184.0, -136.0, 0.0)`.
