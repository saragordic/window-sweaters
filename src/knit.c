#include "misc/knit.h"
#include "misc/chart.h"
#include <pthread.h>
#include <string.h>
#include <math.h>

struct knit_gauge g_knit = {
  .rows = 6.f, .aspect = 1.35f, .row_overlap = 0.12f,
  .yarn = 0.48f, .bow = 0.28f, .jitter = 0.035f,
  .ground = 0.98f, .shadow = 0.74f, .light = 1.18f, .ambient = 0.94f, .depth = 1.0f,
  .relief = 0.70f, .sheen = 0.10f,
  .tuck = 14.f,
};

int g_knit_stitch = KNIT_STOCKINETTE;
int g_knit_basket = 3;
int g_knit_anchor = KNIT_ANCHOR_CORNER;
float g_knit_dim = 0.f;   // unfocused windows look identical by default
bool g_knit_on = true;
int knit_tile_scale_override = 0;   // 0 = auto
int knit_interp_override = -1;      // -1 = high
int knit_mitre_off = 0;            // 1 = rectangular strips instead of mitred

const char* g_knit_stitch_names[] = { "stockinette", "rib", "garter" };

// Baskets of wool. Windows are assigned a colour by hashing their window id,
// so a given window keeps its colour for life and neighbours rarely collide.
static const uint32_t basket_wool[] = {
  0xffd1495b, 0xff4e8098, 0xffedae49, 0xff7c9885, 0xff9b6a8f,
  0xffe8846b, 0xff3f7d6e, 0xffc46a4e, 0xff6d7ba8, 0xffb8935f,
};
static const uint32_t basket_sorbet[] = {
  0xfff08ca4, 0xff7fc6c4, 0xffffc978, 0xffb08ed4, 0xff8fce90,
  0xffff9f7a, 0xff8ab6f0, 0xffe5a3d0,
};
static const uint32_t basket_forest[] = {
  0xff4a6b52, 0xff7d8f5c, 0xff3f6b6e, 0xff8a7248, 0xff5c6b8a,
  0xff6b5344, 0xff2f5e4a,
};
static const uint32_t basket_mono[] = {
  0xff9aa0a6, 0xff7c8288, 0xffb4bac0, 0xff686e74, 0xff8d939a,
};

// Full-chroma hues held at maximum separation around the wheel, so adjacent
// windows never read as the same colour. Deliberately a rainbow palette: the
// per-window colour coding is multi-hue by definition.
static const uint32_t basket_dopamine[] = {
  0xffff2d95, 0xff00d9ff, 0xffffd400, 0xff7c3aff, 0xff00e676,
  0xffff6b00, 0xffff1744, 0xff00b8d4,
};
static const uint32_t basket_neon[] = {
  0xfff50057, 0xff00e5ff, 0xffc6ff00, 0xff651fff, 0xff1de9b6, 0xffff9100,
};
static const uint32_t basket_punch[] = {
  0xffe8175d, 0xff0fb9b1, 0xfffec230, 0xff5f27cd, 0xff10ac84, 0xffee5a24,
};

const struct knit_basket g_knit_baskets[] = {
  { "dopamine", basket_dopamine, sizeof(basket_dopamine) / 4 },
  { "neon",     basket_neon,     sizeof(basket_neon)     / 4 },
  { "punch",    basket_punch,    sizeof(basket_punch)    / 4 },
  { "wool",   basket_wool,   sizeof(basket_wool)   / 4 },
  { "sorbet", basket_sorbet, sizeof(basket_sorbet) / 4 },
  { "forest", basket_forest, sizeof(basket_forest) / 4 },
  { "mono",   basket_mono,   sizeof(basket_mono)   / 4 },
};
const int g_knit_basket_count = sizeof(g_knit_baskets) / sizeof(g_knit_baskets[0]);

// murmur3 finalizer — spreads sequential window ids across the basket
static uint32_t knit_mix(uint32_t h) {
  h ^= h >> 16; h *= 0x85ebca6b;
  h ^= h >> 13; h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

uint32_t knit_color_for_window(uint32_t wid) {
  const struct knit_basket* b = &g_knit_baskets[g_knit_basket];
  return b->colors[knit_mix(wid) % b->len];
}

// Cream shows on every yarn but a pale one; there, a deeper shade of the
// yarn's own hue keeps the zigzag visible (Notion, Chrome's cream, Notes).
uint32_t knit_zigzag_contrast(uint32_t base) {
  double r = ((base >> 16) & 255) / 255.0, g = ((base >> 8) & 255) / 255.0, b = (base & 255) / 255.0;
  double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), l = (mx + mn) / 2, h = 0, s = 0;
  if (l < 0.62) return KNIT_CREAM;
  if (mx != mn) {
    double d = mx - mn;
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h /= 6;
  }
  l -= 0.26;
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, t[3] = {h + 1.0/3, h, h - 1.0/3}, o[3];
  for (int i = 0; i < 3; i++) {
    double c = t[i]; if (c < 0) c += 1; if (c > 1) c -= 1;
    o[i] = c < 1.0/6 ? p + (q - p) * 6 * c : c < 0.5 ? q : c < 2.0/3 ? p + (q - p) * (2.0/3 - c) * 6 : p;
  }
  return 0xff000000u | ((uint32_t)(o[0] * 255) << 16) | ((uint32_t)(o[1] * 255) << 8) | (uint32_t)(o[2] * 255);
}

uint32_t knit_color_for_app(const char* app) {
  // Stable across windows and launches, with no icon decoding or server query.
  // ASCII case folding matches the collection's usual process-name matching.
  uint32_t hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)(app ? app : ""); *p; p++) {
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    hash = (hash ^ c) * 16777619u;
  }
  return knit_color_for_window(hash);
}

// deterministic wobble in [0,1), periodic over the tile so it still wraps
static float knit_noise(int a, int b) {
  float s = sinf(a * 12.9898f + b * 78.233f) * 43758.5453f;
  return s - floorf(s);
}

static void knit_stroke_color(CGContextRef c, uint32_t argb, float f) {
  float a, r, g, b;
  colors_from_hex(argb, &a, &r, &g, &b);
  r *= f; g *= f; b *= f;
  if (r > 1.f) r = 1.f; if (g > 1.f) g = 1.f; if (b > 1.f) b = 1.f;
  CGContextSetRGBStrokeColor(c, r, g, b, a);
}

// Lay `ncols` x `nrows` stitches, plus a two-cell margin so the pattern wraps.
// Each row is one continuous strand, which is both faster to stroke and how
// knitting actually works.
static void knit_lay(CGMutablePathRef path, int ncols, int nrows,
                     float sw, float sh, float row_step, float dx, float dy) {
  float bx = sw * 0.5f * g_knit.bow;
  float jit = sw * g_knit.jitter;

  if (g_knit_stitch == KNIT_RIB) {
    // k1p1: vertical columns of wool, every other one sitting proud
    float h = (nrows + 4) * row_step;
    for (int i = -2; i <= ncols + 1; i++) {
      float x = i * sw + sw * 0.5f + dx;
      float w = (i & 1) ? sw * 0.16f : 0.f;   // purl columns pull inward
      CGPathMoveToPoint(path, NULL, x + w, -2 * row_step + dy);
      CGPathAddLineToPoint(path, NULL, x + w, h + dy);
    }
    return;
  }

  if (g_knit_stitch == KNIT_GARTER) {
    // horizontal ridges: every row a gentle wave, alternating phase
    for (int j = -2; j <= nrows + 1; j++) {
      float y = j * row_step + dy;
      float phase = (j & 1) ? sw * 0.5f : 0.f;
      CGPathMoveToPoint(path, NULL, -2 * sw + dx, y);
      for (int i = -2; i <= ncols + 1; i++) {
        float x = i * sw + phase + dx;
        CGPathAddQuadCurveToPoint(path, NULL, x + sw * 0.25f, y - sh * 0.30f,
                                              x + sw * 0.5f,  y);
        CGPathAddQuadCurveToPoint(path, NULL, x + sw * 0.75f, y + sh * 0.30f,
                                              x + sw,         y);
      }
    }
    return;
  }

  for (int j = -2; j <= nrows + 1; j++) {
    float y = j * row_step + dy;
    float jy = (knit_noise((j + nrows) % nrows, 3) - 0.5f) * jit * 2.f;
    CGPathMoveToPoint(path, NULL, -2 * sw + dx, y + jy);
    for (int i = -2; i <= ncols + 1; i++) {
      float x = i * sw + dx;
      float jx = (knit_noise((i + ncols) % ncols, (j + nrows) % nrows) - 0.5f) * jit * 2.f;
      CGPathAddQuadCurveToPoint(path, NULL, x + bx + jx,      y + sh * 0.55f + jy,
                                            x + sw*0.5f + jx, y + sh + jy);
      CGPathAddQuadCurveToPoint(path, NULL, x + sw - bx + jx, y + sh * 0.55f + jy,
                                            x + sw + jx,      y + jy);
    }
  }
}

// Internal cache key for single-colour knitted corner patches.
enum { KNIT_PATCH_CHART = -2 };

struct knit_tile { float band; uint32_t color; int chart; CGImageRef img; float w, h; };

// Render one seamless tile.
//
// Rather than faking depth with an offset dark copy, this builds a height
// field for the yarn, derives surface normals from it, and lights it. That is
// the flat-shading equivalent of the tangent/normal-mapped ply models used for
// knitwear in the rendering literature, and because the tile is built once and
// cached it costs nothing per frame.
//
// Three buffers, same size:
//   colour  — flat yarn colour per pixel (ground, then each yarn)
//   height  — the ridge each strand of wool stands up in
//   out     — colour lit by the normals derived from height
//
// Every lookup wraps, so the tile stays seamless after blurring and after the
// gradient used for the normals.

#define KNIT_LAYERS 5

// Lay the ridge profile: the same path stroked from wide+low to narrow+high,
// which is a stepped approximation of a round strand of wool.
static void knit_ridge(CGContextRef h, CGMutablePathRef p, float yarn) {
  static const float w[KNIT_LAYERS] = { 1.00f, 0.80f, 0.60f, 0.40f, 0.20f };
  static const float v[KNIT_LAYERS] = { 0.22f, 0.48f, 0.70f, 0.88f, 1.00f };
  for (int i = 0; i < KNIT_LAYERS; i++) {
    CGContextSetGrayStrokeColor(h, v[i], 1.0);
    CGContextSetLineWidth(h, yarn * w[i]);
    CGContextAddPath(h, p);
    CGContextStrokePath(h);
  }
}

// separable 3-tap blur, wrapping — smooths the stepped ridge into a round one
static void knit_blur(float* a, int w, int hgt, float* tmp) {
  for (int y = 0; y < hgt; y++)
    for (int x = 0; x < w; x++) {
      int l = (x - 1 + w) % w, r = (x + 1) % w;
      tmp[y*w + x] = (a[y*w + l] + 2.f*a[y*w + x] + a[y*w + r]) * 0.25f;
    }
  for (int y = 0; y < hgt; y++)
    for (int x = 0; x < w; x++) {
      int u = (y - 1 + hgt) % hgt, d = (y + 1) % hgt;
      a[y*w + x] = (tmp[u*w + x] + 2.f*tmp[y*w + x] + tmp[d*w + x]) * 0.25f;
    }
}

// A compact fabric shader for colourwork. It shades fine yarn directly into
// the cached tile, avoiding thousands of CoreGraphics paths at first paint.
// Chart dimensions stay fixed while each profile chooses its stitch density.
static struct knit_tile knit_make_fabric(float band, uint32_t color, int chart,
                                        float sw, float row_step, int tw, int th, int scale) {
  const struct knit_chart* ch = &g_charts[chart];
  int width = tw * scale, height = th * scale;
  uint32_t* pixels = malloc((size_t)width * height * sizeof *pixels);
  if (!pixels) return (struct knit_tile){band, color, chart, NULL, tw, th};
  // Pattern dimensions stay fixed. Quiet stripes/checks need more readable
  // yarn; intricate motifs keep their finer texture rather than changing globally.
  float stitch_scale = ch->defined_yarn ? 2.f : 1.f;
  float relief = fminf(ch->defined_yarn ? 0.30f : 0.25f, fmaxf(0.f, g_knit.relief * (ch->defined_yarn ? 0.32f : 0.20f)));
  for (int y = 0; y < height; y++) {
    float sy = (y + 0.5f) / (scale * row_step);
    float stitch_y = sy / stitch_scale;
    int row = (int)floorf(stitch_y);
    float v = stitch_y - row;
    for (int x = 0; x < width; x++) {
      float sx = (x + 0.5f) / (scale * sw);
      int column = (int)floorf(sx);
      float stitch_x = sx / stitch_scale;
      int yarn_column = (int)floorf(stitch_x);
      float u = stitch_x - yarn_column;
      // Let the row boundary follow the small stitch head, avoiding a
      // perfectly square pixel-art edge between contrasting yarn colours.
      // Dot masks supply their own curved boundary. Warping their row
      // lookup separately splits the dot at its middle stitch seam.
      float color_warp = ch->round_dots ? 0.f : 0.16f * (1.f - 2.f * fabsf(2.f * u - 1.f));
      int color_row = (int)floorf(sy + color_warp);
      color_row = (color_row % (ch->h * 2) + ch->h * 2) % (ch->h * 2);
      uint32_t yarn = ch->px[(size_t)(color_row / 2) * ch->w + (column / 2) % ch->w];
      if (ch->round_dots && (yarn >> 24) >= 128) {
        int cx = (column / 2) % ch->w, cy = color_row / 2;
        bool left = ch->px[(size_t)cy * ch->w + (cx + ch->w - 1) % ch->w] == yarn;
        bool above = ch->px[(size_t)((cy + ch->h - 1) % ch->h) * ch->w + cx] == yarn;
        float dx = sx * 0.5f - floorf(sx * 0.5f) + (left ? 1.f : 0.f) - 1.f;
        float dy = sy * 0.5f - floorf(sy * 0.5f) + (above ? 1.f : 0.f) - 1.f;
        if (dx * dx + dy * dy > 0.94f) yarn = color;
      }
      if ((yarn >> 24) < 128) yarn = color;
      uint32_t seed = knit_mix((uint32_t)yarn_column * 73856093u ^ (uint32_t)row * 19349663u);
      float wobble = ((seed & 255u) / 255.f - 0.5f) * g_knit.jitter;
      // Rounded, nested stitch legs, with no hard highlight or black crevice.
      float leg = 0.42f * (1.f - v) + 0.08f * v * (1.f - v);
      float distance = fabsf(fabsf(u - 0.5f - wobble) - leg);
      float radius = fmaxf(0.10f, g_knit.yarn * 0.5f);
      float ridge = fmaxf(0.f, 1.f - distance * distance / (radius * radius));
      uint32_t grain = knit_mix((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u);
      float fibre = ((grain & 255u) / 255.f - 0.5f) * 0.014f;
      float shade = g_knit.ground - relief * (1.f - ridge)
                    + (g_knit.ambient - 0.94f) + g_knit.sheen * 0.08f * (0.5f - u) + fibre;
      shade = fmaxf(0.f, shade);
      uint32_t r = (uint32_t)fminf(255.f, ((yarn >> 16) & 255u) * shade);
      uint32_t g = (uint32_t)fminf(255.f, ((yarn >> 8) & 255u) * shade);
      uint32_t b = (uint32_t)fminf(255.f, (yarn & 255u) * shade);
      pixels[(size_t)(height - 1 - y) * width + x] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
  }
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(pixels, width, height, 8, (size_t)width * 4, cs,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGImageRef image = context ? CGBitmapContextCreateImage(context) : NULL;
  if (context) CGContextRelease(context);
  CGColorSpaceRelease(cs);
  free(pixels);
  return (struct knit_tile){band, color, chart, image, tw, th};
}

static struct knit_tile knit_make_tile(float band, uint32_t color, int chart) {
  const struct knit_chart* ch = chart >= 0 && chart < g_chart_count ? &g_charts[chart] : NULL;
  bool sculpted = chart == KNIT_PATCH_CHART || (ch && ch->sculpted_yarn);
  struct knit_gauge material = g_knit;
  if (sculpted) {
    // Raised yarn material. Geometry/gauge stays independent of the colour chart.
    material.row_overlap = 0.05f; material.yarn = 0.40f;
    material.ground = 0.94f; material.ambient = 0.84f;
    material.relief = 1.65f; material.sheen = 0.28f;
  }
  float rows = material.rows; if (rows < 1.5f) rows = 1.5f;
  float row_step = band / rows;
  float sh = row_step / (1.f - material.row_overlap);
  float sw = row_step * material.aspect;

  int ncols, nrows;
  if (ch) {
    // One chart is already a complete repeat. Expanding it to ~96 points
    // repeats the expensive path/height work without adding any detail.
    // Each authored colour cell spans 2x2 sampling units. The motif and
    // border retain their size; fabric detail no longer dictates dot size.
    int density = sculpted ? 1 : 2;
    sw /= density; row_step /= density;
    ncols = ch->w * density; nrows = ch->h * density;
    // Very small user charts still need enough pixels for the ridge filter.
    ncols *= (int)fmaxf(1.f, ceilf(8.f / (sw * ncols)));
    nrows *= (int)fmaxf(1.f, ceilf(8.f / (row_step * nrows)));
  } else {
    // Enough variation to feel hand-knit; even counts preserve rib/garter.
    ncols = 8; nrows = 8;
  }
  int tw = (int)lroundf(sw * ncols), th = (int)lroundf(row_step * nrows);
  if (tw < 8) tw = 8; if (th < 8) th = 8;
  sw = (float)tw / ncols;
  row_step = (float)th / nrows;
  sh = row_step / (1.f - material.row_overlap);
  float yarn = sw * material.yarn; yarn = fmaxf(yarn, sculpted ? 0.8f : 0.35f);
  // Colourwork always uses stockinette, regardless of the last plain choice.
  if (!ch && !sculpted && g_knit_stitch == KNIT_RIB) yarn = sw * 0.55f;

  int S = knit_tile_scale_override ? knit_tile_scale_override
                                   : (row_step < 4.f ? 4 : 2);
  if (ch && !sculpted) return knit_make_fabric(band, color, chart, sw, row_step, tw, th, S);
  int W = tw * S, H = th * S;

  // ---- the paths, one per yarn colour ------------------------------------
  uint32_t cols[16]; CGMutablePathRef paths[16]; int nc = 0;
  if (sculpted) {
    float bx = sw * 0.5f * material.bow;
    for (int j = -2; j <= nrows + 1; j++)
      for (int i = -2; i <= ncols + 1; i++) {
        uint32_t px = 0;
        if (ch) {
          int cx = (i % ch->w + ch->w) % ch->w, cy = (j % ch->h + ch->h) % ch->h;
          px = ch->px[(size_t)cy * ch->w + cx];
        }
        uint32_t yc = (px >> 24) < 128 ? color : (px | 0xff000000u);
        int k = 0; while (k < nc && cols[k] != yc) k++;
        if (k == nc) {
          if (nc == 16) continue;
          cols[nc] = yc; paths[nc++] = CGPathCreateMutable();
        }
        float x = i * sw, y = j * row_step;
        CGPathMoveToPoint(paths[k], NULL, x, y);
        CGPathAddQuadCurveToPoint(paths[k], NULL, x + bx, y + sh * 0.55f, x + sw * 0.5f, y + sh);
        CGPathAddQuadCurveToPoint(paths[k], NULL, x + sw - bx, y + sh * 0.55f, x + sw, y);
      }
  } else {
    cols[0] = color; paths[0] = CGPathCreateMutable(); nc = 1;
    knit_lay(paths[0], ncols, nrows, sw, sh, row_step, 0.f, 0.f);
  }

  CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();

  // ---- flat colour --------------------------------------------------------
  uint32_t* cbuf = calloc((size_t)W * H, 4);
  CGContextRef cc = CGBitmapContextCreate(cbuf, W, H, 8, (size_t)W*4, rgb,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGContextScaleCTM(cc, S, S);
  float a, r, g, b;
  colors_from_hex(color, &a, &r, &g, &b);
  CGContextSetRGBFillColor(cc, r * material.ground, g * material.ground,
                           b * material.ground, 1.f);
  CGContextFillRect(cc, CGRectMake(0, 0, tw, th));
  CGContextSetLineCap(cc, kCGLineCapRound);
  CGContextSetLineJoin(cc, kCGLineJoinRound);
  CGContextSetLineWidth(cc, yarn);
  for (int k = 0; k < nc; k++) {
    colors_from_hex(cols[k], &a, &r, &g, &b);
    CGContextSetRGBStrokeColor(cc, r, g, b, 1.f);
    CGContextAddPath(cc, paths[k]); CGContextStrokePath(cc);
  }
  CGContextRelease(cc);

  // ---- height -------------------------------------------------------------
  uint8_t* hbuf = calloc((size_t)W * H, 1);
  CGContextRef hc = CGBitmapContextCreate(hbuf, W, H, 8, (size_t)W, gray,
                                          kCGImageAlphaNone);
  CGContextScaleCTM(hc, S, S);
  CGContextSetGrayFillColor(hc, 0.f, 1.f);
  CGContextFillRect(hc, CGRectMake(0, 0, tw, th));
  CGContextSetLineCap(hc, kCGLineCapRound);
  CGContextSetLineJoin(hc, kCGLineJoinRound);
  for (int k = 0; k < nc; k++) knit_ridge(hc, paths[k], yarn);
  CGContextRelease(hc);
  for (int k = 0; k < nc; k++) CFRelease(paths[k]);

  float* hf  = malloc(sizeof(float) * W * H);
  float* tmp = malloc(sizeof(float) * W * H);
  for (int i = 0; i < W * H; i++) hf[i] = hbuf[i] / 255.f;
  knit_blur(hf, W, H, tmp);
  knit_blur(hf, W, H, tmp);
  free(tmp); free(hbuf);

  // ---- light it -----------------------------------------------------------
  // from the upper left, the convention that reads as raised rather than sunken
  const float lx = -0.45f, ly = -0.55f, lz = 0.70f;
  float relief = S * material.relief;   // gradient is per-pixel, so scale with S

  uint32_t* obuf = malloc((size_t)W * H * 4);
  for (int y = 0; y < H; y++) {
    int up = (y - 1 + H) % H, dn = (y + 1) % H;
    for (int x = 0; x < W; x++) {
      int lft = (x - 1 + W) % W, rgt = (x + 1) % W;
      float dzdx = (hf[y*W + rgt] - hf[y*W + lft]) * relief;
      float dzdy = (hf[dn*W + x]  - hf[up*W + x])  * relief;
      float nx = -dzdx, ny = -dzdy, nz = 1.f;
      float inv = 1.f / sqrtf(nx*nx + ny*ny + 1.f);
      float ndl = (nx*lx + ny*ly + nz*lz) * inv;
      if (ndl < 0.f) ndl = 0.f;

      // ambient, diffuse, and a little occlusion in the gaps between strands
      float occ = sculpted ? 0.74f + 0.26f * hf[y*W + x]
                           : 0.86f + 0.14f * hf[y*W + x];
      // Fixed, very quiet fibre variation: generated only on a cache miss.
      uint32_t grain = knit_mix((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u);
      float fibre = sculpted ? 1.f : 0.993f + (grain & 255u) * (0.014f / 255.f);
      float shade = (material.ambient + material.sheen * ndl) * occ * fibre;

      uint32_t p = cbuf[y*W + x];
      // Very dark wool still needs a visible strand highlight. Multiplication
      // alone loses almost all relief when a yarn is close to black.
      float peak = fmaxf(p & 255, fmaxf((p >> 8) & 255, (p >> 16) & 255));
      float lift = sculpted ? fmaxf(0.f, 1.f - peak / 48.f) * hf[y*W + x] * 10.f : 0.f;
      float cb = (p & 0xff) * shade + lift;
      float cg = ((p >> 8) & 0xff) * shade + lift;
      float cr = ((p >> 16) & 0xff) * shade + lift;
      if (cr > 255.f) cr = 255.f;
      if (cg > 255.f) cg = 255.f;
      if (cb > 255.f) cb = 255.f;
      obuf[y*W + x] = 0xff000000u | ((uint32_t)cr << 16)
                    | ((uint32_t)cg << 8) | (uint32_t)cb;
    }
  }
  free(hf); free(cbuf);

  CGContextRef oc = CGBitmapContextCreate(obuf, W, H, 8, (size_t)W*4, rgb,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  struct knit_tile t = { band, color, chart,
                         CGBitmapContextCreateImage(oc), (float)tw, (float)th };
  CGContextRelease(oc);
  free(obuf);
  CGColorSpaceRelease(rgb);
  CGColorSpaceRelease(gray);
  return t;
}

#define KNIT_CACHE_LEN 64
static struct knit_tile g_cache[KNIT_CACHE_LEN];
static int g_cache_n = 0;
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

void knit_flush_cache(void) {
  pthread_mutex_lock(&g_cache_lock);
  for (int i = 0; i < g_cache_n; i++) CGImageRelease(g_cache[i].img);
  g_cache_n = 0;
  pthread_mutex_unlock(&g_cache_lock);
}

static struct knit_tile knit_get_tile(float band, uint32_t color, int chart) {
  band = roundf(band * 2.f) / 2.f;
  pthread_mutex_lock(&g_cache_lock);
  for (int i = 0; i < g_cache_n; i++) {
    if (g_cache[i].band == band && g_cache[i].color == color
        && g_cache[i].chart == chart) {
      struct knit_tile t = g_cache[i];
      CGImageRetain(t.img); // caller owns its image across cache flush/eviction
      pthread_mutex_unlock(&g_cache_lock);
      return t;
    }
  }
  struct knit_tile t = knit_make_tile(band, color, chart);
  if (!t.img) { pthread_mutex_unlock(&g_cache_lock); return t; }
  if (g_cache_n == KNIT_CACHE_LEN) {              // full: drop the oldest
    CGImageRelease(g_cache[0].img);
    memmove(g_cache, g_cache + 1, sizeof(struct knit_tile) * (KNIT_CACHE_LEN - 1));
    g_cache_n--;
  }
  g_cache[g_cache_n++] = t;
  CGImageRetain(t.img);
  pthread_mutex_unlock(&g_cache_lock);
  return t;
}

void knit_draw(CGContextRef ctx, CGRect win, float radius, float band,
               uint32_t color, int chart, float dim, float tuck) {
  if (!ctx || !isfinite(band) || !isfinite(radius) || !isfinite(tuck)
      || !isfinite(win.origin.x) || !isfinite(win.origin.y)
      || !isfinite(win.size.width) || !isfinite(win.size.height)
      || win.size.width <= 0 || win.size.height <= 0) return;
  if (band < 2.f) band = 2.f;
  radius = fmaxf(0.f, fminf(radius, fminf(win.size.width, win.size.height) * 0.5f));
  if (tuck < 1.f) tuck = 1.f;
  // never let the tuck swallow a small window
  float half = (win.size.width < win.size.height ? win.size.width
                                                 : win.size.height) * 0.5f - 2.f;
  if (tuck > half) tuck = half > 1.f ? half : 1.f;

  CGRect outer = CGRectInset(win, -band, -band);
  CGRect inner = CGRectInset(win, tuck, tuck);
  float orad = radius + band;
  float irad = radius > tuck ? radius - tuck : 0.f;

  CGContextSaveGState(ctx);

  // clip to the ring — this is what gives correct rounded corners for free
  CGMutablePathRef ring = CGPathCreateMutable();
  CGPathAddRoundedRect(ring, NULL, outer, orad, orad);
  CGPathAddRoundedRect(ring, NULL, inner, irad, irad);
  CGContextAddPath(ctx, ring);
  CGContextEOClip(ctx);
  CFRelease(ring);

  bool solid_corners = chart >= 0 && chart < g_chart_count && g_charts[chart].solid_corners;
  uint32_t corner_color = solid_corners && g_charts[chart].corner_color
                            ? g_charts[chart].corner_color : color;
  struct knit_tile t = knit_get_tile(band, color, chart);
  if (!t.img) { CGContextRestoreGState(ctx); return; }
  struct knit_tile cuff_tile = {0};
  CGMutablePathRef cuff_ring = NULL;
  if (chart >= 0 && chart < g_chart_count && g_charts[chart].cuff_color) {
    cuff_tile = knit_get_tile(band, g_charts[chart].cuff_color, KNIT_PATCH_CHART);
    if (cuff_tile.img) {
      float width = band / 3.f;
      cuff_ring = CGPathCreateMutable();
      CGPathAddRoundedRect(cuff_ring, NULL, outer, orad, orad);
      CGPathAddRoundedRect(cuff_ring, NULL, CGRectInset(outer, width, width),
                          orad - width, orad - width);
    }
  }
  struct knit_tile patch = {0};
  if (solid_corners) patch = knit_get_tile(band, corner_color, KNIT_PATCH_CHART);
  CGContextSetAlpha(ctx, 1.f);   // the band is always fully opaque
  CGContextSetInterpolationQuality(ctx, knit_interp_override < 0
                                   ? kCGInterpolationHigh
                                   : (CGInterpolationQuality)knit_interp_override);

  // Four separate pieces of knitting, mitred at the corners — the way a frame
  // would actually be made. Each side gets its own local space where +x runs
  // along that edge and +y runs inward, so the stitches follow the edge (the
  // sides come out rotated, as real knitting would be) and the pattern repeat
  // is centred on that side rather than inherited from a global grid.
  //
  // Drawing per side also keeps the painted area proportional to the band:
  // DrawTiledImage fills the clip's BOUNDING BOX, so one ring-shaped clip
  // would repaint the whole window area and throw the middle away.
  // At 45 degrees the inner rounded arc reaches deeper than its straight
  // edge. Cover that arc before clipping; band+tuck alone leaves corner holes.
  // The ring clip still defines the exact visible width.
  float d = band + tuck + irad * (1.f - (float)M_SQRT1_2) + 1.f;
  float ow = outer.size.width, oh = outer.size.height;
  if (d > ow * 0.5f) d = ow * 0.5f;
  if (d > oh * 0.5f) d = oh * 0.5f;

  const struct { float ox, oy, angle, len; } sides[4] = {
    { CGRectGetMinX(outer), CGRectGetMinY(outer), 0.f,             ow },
    { CGRectGetMaxX(outer), CGRectGetMinY(outer), (float)M_PI_2,   oh },
    { CGRectGetMaxX(outer), CGRectGetMaxY(outer), (float)M_PI,     ow },
    { CGRectGetMinX(outer), CGRectGetMaxY(outer), (float)(3*M_PI_2), oh },
  };

  for (int i = 0; i < 4; i++) {
    float L = sides[i].len;
    if (L <= 0.f) continue;

    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, sides[i].ox, sides[i].oy);
    CGContextRotateCTM(ctx, sides[i].angle);

    // the mitre: a trapezoid running the length of this edge, its ends cut
    // back at 45 degrees so neighbouring sides meet corner to corner
    if (knit_mitre_off) {
      CGContextClipToRect(ctx, CGRectMake(0.f, 0.f, L, d));
    } else {
      CGMutablePathRef m = CGPathCreateMutable();
      CGPathMoveToPoint(m,    NULL, 0.f,     0.f);
      CGPathAddLineToPoint(m, NULL, L,       0.f);
      CGPathAddLineToPoint(m, NULL, L - d,   d);
      CGPathAddLineToPoint(m, NULL, d,       d);
      CGPathCloseSubpath(m);
      CGContextSetShouldAntialias(ctx, false);
      CGContextAddPath(ctx, m);
      CGContextClip(ctx);
      CGContextSetShouldAntialias(ctx, true);
      CFRelease(m);
    }

    // Along the side: anchored at the corner (steady under resize) or centred
    // (composed, but slides as the length changes). Across the band the phase
    // depends only on `band`, which resizing does not touch, so it is stable
    // either way.
    float px = (g_knit_anchor == KNIT_ANCHOR_CENTRE) ? L * 0.5f - t.w * 0.5f : 0.f;
    // The app collection starts with a cuff at the outside edge. Its phase
    // depends only on the yarn gauge, never on the changing window size.
    bool cuff = chart >= 0 && chart < g_chart_count
                && strncmp(g_charts[chart].name, "atelier-", 8) == 0;
    float py = cuff ? 0.f : band * 0.5f - t.h * 0.5f;
    // Opaque backing also covers image-sampling slivers at tile/curve edges.
    // Its clip is one narrow side, never the full window backing store.
    float a, r, g, b;
    colors_from_hex(corner_color, &a, &r, &g, &b);
    CGContextSetRGBFillColor(ctx, r * 0.96f, g * 0.96f, b * 0.96f, 1.f);
    CGContextFillRect(ctx, CGRectMake(0.f, 0.f, L, d));
    if (patch.img) {
      // Paint only the two small caps, not a second texture under the entire
      // edge. Separate clips keep DrawTiledImage's bounding boxes small.
      float cap = fminf(orad, L * 0.5f);
      for (int end = 0; end < 2; end++) {
        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, CGRectMake(end ? L - cap : 0.f, 0.f, cap, d));
        CGContextDrawTiledImage(ctx, CGRectMake(0.f, py, patch.w, patch.h), patch.img);
        CGContextRestoreGState(ctx);
      }
    }
    if (solid_corners) {
      CGContextSaveGState(ctx);
      // Keep the curved arc in the base yarn; start the stripe after the
      // tangent. Radius is clamped above, and short edges can be all solid.
      float cap = fminf(orad, L * 0.5f);
      float available = fmaxf(0.f, L - 2.f * cap);
      // Both ends stop at the same tangent. Fit the nearest whole number of
      // repeats between them instead of assigning leftover space to a corner.
      // Only the along-edge tile width changes; the cached yarn and band width
      // stay fixed. This also keeps partial motifs out of the curved patches.
      if (available > 0.f) {
        float repeats = fmaxf(1.f, roundf(available / t.w));
        float repeat_width = available / repeats;
        CGContextClipToRect(ctx, CGRectMake(cap, 0.f, available, d));
        CGContextDrawTiledImage(ctx, CGRectMake(cap, py, repeat_width, t.h), t.img);
      }
      CGContextRestoreGState(ctx);
    } else {
      // Symmetric stripe/check charts can meet at both mitres when each edge
      // contains whole repeats. Other designs retain their existing phase.
      bool fitted = chart >= 0 && chart < g_chart_count && g_charts[chart].fitted_repeat;
      float repeat_width = fitted ? L / fmaxf(1.f, roundf(L / t.w)) : t.w;
      CGContextDrawTiledImage(ctx, CGRectMake(fitted ? 0.f : px, py, repeat_width, t.h), t.img);
    }

    if (cuff_ring) {
      // Clip the cuff in window coordinates so its width follows the arc,
      // then texture it in the same stitch direction/gauge as this side.
      // The inherited mitre clip bounds the work to one narrow edge.
      CGContextSaveGState(ctx);
      CGContextRotateCTM(ctx, -sides[i].angle);
      CGContextTranslateCTM(ctx, -sides[i].ox, -sides[i].oy);
      CGContextAddPath(ctx, cuff_ring);
      CGContextEOClip(ctx);
      CGContextTranslateCTM(ctx, sides[i].ox, sides[i].oy);
      CGContextRotateCTM(ctx, sides[i].angle);
      CGContextDrawTiledImage(ctx, CGRectMake(0.f, 0.f, cuff_tile.w, cuff_tile.h), cuff_tile.img);
      CGContextRestoreGState(ctx);
    }

    // Unfocused windows are darkened, not faded. Painting black over a band
    // that has already been laid down opaquely keeps the result opaque, where
    // lowering the layer's alpha would let the desktop through it.
    if (dim > 0.001f) {
      CGContextSetRGBFillColor(ctx, 0.f, 0.f, 0.f, dim);
      CGContextFillRect(ctx, CGRectMake(-d, -d, L + 2*d, d + 2*d));
    }
    CGContextRestoreGState(ctx);
  }

  CGImageRelease(t.img);
  if (patch.img) CGImageRelease(patch.img);
  if (cuff_tile.img) CGImageRelease(cuff_tile.img);
  if (cuff_ring) CFRelease(cuff_ring);
  CGContextRestoreGState(ctx);
}
