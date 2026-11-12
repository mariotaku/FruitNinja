# tools/ps3 — PS3 build & package helpers

## Packages

| File | Script | What |
|---|---|---|
| `TestBuildFruitNinja1.0.pkg` | `package.sh` / `build.sh pkg` | Executable |
| `FruitNinjaNECESARRYAssets.PKG` | `package-assets.sh` | Data base |
| `FruitNinjaDLC_Skittles.pkg` | `package-dlc.sh skittles` | DLC Skittles |
| `FruitNinjaDLC_PussInBoots.pkg` | `package-dlc.sh puss` | DLC Gato con Botas |

## DLC (Skittles / Gato con Botas)

Mismo EBOOT. Cada DLC es un **content pack** de data:

```bash
# 1. Tirás tus assets en packs/skittles/Data/  (layout tipo FruitNinjaBada/Data)
# 2. Empaquetás
bash tools/ps3/package-dlc.sh skittles
bash tools/ps3/package-dlc.sh puss
```

Detalle: `src/platform/ps3/dlc/README.md` y `packs/*/dlc.json`.

## Build ejecutable

```bash
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH

bash tools/ps3/build.sh pkg
```

## Assets base

```bash
bash tools/ps3/package-assets.sh
# o desde Android extract:
bash tools/ps3/package-assets.sh /path/to/Data
```

## Orden de install (CFW / RPCS3)

1. `FruitNinjaNECESARRYAssets.PKG`
2. `TestBuildFruitNinja1.0.pkg`
3. (opcional) `FruitNinjaDLC_Skittles.pkg` / `FruitNinjaDLC_PussInBoots.pkg`

## Legal

Solo empaquetás data que **vos** aportás. No se redistribuyen assets de Halfbrick / Mars / DreamWorks.
