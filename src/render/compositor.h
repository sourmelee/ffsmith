#pragma once
#include <string>
#include "data/bundle.h"

namespace ffsmith {

// Compose a baked map into an RGBA framebuffer, mirroring the toolkit's
// ExtractTab._render_android_map EXACTLY:
//   - slot dispatch on the tile-word high byte (0 -> slot0, 1 -> slot1),
//   - zero-skip when the tile word is 0x0000,
//   - TS = 32 if the slot sheet width >= 512 else 16,
//   - tcols = sheet_w / TS, tile at (tile%tcols, tile//tcols),
//   - alpha-composite over an opaque-black background (PIL div255 rounding).
// `bundle_dir` is the bundle root (must contain tex/). Returns an invalid
// Texture on failure.
Texture compose_map(const std::string& bundle_dir, const FfMap& map);

}  // namespace ffsmith
