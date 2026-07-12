// tools/assets/svg-to-webp.mjs -- BUILD-TIME rasterizer: converts the
// checked-in widget SVGs to WebP-encoded .tex textures under
// assets/ui-widgets/generated/. Invoked by the fn_asset_staging CMake
// target (see CMakeLists.txt), before tools/assets/stage-assets.py, on
// every platform (Windows/CLion included) -- replaces the old OFFLINE
// Python/cairosvg tool (svg_to_tex.py), which couldn't rasterize on a
// plain Windows host.
//
// Usage:
//     node svg-to-webp.mjs [repoRoot]
//
// Source: assets/ui-widgets/<name>.svg (checked in).
// Output: assets/ui-widgets/generated/<name>.tex (WebP bytes, .tex
// extension -- NOT git-tracked, this is now a build artifact regenerated
// every configure/build; stage-assets.py copies these verbatim into the
// staged textures dir for both host and web builds).
//
// Lossless WebP: this is crisp vector UI art (flat colors, sharp edges),
// so lossless keeps it pixel-exact. Rasterized at 4x supersample (density
// 288 vs the nominal 72 dpi) then resized down to the target box for
// antialiased edges.
//
// Self-provisioning: if the `sharp` module isn't installed yet, this runs
// `npm install` in tools/assets/ once (same trick as the Python scripts'
// _ensure_pillow) and retries the import.
//
// Fatal by design: the widget textures have no runtime fallback (SettingsScreen
// / the render tests load them directly via LoadLocalisedTexture, no
// placeholder substitution), so ANY failure (node missing, sharp missing/no
// prebuilt for this platform/arch, npm install failing, no network, rasterize
// error) prints a clear ASCII error and exits NON-ZERO to fail the build loudly
// instead of silently shipping blank widgets. Install node + let npm install
// sharp (internet access required once, cached after) to fix.
//
// Idempotent: an output .tex newer than its source .svg is skipped.

import { existsSync, mkdirSync, statSync, renameSync } from "node:fs";
import { execSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

// name -> [width, height], all POT per the widget's on-screen footprint.
const MANIFEST = {
  checked: [128, 64],
  unchecked: [128, 64],
  // box.tex: the binary's single shared generic beveled-wood box, LoadContent'd
  // by ComboBox (@0x00168b3c), ListBox (@0x00194fdc), and SliderControl
  // (@0x001b7bc0) alike -- see box.svg's own header comment.
  box: [64, 40],
  slider_will: [32, 32],
  // check/caret: standalone transparent glyphs (lime tick, gold chevron) drawn
  // as overlays on the port UI toolkit's NineSlice box.tex (src/ui/Ui*).
  check: [32, 32],
  caret: [32, 32],
  // expand_arrow: NOT 4x supersample-friendly POT -- authored to match
  // ComboBox::Draw's exact caret aspect (32x38; see ComboBox.cpp Draw) so the
  // runtime scale-to-cell is uniform (1:1) instead of stretching.
  expand_arrow: [32, 38],
  vbar: [32, 128],
  vslider: [32, 64],
  arrow: [32, 32],
  settings_button: [64, 64],
};

const DENSITY = 288; // 4x supersample over the nominal 72 dpi for crisp edges.

async function loadSharp(scriptDir) {
  try {
    return (await import("sharp")).default;
  } catch (e) {
    console.log("[svg-to-webp] sharp not found, running npm install in " + scriptDir);
    execSync("npm install", { cwd: scriptDir, stdio: "inherit" });
    return (await import("sharp")).default;
  }
}

async function main() {
  const scriptPath = fileURLToPath(import.meta.url);
  const scriptDir = path.dirname(scriptPath);
  const repoRoot = process.argv[2] || path.resolve(scriptDir, "..", "..");

  const svgDir = path.join(repoRoot, "assets", "ui-widgets");
  const outDir = path.join(svgDir, "generated");
  mkdirSync(outDir, { recursive: true });

  const sharp = await loadSharp(scriptDir);

  let generated = 0;
  let skipped = 0;

  // Render one SVG -> <outName>.tex at (w x h), supersampled at `density`.
  // Returns true if it rendered, false if skipped (output newer than source).
  async function render(svgPath, outName, w, h, density) {
    const outPath = path.join(outDir, outName + ".tex");
    if (existsSync(outPath)) {
      if (statSync(outPath).mtimeMs >= statSync(svgPath).mtimeMs) {
        skipped++;
        return false;
      }
    }
    const tmpPath = outPath + ".tmp.webp";
    await sharp(svgPath, { density: density })
      .resize(w, h, { fit: "fill" })
      .webp({ lossless: true })
      .toFile(tmpPath);
    renameSync(tmpPath, outPath);
    console.log("[svg-to-webp] generated " + outName + ".tex (" + w + "x" + h + ")");
    generated++;
    return true;
  }

  for (const [name, [w, h]] of Object.entries(MANIFEST)) {
    const svgPath = path.join(svgDir, name + ".svg");

    if (!existsSync(svgPath)) {
      console.log("[svg-to-webp] WARNING: missing source SVG " + svgPath + " -- skipping");
      continue;
    }

    // Nominal-res .tex (baseline / fallback) plus an HD "hd_" sibling at 2x the
    // pixel dimensions (density doubled to keep the same supersample). The
    // texture loader (TextureManager::BuildHdPath/Load) silently prefers the
    // hd_ file and halves its reported apparent size, so widgets draw at the
    // SAME on-screen footprint but sample 2x the detail -- crisper vector UI.
    await render(svgPath, name, w, h, DENSITY);
    await render(svgPath, "hd_" + name, w * 2, h * 2, DENSITY * 2);
  }

  console.log("[svg-to-webp] " + generated + " generated, " + skipped + " up to date");
}

main().catch((err) => {
  console.log("[svg-to-webp] ERROR: rasterization failed (" + (err && err.message ? err.message : err) + ")");
  console.log("[svg-to-webp] widget textures are required (no runtime fallback) -- install node and ensure npm can install the 'sharp' package (tools/assets/), then rebuild");
  process.exit(1);
});
