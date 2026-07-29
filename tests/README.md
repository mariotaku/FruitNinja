# tests/

CTest registration lives in `CMakeLists.txt` (`fn_add_game_test` macro) and
`scenes/CMakeLists.txt` (`add_scene_test` macro, standalone visual dev
harnesses). All test executables build into one shared **flat** directory,
`${CMAKE_BINARY_DIR}/tests` (e.g. `build/host/tests/`) -- no per-config
subdir, since build type is already separated by the build dir
(build/host, build/web, ...).

## Headless rendering (optional software GL)

Render/screenshot tests need a real GL 2.0/GLSL context. On Windows, when no
GPU/display is available (monitor asleep, CI, headless), WGL falls back to
Microsoft's GDI generic OpenGL 1.1 (no shader support) and the renderer fails
at init with:

```
glCreateShader(vertex) returned 0 -- GL 2.0 entry points unavailable
```

Fix: drop Mesa3D's llvmpipe software-GL DLLs next to the test executables.

1. Test exes are **32-bit (PE32/x86)** -- get the **x86** Mesa build (bitness
   must match the exe).
2. Download `mesa3d-<ver>-release-msvc.7z` from
   [pal1000/mesa-dist-win releases](https://github.com/pal1000/mesa-dist-win/releases).
3. Extract just the two files needed from the `x86/` folder (Windows' built-in
   `tar.exe`, bsdtar, reads `.7z`):
   ```
   C:\Windows\System32\tar.exe -xf mesa3d-<ver>-release-msvc.7z x86/opengl32.dll x86/libgallium_wgl.dll
   ```
4. Copy both `opengl32.dll` and `libgallium_wgl.dll` into the unified test
   output dir from above (e.g. `build/host/tests/`).
5. Run tests:
   ```
   ctest --test-dir build/host -R screenshot
   ```
   `ctest` sets `GALLIUM_DRIVER=llvmpipe` / `LIBGL_ALWAYS_SOFTWARE=1` /
   `MESA_GL_VERSION_OVERRIDE=2.1` itself on every GL-dependent test (see the
   `ENVIRONMENT` wiring in `tests/CMakeLists.txt`'s `fn_add_game_test` /
   `add_test` override) -- you no longer need to export them by hand for
   `ctest` runs. Without those DLLs in place, WGL falls back to the real
   display adapter regardless of the env vars, which can hard-crash the
   process (no useful error) if the display is asleep.

   Running a test **exe directly** (not through `ctest`) still needs the vars
   exported manually, since nothing else sets them:
   ```
   set GALLIUM_DRIVER=llvmpipe
   set LIBGL_ALWAYS_SOFTWARE=1
   set MESA_GL_VERSION_OVERRIDE=2.1
   ./build/host/tests/test_screen.exe main
   ```

These DLLs are gitignored build artifacts -- do **not** commit them. They're
opt-in for whoever needs headless rendering; on a machine with the display on
and a real GPU they're unnecessary.
