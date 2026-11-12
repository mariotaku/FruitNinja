# Cómo activar FRUIT_PLATFORM_PS3

## En Ubuntu / WSL (recomendado)

```bash
cd ~/FruitNinja
git checkout feature/ps3-scaffold
git pull

# Si CMakeLists quedó en PLACEHOLDER:
bash tools/ps3/apply-cmake-ps3.sh

# Env del SDK
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH

# Configure = prende FRUIT_PLATFORM_PS3 vía toolchain
cmake -B build/ps3 -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ps3.toolchain.cmake \
  -DFRUIT_PLATFORM_PS3=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/ps3 -j$(nproc)

# o todo junto:
bash tools/ps3/build.sh
bash tools/ps3/build.sh pkg   # + TestBuildFruitNinja1.0.pkg
```

El **toolchain file** (`cmake/ps3.toolchain.cmake`) también fuerza
`FRUIT_PLATFORM_PS3=ON` cuando lo usás, así que el flag es redundante pero claro.

## Qué hace el flag

| Efecto | Detalle |
|---|---|
| `option(FRUIT_PLATFORM_PS3)` | Opt-in en CMake |
| `#define FRUIT_PLATFORM_PS3 1` | Guards en `src/platform/ps3/*` |
| `FN_BIG_ENDIAN` | Cell PPU |
| Sin SDL2 | Como Wii |
| `add_executable(... mainPS3.cpp)` | Entry PS3 |
| Link `-lio -lsysutil -lrt -llv2 -lm` | Pad + sysutil scaffold |

## Si el patch no aplica limpio

Abrí `cmake/ps3-cmake.patch` y meté los hunks a mano (son 4 bloques chicos).
