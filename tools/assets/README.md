# tools/assets/

`.tex` texture asset conversion (FruitNinjaBada → PNG/gallery).

- **`convert_tex.py`** — CLI: convert `.tex` files to PNG and emit an `index.html` gallery (PIL). `python tools/assets/convert_tex.py [IN] [OUT]`.
- **`tex2png.cpp`** — standalone C++ `.tex`→`.png` + HTML gallery (zlib). Build: `g++ -O2 -o tex2png tex2png.cpp -lz`.
- **`convert_textures.cpp`** — standalone C++ `.tex`→`.png` (minimal, zlib). Build: `g++ -o convert_textures convert_textures.cpp -lz`.

The `.py` is the convenient default; the `.cpp` variants need compiling and have no Python/PIL dependency.
