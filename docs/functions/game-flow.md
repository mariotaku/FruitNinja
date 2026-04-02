# Game Flow & Input Functions

## Game Flow Functions

### HitBomb (0x0016b0fc, 63 lines)

| Address | Signature |
|---------|-----------|
| 0x0016b0fc | `void HitBomb(Vec3 pos)` — camera shake, set Game.bombTimer=2.0, play SFX |

### HitMenuBomb (0x0016b234, 37 lines)

| Address | Signature |
|---------|-----------|
| 0x0016b234 | `void HitMenuBomb(Vec3 pos)` — menu transition bomb effect |

### QuitToMenu (0x00169e50, 39 lines)

| Address | Signature |
|---------|-----------|
| 0x00169e50 | `void QuitToMenu()` — sets Game.pauseFlag=1, Game.menuReturnTimer=countdown |

### GameOver (0x00169ed4, 72 lines)

| Address | Signature |
|---------|-----------|
| 0x00169ed4 | `void GameOver(int endReason, float endScore, int endParam)` |

### SaveCurrentData (0x0016ccc8, 108 lines)

| Address | Signature |
|---------|-----------|
| 0x0016ccc8 | `void SaveCurrentData(bool fullSave)` |

### PrepareForLevelStart (0x00169a9c)

| Address | Signature |
|---------|-----------|
| 0x00169a9c | `void PrepareForLevelStart()` — resets all game entities for new round |

---

## Touch Input

### GlesForm::TransformTouchPos (0x0018327c, 16 lines)

```c
Point TransformTouchPos(Point raw) {
    result.x = (int)(raw.x * 320.0 / screenWidth);
    result.y = 319 - (int)(raw.y * 480.0 / 320.0);
    return result;
}
```

### Touch::__UpdateInternal (0x00195690, 25 lines)

| Address | Signature |
|---------|-----------|
| 0x00195690 | `void Touch::__UpdateInternal(uint id, bool pressed, float x, float y, float pressure)` |

---

