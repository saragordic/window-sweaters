// Software CoreGraphics subset. See shim/cg.h for the conventions.
//
// Paths are flattened to polygons in raster space (y down, row 0 at the top
// of memory) and scan converted with 8 sub-scanlines per pixel row and exact
// horizontal coverage, honouring the non-zero and even-odd rules. Strokes are
// built as the union of one quad per segment and a disc at every vertex:
// exactly a round-joined, round-capped stroke, which is the only kind the
// renderer asks for. Clips are coverage masks over their bounding box, shared
// between saved graphics states until changed.
#include "cg.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define CG_SUBSAMPLES 8
#define CG_FLATNESS 0.08f
#define CG_STACK_MAX 32

enum { OBJ_PATH = 1, OBJ_CONTEXT, OBJ_IMAGE, OBJ_SPACE, OBJ_STRING, OBJ_SOURCE, OBJ_STUB };
struct cgobj { int type; long refs; };

// --- colour spaces ----------------------------------------------------------
struct CGColorSpace { struct cgobj hdr; bool gray; };
static struct CGColorSpace g_rgb_space = { { OBJ_SPACE, 1 }, false };
static struct CGColorSpace g_gray_space = { { OBJ_SPACE, 1 }, true };

CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name) { (void)name; return &g_rgb_space; }
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void) { return &g_rgb_space; }
CGColorSpaceRef CGColorSpaceCreateDeviceGray(void) { return &g_gray_space; }
void CGColorSpaceRelease(CGColorSpaceRef space) { (void)space; }

// --- paths ------------------------------------------------------------------
enum { OP_MOVE, OP_LINE, OP_QUAD, OP_CUBIC, OP_CLOSE };

struct CGPath {
  struct cgobj hdr;
  uint8_t* ops; int nops, capops;
  CGPoint* pts; int npts, cappts;
  CGPoint start, cur;
  bool has_cur;
};

static void path_reserve(struct CGPath* p, int ops, int pts) {
  if (p->nops + ops > p->capops) {
    int cap = p->capops ? p->capops * 2 : 16;
    while (cap < p->nops + ops) cap *= 2;
    p->ops = realloc(p->ops, (size_t)cap);
    p->capops = cap;
  }
  if (p->npts + pts > p->cappts) {
    int cap = p->cappts ? p->cappts * 2 : 32;
    while (cap < p->npts + pts) cap *= 2;
    p->pts = realloc(p->pts, sizeof(CGPoint) * (size_t)cap);
    p->cappts = cap;
  }
}

static CGPoint xf(const CGAffineTransform* m, CGFloat x, CGFloat y) {
  CGPoint p = { x, y };
  return m ? CGPointApplyAffineTransform(p, *m) : p;
}

static void path_move(struct CGPath* p, CGPoint a) {
  path_reserve(p, 1, 1);
  p->ops[p->nops++] = OP_MOVE;
  p->pts[p->npts++] = a;
  p->start = p->cur = a;
  p->has_cur = true;
}

static void path_line(struct CGPath* p, CGPoint a) {
  if (!p->has_cur) { path_move(p, a); return; }
  path_reserve(p, 1, 1);
  p->ops[p->nops++] = OP_LINE;
  p->pts[p->npts++] = a;
  p->cur = a;
}

static void path_quad(struct CGPath* p, CGPoint c, CGPoint a) {
  if (!p->has_cur) path_move(p, c);
  path_reserve(p, 1, 2);
  p->ops[p->nops++] = OP_QUAD;
  p->pts[p->npts++] = c;
  p->pts[p->npts++] = a;
  p->cur = a;
}

static void path_cubic(struct CGPath* p, CGPoint c1, CGPoint c2, CGPoint a) {
  if (!p->has_cur) path_move(p, c1);
  path_reserve(p, 1, 3);
  p->ops[p->nops++] = OP_CUBIC;
  p->pts[p->npts++] = c1;
  p->pts[p->npts++] = c2;
  p->pts[p->npts++] = a;
  p->cur = a;
}

static void path_close(struct CGPath* p) {
  if (!p->has_cur) return;
  path_reserve(p, 1, 0);
  p->ops[p->nops++] = OP_CLOSE;
  p->cur = p->start;
}

static void path_clear(struct CGPath* p) {
  p->nops = p->npts = 0;
  p->has_cur = false;
}

static void path_free_storage(struct CGPath* p) {
  free(p->ops); free(p->pts);
  p->ops = NULL; p->pts = NULL;
  p->nops = p->npts = p->capops = p->cappts = 0;
}

CGMutablePathRef CGPathCreateMutable(void) {
  struct CGPath* p = calloc(1, sizeof *p);
  if (p) { p->hdr.type = OBJ_PATH; p->hdr.refs = 1; }
  return p;
}

void CGPathRelease(CGPathRef path) { if (path) CFRelease(path); }

void CGPathMoveToPoint(CGMutablePathRef p, const CGAffineTransform* m, CGFloat x, CGFloat y) {
  if (p) path_move(p, xf(m, x, y));
}
void CGPathAddLineToPoint(CGMutablePathRef p, const CGAffineTransform* m, CGFloat x, CGFloat y) {
  if (p) path_line(p, xf(m, x, y));
}
void CGPathAddQuadCurveToPoint(CGMutablePathRef p, const CGAffineTransform* m,
                               CGFloat cpx, CGFloat cpy, CGFloat x, CGFloat y) {
  if (p) path_quad(p, xf(m, cpx, cpy), xf(m, x, y));
}
void CGPathAddCurveToPoint(CGMutablePathRef p, const CGAffineTransform* m,
                           CGFloat c1x, CGFloat c1y, CGFloat c2x, CGFloat c2y,
                           CGFloat x, CGFloat y) {
  if (p) path_cubic(p, xf(m, c1x, c1y), xf(m, c2x, c2y), xf(m, x, y));
}
void CGPathCloseSubpath(CGMutablePathRef p) { if (p) path_close(p); }

void CGPathAddRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect r) {
  if (!p) return;
  r = CGRectStandardize(r);
  CGFloat x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;
  path_move(p, xf(m, x0, y0));
  path_line(p, xf(m, x1, y0));
  path_line(p, xf(m, x1, y1));
  path_line(p, xf(m, x0, y1));
  path_close(p);
}

// Quarter arcs as cubics; the usual kappa gives < 0.03% radial error.
#define CG_KAPPA 0.5522847498

void CGPathAddRoundedRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect r,
                          CGFloat rw, CGFloat rh) {
  if (!p) return;
  r = CGRectStandardize(r);
  if (rw > r.size.width * 0.5) rw = r.size.width * 0.5;
  if (rh > r.size.height * 0.5) rh = r.size.height * 0.5;
  if (rw <= 0 || rh <= 0) { CGPathAddRect(p, m, r); return; }
  CGFloat x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;
  CGFloat kx = rw * CG_KAPPA, ky = rh * CG_KAPPA;
  // Same direction as CGPathAddRect: counter-clockwise in y-up space.
  path_move(p, xf(m, x0 + rw, y0));
  path_line(p, xf(m, x1 - rw, y0));
  path_cubic(p, xf(m, x1 - rw + kx, y0), xf(m, x1, y0 + rh - ky), xf(m, x1, y0 + rh));
  path_line(p, xf(m, x1, y1 - rh));
  path_cubic(p, xf(m, x1, y1 - rh + ky), xf(m, x1 - rw + kx, y1), xf(m, x1 - rw, y1));
  path_line(p, xf(m, x0 + rw, y1));
  path_cubic(p, xf(m, x0 + rw - kx, y1), xf(m, x0, y1 - rh + ky), xf(m, x0, y1 - rh));
  path_line(p, xf(m, x0, y0 + rh));
  path_cubic(p, xf(m, x0, y0 + rh - ky), xf(m, x0 + rw - kx, y0), xf(m, x0 + rw, y0));
  path_close(p);
}

void CGPathAddEllipseInRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect r) {
  if (!p) return;
  r = CGRectStandardize(r);
  CGFloat rx = r.size.width * 0.5, ry = r.size.height * 0.5;
  CGFloat cx = r.origin.x + rx, cy = r.origin.y + ry;
  CGFloat kx = rx * CG_KAPPA, ky = ry * CG_KAPPA;
  path_move(p, xf(m, cx + rx, cy));
  path_cubic(p, xf(m, cx + rx, cy + ky), xf(m, cx + kx, cy + ry), xf(m, cx, cy + ry));
  path_cubic(p, xf(m, cx - kx, cy + ry), xf(m, cx - rx, cy + ky), xf(m, cx - rx, cy));
  path_cubic(p, xf(m, cx - rx, cy - ky), xf(m, cx - kx, cy - ry), xf(m, cx, cy - ry));
  path_cubic(p, xf(m, cx + kx, cy - ry), xf(m, cx + rx, cy - ky), xf(m, cx + rx, cy));
  path_close(p);
}

void CGPathAddPath(CGMutablePathRef p, const CGAffineTransform* m, CGPathRef other) {
  if (!p || !other) return;
  const CGPoint* q = other->pts;
  for (int i = 0; i < other->nops; i++) {
    switch (other->ops[i]) {
      case OP_MOVE:  path_move(p, xf(m, q[0].x, q[0].y)); q += 1; break;
      case OP_LINE:  path_line(p, xf(m, q[0].x, q[0].y)); q += 1; break;
      case OP_QUAD:  path_quad(p, xf(m, q[0].x, q[0].y), xf(m, q[1].x, q[1].y)); q += 2; break;
      case OP_CUBIC: path_cubic(p, xf(m, q[0].x, q[0].y), xf(m, q[1].x, q[1].y),
                                xf(m, q[2].x, q[2].y)); q += 3; break;
      case OP_CLOSE: path_close(p); break;
    }
  }
}

CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform* m) {
  CGMutablePathRef p = CGPathCreateMutable();
  CGPathAddRect(p, m, rect);
  return p;
}

CGPathRef CGPathCreateWithRoundedRect(CGRect rect, CGFloat rw, CGFloat rh, const CGAffineTransform* m) {
  CGMutablePathRef p = CGPathCreateMutable();
  CGPathAddRoundedRect(p, m, rect, rw, rh);
  return p;
}

// --- flattening -------------------------------------------------------------
// A polyline in raster space. Subpaths are separated by `starts`.
struct poly {
  float* xy; int n, cap;
  int* starts; uint8_t* closed; int nsub, capsub;
};

static void poly_point(struct poly* pl, float x, float y) {
  if (pl->n == pl->cap) {
    pl->cap = pl->cap ? pl->cap * 2 : 64;
    pl->xy = realloc(pl->xy, sizeof(float) * 2 * (size_t)pl->cap);
  }
  pl->xy[2 * pl->n] = x;
  pl->xy[2 * pl->n + 1] = y;
  pl->n++;
}

static void poly_subpath(struct poly* pl) {
  if (pl->nsub == pl->capsub) {
    pl->capsub = pl->capsub ? pl->capsub * 2 : 8;
    pl->starts = realloc(pl->starts, sizeof(int) * (size_t)pl->capsub);
    pl->closed = realloc(pl->closed, (size_t)pl->capsub);
  }
  pl->starts[pl->nsub] = pl->n;
  pl->closed[pl->nsub] = 0;
  pl->nsub++;
}

static void poly_free(struct poly* pl) {
  free(pl->xy); free(pl->starts); free(pl->closed);
  memset(pl, 0, sizeof *pl);
}

static int curve_steps(float dev) {
  if (!(dev > CG_FLATNESS)) return 1;
  int n = (int)ceilf(sqrtf(dev / CG_FLATNESS));
  return n > 256 ? 256 : n;
}

static void flatten(const struct CGPath* p, struct poly* pl) {
  const CGPoint* q = p->pts;
  float cx = 0, cy = 0;
  bool reopen = false;   // drawing on after a close starts a new subpath there
  for (int i = 0; i < p->nops; i++) {
    if (reopen && p->ops[i] != OP_MOVE && p->ops[i] != OP_CLOSE) {
      poly_subpath(pl);
      poly_point(pl, cx, cy);
    }
    reopen = false;
    switch (p->ops[i]) {
      case OP_MOVE:
        poly_subpath(pl);
        cx = (float)q[0].x; cy = (float)q[0].y;
        poly_point(pl, cx, cy);
        q += 1;
        break;
      case OP_LINE:
        cx = (float)q[0].x; cy = (float)q[0].y;
        poly_point(pl, cx, cy);
        q += 1;
        break;
      case OP_QUAD: {
        float x1 = (float)q[0].x, y1 = (float)q[0].y, x2 = (float)q[1].x, y2 = (float)q[1].y;
        float ddx = cx - 2 * x1 + x2, ddy = cy - 2 * y1 + y2;
        int n = curve_steps(sqrtf(ddx * ddx + ddy * ddy) * 0.25f);
        for (int k = 1; k <= n; k++) {
          float t = (float)k / n, u = 1 - t;
          poly_point(pl, u * u * cx + 2 * u * t * x1 + t * t * x2,
                         u * u * cy + 2 * u * t * y1 + t * t * y2);
        }
        cx = x2; cy = y2;
        q += 2;
        break;
      }
      case OP_CUBIC: {
        float x1 = (float)q[0].x, y1 = (float)q[0].y, x2 = (float)q[1].x, y2 = (float)q[1].y;
        float x3 = (float)q[2].x, y3 = (float)q[2].y;
        float ax = cx - 2 * x1 + x2, ay = cy - 2 * y1 + y2;
        float bx = x1 - 2 * x2 + x3, by = y1 - 2 * y2 + y3;
        float dev = 0.75f * sqrtf(fmaxf(ax * ax + ay * ay, bx * bx + by * by));
        int n = curve_steps(dev);
        for (int k = 1; k <= n; k++) {
          float t = (float)k / n, u = 1 - t;
          float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
          poly_point(pl, w0 * cx + w1 * x1 + w2 * x2 + w3 * x3,
                         w0 * cy + w1 * y1 + w2 * y2 + w3 * y3);
        }
        cx = x3; cy = y3;
        q += 3;
        break;
      }
      case OP_CLOSE:
        if (pl->nsub) {
          pl->closed[pl->nsub - 1] = 1;
          int s = pl->starts[pl->nsub - 1];
          cx = pl->xy[2 * s]; cy = pl->xy[2 * s + 1];
          reopen = true;
        }
        break;
    }
  }
}

// --- edges and scan conversion ----------------------------------------------
struct edge { float x0, y0, x1, y1, dxdy; int dir; };
struct edges { struct edge* e; int n, cap; float minx, miny, maxx, maxy; };

static void edges_init(struct edges* es) {
  memset(es, 0, sizeof *es);
  es->minx = es->miny = FLT_MAX;
  es->maxx = es->maxy = -FLT_MAX;
}

static void edge_add(struct edges* es, float x0, float y0, float x1, float y1) {
  if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) return;
  if (y0 == y1) return;
  int dir = 1;
  if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
  if (es->n == es->cap) {
    es->cap = es->cap ? es->cap * 2 : 64;
    es->e = realloc(es->e, sizeof(struct edge) * (size_t)es->cap);
  }
  struct edge* e = &es->e[es->n++];
  e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1;
  e->dxdy = (x1 - x0) / (y1 - y0);
  e->dir = dir;
  if (x0 < es->minx) es->minx = x0; if (x1 < es->minx) es->minx = x1;
  if (x0 > es->maxx) es->maxx = x0; if (x1 > es->maxx) es->maxx = x1;
  if (y0 < es->miny) es->miny = y0;
  if (y1 > es->maxy) es->maxy = y1;
}

// Every subpath is closed for filling.
static void edges_from_poly(struct edges* es, const struct poly* pl) {
  for (int s = 0; s < pl->nsub; s++) {
    int a = pl->starts[s], b = s + 1 < pl->nsub ? pl->starts[s + 1] : pl->n;
    if (b - a < 2) continue;
    for (int i = a; i < b; i++) {
      int j = i + 1 < b ? i + 1 : a;
      edge_add(es, pl->xy[2 * i], pl->xy[2 * i + 1], pl->xy[2 * j], pl->xy[2 * j + 1]);
    }
  }
}

static void edges_polygon(struct edges* es, const float* xy, int n) {
  // Counter-clockwise in raster space, so overlapping pieces add up under
  // the non-zero rule instead of cancelling.
  float area = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
  }
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    if (area >= 0) edge_add(es, xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1]);
    else edge_add(es, xy[2 * j], xy[2 * j + 1], xy[2 * i], xy[2 * i + 1]);
  }
}

static void edges_disc(struct edges* es, float cx, float cy, float r) {
  int n = (int)ceilf(2.f * (float)M_PI * r / 0.6f);
  if (n < 8) n = 8;
  if (n > 96) n = 96;
  float xy[2 * 96];
  for (int i = 0; i < n; i++) {
    float a = 2.f * (float)M_PI * i / n;
    xy[2 * i] = cx + cosf(a) * r;
    xy[2 * i + 1] = cy + sinf(a) * r;
  }
  edges_polygon(es, xy, n);
}

static void edges_stroke(struct edges* es, const struct poly* pl, float width, bool round_cap) {
  float r = width * 0.5f;
  if (!(r > 0.01f)) return;
  for (int s = 0; s < pl->nsub; s++) {
    int a = pl->starts[s], b = s + 1 < pl->nsub ? pl->starts[s + 1] : pl->n;
    bool closed = pl->closed[s];
    int count = b - a;
    if (count < 1) continue;
    float px = pl->xy[2 * a], py = pl->xy[2 * a + 1];
    if (count == 1) {
      if (round_cap) edges_disc(es, px, py, r);
      continue;
    }
    int segs = closed ? count : count - 1;
    for (int k = 0; k < segs; k++) {
      int i = a + k, j = a + (k + 1) % count;
      float x0 = pl->xy[2 * i], y0 = pl->xy[2 * i + 1];
      float x1 = pl->xy[2 * j], y1 = pl->xy[2 * j + 1];
      float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
      if (len > 1e-6f) {
        float nx = -dy / len * r, ny = dx / len * r;
        float quad[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
        edges_polygon(es, quad, 4);
      }
      // A disc at every interior vertex is a round join; at the two ends of
      // an open subpath it is a round cap.
      bool end = !closed && (k == 0);
      if (!end || round_cap) edges_disc(es, x0, y0, r);
    }
    if (!closed && round_cap) {
      int last = b - 1;
      edges_disc(es, pl->xy[2 * last], pl->xy[2 * last + 1], r);
    }
  }
}

static int cmp_edge_y(const void* a, const void* b) {
  float d = ((const struct edge*)a)->y0 - ((const struct edge*)b)->y0;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}

struct crossing { float x; int dir; };

static int cmp_cross(const void* a, const void* b) {
  float d = ((const struct crossing*)a)->x - ((const struct crossing*)b)->x;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}

struct irect { int x0, y0, x1, y1; };

static struct irect irect_intersect(struct irect a, struct irect b) {
  struct irect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0,
                     a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
  if (r.x1 < r.x0) r.x1 = r.x0;
  if (r.y1 < r.y0) r.y1 = r.y0;
  return r;
}

static bool irect_empty(struct irect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }

static struct irect edges_bounds(const struct edges* es) {
  if (!es->n) { struct irect r = { 0, 0, 0, 0 }; return r; }
  float lx = floorf(es->minx), ly = floorf(es->miny), hx = ceilf(es->maxx), hy = ceilf(es->maxy);
  struct irect r = { lx < -1e8f ? -100000000 : (int)lx, ly < -1e8f ? -100000000 : (int)ly,
                     hx > 1e8f ? 100000000 : (int)hx, hy > 1e8f ? 100000000 : (int)hy };
  return r;
}

// Coverage (0..255) of the edges over `area`, row-major, area width stride.
static uint8_t* rasterize(struct edges* es, bool evenodd, bool aa, struct irect area) {
  int bw = area.x1 - area.x0, bh = area.y1 - area.y0;
  if (bw <= 0 || bh <= 0) return NULL;
  uint8_t* out = calloc((size_t)bw * bh, 1);
  if (!out || !es->n) return out;
  qsort(es->e, (size_t)es->n, sizeof(struct edge), cmp_edge_y);
  float* acc = calloc((size_t)bw + 2, sizeof(float));
  float* diff = calloc((size_t)bw + 2, sizeof(float));
  int* active = malloc(sizeof(int) * (size_t)es->n);
  struct crossing* xs = malloc(sizeof(struct crossing) * (size_t)es->n);
  if (!acc || !diff || !active || !xs) { free(acc); free(diff); free(active); free(xs); return out; }
  int na = 0, next = 0;
  const int ss = aa ? CG_SUBSAMPLES : 1;
  const float weight = 1.f / ss;
  const float fx0 = (float)area.x0;

  // Skip edges wholly above the area.
  for (int row = 0; row < bh; row++) {
    float ytop = (float)(area.y0 + row), ybot = ytop + 1.f;
    while (next < es->n && es->e[next].y0 < ybot) {
      if (es->e[next].y1 > ytop) active[na++] = next;
      next++;
    }
    int keep = 0;
    for (int k = 0; k < na; k++) if (es->e[active[k]].y1 > ytop) active[keep++] = active[k];
    na = keep;
    if (!na) {
      if (next >= es->n) break;
      continue;
    }
    memset(acc, 0, sizeof(float) * ((size_t)bw + 2));
    memset(diff, 0, sizeof(float) * ((size_t)bw + 2));
    bool any = false;
    for (int s = 0; s < ss; s++) {
      float sy = ytop + (s + 0.5f) / ss;
      int nx = 0;
      for (int k = 0; k < na; k++) {
        const struct edge* e = &es->e[active[k]];
        if (e->y0 <= sy && sy < e->y1) {
          xs[nx].x = e->x0 + (sy - e->y0) * e->dxdy - fx0;
          xs[nx].dir = e->dir;
          nx++;
        }
      }
      if (nx < 2) continue;
      if (nx < 24) {
        for (int i = 1; i < nx; i++) {
          struct crossing c = xs[i];
          int j = i - 1;
          while (j >= 0 && xs[j].x > c.x) { xs[j + 1] = xs[j]; j--; }
          xs[j + 1] = c;
        }
      } else {
        qsort(xs, (size_t)nx, sizeof *xs, cmp_cross);
      }
      int wind = 0;
      float start = 0;
      for (int k = 0; k < nx; k++) {
        bool was = evenodd ? (wind & 1) : wind != 0;
        wind += xs[k].dir;
        bool now = evenodd ? (wind & 1) : wind != 0;
        if (!was && now) start = xs[k].x;
        else if (was && !now) {
          float xa = start, xb = xs[k].x;
          if (xa < 0) xa = 0;
          if (xb > (float)bw) xb = (float)bw;
          if (xb <= xa) continue;
          any = true;
          if (aa) {
            int ia = (int)xa, ib = (int)xb;
            if (ia == ib) acc[ia] += (xb - xa) * weight;
            else {
              acc[ia] += (ia + 1 - xa) * weight;
              diff[ia + 1] += weight;
              diff[ib] -= weight;
              acc[ib] += (xb - ib) * weight;
            }
          } else {
            int ia = (int)ceilf(xa - 0.5f), ib = (int)ceilf(xb - 0.5f);
            if (ia < 0) ia = 0;
            if (ib > bw) ib = bw;
            if (ib > ia) { diff[ia] += 1.f; diff[ib] -= 1.f; }
          }
        }
      }
    }
    if (!any) continue;
    uint8_t* line = out + (size_t)row * bw;
    float run = 0;
    for (int x = 0; x < bw; x++) {
      run += diff[x];
      float v = run + acc[x];
      if (v <= 0.002f) continue;
      line[x] = v >= 0.998f ? 255 : (uint8_t)(v * 255.f + 0.5f);
    }
  }
  free(acc); free(diff); free(active); free(xs);
  return out;
}

// --- contexts ---------------------------------------------------------------
struct clip { long refs; struct irect r; uint8_t* m; };   // m NULL: fully inside r

static struct clip* clip_retain(struct clip* c) { if (c) c->refs++; return c; }
static void clip_release(struct clip* c) {
  if (c && --c->refs == 0) { free(c->m); free(c); }
}

struct gstate {
  CGAffineTransform ctm;      // user space -> raster space (y down)
  float fill[4], stroke[4];   // straight RGBA, 0..1
  float line_width, alpha;
  int cap, join;
  bool aa;
  int interp;
  struct clip* clip;          // NULL: the whole context
};

struct CGContext {
  struct cgobj hdr;
  int w, h;
  bool gray;
  uint8_t* data;
  size_t stride;
  bool owns;
  struct gstate gs[CG_STACK_MAX];
  int depth;
  struct CGPath path;         // the current path, already in raster space
};

struct CGImage { struct cgobj hdr; int w, h; uint32_t* px; };

static struct gstate* GS(CGContextRef c) { return &c->gs[c->depth]; }

CGContextRef CGBitmapContextCreate(void* data, size_t width, size_t height,
                                   size_t bpc, size_t bpr, CGColorSpaceRef space,
                                   CGBitmapInfo info) {
  (void)info;
  if (width == 0 || height == 0 || width > 32768 || height > 32768 || bpc != 8) return NULL;
  bool gray = space && space->gray;
  size_t min_stride = width * (gray ? 1 : 4);
  if (bpr == 0) bpr = min_stride;
  if (bpr < min_stride) return NULL;
  struct CGContext* c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->hdr.type = OBJ_CONTEXT; c->hdr.refs = 1;
  c->w = (int)width; c->h = (int)height; c->gray = gray; c->stride = bpr;
  if (data) c->data = data;
  else {
    c->data = calloc(bpr, height);
    c->owns = true;
    if (!c->data) { free(c); return NULL; }
  }
  struct gstate* g = &c->gs[0];
  g->ctm = CGAffineTransformMake(1, 0, 0, -1, 0, (CGFloat)height);
  g->fill[3] = 1; g->stroke[3] = 1;
  g->line_width = 1; g->alpha = 1;
  g->cap = kCGLineCapButt; g->join = kCGLineJoinMiter;
  g->aa = true; g->interp = kCGInterpolationDefault;
  c->path.hdr.type = OBJ_PATH;
  return c;
}

void* CGBitmapContextGetData(CGContextRef c) { return c ? c->data : NULL; }
size_t CGBitmapContextGetWidth(CGContextRef c) { return c ? (size_t)c->w : 0; }
size_t CGBitmapContextGetHeight(CGContextRef c) { return c ? (size_t)c->h : 0; }
void CGContextFlush(CGContextRef c) { (void)c; }

static void context_free(struct CGContext* c) {
  for (int i = 0; i <= c->depth; i++) clip_release(c->gs[i].clip);
  path_free_storage(&c->path);
  if (c->owns) free(c->data);
  free(c);
}

void CGContextRelease(CGContextRef c) { if (c) CFRelease(c); }

void CGContextSaveGState(CGContextRef c) {
  if (!c || c->depth + 1 >= CG_STACK_MAX) return;
  c->gs[c->depth + 1] = c->gs[c->depth];
  clip_retain(c->gs[c->depth + 1].clip);
  c->depth++;
}

void CGContextRestoreGState(CGContextRef c) {
  if (!c || c->depth == 0) return;
  clip_release(c->gs[c->depth].clip);
  c->depth--;
}

void CGContextConcatCTM(CGContextRef c, CGAffineTransform t) {
  if (c) GS(c)->ctm = CGAffineTransformConcat(t, GS(c)->ctm);
}
void CGContextScaleCTM(CGContextRef c, CGFloat sx, CGFloat sy) {
  CGContextConcatCTM(c, CGAffineTransformMakeScale(sx, sy));
}
void CGContextTranslateCTM(CGContextRef c, CGFloat tx, CGFloat ty) {
  CGContextConcatCTM(c, CGAffineTransformMakeTranslation(tx, ty));
}
void CGContextRotateCTM(CGContextRef c, CGFloat angle) {
  CGContextConcatCTM(c, CGAffineTransformMakeRotation(angle));
}

static float clamp01(CGFloat v) { return v < 0 ? 0.f : v > 1 ? 1.f : (float)v; }

void CGContextSetRGBFillColor(CGContextRef c, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  if (!c) return;
  float* f = GS(c)->fill;
  f[0] = clamp01(r); f[1] = clamp01(g); f[2] = clamp01(b); f[3] = clamp01(a);
}
void CGContextSetRGBStrokeColor(CGContextRef c, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  if (!c) return;
  float* f = GS(c)->stroke;
  f[0] = clamp01(r); f[1] = clamp01(g); f[2] = clamp01(b); f[3] = clamp01(a);
}
void CGContextSetGrayFillColor(CGContextRef c, CGFloat gray, CGFloat a) {
  CGContextSetRGBFillColor(c, gray, gray, gray, a);
}
void CGContextSetGrayStrokeColor(CGContextRef c, CGFloat gray, CGFloat a) {
  CGContextSetRGBStrokeColor(c, gray, gray, gray, a);
}
void CGContextSetLineWidth(CGContextRef c, CGFloat w) { if (c) GS(c)->line_width = (float)w; }
void CGContextSetLineCap(CGContextRef c, CGLineCap cap) { if (c) GS(c)->cap = cap; }
void CGContextSetLineJoin(CGContextRef c, CGLineJoin join) { if (c) GS(c)->join = join; }
void CGContextSetAlpha(CGContextRef c, CGFloat a) { if (c) GS(c)->alpha = clamp01(a); }
void CGContextSetShouldAntialias(CGContextRef c, bool aa) { if (c) GS(c)->aa = aa; }
void CGContextSetInterpolationQuality(CGContextRef c, CGInterpolationQuality q) {
  if (c) GS(c)->interp = q;
}

void CGContextBeginPath(CGContextRef c) { if (c) path_clear(&c->path); }
void CGContextAddPath(CGContextRef c, CGPathRef p) {
  if (c && p) CGPathAddPath(&c->path, &GS(c)->ctm, p);
}
void CGContextMoveToPoint(CGContextRef c, CGFloat x, CGFloat y) {
  if (c) path_move(&c->path, xf(&GS(c)->ctm, x, y));
}
void CGContextAddLineToPoint(CGContextRef c, CGFloat x, CGFloat y) {
  if (c) path_line(&c->path, xf(&GS(c)->ctm, x, y));
}
void CGContextAddCurveToPoint(CGContextRef c, CGFloat c1x, CGFloat c1y,
                              CGFloat c2x, CGFloat c2y, CGFloat x, CGFloat y) {
  if (c) CGPathAddCurveToPoint(&c->path, &GS(c)->ctm, c1x, c1y, c2x, c2y, x, y);
}
void CGContextClosePath(CGContextRef c) { if (c) path_close(&c->path); }
void CGContextAddRect(CGContextRef c, CGRect r) { if (c) CGPathAddRect(&c->path, &GS(c)->ctm, r); }
void CGContextAddEllipseInRect(CGContextRef c, CGRect r) {
  if (c) CGPathAddEllipseInRect(&c->path, &GS(c)->ctm, r);
}

static struct irect context_bounds(CGContextRef c) {
  struct irect r = { 0, 0, c->w, c->h };
  return r;
}

static struct irect clip_bounds(CGContextRef c) {
  struct clip* cl = GS(c)->clip;
  return cl ? irect_intersect(cl->r, context_bounds(c)) : context_bounds(c);
}

static uint8_t clip_at(const struct clip* cl, int x, int y) {
  if (!cl) return 255;
  if (x < cl->r.x0 || x >= cl->r.x1 || y < cl->r.y0 || y >= cl->r.y1) return 0;
  return cl->m ? cl->m[(size_t)(y - cl->r.y0) * (cl->r.x1 - cl->r.x0) + (x - cl->r.x0)] : 255;
}

// Coverage of the current path (consumed) over its bounds within the clip.
static uint8_t* path_coverage(CGContextRef c, bool evenodd, bool stroke, struct irect* area) {
  struct poly pl = { 0 };
  flatten(&c->path, &pl);
  path_clear(&c->path);
  struct edges es;
  edges_init(&es);
  if (stroke) {
    const CGAffineTransform* m = &GS(c)->ctm;
    float scale = sqrtf(fabsf((float)(m->a * m->d - m->b * m->c)));
    edges_stroke(&es, &pl, GS(c)->line_width * scale, GS(c)->cap == kCGLineCapRound);
  } else {
    edges_from_poly(&es, &pl);
  }
  poly_free(&pl);
  *area = irect_intersect(edges_bounds(&es), clip_bounds(c));
  uint8_t* cov = NULL;
  if (!irect_empty(*area)) cov = rasterize(&es, evenodd, GS(c)->aa, *area);
  free(es.e);
  return cov;
}

static void composite_solid(CGContextRef c, const uint8_t* cov, struct irect area, const float* color) {
  struct gstate* g = GS(c);
  float a = color[3] * g->alpha;
  if (a <= 0) return;
  const struct clip* cl = g->clip;
  int aw = area.x1 - area.x0;
  if (c->gray) {
    float v = (0.299f * color[0] + 0.587f * color[1] + 0.114f * color[2]) * 255.f;
    for (int y = area.y0; y < area.y1; y++) {
      uint8_t* d = c->data + (size_t)y * c->stride;
      const uint8_t* m = cov + (size_t)(y - area.y0) * aw;
      for (int x = area.x0; x < area.x1; x++) {
        unsigned k = m[x - area.x0];
        if (!k) continue;
        if (cl) { k = (k * clip_at(cl, x, y) + 127) / 255; if (!k) continue; }
        float t = k / 255.f * a;
        d[x] = (uint8_t)(v * t + d[x] * (1 - t) + 0.5f);
      }
    }
    return;
  }
  float sr = color[0] * 255.f, sg = color[1] * 255.f, sb = color[2] * 255.f;
  for (int y = area.y0; y < area.y1; y++) {
    uint32_t* d = (uint32_t*)(c->data + (size_t)y * c->stride);
    const uint8_t* m = cov + (size_t)(y - area.y0) * aw;
    for (int x = area.x0; x < area.x1; x++) {
      unsigned k = m[x - area.x0];
      if (!k) continue;
      if (cl) { k = (k * clip_at(cl, x, y) + 127) / 255; if (!k) continue; }
      float t = k / 255.f * a, keep = 1 - t;
      uint32_t p = d[x];
      if (t >= 0.999f) {
        d[x] = 0xff000000u | ((uint32_t)(sr + 0.5f) << 16) | ((uint32_t)(sg + 0.5f) << 8)
             | (uint32_t)(sb + 0.5f);
        continue;
      }
      float pa = (float)(p >> 24), pr = (float)((p >> 16) & 255), pg = (float)((p >> 8) & 255),
            pb = (float)(p & 255);
      d[x] = ((uint32_t)(255.f * t + pa * keep + 0.5f) << 24)
           | ((uint32_t)(sr * t + pr * keep + 0.5f) << 16)
           | ((uint32_t)(sg * t + pg * keep + 0.5f) << 8)
           | (uint32_t)(sb * t + pb * keep + 0.5f);
    }
  }
}

static void fill_current(CGContextRef c, bool evenodd, bool stroke) {
  if (!c) return;
  struct irect area;
  uint8_t* cov = path_coverage(c, evenodd, stroke, &area);
  if (cov) composite_solid(c, cov, area, stroke ? GS(c)->stroke : GS(c)->fill);
  free(cov);
}

void CGContextFillPath(CGContextRef c) { fill_current(c, false, false); }
void CGContextEOFillPath(CGContextRef c) { fill_current(c, true, false); }
void CGContextStrokePath(CGContextRef c) { fill_current(c, false, true); }

void CGContextFillRect(CGContextRef c, CGRect r) {
  if (!c) return;
  path_clear(&c->path);
  CGContextAddRect(c, r);
  CGContextFillPath(c);
}

void CGContextFillEllipseInRect(CGContextRef c, CGRect r) {
  if (!c) return;
  path_clear(&c->path);
  CGContextAddEllipseInRect(c, r);
  CGContextFillPath(c);
}

void CGContextClearRect(CGContextRef c, CGRect r) {
  if (!c) return;
  path_clear(&c->path);
  CGContextAddRect(c, r);
  struct irect area;
  uint8_t* cov = path_coverage(c, false, false, &area);
  if (!cov) return;
  const struct clip* cl = GS(c)->clip;
  int aw = area.x1 - area.x0, bpp = c->gray ? 1 : 4;
  for (int y = area.y0; y < area.y1; y++) {
    uint8_t* d = c->data + (size_t)y * c->stride;
    for (int x = area.x0; x < area.x1; x++) {
      unsigned k = cov[(size_t)(y - area.y0) * aw + (x - area.x0)];
      if (cl) k = (k * clip_at(cl, x, y) + 127) / 255;
      if (!k) continue;
      for (int b = 0; b < bpp; b++) d[x * bpp + b] = (uint8_t)(d[x * bpp + b] * (255 - k) / 255);
    }
  }
  free(cov);
}

static void clip_current(CGContextRef c, bool evenodd) {
  if (!c) return;
  struct irect area;
  uint8_t* cov = path_coverage(c, evenodd, false, &area);
  struct clip* old = GS(c)->clip;
  struct clip* nc = calloc(1, sizeof *nc);
  if (!nc) { free(cov); return; }
  nc->refs = 1;
  nc->r = area;
  nc->m = cov;
  if (cov && old && old->m) {
    int aw = area.x1 - area.x0;
    for (int y = area.y0; y < area.y1; y++)
      for (int x = area.x0; x < area.x1; x++) {
        uint8_t* v = &cov[(size_t)(y - area.y0) * aw + (x - area.x0)];
        *v = (uint8_t)((*v * clip_at(old, x, y) + 127) / 255);
      }
  }
  if (!cov) { nc->r.x1 = nc->r.x0; nc->r.y1 = nc->r.y0; }   // nothing survives
  clip_release(old);
  GS(c)->clip = nc;
}

void CGContextClip(CGContextRef c) { clip_current(c, false); }
void CGContextEOClip(CGContextRef c) { clip_current(c, true); }
void CGContextClipToRect(CGContextRef c, CGRect r) {
  if (!c) return;
  path_clear(&c->path);
  CGContextAddRect(c, r);
  CGContextClip(c);
}

// --- images -----------------------------------------------------------------
static struct CGImage* image_alloc(int w, int h) {
  if (w <= 0 || h <= 0) return NULL;
  struct CGImage* img = calloc(1, sizeof *img);
  if (!img) return NULL;
  img->px = malloc(sizeof(uint32_t) * (size_t)w * h);
  if (!img->px) { free(img); return NULL; }
  img->hdr.type = OBJ_IMAGE; img->hdr.refs = 1;
  img->w = w; img->h = h;
  return img;
}

CGImageRef CGImageCreateWithPixels(const uint32_t* px, int w, int h) {
  struct CGImage* img = image_alloc(w, h);
  if (img && px) memcpy(img->px, px, sizeof(uint32_t) * (size_t)w * h);
  return img;
}

const uint32_t* CGImageGetPixels(CGImageRef image) { return image ? image->px : NULL; }

CGImageRef CGBitmapContextCreateImage(CGContextRef c) {
  if (!c) return NULL;
  struct CGImage* img = image_alloc(c->w, c->h);
  if (!img) return NULL;
  for (int y = 0; y < c->h; y++) {
    const uint8_t* s = c->data + (size_t)y * c->stride;
    uint32_t* d = img->px + (size_t)y * c->w;
    if (c->gray) for (int x = 0; x < c->w; x++) d[x] = 0xff000000u | s[x] * 0x010101u;
    else memcpy(d, s, sizeof(uint32_t) * (size_t)c->w);
  }
  return img;
}

CGImageRef CGImageRetain(CGImageRef image) { if (image) CFRetain(image); return image; }
void CGImageRelease(CGImageRef image) { if (image) CFRelease(image); }
size_t CGImageGetWidth(CGImageRef image) { return image ? (size_t)image->w : 0; }
size_t CGImageGetHeight(CGImageRef image) { return image ? (size_t)image->h : 0; }

static inline int wrap(int v, int n) { v %= n; return v < 0 ? v + n : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Premultiplied BGRA sample at image coordinates (u, v), pixel centres at +0.5.
static void sample(const struct CGImage* img, float u, float v, bool tiled, bool smooth, float out[4]) {
  if (!smooth) {
    int x = (int)floorf(u), y = (int)floorf(v);
    x = tiled ? wrap(x, img->w) : clampi(x, 0, img->w - 1);
    y = tiled ? wrap(y, img->h) : clampi(y, 0, img->h - 1);
    uint32_t p = img->px[(size_t)y * img->w + x];
    out[0] = (float)(p & 255); out[1] = (float)((p >> 8) & 255);
    out[2] = (float)((p >> 16) & 255); out[3] = (float)(p >> 24);
    return;
  }
  u -= 0.5f; v -= 0.5f;
  float fu = floorf(u), fv = floorf(v);
  float tx = u - fu, ty = v - fv;
  int x0 = (int)fu, y0 = (int)fv, x1 = x0 + 1, y1 = y0 + 1;
  if (tiled) { x0 = wrap(x0, img->w); x1 = wrap(x1, img->w); y0 = wrap(y0, img->h); y1 = wrap(y1, img->h); }
  else {
    x0 = clampi(x0, 0, img->w - 1); x1 = clampi(x1, 0, img->w - 1);
    y0 = clampi(y0, 0, img->h - 1); y1 = clampi(y1, 0, img->h - 1);
  }
  uint32_t p00 = img->px[(size_t)y0 * img->w + x0], p10 = img->px[(size_t)y0 * img->w + x1];
  uint32_t p01 = img->px[(size_t)y1 * img->w + x0], p11 = img->px[(size_t)y1 * img->w + x1];
  float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
  for (int k = 0; k < 4; k++) {
    int sh = 8 * k;
    out[k] = ((p00 >> sh) & 255) * w00 + ((p10 >> sh) & 255) * w10
           + ((p01 >> sh) & 255) * w01 + ((p11 >> sh) & 255) * w11;
  }
}

static void draw_image(CGContextRef c, CGRect rect, CGImageRef img, bool tiled) {
  if (!c || !img || rect.size.width == 0 || rect.size.height == 0) return;
  struct gstate* g = GS(c);
  // image pixel space (v down from the top row) -> user space -> raster space
  CGAffineTransform to_user = CGAffineTransformMake(rect.size.width / img->w, 0, 0,
      -rect.size.height / img->h, rect.origin.x, rect.origin.y + rect.size.height);
  CGAffineTransform m = CGAffineTransformConcat(to_user, g->ctm);
  CGFloat det = m.a * m.d - m.b * m.c;
  if (fabs(det) < 1e-12) return;
  CGAffineTransform inv = CGAffineTransformInvert(m);

  struct irect area = clip_bounds(c);
  if (!tiled) {
    CGPoint q[4] = { xf(&m, 0, 0), xf(&m, img->w, 0), xf(&m, 0, img->h), xf(&m, img->w, img->h) };
    CGFloat lx = q[0].x, hx = q[0].x, ly = q[0].y, hy = q[0].y;
    for (int i = 1; i < 4; i++) {
      lx = fmin(lx, q[i].x); hx = fmax(hx, q[i].x); ly = fmin(ly, q[i].y); hy = fmax(hy, q[i].y);
    }
    struct irect r = { (int)floor(lx), (int)floor(ly), (int)ceil(hx), (int)ceil(hy) };
    area = irect_intersect(area, r);
  }
  if (irect_empty(area)) return;

  // Texels per output pixel: minified tiles are box-filtered with n x n taps.
  float texels = (float)fmax(sqrt(inv.a * inv.a + inv.b * inv.b), sqrt(inv.c * inv.c + inv.d * inv.d));
  bool smooth = g->interp != kCGInterpolationNone;
  int taps = smooth ? (int)ceilf(texels - 0.25f) : 1;
  if (taps < 1) taps = 1;
  if (taps > 4) taps = 4;
  const float inv_taps = 1.f / (taps * taps);
  const struct clip* cl = g->clip;
  float alpha = g->alpha;

  for (int y = area.y0; y < area.y1; y++) {
    uint8_t* row = c->data + (size_t)y * c->stride;
    for (int x = area.x0; x < area.x1; x++) {
      unsigned k = clip_at(cl, x, y);
      if (!k) continue;
      float acc[4] = { 0, 0, 0, 0 };
      int hits = 0;
      for (int j = 0; j < taps; j++)
        for (int i = 0; i < taps; i++) {
          float px = x + (i + 0.5f) / taps, py = y + (j + 0.5f) / taps;
          float u = (float)(inv.a * px + inv.c * py + inv.tx);
          float v = (float)(inv.b * px + inv.d * py + inv.ty);
          if (!tiled && (u < 0 || v < 0 || u >= img->w || v >= img->h)) continue;
          float s[4];
          sample(img, u, v, tiled, smooth, s);
          acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; acc[3] += s[3];
          hits++;
        }
      if (!hits) continue;
      float t = k / 255.f * alpha * inv_taps;
      float sa = acc[3] * t;
      if (c->gray) {
        float l = (0.114f * acc[0] + 0.587f * acc[1] + 0.299f * acc[2]) * t;
        row[x] = (uint8_t)fminf(255.f, l + row[x] * (1 - sa / 255.f) + 0.5f);
        continue;
      }
      uint32_t* d = (uint32_t*)row + x;
      uint32_t p = *d;
      float keep = 1 - sa / 255.f;
      float b = acc[0] * t + (p & 255) * keep;
      float gg = acc[1] * t + ((p >> 8) & 255) * keep;
      float r = acc[2] * t + ((p >> 16) & 255) * keep;
      float a = sa + (p >> 24) * keep;
      *d = ((uint32_t)fminf(255.f, a + 0.5f) << 24) | ((uint32_t)fminf(255.f, r + 0.5f) << 16)
         | ((uint32_t)fminf(255.f, gg + 0.5f) << 8) | (uint32_t)fminf(255.f, b + 0.5f);
    }
  }
}

void CGContextDrawImage(CGContextRef c, CGRect rect, CGImageRef image) {
  draw_image(c, rect, image, false);
}
void CGContextDrawTiledImage(CGContextRef c, CGRect rect, CGImageRef image) {
  draw_image(c, rect, image, true);
}

// --- strings and image sources ----------------------------------------------
struct cgstring { struct cgobj hdr; char* s; };
struct CGImageSource { struct cgobj hdr; char* path; };

CFStringRef CFStringCreateWithCString(CFAllocatorRef alloc, const char* s, CFStringEncoding enc) {
  (void)alloc; (void)enc;
  if (!s) return NULL;
  struct cgstring* str = calloc(1, sizeof *str);
  if (!str) return NULL;
  str->hdr.type = OBJ_STRING; str->hdr.refs = 1;
  str->s = _strdup(s);
  return (CFStringRef)str;
}

CFURLRef CFURLCreateWithFileSystemPath(CFAllocatorRef alloc, CFStringRef path, int style, bool dir) {
  (void)style; (void)dir;
  if (!path) return NULL;
  return (CFURLRef)CFStringCreateWithCString(alloc, ((const struct cgstring*)path)->s, kCFStringEncodingUTF8);
}

CGImageSourceRef CGImageSourceCreateWithURL(CFURLRef url, CFDictionaryRef options) {
  (void)options;
  if (!url) return NULL;
  struct CGImageSource* src = calloc(1, sizeof *src);
  if (!src) return NULL;
  src->hdr.type = OBJ_SOURCE; src->hdr.refs = 1;
  src->path = _strdup(((const struct cgstring*)url)->s);
  return src;
}

CGImageRef CGImageSourceCreateImageAtIndex(CGImageSourceRef src, size_t index, CFDictionaryRef options) {
  (void)options;
  if (!src || index != 0) return NULL;
  return cg_image_load_file(src->path);
}

// --- reference counting -----------------------------------------------------
CFTypeRef CFRetain(CFTypeRef object) {
  if (object) ((struct cgobj*)object)->refs++;
  return object;
}

void CFRelease(CFTypeRef object) {
  if (!object) return;
  struct cgobj* o = (struct cgobj*)object;
  if (o->type == OBJ_SPACE) return;
  if (--o->refs > 0) return;
  switch (o->type) {
    case OBJ_PATH: path_free_storage((struct CGPath*)o); free(o); break;
    case OBJ_CONTEXT: context_free((struct CGContext*)o); break;
    case OBJ_IMAGE: free(((struct CGImage*)o)->px); free(o); break;
    case OBJ_STRING: free(((struct cgstring*)o)->s); free(o); break;
    case OBJ_SOURCE: free(((struct CGImageSource*)o)->path); free(o); break;
    default: free(o); break;
  }
}

// --- stubs for src/misc/drawing.h -------------------------------------------
const int kCFTypeArrayCallBacks = 0;
static struct cgobj* stub(void) {
  struct cgobj* o = calloc(1, sizeof *o);
  if (o) { o->type = OBJ_STUB; o->refs = 1; }
  return o;
}
CGColorRef CGColorCreateGenericRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  (void)r; (void)g; (void)b; (void)a; return (CGColorRef)stub();
}
CGColorRef CGColorCreateSRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  return CGColorCreateGenericRGB(r, g, b, a);
}
void CGColorRelease(CGColorRef color) { if (color) CFRelease(color); }
void CGContextSetShadowWithColor(CGContextRef c, CGSize o, CGFloat blur, CGColorRef color) {
  (void)c; (void)o; (void)blur; (void)color;
}
void CGContextReplacePathWithStrokedPath(CGContextRef c) { (void)c; }
CFArrayRef CFArrayCreate(CFAllocatorRef alloc, const void** v, CFIndex n, const void* cb) {
  (void)alloc; (void)v; (void)n; (void)cb; return (CFArrayRef)stub();
}
CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef s, CFArrayRef colors, const CGFloat* l) {
  (void)s; (void)colors; (void)l; return (CGGradientRef)stub();
}
void CGContextDrawLinearGradient(CGContextRef c, CGGradientRef g, CGPoint a, CGPoint b, uint32_t o) {
  (void)c; (void)g; (void)a; (void)b; (void)o;
}
