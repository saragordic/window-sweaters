// Icon-derived colourways: By App sweaters for apps without an authored rule,
// and the shared Zigzag's main yarn for every app.
// Cache, charts and callbacks live on the main thread. Only icon decoding and
// palette extraction run on the serial worker; it never touches renderer state.
#import <Cocoa/Cocoa.h>
#include "misc/autoyarn.h"
#include "misc/chart.h"
#include "misc/apps.h"
#include "misc/knit.h"
#include <pthread.h>

#define KNIT_AUTO_MAX 64
struct auto_yarn {
  char app[64];
  pid_t pid;
  uint32_t base, contrast;
  int chart;             // one generated chart per app, for the current mode
  int chart_kind;        // which mode that chart was built for
  uint32_t chart_yarn;   // and in which contrast yarn
  unsigned generation;
  uint64_t request, used;
  CFAbsoluteTime retry_after;
  bool pending, ok, dirty;
};
static struct auto_yarn g_auto[KNIT_AUTO_MAX];
static int g_auto_count;
static uint64_t g_auto_clock;
static dispatch_queue_t g_auto_queue;
// Owners turned away because every slot held in-flight work. When a request
// settles, only these are asked again, each once; one still turned away waits
// for the next completion. Never a redraw of everyone: with more apps than
// slots, that evicts and re-requests forever.
// The list grows as needed, so no turned-away owner is ever dropped.
static pid_t* g_deferred;
static int g_deferred_count, g_deferred_capacity;

static void defer_owner(pid_t pid) {
  for (int i = 0; i < g_deferred_count; i++) if (g_deferred[i] == pid) return;
  if (g_deferred_count == g_deferred_capacity) {
    int capacity = g_deferred_capacity ? g_deferred_capacity * 2 : KNIT_AUTO_MAX;
    pid_t* grown = realloc(g_deferred, sizeof(pid_t) * (size_t)capacity);
    if (!grown) return;                  // out of memory: it waits for a natural redraw
    g_deferred = grown;
    g_deferred_capacity = capacity;
  }
  g_deferred[g_deferred_count++] = pid;
}

static void retry_deferred(void) {
  if (!g_deferred_count) return;
  // Take the list: a retry may defer its owner again, into a fresh one.
  pid_t* owners = g_deferred;
  int count = g_deferred_count;
  g_deferred = NULL;
  g_deferred_count = g_deferred_capacity = 0;
  for (int i = 0; i < count; i++) knit_auto_yarn_ready(owners[i]);
  if (!g_deferred) { g_deferred = owners; g_deferred_capacity = count; }   // reuse storage
  else free(owners);
}
enum { CHART_NONE, CHART_BY_APP, CHART_ZIGZAG };
static const uint32_t CREAM = KNIT_CREAM;                    // the collection's cream

static void rgb2hsl(uint32_t v, double* h, double* s, double* l) {
  double r=((v>>16)&255)/255.0, g=((v>>8)&255)/255.0, b=(v&255)/255.0;
  double mx=fmax(r,fmax(g,b)), mn=fmin(r,fmin(g,b)); *l=(mx+mn)/2; *h=0; *s=0;
  if (mx!=mn) { double d=mx-mn; *s = *l>0.5 ? d/(2-mx-mn) : d/(mx+mn);
    if (mx==r) *h=(g-b)/d+(g<b?6:0); else if (mx==g) *h=(b-r)/d+2; else *h=(r-g)/d+4; *h/=6; }
}
static uint32_t hsl2rgb(double h, double s, double l) {
  double q = l<0.5 ? l*(1+s) : l+s-l*s, p = 2*l-q, t[3]={h+1.0/3,h,h-1.0/3}, o[3];
  for (int i=0;i<3;i++) { double c=t[i]; if(c<0)c+=1; if(c>1)c-=1;
    o[i] = c<1.0/6 ? p+(q-p)*6*c : c<0.5 ? q : c<2.0/3 ? p+(q-p)*(2.0/3-c)*6 : p; }
  return 0xff000000u|((uint32_t)(o[0]*255)<<16)|((uint32_t)(o[1]*255)<<8)|(uint32_t)(o[2]*255);
}
// The hand-picked palette averages ~0.49 saturation; raw icon colour is near 1.0,
// so it is halved and kept mid-toned or the band looks like plastic, not wool.
static uint32_t soften(uint32_t v, double lshift) {
  double h,s,l; rgb2hsl(v,&h,&s,&l);
  s = fmin(0.60, fmax(0.22, s*0.48));
  l = fmin(0.72, fmax(0.30, l*0.85+0.10+lshift));
  return hsl2rgb(h,s,l);
}

// Pixels are read through CoreGraphics rather than NSGraphicsContext so this
// stays safe when a border is drawn off the main thread.
static bool icon_pixels(NSImage* icon, uint32_t* out_base, uint32_t* out_contrast) {
  if (!icon) return false;
  CGImageRef cg = [icon CGImageForProposedRect:NULL context:nil hints:nil];
  if (!cg) return false;

  const int S = 48;
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  uint32_t* px = calloc((size_t)S*S, 4);
  if (!px) { CGColorSpaceRelease(cs); return false; }
  CGContextRef ctx = CGBitmapContextCreate(px,S,S,8,S*4,cs,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(cs);
  if (!ctx) { free(px); return false; }
  CGContextDrawImage(ctx, CGRectMake(0,0,S,S), cg);
  CGContextRelease(ctx);

  int counts[4096];
  memset(counts,0,sizeof counts);
  int kept = 0;
  for (int i=0;i<S*S;i++) {
    uint32_t v = px[i];
    if (((v>>24)&255) < 217) continue;                       // transparent corners
    // CoreGraphics gives premultiplied RGB; recover the actual icon colour.
    double alpha = (v >> 24) & 255;
    double r=((v>>16)&255)/alpha,g=((v>>8)&255)/alpha,b=(v&255)/alpha;
    double mx=fmax(r,fmax(g,b)), mn=fmin(r,fmin(g,b));
    double sat = mx<=0 ? 0 : (mx-mn)/mx;
    // Divide-by-brightness saturation calls a faintly tinted near-black
    // "colourful"; require real colour strength too, or a black-and-white
    // icon (ChatGPT's knot) reads as navy.
    if (sat < 0.18 || mx < 0.12 || mx - mn < 0.10) continue;   // greys, whites, blacks
    counts[((int)(r*15)<<8)|((int)(g*15)<<4)|(int)(b*15)]++; kept++;
  }
  free(px);
  // ~1.7% of the icon: enough for a small accent on a neutral icon (Calculator's
  // orange keys), not so little that a stray highlight counts as a colour.
  if (kept < S*S/60) return false;                            // no committed colour

  uint32_t picked[3]; int np=0;
  for (int pass=0; pass<3 && np<3; pass++) {
    int best=-1,bc=0;
    for (int i=0;i<4096;i++) {
      if (!counts[i]) continue;
      uint32_t col=0xff000000u|((uint32_t)(((i>>8)&15)/15.0*255)<<16)
                  |((uint32_t)(((i>>4)&15)/15.0*255)<<8)|(uint32_t)((i&15)/15.0*255);
      bool far=true;
      double ch,cs_,cl; rgb2hsl(col,&ch,&cs_,&cl);
      for (int j=0;j<np;j++) {
        double ph,ps,pl; rgb2hsl(picked[j],&ph,&ps,&pl);
        double dh=fabs(ch-ph); if (dh>0.5) dh=1-dh;
        if (dh<0.06 && fabs(cl-pl)<0.20) far=false;
      }
      if (far && counts[i]>bc) { bc=counts[i]; best=i; }
    }
    if (best<0) break;
    picked[np++]=0xff000000u|((uint32_t)(((best>>8)&15)/15.0*255)<<16)
                |((uint32_t)(((best>>4)&15)/15.0*255)<<8)|(uint32_t)((best&15)/15.0*255);
  }
  if (!np) return false;

  double bh,bs,bl; rgb2hsl(picked[0],&bh,&bs,&bl);
  int ci=-1;
  for (int j=1;j<np;j++) { double h2,s2,l2; rgb2hsl(picked[j],&h2,&s2,&l2);
    if (fabs(l2-bl) >= 0.20) { ci=j; break; } }
  *out_base = soften(picked[0],0);
  *out_contrast = ci>=0 ? soften(picked[ci],0) : CREAM;
  // Softening can erase a contrast that existed in the raw icon. Check the
  // yarns that will actually be rendered, including after lightness clamping.
  double h,s,l, ch,contrast_s,cl;
  rgb2hsl(*out_base,&h,&s,&l);
  rgb2hsl(*out_contrast,&ch,&contrast_s,&cl);
  if (ci < 0 || fabs(l-cl) < 0.20)
    *out_contrast = l < 0.62 ? CREAM : hsl2rgb(h,s,l-0.26);
  return true;
}

// Clone a built-in motif knitted in one contrast yarn. Each app owns at most
// one generated chart, rebuilt in place when its mode or yarn changes, so the
// chart table stays bounded by the cache. Cached tiles must be dropped before
// a chart is replaced.
static int build_chart(struct auto_yarn* entry, const char* motif, int kind, uint32_t yarn) {
  int target = entry->generation == g_charts_generation ? entry->chart : -1;
  if (target < 0 || target >= g_chart_count || !g_charts[target].generated)
    target = -1;
  if (target < 0 && g_chart_count >= KNIT_CHART_MAX) return -1;
  int source = knit_chart_index(motif);
  if (source < 0) return -1;
  struct knit_chart chart = g_charts[source];
  if (!chart.px || chart.w <= 0 || chart.h <= 0) return -1;
  uint32_t* pixels = malloc(sizeof(uint32_t) * chart.w * chart.h);
  if (!pixels) return -1;
  for (int k = 0; k < chart.w * chart.h; k++)
    pixels[k] = (chart.px[k] >> 24) >= 128 ? yarn : 0;
  chart.px = pixels;
  chart.generated = true;
  // Use a request identity rather than truncating a potentially long app name.
  snprintf(chart.name, sizeof chart.name, "auto-%llu", (unsigned long long)entry->request);
  if (chart.corner_color) chart.corner_color = yarn;
  if (chart.cuff_color) chart.cuff_color = yarn;
  if (target < 0) target = g_chart_count++;
  else { knit_flush_cache(); free(g_charts[target].px); }
  g_charts[target] = chart;
  entry->chart_kind = kind;
  entry->chart_yarn = yarn;
  return target;
}

// The By App motif is a stable hash of the app name, independent of colour.
static const char* by_app_motif(const char* app) {
  static const char* lean[] = { "zigzag", "picnic", "twinkle" };
  unsigned hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)app; *p; p++)
    hash = (hash ^ *p) * 16777619u;
  return lean[hash % 3];
}

static NSImage* icon_for_pid(pid_t pid) {
#ifdef KNIT_AUTO_TEST
  extern NSImage* knit_test_icon(pid_t pid);
  return knit_test_icon(pid);
#else
  // The window owner is authoritative. Localized display names, executable
  // names and helpers need not match, and prefixes can select another app.
  return [NSRunningApplication runningApplicationWithProcessIdentifier:pid].icon;
#endif
}

static void sample_icon(int slot) {
  struct auto_yarn* entry = &g_auto[slot];
  entry->pending = true;
  uint64_t request = entry->request;
  pid_t pid = entry->pid;
  if (!g_auto_queue)
    g_auto_queue = dispatch_queue_create("local.knitborders.autoyarn", DISPATCH_QUEUE_SERIAL);
  dispatch_async(g_auto_queue, ^{
    @autoreleasepool {
      NSImage* icon = icon_for_pid(pid);
      uint32_t base = 0, contrast = 0;
      bool ok = icon_pixels(icon, &base, &contrast);
      bool missing = icon == nil;
      dispatch_async(dispatch_get_main_queue(), ^{
        struct auto_yarn* current = &g_auto[slot];
        if (current->request != request) { retry_deferred(); return; }
        current->pending = false;
        current->ok = ok;
        current->dirty = ok;
        current->base = base;
        current->contrast = contrast;
        // A launching app may not expose an icon yet. Retry later, without
        // repeatedly decoding a genuinely colourless icon on every redraw.
        current->retry_after = missing ? CFAbsoluteTimeGetCurrent() + 5.0 : INFINITY;
        if (ok) knit_auto_yarn_ready(pid);
        else if (missing) {
          // Retry a still-visible launching window even if it never moves.
          // The callback resolves live windows; closed windows do no work.
          dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                         dispatch_get_main_queue(), ^{
            if (g_auto[slot].request == request && !g_auto[slot].pending)
              knit_auto_yarn_ready(pid);
          });
        }
        retry_deferred();                    // this slot is free to hand on
      });
    }
  });
}

// Find or start the icon work for this app. Returns NULL only for unusable
// input or a cache full of in-flight work. The entry may still be pending.
static struct auto_yarn* auto_entry(const char* app, pid_t pid) {
  assert(pthread_main_np());
  if (!app || !*app || strlen(app) >= sizeof g_auto[0].app || pid <= 0) return NULL;
  int slot = -1;
  for (int i = 0; i < g_auto_count; i++)
    if (g_auto[i].pid == pid && strcmp(g_auto[i].app, app) == 0) { slot = i; break; }
  if (slot < 0) {
    if (g_auto_count < KNIT_AUTO_MAX) slot = g_auto_count++;
    else {
      // Bound memory without permanently refusing the 65th app. Never evict
      // in-flight requests; callbacks carry a token so old work cannot win.
      for (int i = 0; i < g_auto_count; i++)
        if (!g_auto[i].pending && (slot < 0 || g_auto[i].used < g_auto[slot].used)) slot = i;
      if (slot < 0) { defer_owner(pid); return NULL; }
    }
    struct auto_yarn* entry = &g_auto[slot];
    // A recycled slot keeps its chart, to be rebuilt for the new app.
    int previous = entry->generation == g_charts_generation ? entry->chart : -1;
    *entry = (struct auto_yarn){.pid=pid, .chart=previous,
      .generation=g_charts_generation, .request=++g_auto_clock};
    snprintf(entry->app,sizeof entry->app,"%s",app);
    entry->used = ++g_auto_clock;
    sample_icon(slot);
    return entry;
  }
  struct auto_yarn* entry = &g_auto[slot];
  entry->used = ++g_auto_clock;
  if (!entry->pending && !entry->ok && CFAbsoluteTimeGetCurrent() >= entry->retry_after)
    sample_icon(slot);
  return entry;
}

// The chart this entry needs, rebuilt only when the mode, the yarn or the
// chart table has changed since it was last built.
static int entry_chart(struct auto_yarn* entry, const char* motif, int kind, uint32_t yarn) {
  if (entry->generation != g_charts_generation || entry->chart < 0 || entry->dirty
      || entry->chart_kind != kind || entry->chart_yarn != yarn) {
    entry->chart = build_chart(entry, motif, kind, yarn);
    entry->generation = g_charts_generation;
    entry->dirty = false;
  }
  return entry->chart;
}

bool knit_auto_yarn(const char* app, pid_t pid, uint32_t* yarn, int* chart) {
  if (!yarn || !chart || knit_app_rule(app)) return false;
  struct auto_yarn* entry = auto_entry(app, pid);
  if (!entry || entry->pending || !entry->ok) return false;
  // Defer chart allocation in plain/global modes. Colours remain app-derived,
  // while the explicit pattern keeps the user's selected motif and yarns.
  *yarn = entry->base;
  *chart = g_knit_pattern_by_app
         ? entry_chart(entry, by_app_motif(entry->app), CHART_BY_APP, entry->contrast)
         : -1;
  return true;
}

bool knit_zigzag_active(void) {
  return !g_knit_pattern_by_app && g_chart_active >= 0
         && g_chart_active == knit_chart_index("zigzag");
}

static uint32_t zigzag_yarn(uint32_t base) { return knit_zigzag_contrast(base); }

bool knit_zigzag_yarn(const char* app, pid_t pid, uint32_t* yarn, int* chart) {
  if (!yarn || !chart) return false;
  // A colour the user chose in apps.conf is theirs in every pattern; only the
  // built-in collection gives way to icon colours here.
  const struct app_rule* rule = knit_app_rule(app);
  bool personal = rule && knit_app_rule_personal(rule);
  struct auto_yarn* entry = personal ? NULL : auto_entry(app, pid);
  bool changed = false;
  if (entry && !entry->pending && entry->ok) { *yarn = entry->base; changed = true; }
  // A zigzag the user drew themselves is shown exactly as drawn, for every app.
  if (*chart >= 0 && *chart < g_chart_count && g_charts[*chart].custom) return changed;
  uint32_t contrast = zigzag_yarn(*yarn);
  if (contrast == CREAM) return changed;          // the shared zigzag is already cream
  if (!entry) entry = auto_entry(app, pid);       // a personal colour still needs a chart slot
  // No room for this app's own zigzag: plain knitting beats cream on cream.
  *chart = entry ? entry_chart(entry, "zigzag", CHART_ZIGZAG, contrast) : -1;
  return true;
}
