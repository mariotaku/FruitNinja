# Power-Up Functions

## Power-Up System

### PowerUpManager::ActivatePower (0x001197c4, 118 lines)

| Address | Signature |
|---------|-----------|
| 0x001197c4 | `PowerUp* PowerUpManager::ActivatePower(ulong hash, Vec3 pos, float* extra)` |

### PowerUp::Activate (0x00119134, 48 lines)

| Address | Signature |
|---------|-----------|
| 0x00119134 | `void PowerUp::Activate(bool showPopup, bool isPurchased, Vec3 pos, float* extra)` |

### PowerUp::Parse (0x001194f0, 128 lines)

| Address | Signature |
|---------|-----------|
| 0x001194f0 | `void PowerUp::Parse(TiXmlElement* xml)` |

---

## GameModifier Types — Full Analysis

### ScoreModifier (0x3c bytes)

**ParseSpecific** (0x0011ccb0, 26 lines): Parses `<score>` XML child element.

```c
void ScoreModifier::ParseSpecific(TiXmlElement* xml) {
    TiXmlElement* child = xml->FirstChildElement("score");
    vtable->InitDefaults(this);  // gainAdd=0, gainMul=1, lossAdd=0, lossMul=1
    if (child) {
        child->QueryIntAttribute("gainadd",      &this->scoreGainAdd);       // +0x20
        child->QueryIntAttribute("gainmultiply", &this->scoreGainMultiply);  // +0x24
        child->QueryIntAttribute("lossadd",      &this->scoreLossAdd);       // +0x28
        child->QueryIntAttribute("lossmultiply", &this->scoreLossMultiply);  // +0x2c
        this->applied = CompareWords(child->Attribute("applied"), "true");    // +0x34
    }
}
```

**UpdateSpecific** (0x0011cc50, 20 lines): Applies score multipliers each frame.

```c
int ScoreModifier::UpdateSpecific(float dt) {
    if (!this->applied) {  // +0x34: one-shot vs continuous
        PowerUpManager::AddToScoreGainAdd(applyCount * scoreGainAdd);
        PowerUpManager::AddToScoreLossAdd(applyCount * scoreLossAdd);
        for (int i = 0; i < applyCount; i++) {
            PowerUpManager::AddToScoreGainMultiply(scoreGainMultiply);
            PowerUpManager::AddToScoreLossMultiply(scoreLossMultiply);
        }
    }
    return 0;  // never expires on its own
}
```

**RemoveModifier** (0x0011cd44): If `applied` flag set, installs a score delegate callback.

### TimeModifier (0x3c bytes)

**ParseSpecific** (0x00120100, 35 lines): Parses `<time>` XML element.

```c
void TimeModifier::ParseSpecific(TiXmlElement* xml) {
    this->bStopClock = CompareWords(xml->Attribute("type"), "stop");  // +0x2c
    xml->QueryFloatAttribute("scale", &this->timeScale);    // +0x30, default 1.0
    xml->QueryFloatAttribute("addtime", &this->addTime);    // +0x34
    if (addTime != 0) this->addTimeCountdown = 1;           // +0x38
    // Parse <ramp> child for gradual time scale change
    this->rampDuration = 0;  // +0x24
    this->rampTarget = 1.0;  // +0x20
    this->currentScale = 1.0; // +0x28
    TiXmlElement* ramp = xml->FirstChildElement("ramp");
    if (ramp) {
        ramp->QueryFloatAttribute("duration", &this->rampDuration);
        ramp->QueryFloatAttribute("target",   &this->rampTarget);
    }
}
```

**UpdateSpecific** (0x001200a0, 75 lines): Applies time scaling each frame.

```c
int TimeModifier::UpdateSpecific(float dt) {
    // One-shot: add time to TimeControl then expire
    if (addTimeCountdown > 0 && --addTimeCountdown == 0) {
        TimeControl::AddTime(Game->timeControl, addTime);
        return 1;  // expire
    }
    // Stop clock
    if (bStopClock) PowerUpManager::StopClock(timeScale);
    // Slow clock
    if (timeScale != 1.0) PowerUpManager::SlowClock(timeScale);
    // Ramp: gradually interpolate currentScale toward rampTarget
    if (rampDuration <= 0) {
        currentScale = rampTarget;
    } else {
        // Lerp currentScale toward rampTarget over rampDuration
        // Speed depends on whether ramping up or down, and remaining time ratio
    }
    PowerUpManager::ApplyDtMod(currentScale);
    return 0;
}
```

### SlashModifier (0x40 bytes) — NO UpdateSpecific

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x20 | Colour* | m_pColours | Heap array of blade colours |
| +0x24 | int | m_ColourCount | Number of colours |
| +0x28 | int | m_ColourType | From ParseSlashModColourType |
| +0x2c | float | m_Width | Blade width (default 1.0) |
| +0x30 | char* | m_Texture | Cloned string, blade texture name |
| +0x34 | char* | m_FxTexture | Blade FX texture name (0x40 buffer) |
| +0x38 | uint | m_PowerMask | OR'd from ParseSlashPowerMask |
| +0x3c | bool | m_bApplied | |

**ParseSpecific** (0x0011f464, ~60 lines): Parses `<slash>` XML element.

```c
void SlashModifier::ParseSpecific(TiXmlElement* xml) {
    xml->QueryDoubleAttribute("width", &tmpDouble);
    m_Width = (float)tmpDouble;                     // +0x2c
    m_ColourType = ParseSlashModColourType(xml->Attribute("type"));  // +0x28
    m_Texture = CloneString(xml->Attribute("texture"));              // +0x30
    m_FxTexture = CloneString(xml->Attribute("fxtexture"));          // +0x34
    // Parse child <Power> elements → OR'd bitmask
    for (child = xml->FirstChildElement("Power"); child; child = child->NextSiblingElement("Power"))
        m_PowerMask |= ParseSlashPowerMask(child);   // +0x38
    // Parse child <Colour> elements → heap Colour array
    m_ColourCount = CountChildElements(xml, "Colour");
    m_pColours = new Colour[m_ColourCount];
    // ... parse each R,G,B,A
}
```

**ApplyModifier** (0x0011f320): Calls `SlashEntity::SetModColours(colours, colourType, width, texture, fxTexture)`. Applies blade visual changes at activation time — no per-frame update needed.

**RemoveModifier** (0x0011f2e0): Decrements global counter; if 0, resets to default equipped blade via `ItemManager::SetEquippedItem`.

### WaveModifier (0x44 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x20 | vector\<PROBABILITY_OVERIDE\> | m_Overrides | 12 bytes, spawn probability overrides |
| +0x2c | float | m_BombMultiplier | Default 1.0 |
| +0x30 | float | m_BombScale | Default 1.0 |
| +0x34 | float | m_FruitMultiplier | Default 1.0 |
| +0x38 | float | m_PowerupDtMod | Default 1.0 |
| +0x3c | int | m_SetWave | Default 10000 (= don't override) |
| +0x40 | float | m_CriticalChanceMod | Default 1.0 |

**ParseSpecific** (0x0012836c, ~40 lines): Parses `<wave>` XML element.

```c
void WaveModifier::ParseSpecific(TiXmlElement* xml) {
    xml->QueryFloatAttribute("fruitMultiplier",   &m_FruitMultiplier);   // +0x34
    xml->QueryFloatAttribute("bombMultiplier",    &m_BombMultiplier);    // +0x2c
    xml->QueryFloatAttribute("bombScale",         &m_BombScale);         // +0x30
    xml->QueryFloatAttribute("criticalChanceMod", &m_CriticalChanceMod); // +0x40
    xml->QueryFloatAttribute("powerupDtMod",      &m_PowerupDtMod);     // +0x38
    xml->QueryIntAttribute("setWave",             &m_SetWave);           // +0x3c
    // Parse child <ProbabilityOveride> elements
    for (child : xml children "ProbabilityOveride")
        PROBABILITY_OVERIDE::Parse(child) → push to m_Overrides
}
```

**UpdateSpecific** (0x001280e4, 25 lines): Applies wave parameter overrides each frame.

```c
int WaveModifier::UpdateSpecific(float dt) {
    WaveManager::FruitMultiplyer(m_FruitMultiplier);
    WaveManager::BombMultiplyer(m_BombMultiplier);
    WaveManager::BombScale(m_BombScale);
    WaveManager::CriticalChanceMod(m_CriticalChanceMod);
    PowerUpManager::PowerupDtModMultiply(m_PowerupDtMod);
    return 0;
}
```

**ApplyModifier** (0x001282d4): If `m_SetWave < 10000` and less than current wave, calls `WaveManager::SetCurrentWave`. Iterates override vector calling `SelectType`, inserts into `WaveManager::GetCurrentOverideList`.

**RemoveModifier** (0x00128340): If current wave negative and ≤ m_SetWave, resets wave via `WaveManager::SetCurrentWave(5, 0.25, 0)`.

### Modifier Summary

| Type | Parse | Update | Apply | Remove | Key Effect |
|------|-------|--------|-------|--------|------------|
| ScoreModifier | Gain/loss add+multiply | Accumulates to PowerUpManager | — | Sets score delegate | Score ×N |
| TimeModifier | Stop/slow/ramp/addtime | ApplyDtMod per frame | — | — | Frenzy slow-mo |
| SlashModifier | Colours, width, texture | **None** (apply-only) | SetModColours | Reset equipped blade | Blade skins |
| WaveModifier | Fruit/bomb multipliers | Overrides each frame | SetCurrentWave + overrides | Reset wave | Spawn changes |

---

## Particle System

### PSPParticleManager::AddEmitter (0x001149e0, 56 lines)

| Address | Signature |
|---------|-----------|
| 0x001149e0 | `PSPParticleEmitter* AddEmitter(ulong hash, PSPParticleEmitter** ppRef, bool persistent)` |

### PSPParticleEmitter::Update (0x00115d9c, 53 lines)

| Address | Signature |
|---------|-----------|
| 0x00115d9c | `void PSPParticleEmitter::Update(float dt)` |

### PSPParticleManager::Update (0x00115ed8, 37 lines)

| Address | Signature |
|---------|-----------|
| 0x00115ed8 | `void PSPParticleManager::Update(float dt, bool paused)` |

### PSPParticleManager::Draw (0x00114c64, 382 lines)

| Address | Signature |
|---------|-----------|
| 0x00114c64 | `void PSPParticleManager::Draw(float dt, bool paused, int layer)` |

---

