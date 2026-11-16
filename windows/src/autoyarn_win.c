// Icon-derived colourways, a port of src/autoyarn.m: By App sweaters for apps
// without an authored rule, and the shared Zigzag's main yarn for every app.
//
// The colour analysis is the same as on macOS. Icons are read synchronously
// on the main thread, once per process, because extracting a Windows icon is
// a few milliseconds of work; there is no worker queue to keep in sync.
#include "app.h"
#include "src/misc/autoyarn.h"
#include "src/misc/chart.h"
#include "src/misc/apps.h"
#include "src/misc/knit.h"
#include <stdio.h>
#include <stdlib.h>

#define KNIT_AUTO_MAX 64
#define ICON_SIZE 48

struct auto_yarn {
  char app[64];
  pid_t pid;
  uint32_t base, contrast;
  int chart;
  int chart_kind;
  uint32_t chart_yarn;
  unsigned generation;
  uint64_t request, used;
  ULONGLONG retry_after;
  int retries;
  bool ok, dirty, sampled;
};
static struct auto_yarn g_auto[KNIT_AUTO_MAX];
static int g_auto_count;
static uint64_t g_auto_clock;

enum { CHART_NONE, CHART_BY_APP, CHART_ZIGZAG };
static const uint32_t CREAM = KNIT_CREAM;

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
static uint32_t soften(uint32_t v, double lshift) {
  double h,s,l; rgb2hsl(v,&h,&s,&l);
  s = fmin(0.60, fmax(0.22, s*0.48));
  l = fmin(0.72, fmax(0.30, l*0.85+0.10+lshift));
  return hsl2rgb(h,s,l);
}

// Same thresholds as icon_pixels() in src/autoyarn.m.
static bool icon_colours(const uint32_t* px, uint32_t* out_base, uint32_t* out_contrast) {
  const int S = ICON_SIZE;
  static int counts[4096];
  memset(counts,0,sizeof counts);
  int kept = 0;
  for (int i=0;i<S*S;i++) {
    uint32_t v = px[i];
    if (((v>>24)&255) < 217) continue;
    double alpha = (v >> 24) & 255;
    double r=((v>>16)&255)/alpha,g=((v>>8)&255)/alpha,b=(v&255)/alpha;
    if (r > 1) r = 1; if (g > 1) g = 1; if (b > 1) b = 1;
    double mx=fmax(r,fmax(g,b)), mn=fmin(r,fmin(g,b));
    double sat = mx<=0 ? 0 : (mx-mn)/mx;
    if (sat < 0.18 || mx < 0.12 || mx - mn < 0.10) continue;
    counts[((int)(r*15)<<8)|((int)(g*15)<<4)|(int)(b*15)]++; kept++;
  }
  if (kept < S*S/60) return false;

  uint32_t picked[3]; int np=0;
  for (int pass=0; pass<3 && np<3; pass++) {
    int best=-1,bc=0;
    for (int i=0;i<4096;i++) {
      if (!counts[i]) continue;
      uint32_t col=0xff000000u|((uint32_t)(((i>>8)&15)/15.0*255)<<16)
                  |((uint32_t)(((i>>4)&15)/15.0*255)<<8)|(uint32_t)((i&15)/15.0*255);
      bool distinct=true;
      double ch,cs_,cl; rgb2hsl(col,&ch,&cs_,&cl);
      for (int j=0;j<np;j++) {
        double ph,ps,pl; rgb2hsl(picked[j],&ph,&ps,&pl);
        double dh=fabs(ch-ph); if (dh>0.5) dh=1-dh;
        if (dh<0.06 && fabs(cl-pl)<0.20) distinct=false;
      }
      if (distinct && counts[i]>bc) { bc=counts[i]; best=i; }
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
  double h,s,l, ch,contrast_s,cl;
  rgb2hsl(*out_base,&h,&s,&l);
  rgb2hsl(*out_contrast,&ch,&contrast_s,&cl);
  if (ci < 0 || fabs(l-cl) < 0.20)
    *out_contrast = l < 0.62 ? CREAM : hsl2rgb(h,s,l-0.26);
  return true;
}

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

static const char* by_app_motif(const char* app) {
  static const char* lean[] = { "zigzag", "picnic", "twinkle" };
  unsigned hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)app; *p; p++)
    hash = (hash ^ *p) * 16777619u;
  return lean[hash % 3];
}

static VOID CALLBACK retry_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
  (void)hwnd; (void)msg; (void)time;
  KillTimer(NULL, id);
  // Redraw any window still owned by a process whose icon was not ready.
  for (int i = 0; i < g_auto_count; i++)
    if (!g_auto[i].ok && g_auto[i].retry_after) knit_auto_yarn_ready(g_auto[i].pid);
}

static void sample_icon(struct auto_yarn* entry) {
  static uint32_t px[ICON_SIZE * ICON_SIZE];
  memset(px, 0, sizeof px);
  bool have = proc_icon_pixels((DWORD)entry->pid, NULL, ICON_SIZE, px);
  uint32_t base = 0, contrast = 0;
  entry->ok = have && icon_colours(px, &base, &contrast);
  entry->dirty = entry->ok;
  entry->base = base;
  entry->contrast = contrast;
  entry->sampled = true;
  // A launching app may not expose an icon yet; try again in a few seconds.
  // Give up after a minute: some processes simply have no icon.
  bool retry = !have && entry->retries++ < 12;
  entry->retry_after = retry ? GetTickCount64() + 5000 : 0;
  if (retry) SetTimer(NULL, 0, 5200, retry_timer);
}

static struct auto_yarn* auto_entry(const char* app, pid_t pid) {
  if (!app || !*app || strlen(app) >= sizeof g_auto[0].app || pid <= 0) return NULL;
  int slot = -1;
  for (int i = 0; i < g_auto_count; i++)
    if (g_auto[i].pid == pid && strcmp(g_auto[i].app, app) == 0) { slot = i; break; }
  if (slot < 0) {
    if (g_auto_count < KNIT_AUTO_MAX) slot = g_auto_count++;
    else {
      for (int i = 0; i < g_auto_count; i++)
        if (slot < 0 || g_auto[i].used < g_auto[slot].used) slot = i;
    }
    struct auto_yarn* entry = &g_auto[slot];
    int previous = entry->generation == g_charts_generation ? entry->chart : -1;
    *entry = (struct auto_yarn){ .pid = pid, .chart = previous,
      .generation = g_charts_generation, .request = ++g_auto_clock };
    snprintf(entry->app, sizeof entry->app, "%s", app);
    entry->used = ++g_auto_clock;
    sample_icon(entry);
    return entry;
  }
  struct auto_yarn* entry = &g_auto[slot];
  entry->used = ++g_auto_clock;
  if (!entry->ok && entry->retry_after && GetTickCount64() >= entry->retry_after)
    sample_icon(entry);
  return entry;
}

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
  if (!entry || !entry->ok) return false;
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

bool knit_zigzag_yarn(const char* app, pid_t pid, uint32_t* yarn, int* chart) {
  if (!yarn || !chart) return false;
  const struct app_rule* rule = knit_app_rule(app);
  bool personal = rule && knit_app_rule_personal(rule);
  struct auto_yarn* entry = personal ? NULL : auto_entry(app, pid);
  bool changed = false;
  if (entry && entry->ok) { *yarn = entry->base; changed = true; }
  if (*chart >= 0 && *chart < g_chart_count && g_charts[*chart].custom) return changed;
  uint32_t contrast = knit_zigzag_contrast(*yarn);
  if (contrast == CREAM) return changed;
  if (!entry) entry = auto_entry(app, pid);
  *chart = entry ? entry_chart(entry, "zigzag", CHART_ZIGZAG, contrast) : -1;
  return true;
}
