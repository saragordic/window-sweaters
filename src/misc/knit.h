#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include "drawing.h"

// Knitted window borders.
//
// The band is the ring between the window's rounded rect and that rect grown
// by `band`. Because the ring is a clip path built from the window's *real*
// corner radius, the knit follows the window's rounding exactly.
//
// The stitch pattern is strictly periodic, so it is rendered once into a small
// seamless tile and then tiled into the ring. This matters enormously:
// stroking the stitches directly costs ~1.5s for a full-screen window, because
// CoreGraphics has a large per-subpath overhead and tens of thousands of tiny
// curves defeat it. Tiling turns every redraw into a single DrawTiledImage.
//
// State lives in knit.c, not here: these are shared by the renderer and the
// menu bar, so they must be one object rather than one per translation unit.

enum knit_stitch { KNIT_STOCKINETTE = 0, KNIT_RIB, KNIT_GARTER, KNIT_STITCH_COUNT };

struct knit_gauge {
  float rows;        // chart rows across the band (plain knitting: stitch rows).
                     // A chart N rows tall needs at least N here to read.
                     // Colourwork cells contain 2x2 finer fabric stitches.
  float aspect;      // stitch width / row height; real stockinette is ~1.35
  float row_overlap; // how far each row nests into the one above (0..1)
  float yarn;        // yarn thickness, as a fraction of stitch width
  float bow;         // how much the arms of the stitch bow outward (0..1)
  float jitter;      // hand-knit wobble, as a fraction of stitch width
  float ground;      // brightness of the wool behind the stitches
  float shadow;      // brightness of the shadow cast under each stitch
  float light;       // brightness of the yarn itself
  float depth;       // how far the shadow is offset, in yarn widths
  float ambient;     // light present everywhere; raise it to keep hues vivid
  float relief;      // how steeply the yarn stands up under the light
  float sheen;       // strength of the directional light on the yarn
  float tuck;        // how far the knit continues UNDER the window edge.
                     // Hidden while still; during a drag the border window
                     // lags the real one by a frame, and this is what stops
                     // bare desktop showing through on the trailing edge.
};

struct knit_basket { const char* name; const uint32_t* colors; int len; };

extern struct knit_gauge g_knit;
extern int g_knit_stitch;       // enum knit_stitch
extern int g_knit_basket;       // index into g_knit_baskets

// Where each unpatched side's pattern repeat is anchored.
// Solid-patch profiles fit complete repeats between fixed equal caps.
// Symmetric fitted-repeat profiles fit the full edge for matching mitres.
//
//   KNIT_ANCHOR_CORNER  cast on at the mitre corner. Rock steady while the
//                       window resizes, because the anchor does not depend on
//                       the side's length. The far end takes a partial repeat.
//   KNIT_ANCHOR_CENTRE  the repeat is centred on each side, so both ends match.
//                       Composed, but it must slide at half the resize rate,
//                       because centring is defined by the length that changes.
enum knit_anchor { KNIT_ANCHOR_CORNER = 0, KNIT_ANCHOR_CENTRE };
extern int g_knit_anchor;
extern const struct knit_basket g_knit_baskets[];
extern const int g_knit_basket_count;
extern const char* g_knit_stitch_names[];
extern bool g_knit_on;              // master on/off, driven by the menu bar

float knit_current_width(void);     // defined in main.c

uint32_t knit_color_for_window(uint32_t wid);
// Stable fallback for an app without a curated colourway.
uint32_t knit_color_for_app(const char* app);

/// The shared Zigzag's contrast yarn for a main yarn: the collection's cream,
/// or, on a pale yarn (HSL lightness >= 0.62), a deeper shade of that yarn.
#define KNIT_CREAM 0xfff6f0deu
uint32_t knit_zigzag_contrast(uint32_t base);

/// Draw a knitted band in the ring between `win` (the window rect, in border
/// window coordinates) and that rect grown by `band`.
///
/// radius  the window's real corner radius
/// color   base yarn colour (ARGB)
/// dim     darkening of unfocused windows (the yarn remains opaque)
void knit_draw(CGContextRef ctx, CGRect win, float radius, float band,
               uint32_t color, int chart, float dim, float tuck);

/// How much an unfocused window's band is darkened, 0..1. This darkens rather
/// than fading: the band stays fully opaque at any value, so the desktop never
/// shows through it. 0 makes every window identical.
extern float g_knit_dim;

/// Discard every cached tile. Call after changing the gauge, stitch or basket.
void knit_flush_cache(void);
