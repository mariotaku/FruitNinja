Here's what we know and what's left to investigate for the globalScaleVec:

## Known Facts

- **BSS address**: 0x001F4334 (Vec3, 12 bytes)
- **GOT entry**: 0x001F38FC → 0x001F4334
- **Static init**: `_GLOBAL__I_Initialise.cpp` sets it to **(1,1,1)** — confirmed from decompile
- **Used by**: Fruit::Init (SetFruitType), Bomb::Init, SpawnFruit, SpawnBomb, Coin::InitCoin, and ~40 other functions (all as PARAM/read)
- **No WRITE xrefs** found to 0x001F4334 besides the static init — meaning either it stays (1,1,1) forever, or it's written via an indirect pointer we didn't trace
- **Current port value**: 3.76 (visual calibration so watermelon fills menu button circle)
- **At (1,1,1)**: fruits are ~13px at 480×320 — comically small
- **At (3.76,3.76,3.76)**: watermelon ≈ 48px — looks proportionally correct

## What Doesn't Add Up

The static init IS (1,1,1), but that produces 13-pixel fruits on a 480×320 screen. The original game clearly has larger fruits. Something must set it to a bigger value at runtime, but no direct WRITE xref to 0x001F4334 was found.

## Investigation Steps

1. **Check if the value is written indirectly** — The BSS at 0x001F4334 could be written through a base pointer + offset. Search for code that writes to `some_ptr + offset` where the result equals 0x001F4334. The global bomb config pointer (0x001F43B8) is 132 bytes away — maybe there's a struct base at 0x001F4300 or similar.

2. **Check `SetupGameWork` more carefully** (0x0010b542) — It writes to `puVar4 + 0x88` where `puVar4` comes from a GOT pointer. Verify that `puVar4` is NOT 0x001F4334 - 0x88 = 0x001F42AC.

3. **Check GameInitialise steps** — The game init at 0x0010c000+ runs ~25 steps. One of them might call a function that sets globalScaleVec. Decompile the init steps that run BEFORE fruit/bomb loading (steps 1-7) and look for writes to Vec3 values.

4. **Check `DisplayManager` or screen setup** — The scale might be set based on screen resolution/DPI. Look at `DisplayManager::Initialise` or `GlesForm::OnInitializing` for writes to globalScaleVec.

5. **Read BSS at runtime** — If you can attach a debugger or add a memory read hook, dump the float values at 0x001F4334 right before SpawnFruit is called. This would give the exact runtime value.

6. **Try the Bada emulator** — Run the original binary in a Bada SDK emulator and inspect memory at 0x001F4334 after game init.

7. **Check if the Vec3 constructor is inlined** — The (1,1,1) init might be a DEFAULT that gets overwritten by a memcpy or assignment from a config file. Search for `vstr` instructions writing to addresses near 0x001F4334.

## Quick Test Values

If you want to narrow it down visually:

| globalScaleVec | Watermelon menu px | Notes |
|---------------|-------------------|-------|
| 1.0 | 13 px | Binary static init (too small) |
| 2.75 | 35 px | Previous guess (30-50% small) |
| 3.76 | 48 px | Current calibration |
| 5.0 | 64 px | Would exactly fill inner circle |