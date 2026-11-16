// Renders a few sweaters through the shared renderer and the CoreGraphics
// shim, checks basic invariants, and writes PNGs for a visual check:
//
//   render-test.exe [output-folder]
//
// Exit status is non-zero if any invariant fails.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/misc/knit.h"
#include "src/misc/chart.h"
#include "src/misc/apps.h"

float knit_current_width(void) { return 12.f; }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint32_t px_at(CGContextRef c, int x, int y_top) {
  uint32_t* d = CGBitmapContextGetData(c);
  return d[(size_t)y_top * CGBitmapContextGetWidth(c) + x];
}

static float g_band = 12.f, g_ww = 420.f, g_wh = 260.f;

static void render(const char* dir, const char* app, float scale, float grey, float dim) {
  const float band = g_band, radius = 8.f, tuck = 14.f;
  const float ww = g_ww, wh = g_wh;
  int W = (int)((ww + 2 * band + 16) * scale), H = (int)((wh + 2 * band + 16) * scale);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef c = CGBitmapContextCreate(NULL, W, H, 8, 0, cs,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(cs);
  CHECK(c != NULL, "context for %s", app);
  if (!c) return;
  CGContextScaleCTM(c, scale, scale);
  CGRect win = CGRectMake(band + 8, band + 8, ww, wh);

  uint32_t yarn = knit_color_for_app(app);
  const struct app_rule* rule = knit_app_rule(app);
  if (rule) yarn = rule->color;
  int chart = knit_pattern_for_app(app);
  knit_draw_faded(c, win, radius, band, yarn, chart, dim, grey, tuck);

  // The middle of the window is untouched; the middle of each band is opaque.
  int cx = W / 2, cy = H / 2;
  CHECK(px_at(c, cx, cy) == 0, "%s: window interior painted", app);
  int band_mid = (int)((8 + band * 0.5f) * scale);
  CHECK((px_at(c, cx, band_mid) >> 24) == 255, "%s: top band not opaque", app);
  CHECK((px_at(c, cx, H - 1 - band_mid) >> 24) == 255, "%s: bottom band not opaque", app);
  CHECK((px_at(c, band_mid, cy) >> 24) == 255, "%s: left band not opaque", app);
  CHECK((px_at(c, W - 1 - band_mid, cy) >> 24) == 255, "%s: right band not opaque", app);
  CHECK(px_at(c, 1, 1) == 0, "%s: outside the band painted", app);

  if (grey >= 1.f) {
    uint32_t p = px_at(c, cx, band_mid);
    int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    CHECK(abs(r - g) <= 2 && abs(g - b) <= 2, "%s: grey band still coloured (%d,%d,%d)", app, r, g, b);
  }

  if (dir) {
    // Lay a plain window over the knit, as on screen: the tuck goes under it.
    CGMutablePathRef window = CGPathCreateMutable();
    CGPathAddRoundedRect(window, NULL, win, radius, radius);
    CGContextAddPath(c, window);
    CGContextSetRGBFillColor(c, 0.97, 0.97, 0.96, 1);
    CGContextFillPath(c);
    CFRelease(window);
    char path[1024], name[128];
    snprintf(name, sizeof name, "%s", app);
    for (char* p = name; *p; p++) if (*p == ' ') *p = '-';
    snprintf(path, sizeof path, "%s\\%s%s@%gx%s.png", dir, name, g_ww < 200 ? "-zoom" : "", scale,
             grey > 0 ? "-grey" : dim > 0 ? "-dim" : "");
    CGImageRef img = CGBitmapContextCreateImage(c);
    CHECK(cg_image_write_png(img, path), "write %s", path);
    CGImageRelease(img);
  }
  CGContextRelease(c);
}

// Geometry checks of the rasterizer itself, independent of the renderer.
static void shim_checks(void) {
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef c = CGBitmapContextCreate(NULL, 40, 20, 8, 0, cs, kCGImageAlphaPremultipliedFirst);
  // y-up: a rect at the bottom of user space lands in the last memory rows.
  CGContextSetRGBFillColor(c, 1, 0, 0, 1);
  CGContextFillRect(c, CGRectMake(0, 0, 40, 5));
  CHECK(px_at(c, 10, 19) == 0xffff0000u, "fill bottom row (got %08x)", px_at(c, 10, 19));
  CHECK(px_at(c, 10, 0) == 0, "top row untouched");
  // Half-pixel edges antialias to roughly half coverage.
  CGContextSetRGBFillColor(c, 0, 0, 1, 1);
  CGContextFillRect(c, CGRectMake(10.5, 10, 5, 5));
  uint32_t edge = px_at(c, 10, 7);
  CHECK((edge >> 24) > 100 && (edge >> 24) < 160, "half-covered pixel alpha %u", edge >> 24);
  // Even-odd ring leaves a hole.
  CGContextSetRGBFillColor(c, 0, 1, 0, 1);
  CGMutablePathRef ring = CGPathCreateMutable();
  CGPathAddRect(ring, NULL, CGRectMake(20, 6, 14, 12));
  CGPathAddRect(ring, NULL, CGRectMake(24, 9, 6, 6));
  CGContextAddPath(c, ring);
  CGContextEOClip(c);
  CFRelease(ring);
  CGContextFillRect(c, CGRectMake(0, 0, 40, 20));
  CHECK(px_at(c, 21, 7) == 0xff00ff00u, "ring filled");
  CHECK(px_at(c, 27, 8) == 0, "ring hole stays empty");
  CGContextRelease(c);

  // Tiled image: a 2x2 checker repeats.
  uint32_t tile[4] = { 0xffffffffu, 0xff000000u, 0xff000000u, 0xffffffffu };
  CGImageRef img = CGImageCreateWithPixels(tile, 2, 2);
  c = CGBitmapContextCreate(NULL, 8, 8, 8, 0, cs, kCGImageAlphaPremultipliedFirst);
  CGContextSetInterpolationQuality(c, kCGInterpolationNone);
  CGContextDrawTiledImage(c, CGRectMake(0, 0, 2, 2), img);
  CHECK(px_at(c, 0, 0) == 0xffffffffu && px_at(c, 1, 0) == 0xff000000u
        && px_at(c, 2, 0) == 0xffffffffu && px_at(c, 1, 1) == 0xffffffffu, "tiled checker");
  CGImageRelease(img);
  CGContextRelease(c);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : NULL;
  shim_checks();
  knit_charts_load(NULL);
  CHECK(g_chart_count > 30, "built-in charts loaded (%d)", g_chart_count);

  const char* apps[] = { "Claude", "Spotify", "Visual Studio Code", "Google Chrome",
                         "Microsoft Word", "Discord", "Notes", "Slack", "Some Other App" };
  for (size_t i = 0; i < sizeof apps / sizeof apps[0]; i++) render(dir, apps[i], 1.f, 0, 0);
  render(dir, "Claude", 1.5f, 0, 0);
  render(dir, "Claude", 2.f, 0, 0);
  // Close-ups at the catalogue's 10 pt, for comparing stitches with docs/collection.
  g_band = 10.f; g_ww = 150.f; g_wh = 70.f;
  render(dir, "Claude", 4.f, 0, 0);
  render(dir, "Spotify", 4.f, 0, 0);
  g_band = 12.f; g_ww = 420.f; g_wh = 260.f;
  render(dir, "Spotify", 1.f, 1.f, 0);
  render(dir, "Spotify", 1.f, 0, KNIT_INACTIVE_DIM);

  // Plain knitting in each stitch, and a global pattern.
  knit_pattern_select("none");
  for (int s = 0; s < KNIT_STITCH_COUNT; s++) {
    g_knit_stitch = s;
    knit_flush_cache();
    char name[64];
    snprintf(name, sizeof name, "plain-%s", g_knit_stitch_names[s]);
    render(dir, name, 1.f, 0, 0);
  }
  g_knit_stitch = KNIT_STOCKINETTE;
  knit_pattern_select("zigzag");
  knit_flush_cache();
  render(dir, "zigzag-Slack", 1.f, 0, 0);

  if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
  printf("render-test: all checks passed\n");
  return 0;
}
