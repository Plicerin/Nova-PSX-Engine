// Software rasterizer (spec 5.2-5.7). Draws one primitive packet into the
// framebuffer, honoring the global authenticity toggles:
//   - affine UV interpolation by default; perspective-correct only when
//     g_config.perspective_correct (debug)
//   - nearest-neighbor sampling by default; bilinear when g_config.bilinear_filter
//   - dither+quantize applied per pixel via Fb_Put
//   - optional debug z-buffer test/write when g_config.zbuffer
//   - wireframe mode draws triangle edges as lines
//
// Vertex coords arrive in subpixel units with subpix fractional bits
// (g_config.EffectiveSubpix()). The rasterizer converts to 16.16 internally.
//
// Fill rule: scanline edge-walking. For each scanline y (integer pixel
// centers), spans cover x in [ceil(xl - 0.5), ceil(xr - 0.5)) — i.e. a pixel
// is filled if its center lies in [xl, xr). Deterministic, no double-drawn
// shared edges, no gaps between adjacent triangles.
//
// Texturing rules (PS1):
//   - texel value 0x0000 is fully transparent (skipped) for textured prims
//   - texel STP bit (0x8000) marks that texel semi-transparent when the prim
//     has PF_SEMITRANS; texels without STP draw opaque even on semi-trans prims
//   - modulation: out = (texel8 * vertcolor) >> 7, clamped (128 = neutral)
//   - untextured semi-trans prims blend all pixels
//   - UVs wrap by power-of-two mask (texture w/h are enforced pow2)
#pragma once
#include "engine/core/types.h"
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/prim.h"

struct RasterStats {
    u32 prims_drawn;
    u32 tris_drawn;
    u32 pixels_filled;
};

extern RasterStats g_raster_stats;

void Raster_ResetStats();

// Draw one primitive. fb must match the current internal resolution; the
// z-buffer (g_zbuffer) is used only when g_config.zbuffer is on.
void Raster_DrawPrim(Framebuffer* fb, const Prim* p);

// Flat line helper, also used by debug overlay. Coords in PIXELS (not subpixel).
void Raster_LinePx(Framebuffer* fb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b);
