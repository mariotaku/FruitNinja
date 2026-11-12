# PS3 DLC / content packs

Un solo EBOOT (`TestBuildFruitNinja1.0.pkg`). Los "DLC" son **pkgs de data**
que instalan otro `Data/` (o un overlay) con reskins tipo:

- **Skittles** — caramelos en vez de frutas
- **Gato con Botas** (Puss in Boots) — crossover DreamWorks

## Idea

```
HDD0/game/
  FNTEST100/          <- ejecutable (APPID del test build)
    USRDIR/
      EBOOT.BIN
  FNASSET01/          <- assets base (FruitNinjaNECESARRYAssets.PKG)
    USRDIR/Data/...
  FNDLC_SKT/          <- DLC Skittles
    USRDIR/Data/...
  FNDLC_PIB/          <- DLC Gato con Botas
    USRDIR/Data/...
```

En runtime el juego elige qué pack usar (menú Settings / boot flag / archivo
`active_dlc.txt`). Mismo código de corte; solo cambian texturas, sfx y strings.

## Manifest (`dlc.json`)

Cada pack lleva un `dlc.json` en la raíz de su Data:

```json
{
  "id": "skittles",
  "title": "Skittles Ninja",
  "version": "1.0",
  "appid": "FNDLC_SKT",
  "replaces": ["textures", "sfx", "strings"],
  "notes": "Reskin caramelos — assets los pone el usuario"
}
```

## Legal

Vos tenés que aportar los assets (dump propio). Este repo **no** distribuye
arte de Mars, Halfbrick ni DreamWorks. Solo el cableado del port + empaquetado.

## Build de un DLC

```bash
# Poné tus assets en packs/skittles/Data/ (mismo layout que FruitNinjaBada/Data)
bash tools/ps3/package-dlc.sh skittles
# -> build/ps3/FruitNinjaDLC_Skittles.pkg

bash tools/ps3/package-dlc.sh puss
# -> build/ps3/FruitNinjaDLC_PussInBoots.pkg
```

Ver `tools/ps3/package-dlc.sh` y `packs/`.
