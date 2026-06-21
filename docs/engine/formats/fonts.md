# .fnt Font Format

## Format

Standard **BMFont text format** (AngelCode Bitmap Font Generator).

Well-documented standard: http://www.angelcode.com/products/bmfont/doc/file_format.html

## Structure

```
info face="Gang of Four" size=24 bold=0 italic=0 charset="" unicode=1 ...
common lineHeight=28 base=22 scaleW=256 scaleH=256 pages=1
page id=0 file="font_fruit_ninja_0.tex"
chars count=95
char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=22 xadvance=7
char id=33 x=247 y=76 width=9 height=22 xoffset=0 yoffset=2 xadvance=8
...
```

Each .fnt file references a corresponding `*_0.tex` texture atlas containing the glyph bitmaps.

## Font Files (12 total)

| Font File | Texture Atlas | Usage |
|-----------|--------------|-------|
| font_fruit_ninja.fnt | font_fruit_ninja_0.tex | Main game font |
| font_fruit_ninja_hd.fnt | font_fruit_ninja_hd_0.tex | HD variant |
| fruit_ninja_numbers.fnt | fruit_ninja_numbers_0.tex | Score numbers |
| fruit_ninja_numbers_hd.fnt | fruit_ninja_numbers_0_hd.tex | HD score numbers |
| fruit_ninja_numbers_blue.fnt | fruit_ninja_numbers_blue_0.tex | Blue numbers (player 2) |
| fruit_ninja_numbers_blue2.fnt | fruit_ninja_numbers_blue2_0.tex | Alt blue numbers |
| fruit_ninja_numbers_green.fnt | fruit_ninja_numbers_green_0.tex | Green numbers |
| fruit_ninja_numbers_red.fnt | fruit_ninja_numbers_red_0.tex | Red numbers (negative) |
| fruit_ninja_numbers_silver.fnt | fruit_ninja_numbers_silver_0.tex | Silver numbers |
| arcade_results_numbers.fnt | arcade_results_numbers_0.tex | Arcade results |
| electrofied_medium.fnt | electrofied_medium_0.tex | Electrified style |
| verdana.fnt | (system font?) | Fallback font |

## For Porting

BMFont is natively supported by many libraries. The .fnt text files can be used as-is — only the texture atlases (.tex) need conversion to PNG using the .tex converter.

