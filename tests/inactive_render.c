// Inactive-window greying across every chart, radius and scale; bitmaps only, no windows.
#define main original_render_main
#include "render.c"
#undef main
#include "../src/parse.h"

static void check_ring(void) {
  int cases = 0, coloured_edges = 0, alpha_changes = 0, opaque_coloured = 0;
  const float radii[] = {0, 10, 80};
  for (int chart = -1; chart < g_chart_count; chart++)
    for (int scale = 1; scale <= 2; scale++)
      for (int r = 0; r < 3; r++) {
        CGContextRef a = canvas(160, 120, scale), b = canvas(160, 120, scale);
        CGRect win = CGRectMake(24, 24, 112, 72);
        knit_draw(a, win, radii[r], 12, 0xffd0503a, chart, 0, 1);
        knit_draw_faded(b, win, radii[r], 12, 0xffd0503a, chart, 0, 1, 1);
        // Greying must not mutate the shared colour tile retained by the cache.
        const void* identity = knit_tile_identity(12, 0xffd0503a, chart);
        CGContextRef again = canvas(160, 120, scale);
        knit_draw(again, win, radii[r], 12, 0xffd0503a, chart, 0, 1);
        assert(identity == knit_tile_identity(12, 0xffd0503a, chart));
        for (int y = 0; y < 120*scale; y++) for (int x = 0; x < 160*scale; x++) {
          uint32_t p = pixel(a,x,y), q = pixel(b,x,y);
          assert(p == pixel(again,x,y));
          int pa = p >> 24, qa = q >> 24;
          assert(pa != 0 || qa == 0); // no new coverage outside the original ring
          assert(pa != 255 || qa == 255); // opaque body and corner seams stay opaque
          int red=(q>>16)&255, green=(q>>8)&255, blue=q&255;
          if (qa == 255 && (abs(red-green)>2 || abs(green-blue)>2)) {
            if (opaque_coloured++ < 8) fprintf(stderr, "coloured opaque pixel: chart=%d scale=%d radius=%g x=%d y=%d before=%08x after=%08x\n", chart,scale,radii[r],x,y,p,q);
          }
          if (qa > 0 && qa < 255 && (abs(red-green)>2 || abs(green-blue)>2)) coloured_edges++;
          if (pa != qa) alpha_changes++;
        }
        CGContextRelease(a); CGContextRelease(b); CGContextRelease(again); cases++;
      }
  printf("PASS: Checked %d all-pixel ring cases; antialiased coloured edge pixels=%d, alpha changes=%d\n", cases, coloured_edges, alpha_changes);
  fprintf(stderr, "Opaque coloured pixels: %d\n", opaque_coloured);
  assert(opaque_coloured == 0);
  assert(coloured_edges == 0);
  assert(alpha_changes == 0);
}

static void check_invalid(void) {
  struct settings settings = {0};
  const char* invalid[] = {"grey=nan", "gray=NAN", "grey=inf", "gray=-inf",
                          "grey=1e99", "grey=oops", "grey="};
  for (size_t i=0; i<sizeof invalid/sizeof *invalid; i++) {
    char* arg = (char*)invalid[i];
    g_knit_grey = .4f;
    assert(parse_settings(&settings, 1, &arg) == 0 && g_knit_grey == .4f);
  }
  const float values[] = {NAN, INFINITY, -INFINITY, -1, 0, 2};
  for (size_t i=0; i<sizeof values/sizeof *values; i++) {
    CGContextRef a=canvas(160,120,2), b=canvas(160,120,2);
    CGRect win=CGRectMake(24,24,112,72);
    float expected=values[i]==2 ? 1 : 0;
    knit_draw_faded(a,win,10,12,0xffd0503a,-1,0,expected,1);
    knit_draw_faded(b,win,10,12,0xffd0503a,-1,0,values[i],1);
    for (int y=0;y<240;y++) for (int x=0;x<320;x++)
      assert(pixel(a,x,y)==pixel(b,x,y));
    CGContextRelease(a); CGContextRelease(b);
  }
  char* low="grey=-1", *high="gray=2";
  assert(parse_settings(&settings,1,&low)==BORDER_UPDATE_MASK_ALL && g_knit_grey==0);
  assert(parse_settings(&settings,1,&high)==BORDER_UPDATE_MASK_ALL && g_knit_grey==1);
  puts("PASS: invalid parser values preserve state; renderer fallback/clamping is pixel-identical");
}

int main(int argc, char** argv) {
  knit_charts_load("charts");
  if (argc > 1 && !strcmp(argv[1], "--nonfinite")) {
    check_invalid();
    struct settings settings = {0};
    char* arg = "grey=nan";
    g_knit_grey = 1;
    // Not a number: ignored, the current greying is kept.
    assert(parse_settings(&settings, 1, &arg) == 0 && g_knit_grey == 1);
    g_knit_grey = NAN;  // and the renderer survives one that slips through
    CGContextRef c = canvas(160,120,1);
    knit_draw_faded(c, CGRectMake(24,24,112,72),10,12,0xffd0503a,-1,0,g_knit_grey,1);
    // Invalid optional greying must not erase the whole inactive sweater.
    assert((pixel(c,80,18)>>24) == 255);
    CGContextRelease(c);
  } else check_ring();
  knit_flush_cache();
  return 0;
}
