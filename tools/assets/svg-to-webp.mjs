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
// Non-fatal by design: ANY failure (node missing sharp prebuilt for this
// platform/arch, npm install failing, no network, etc.) prints a single
// ASCII warning line and exits 0. Widgets then fall back to placeholder
// art (see SettingsScreen.cpp LoadOrPlaceholder) -- this must never fail
// the build.
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
  combo_bar: [128, 32],
  _dialog_box: [128, 16],
  slider_will: [32, 32],
  expand_arrow: [32, 32],
  vbar: [32, 128],
  vslider: [32, 64],
  arrow: [32, 32],
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

  for (const [name, [w, h]] of Object.entries(MANIFEST)) {
    const svgPath = path.join(svgDir, name + ".svg");
    const outPath = path.join(outDir, name + ".tex");

    if (!existsSync(svgPath)) {
      console.log("[svg-to-webp] WARNING: missing source SVG " + svgPath + " -- skipping");
      continue;
    }

    if (existsSync(outPath)) {
      const svgMtime = statSync(svgPath).mtimeMs;
      const outMtime = statSync(outPath).mtimeMs;
      if (outMtime >= svgMtime) {
        skipped++;
        continue;
      }
    }

    const tmpPath = outPath + ".tmp.webp";
    await sharp(svgPath, { density: DENSITY })
      .resize(w, h, { fit: "fill" })
      .webp({ lossless: true })
      .toFile(tmpPath);
    renameSync(tmpPath, outPath);

    console.log("[svg-to-webp] generated " + name + ".tex (" + w + "x" + h + ")");
    generated++;
  }

  console.log("[svg-to-webp] " + generated + " generated, " + skipped + " up to date");
}

main().catch((err) => {
  console.log("[svg-to-webp] WARNING: rasterization failed (" + (err && err.message ? err.message : err) + ") -- widgets will fall back to placeholder art");
  process.exit(0);
});
