# Texture Resource Audit

This document inventories texture assets (`.tex` files) present in the port's data drop against their usage in the v1.6.1 binary's code and XML configuration. It surfaces two classes of divergence:

1. **Missing** — referenced in code/XML but not present in the extracted data drop.
2. **Unused** — present in the data drop but no literal reference found in code/XML.

## CRITICAL CAVEAT: This audit is heuristic

The port constructs many texture names **dynamically** via `sprintf` (e.g., `"minus_%d_%s"`, `"combo_%d"`, per-tier/per-achievement/per-powerup suffixes) and through texture-loader indirection (particle templates, achievement icon tables). A static string scan **cannot resolve** these patterns.

**Consequence:** The "unused" list means "no literal reference found," **not** "safe to delete." Group B below lists direct evidence of this dynamic construction. Anyone acting on the unused list must verify each entry's actual call sites (sprintf format strings, template indirection) before deletion.

## 1. Missing Textures (20 references)

Referenced in code/XML but **not present** in `FruitNinjaBada/Data/`.

| Texture | Source(s) | Status | Notes |
|---------|-----------|--------|-------|
| `fruit_shadow.tex` | `src/entities/FruitInfo.cpp` | **LIVE** | Fruit drop shadow drawn during gameplay. Confirmed absent from **every accessible Mortar SKU** — v1.6.1 Bada (HLE ground truth), Android 1.5.4 and 1.7.4 (447-`.tex` APKs, same format) — so the binary never drew it there either → port faithful. Present only in the iOS build, packed unreachably in `fileArcive.bin`. Not fillable. |
| `comming_soon.tex` | `src/screens/MainScreen.cpp:~1993ac` | **LIVE** | "Coming Soon" banner drawn on MainScreen carousel. Confirmed absent from Bada 1.6.1 **and** Android 1.5.4/1.7.4 → drew nothing there → port faithful. iOS-packed only. |
| `checked.tex` | `src/hud/CheckBox.cpp` | **DEAD** | CheckBox ctor loads texture; CheckBox is never instantiated in any live code path (class exists as dead code only). |
| `unchecked.tex` | `src/hud/CheckBox.cpp` | **DEAD** | Same as `checked.tex` — dead class, zero instantiations. |
| `clock_backing` | `poweruplist.xml` | **DEAD** | Time Sink powerup, commented out in active itemlist.xml; present only in never-loaded `itemlistnfc.xml` (NFC variant). |
| `frenzy_sides` | `poweruplist.xml` | **DEAD** | Frenzy powerup FX, commented out in active itemlist.xml. |
| `GB_game_02` through `GB_game_14` | `itemlist.xml`, `itemlistnfc.xml` | **DEAD** | Game-mode icons 2–14 are **commented out** in the active `itemlist.xml` and present only in the never-loaded `itemlistnfc.xml`. v1.6.1 loads exactly one entry (GB_game_01 "Zen"); the rest exist as code-path residue only. |
| `comming_soon_highscore.tex` | `src/screens/GameOverScreen.cpp` | **DEAD** | String `"comming_soon_highscore"` appears in code but is **never loaded by any call path** (binary OR port). |
| `<TBD>.tex` | `src/game/GameInitialise.cpp` | **DEAD** | Code placeholder; `<TBD>` is not a valid filename. |

**Summary:** The two LIVE textures (`fruit_shadow`, `comming_soon`) are **not port bugs** — a cross-SKU hunt (Bada 1.6.1 via HLE; Android 1.5.4 + 1.7.4 APKs, both same Mortar `.tex` format) found neither in **any** accessible non-iOS build, confirming the original binary never drew them on Bada/Android. They ship only in the iOS SKU, packed in `fileArcive.bin` (hashed index, no plaintext names — not extractable). The port is faithful; these gaps are unfillable and left as-is.

The DEAD entries are features never activated (CheckBox unused, powers commented-out, NFC mode never loaded).

## 2. Unused Textures (186 candidates)

Present in `FruitNinjaBada/Data/textures/` or `particles/` but **no literal reference** found in `src/**/*.{cpp,h}` or `.xml` files.

### HEURISTIC WARNING (top of section)

See caveat above. Many of these are loaded via **dynamic construction**, as shown in Group B. Do NOT delete without verifying their actual call sites.

### Group A: Defunct-Feature Assets (Expected Unused)

Cross-reference: [`docs/engine/online-services-audit.md`](online-services-audit.md).

These are UI/FX assets for permanently-disabled subsystems (online services, network play, free Lite version, iOS HD SKU):

**OpenFeint social network:** `feint.tex`, `op_add_friends_button.tex`, `op_connect_button.tex`, `op_title.tex`, `openfeint.tex`, `openfeint_gamecenter.tex`

**GameCenter:** `connect_game_center.tex`, `gc_achievements.tex`, `gc_add_friends_button.tex`, `gc_connect_button.tex`, `gc_connecting.tex`, `gc_no_score_this_week.tex`, `gc_title.tex`

**Online news / leaderboards:** `news_backing.tex`, `news_icon_off.tex`, `news_icon_on.tex`, `newsurlbutton.tex`, `no_score_this_week.tex`, `hd_news_icon_off.tex`, `hd_news_icon_on.tex`

**Multiplayer / P2P / voice chat:** `battlebacking.tex`, `draw.tex`, `multi_player.tex`, `multi_player_connect.tex`, `multi_sensei.tex`, `multi_sensei_results.tex`, `multiplayer_iphone.tex`, `multiplayer_mode.tex`, `ready_multiplayer.tex`, `red_fruit_atlas.tex`, `skynet_title.tex`, `start_voice_chat.tex`, `voice_chat_enabled.tex`, `voicechat_not_available.tex`, `wifi_for_voice_chat.tex`, `wins_losses.tex`, `wins_losses_backing.tex`, `you_win.tex`, `connect_microphone.tex`, `speaking_visual.tex`

**Third-party social (Facebook, Twitter):** `facebook.tex`, `facebook_tick.tex`, `morehalfbrickgames.tex`, `twitter.tex`, `twitter_tick.tex`, `twitterbook.tex`, `twitterbook_tick.tex`, `twitterbook_tick_vert.tex`, `twitterbook_vert.tex`

**iOS HD variant (iPad SKU):** `hd_credits.tex`, `hd_fruit_text.tex`, `hd_gameover.tex`, `hd_haikus.tex`, `hd_hud_fruit.tex`, `hd_mode_words.tex`, `hd_modes.tex`, `hd_music.tex`, `hd_music_cross.tex`, `hd_ninja_text.tex`, `hd_score.tex`, `hd_slice_fruit.tex`, `hd_sml_title.tex`, `hd_sound.tex`, `hd_sound_cross.tex`, `hd_swipe_fruit_begin.tex`

**Lite free version:** `lite_dojo_icon.tex`, `lite_word.tex`, `lite_word_sml.tex`

**Thumbnail/small variants:** `bg_fruit_ninja_sml.tex`, `bg_greatwave_sml.tex`, `bg_i_heart_sensei_sml.tex`, `bg_store_sml.tex`, `bg_yinyang_sml.tex`, `gb_game_sml.tex`

### Group B: Likely FALSE POSITIVES — Dynamic Texture Construction

These are **almost certainly loaded dynamically** via `sprintf` or template indirection and would break the game if deleted. They **must NOT be treated as safe to delete** without verifying their dynamic call sites in code.

Evidence in code/XML:

- **Arcade powerup meter overlays** (`arcade_banana_meter_freeze.tex`, `arcade_banana_meter_frenzy.tex`, `arcade_banana_meter_scorex2.tex`, `word_freeze.tex`, `word_frenzy.tex`, `word_scorex2.tex`) — loaded by powerup-effect renderers that construct names via sprintf.
- **Score popup FX** (`minus_3_blue.tex`, `minus_3_purple.tex`, `minus_3_red.tex`, `minus_5_blue.tex`, `minus_5_purple.tex`, `minus_5_red.tex`, `plus_3_blue.tex`, `plus_3_green.tex`, `plus_3_red.tex`, `plus_5.tex`, `plus_5_red.tex`, `sml_minus_10.tex`) — per-tier, per-color score-damage popups, built via `"minus_%d_%s"` format.
- **Combo awards** (`score_10_fruit_combo.tex`, `score_5_fruit_combo.tex`, `sml_combo_3.tex` through `sml_combo_10.tex`, `100_bonus_points.tex`, `over_300_points.tex`) — tier-based combo visuals, dynamically indexed.
- **Arcade results / bonus UI** (`arcade_results_bonus_box.tex`, `arcade_results_diolog_box.tex`, `arcade_results_score_box.tex`, `arcade_hint_board.tex`, `arcade_hint_sensei.tex`, `arcade_start.tex`, `results_backing_zen.tex`, `results_screen_board.tex`) — dynamic panel construction.
- **Achievement icons** (`ach_all_outta_juice.tex`, `ach_first_taste.tex`, `ach_learned_ninja.tex`, `all_out_of_juice.tex`, `first_taste.tex`, `learned_ninja.tex`) — achievement table-driven icon lookup via achievement ID / name table.
- **Special-fruit FX** (`pomegranate_rays.tex`, `pom_main_splash.tex`, `rays.tex`) — particle/fruit-type dispatch in emitter templates.
- **Particle system textures** (`particles/drip.tex`, `particles/grid_particle.tex`, `particles/gust_2.tex`, `particles/music_note_1.tex`, `particles/music_note_2.tex`, `particles/music_note_treb.tex`, `particles/ring_wave.tex`, `particles/sparks_thin.tex`, `particles/wings.tex`) — loaded via particle XML template *name* indirection, not string literals in code.
- **Blade skins** (`rave_blade.tex`, `rave_blade_glow.tex`) — the `rave_blade_glow` texture is evidence of issue #313 (ghost-trail blade); both are pulled from skin/blade-item tables.

### Group C: Genuinely Uncertain

Remaining entries not classified as defunct or dynamic:

`bad_luck.tex`, `bonus_icon_bomb_crit.tex`, `buy_now.tex`, `buy_now_ring.tex`, `challenges.tex`, `change_gc.tex`, `change_mode.tex`, `change_of.tex`, `classic_attack.tex`, `coin_buy.tex`, `coin_to_spend.tex`, `coins_icon.tex`, `dialog_box.tex`, `diolog_box_big.tex`, `dojo_icon.tex`, `extra_time.tex`, `flatalpha.tex`, `fruit.tex`, `green_button.tex`, `leaderboard_vertical_divider_2.tex`, `leaderboards_button_arcade.tex`, `maroon_button.tex`, `medbacking.tex`, `mode_words.tex`, `need_more_coins.tex`, `neutral_fruit_outline.tex`, `new_stroke.tex`, `newgame.tex`, `notification.tex`, `notificationblank.tex`, `outline_blue.tex`, `outline_red.tex`, `progress_box_big.tex`, `progress_button.tex`, `progressbar.tex`, `quit_buttons_arcade.tex`, `score_limit_reached.tex`, `score_you.tex`, `scrollbar.tex`, `selected_ring.tex`, `selected_stroke.tex`, `slide_bar_wifi_multi.tex`, `slider.tex`, `slider_bar.tex`, `sml_critical.tex`, `sml_hud_x2_sign.tex`, `sml_ice_cover.tex`, `sml_sf.tex`, `sml_white_splash.tex`, `swipe_to_scroll.tex`, `tutorial_baord.tex`, `particles/dummy.tex`

These may be:
- Dead code assets (UI buttons/overlays for disabled features that were not grouped above).
- Genuinely unreachable (e.g., `particles/dummy.tex` placeholder).
- Obscure dynamic refs not caught by the static scan.

**Action:** Verify individual entries against call sites before deletion. Some may be safe; others may break obscure code paths.

## Methodology

The audit is generated by two scripts in `tools/asset-audit/`:

**`scan_missing_textures.py`** — scans `src/**/*.{cpp,h}`, `FruitNinjaBada/Data/**/*.xml`, and identifies texture references via:
- C++ code: `LoadTexture("name")` / `LoadLocalisedTexture("name")` literal arguments.
- XML attributes: `texture=`, `factTexture=`, `icon=`, `iconTexture=`, `backTexture=`, `buttonTexture=`, `image=`.
- Compares against available `.tex` basenames in `FruitNinjaBada/Data/textures/` and `particles/`.

**`scan_unused_textures.py`** — cross-indexes to find textures **present** but **unreferenced**:
- Collects literal texture names from code and XML (same regex as above).
- Collects **dynamic prefixes** — the literal part of format strings before `%s`/`%d` (e.g., `"combo_"` from `"combo_%d"`), but **only** meaningful prefixes ≥3 chars and close to texture-related keywords (Load, texture, icon, etc.).
- Classifies each texture as used (literal match or dynamic-prefix match) or unused.
- Reports dynamic-prefix hits separately so dynamic-construction entries are excluded from the unused list.

**To regenerate:**

```bash
cd <project-root>
python tools/asset-audit/scan_missing_textures.py > tmp/audit_missing.txt
python tools/asset-audit/scan_unused_textures.py > tmp/audit_unused.txt
```

**Scope:** `.tex` textures only (in `textures/` and `particles/` subdirs). Other asset types (`.mad`/`.mmd` models, `.fnt` bitmap fonts, `.sfx` audio) are not yet audited.

## Notes

- The missing LIVE textures (`fruit_shadow`, `comming_soon`) are confirmed absent from the v1.6.1 Bada data set the HLE executes, proving the original binary never drew them. These are asset-drop gaps, not port bugs.
- The unused list is **not a deletion list**; it is a heuristic hint. Group B (dynamic construction) is proof the scan cannot resolve all references. Treat entries in Group C as "verify before acting" only.
- For tool usage documentation and data format specs, see [`docs/resources.md`](resources.md).
