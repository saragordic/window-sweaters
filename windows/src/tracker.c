// Following windows and knitting their sweaters.
//
// Each sweater is four click-through layered windows (top, bottom, left and
// right strips) stacked directly BELOW the window that wears it, so the window
// itself covers the part of the knit that tucks under its edge and anything
// above the window covers the sweater too. Strips keep the pixels drawn per
// sweater proportional to the band, not to the window: a full-screen window
// is a few hundred thousand pixels of knitting, never a full-screen bitmap.
//
// Changes arrive through out-of-context WinEvent hooks. A once-a-second
// reconcile pass repairs anything an event missed.
#define COBJMACROS
#include "app.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/misc/knit.h"
#include "src/misc/chart.h"
#include "src/misc/apps.h"
#include "src/misc/autoyarn.h"

enum { STRIP_TOP, STRIP_BOTTOM, STRIP_LEFT, STRIP_RIGHT, STRIPS };

struct sweater {
  HWND target;
  DWORD pid;
  char app[64];
  HWND strip[STRIPS];
  RECT strip_rect[STRIPS];   // relative to the outer frame's top left, physical px
  RECT frame;                // visible bounds of the target, physical px
  int band_px;
  UINT dpi;
  float radius;              // corner radius in DIPs
  bool focused, shown, topmost, rendered, resize_hidden;
  // what the strips currently show
  int drawn_w, drawn_h, drawn_band;
  UINT drawn_dpi;
  float drawn_radius;
};

static struct sweater** g_sweaters;
static int g_count, g_capacity;
static HWINEVENTHOOK g_hooks[8];
static int g_hook_count;
static HWND g_movesize;          // window being dragged or resized, if any
static bool g_busy;              // inside a pass that must not be re-entered
static bool g_restack_posted;
static ATOM g_strip_class;
static HDC g_memdc;
static bool g_win11;
static IVirtualDesktopManager* g_desktops;
static BOOL (WINAPI* p_IsWindowArranged)(HWND);

#define RECONCILE_TIMER 0x5357
#define RECONCILE_MS 1000

// --- windows that can wear a sweater ----------------------------------------

static bool cloaked(HWND h) {
  DWORD c = 0;
  return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof c)) && c;
}

static bool visible_frame(HWND h, RECT* r) {
  if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, r, sizeof *r))) return true;
  return GetWindowRect(h, r) != 0;
}

static UINT monitor_dpi(HWND h) {
  UINT x = 96, y = 96;
  HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
  if (!m || FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &x, &y))) return 96;
  return x ? x : 96;
}

static bool covers_monitor(HWND h, const RECT* f) {
  MONITORINFO mi = { sizeof mi };
  HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
  if (!m || !GetMonitorInfoW(m, &mi)) return false;
  return f->left <= mi.rcMonitor.left && f->top <= mi.rcMonitor.top
      && f->right >= mi.rcMonitor.right && f->bottom >= mi.rcMonitor.bottom;
}

static const wchar_t* k_skip_classes[] = {
  L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
  L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
  L"TopLevelWindowForOverflowXamlIsland", L"NotifyIconOverflowWindow",
  L"MultitaskingViewFrame", L"ForegroundStaging", L"TaskListThumbnailWnd",
  L"Xaml_WindowedPopupClass", L"Ghost", L"#32768", L"SysShadow",
  L"WindowSweatersStrip", L"WindowSweatersMessage",
};

// `any_app`: ignore the Apps menu, for listing every app that could wear one.
static bool eligible(HWND h, bool any_app) {
  if (!g_knit_on && !any_app) return false;
  if (!IsWindow(h) || !IsWindowVisible(h) || IsIconic(h) || IsZoomed(h)) return false;
  if (GetAncestor(h, GA_ROOT) != h) return false;
  LONG style = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
  if (style & WS_CHILD) return false;
  if ((ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) && !(ex & WS_EX_APPWINDOW)) return false;
  if ((style & WS_CAPTION) != WS_CAPTION && !(style & WS_THICKFRAME)) return false;
  if (cloaked(h)) return false;
  wchar_t cls[96];
  if (!GetClassNameW(h, cls, 96)) return false;
  for (size_t i = 0; i < sizeof k_skip_classes / sizeof k_skip_classes[0]; i++)
    if (wcscmp(cls, k_skip_classes[i]) == 0) return false;
  RECT f;
  if (!visible_frame(h, &f)) return false;
  UINT dpi = monitor_dpi(h);
  int min = MulDiv(48, (int)dpi, 96);
  if (f.right - f.left < min || f.bottom - f.top < min) return false;
  if (covers_monitor(h, &f)) return false;   // full screen: nowhere for a sweater
  DWORD pid = window_owner_pid(h);
  if (!pid || pid == GetCurrentProcessId()) return false;
  if (!any_app && knit_pid_hidden((int)pid)) return false;
  return true;
}

static float corner_radius(HWND h) {
  if (!g_win11) return 0.f;                       // Windows 10 windows are square
  if (p_IsWindowArranged && p_IsWindowArranged(h)) return 0.f;   // snapped
  DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DEFAULT;
  if (FAILED(DwmGetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref)))
    pref = DWMWCP_DEFAULT;
  if (pref == DWMWCP_DONOTROUND) return 0.f;
  if (pref == DWMWCP_ROUNDSMALL) return 4.f;
  return 8.f;
}

// --- strips -------------------------------------------------------------------

static LRESULT CALLBACK strip_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_DPICHANGED: return 0;   // sized by the tracker, never by the system
  }
  return DefWindowProcW(h, msg, wp, lp);
}

static HWND strip_create(void) {
  return CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                         MAKEINTATOM(g_strip_class), L"", WS_POPUP, 0, 0, 1, 1,
                         NULL, NULL, g_instance, NULL);
}

static void sweater_hide(struct sweater* s) {
  if (!s->shown) return;
  for (int i = 0; i < STRIPS; i++) if (s->strip[i]) ShowWindow(s->strip[i], SW_HIDE);
  s->shown = false;
}

static bool stacked(const struct sweater* s) {
  HWND h = s->target;
  for (int i = 0; i < STRIPS; i++) {
    h = GetWindow(h, GW_HWNDNEXT);
    if (h != s->strip[i]) return false;
  }
  return true;
}

static void sweater_stack(struct sweater* s, bool force) {
  if (!s->shown) return;
  bool topmost = (GetWindowLongW(s->target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  if (topmost != s->topmost) {
    for (int i = 0; i < STRIPS; i++)
      SetWindowPos(s->strip[i], topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    s->topmost = topmost;
    force = true;
  }
  if (!force && stacked(s)) return;
  HWND after = s->target;
  for (int i = 0; i < STRIPS; i++) {
    SetWindowPos(s->strip[i], after, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
    after = s->strip[i];
  }
}

// Strips belong to whichever virtual desktop they were made on; keep them on
// the target's, or they would vanish when the user switches desktops.
static void sweater_follow_desktop(struct sweater* s) {
  if (!g_desktops || !s->shown) return;
  BOOL here = TRUE;
  if (FAILED(IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(g_desktops, s->strip[0], &here))
      || here)
    return;
  GUID desk;
  if (SUCCEEDED(IVirtualDesktopManager_GetWindowDesktopId(g_desktops, s->target, &desk)))
    for (int i = 0; i < STRIPS; i++)
      IVirtualDesktopManager_MoveWindowToDesktop(g_desktops, s->strip[i], &desk);
}

static void colours_for(struct sweater* s, uint32_t* yarn_out, int* chart_out) {
  uint32_t yarn = knit_color_for_app(s->app);
  int chart = knit_pattern_for_app(s->app);
  const struct app_rule* rule = knit_app_rule(s->app);
  if (rule) yarn = rule->color;
  if (knit_zigzag_active()) {
    knit_zigzag_yarn(s->app, (pid_t)s->pid, &yarn, &chart);
  } else if (!rule) {
    uint32_t auto_yarn; int auto_chart;
    if (knit_auto_yarn(s->app, (pid_t)s->pid, &auto_yarn, &auto_chart)) {
      yarn = auto_yarn;
      if (auto_chart >= 0) chart = auto_chart;
    }
  }
  *yarn_out = yarn;
  *chart_out = chart;
}

// Lay out the strips around the frame and knit each of them.
static void sweater_render(struct sweater* s) {
  float scale = s->dpi / 96.f;
  int band_px = (int)lroundf(knit_current_width() * scale);
  if (band_px < 2) band_px = 2;
  float band = band_px / scale;
  int ww = s->frame.right - s->frame.left, wh = s->frame.bottom - s->frame.top;
  int fw = ww + 2 * band_px, fh = wh + 2 * band_px;

  // The same tuck clamp knit_draw applies, to size the strips to the ring.
  float tuck = g_knit.tuck < 1.f ? 1.f : g_knit.tuck;
  float half = fminf((float)ww, (float)wh) / scale * 0.5f - 2.f;
  if (tuck > half) tuck = half > 1.f ? half : 1.f;
  float irad = s->radius > tuck ? s->radius - tuck : 0.f;
  int depth = band_px + (int)ceilf((tuck + irad * (1.f - 0.70710678f) + 1.f) * scale);
  if (depth * 2 > fh) depth = fh / 2;
  if (depth * 2 > fw) depth = fw / 2;
  SetRect(&s->strip_rect[STRIP_TOP], 0, 0, fw, depth);
  SetRect(&s->strip_rect[STRIP_BOTTOM], 0, fh - depth, fw, fh);
  SetRect(&s->strip_rect[STRIP_LEFT], 0, depth, depth, fh - depth);
  SetRect(&s->strip_rect[STRIP_RIGHT], fw - depth, depth, fw, fh - depth);
  s->band_px = band_px;

  uint32_t yarn; int chart;
  colours_for(s, &yarn, &chart);
  float dim = s->focused ? 0.f : g_knit_dim;
  float grey = s->focused ? 0.f : g_knit_grey;
  CGRect win = CGRectMake(band, band, ww / scale, wh / scale);
  CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
  BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

  for (int i = 0; i < STRIPS; i++) {
    RECT r = s->strip_rect[i];
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) { ShowWindow(s->strip[i], SW_HIDE); continue; }
    uint32_t* bits = NULL;
    HBITMAP dib = knit_dib(w, h, &bits);
    if (!dib) continue;
    CGContextRef c = CGBitmapContextCreate(bits, (size_t)w, (size_t)h, 8, (size_t)w * 4, rgb,
                                           kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    if (c) {
      // Frame space is y-up from the frame's bottom edge, in DIPs.
      CGContextTranslateCTM(c, -r.left, -(fh - r.bottom));
      CGContextScaleCTM(c, scale, scale);
      knit_draw_faded(c, win, s->radius, band, yarn, chart, dim, grey, tuck);
      CGContextRelease(c);
    }
    GdiFlush();
    HGDIOBJ old = SelectObject(g_memdc, dib);
    POINT dst = { s->frame.left - band_px + r.left, s->frame.top - band_px + r.top };
    SIZE size = { w, h };
    POINT src = { 0, 0 };
    UpdateLayeredWindow(s->strip[i], NULL, &dst, &size, g_memdc, &src, 0, &blend, ULW_ALPHA);
    SelectObject(g_memdc, old);
    DeleteObject(dib);
    // A side strip that was empty on a tiny window comes back in place.
    if (s->shown && !IsWindowVisible(s->strip[i])) ShowWindow(s->strip[i], SW_SHOWNA);
  }
  CGColorSpaceRelease(rgb);
  s->drawn_w = ww; s->drawn_h = wh; s->drawn_band = band_px;
  s->drawn_dpi = s->dpi; s->drawn_radius = s->radius;
  s->rendered = true;
}

// Shown already in place under the target, so a new sweater never flashes
// above other windows for a frame.
static void sweater_show(struct sweater* s) {
  if (s->shown) return;
  bool topmost = (GetWindowLongW(s->target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  if (topmost != s->topmost) {
    for (int i = 0; i < STRIPS; i++)
      SetWindowPos(s->strip[i], topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    s->topmost = topmost;
  }
  HWND after = s->target;
  for (int i = 0; i < STRIPS; i++) {
    RECT r = s->strip_rect[i];
    UINT show = r.right > r.left && r.bottom > r.top ? SWP_SHOWWINDOW : 0;
    SetWindowPos(s->strip[i], after, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | show);
    after = s->strip[i];
  }
  s->shown = true;
  sweater_follow_desktop(s);
}

static void sweater_move(struct sweater* s) {
  HDWP dwp = BeginDeferWindowPos(STRIPS);
  HWND after = s->target;
  for (int i = 0; i < STRIPS && dwp; i++) {
    RECT r = s->strip_rect[i];
    dwp = DeferWindowPos(dwp, s->strip[i], after,
                         s->frame.left - s->band_px + r.left, s->frame.top - s->band_px + r.top,
                         0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    after = s->strip[i];
  }
  if (dwp) EndDeferWindowPos(dwp);
}

static bool is_focused(HWND target) {
  HWND fg = GetForegroundWindow();
  return fg && GetAncestor(fg, GA_ROOT) == target;
}

// --- the set of sweaters --------------------------------------------------------

static int find_index(HWND h) {
  for (int i = 0; i < g_count; i++) if (g_sweaters[i]->target == h) return i;
  return -1;
}

static void sweater_destroy_at(int index) {
  struct sweater* s = g_sweaters[index];
  for (int i = 0; i < STRIPS; i++) if (s->strip[i]) DestroyWindow(s->strip[i]);
  free(s);
  g_sweaters[index] = g_sweaters[--g_count];
}

// Bring one sweater in line with its window: hide, re-knit, or just follow.
static void sweater_update(struct sweater* s, bool redraw) {
  RECT f;
  if (!visible_frame(s->target, &f)) return;
  UINT dpi = monitor_dpi(s->target);
  float radius = corner_radius(s->target);
  bool focused = is_focused(s->target);
  int band_px = (int)lroundf(knit_current_width() * dpi / 96.f);
  if (band_px < 2) band_px = 2;
  bool resized = !s->rendered || f.right - f.left != s->drawn_w || f.bottom - f.top != s->drawn_h
              || dpi != s->drawn_dpi || radius != s->drawn_radius || band_px != s->drawn_band;
  bool moved = !EqualRect(&f, &s->frame);
  if (focused != s->focused && (g_knit_dim > 0.001f || g_knit_grey > 0.001f)) redraw = true;
  s->focused = focused;
  s->frame = f;
  s->dpi = dpi;
  s->radius = radius;

  if (resized && g_movesize == s->target) {
    // Mid-drag resize: stay out of the way until the user lets go.
    sweater_hide(s);
    s->resize_hidden = true;
    return;
  }
  s->resize_hidden = false;
  if (resized || redraw) {
    sweater_render(s);       // UpdateLayeredWindow also moves the strips
    if (!s->shown) sweater_show(s);
    else sweater_stack(s, false);
  } else {
    if (moved) sweater_move(s);
    if (!s->shown) sweater_show(s);
    else if (!moved) sweater_stack(s, false);
  }
}

static struct sweater* sweater_add(HWND h) {
  struct sweater* s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->target = h;
  s->pid = window_owner_pid(h);
  snprintf(s->app, sizeof s->app, "%s", proc_info_get(s->pid)->name);
  for (int i = 0; i < STRIPS; i++) {
    s->strip[i] = strip_create();
    if (!s->strip[i]) {
      for (int j = 0; j < i; j++) DestroyWindow(s->strip[j]);
      free(s);
      return NULL;
    }
  }
  if (g_count == g_capacity) {
    int cap = g_capacity ? g_capacity * 2 : 32;
    struct sweater** grown = realloc(g_sweaters, sizeof *grown * (size_t)cap);
    if (!grown) { for (int i = 0; i < STRIPS; i++) DestroyWindow(s->strip[i]); free(s); return NULL; }
    g_sweaters = grown;
    g_capacity = cap;
  }
  g_sweaters[g_count++] = s;
  return s;
}

// A window appeared, moved or changed state: add, update or drop its sweater.
static void consider(HWND h, bool redraw) {
  int index = find_index(h);
  if (!eligible(h, false)) {
    if (index >= 0) sweater_destroy_at(index);
    return;
  }
  struct sweater* s = index >= 0 ? g_sweaters[index] : sweater_add(h);
  if (s) sweater_update(s, redraw);
}

static void post_restack(void) {
  if (g_restack_posted || !g_msgwnd) return;
  g_restack_posted = true;
  PostMessageW(g_msgwnd, WM_KNIT_RESTACK, 0, 0);
}

static bool g_reconcile_posted;

static void post_reconcile(void) {
  if (g_reconcile_posted || !g_msgwnd) return;
  g_reconcile_posted = true;
  PostMessageW(g_msgwnd, WM_TIMER, RECONCILE_TIMER, 0);
}

static void CALLBACK on_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG obj, LONG child,
                              DWORD thread, DWORD time) {
  (void)hook; (void)thread; (void)time;
  if (g_busy) { post_reconcile(); return; }   // never re-enter a pass in progress
  if (event == EVENT_SYSTEM_FOREGROUND) {
    g_busy = true;
    // Focus changes repaint only when inactive windows look different.
    for (int i = 0; i < g_count; i++) {
      struct sweater* s = g_sweaters[i];
      bool focused = is_focused(s->target);
      if (focused != s->focused) sweater_update(s, false);
    }
    g_busy = false;
    post_restack();
    return;
  }
  if (event == EVENT_OBJECT_REORDER) { post_restack(); return; }
  if (!hwnd || obj != OBJID_WINDOW || child != CHILDID_SELF) return;

  g_busy = true;
  switch (event) {
    case EVENT_OBJECT_LOCATIONCHANGE: {
      int index = find_index(hwnd);
      if (index >= 0) consider(hwnd, false);
      else if (g_knit_on && GetAncestor(hwnd, GA_ROOT) == hwnd && IsWindowVisible(hwnd))
        consider(hwnd, false);   // e.g. restored from maximised
      break;
    }
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_UNCLOAKED:
    case EVENT_SYSTEM_MINIMIZEEND:
      consider(hwnd, false);
      break;
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_CLOAKED:
    case EVENT_SYSTEM_MINIMIZESTART:
    case EVENT_OBJECT_DESTROY: {
      int index = find_index(hwnd);
      if (index >= 0) sweater_destroy_at(index);
      break;
    }
    case EVENT_SYSTEM_MOVESIZESTART:
      g_movesize = hwnd;
      break;
    case EVENT_SYSTEM_MOVESIZEEND: {
      g_movesize = NULL;
      int index = find_index(hwnd);
      if (index >= 0 && g_sweaters[index]->resize_hidden) sweater_update(g_sweaters[index], true);
      else consider(hwnd, false);
      break;
    }
  }
  g_busy = false;
}

// --- public ----------------------------------------------------------------------

static BOOL CALLBACK enum_add(HWND h, LPARAM lp) {
  (void)lp;
  if (eligible(h, false)) consider(h, false);
  return TRUE;
}

static void hook(DWORD first, DWORD last) {
  HWINEVENTHOOK h = SetWinEventHook(first, last, NULL, on_event, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  if (h && g_hook_count < (int)(sizeof g_hooks / sizeof g_hooks[0])) g_hooks[g_hook_count++] = h;
}

static bool windows_11(void) {
  typedef LONG (WINAPI* rtl_get_version)(OSVERSIONINFOW*);
  rtl_get_version get = (rtl_get_version)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  OSVERSIONINFOW v = { sizeof v };
  return get && get(&v) == 0 && v.dwMajorVersion >= 10 && v.dwBuildNumber >= 22000;
}

void tracker_start(void) {
  WNDCLASSEXW wc = { sizeof wc };
  wc.lpfnWndProc = strip_proc;
  wc.hInstance = g_instance;
  wc.lpszClassName = L"WindowSweatersStrip";
  g_strip_class = RegisterClassExW(&wc);
  g_memdc = CreateCompatibleDC(NULL);
  g_win11 = windows_11();
  p_IsWindowArranged = (BOOL (WINAPI*)(HWND))(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                     "IsWindowArranged");
  if (FAILED(CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL,
                              &IID_IVirtualDesktopManager, (void**)&g_desktops)))
    g_desktops = NULL;

  hook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND);
  hook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND);
  hook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND);
  hook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER);   // destroy, show, hide, reorder
  hook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE);
  hook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED);

  g_busy = true;
  EnumWindows(enum_add, 0);
  g_busy = false;
  SetTimer(g_msgwnd, RECONCILE_TIMER, RECONCILE_MS, NULL);
}

void tracker_stop(void) {
  KillTimer(g_msgwnd, RECONCILE_TIMER);
  for (int i = 0; i < g_hook_count; i++) UnhookWinEvent(g_hooks[i]);
  g_hook_count = 0;
  while (g_count) sweater_destroy_at(g_count - 1);
  if (g_desktops) { IVirtualDesktopManager_Release(g_desktops); g_desktops = NULL; }
  if (g_memdc) { DeleteDC(g_memdc); g_memdc = NULL; }
}

void tracker_redraw_all(void) {
  if (g_busy) { post_reconcile(); return; }
  g_busy = true;
  for (int i = g_count - 1; i >= 0; i--) sweater_update(g_sweaters[i], true);
  g_busy = false;
}

void tracker_redraw_pid(DWORD pid) {
  if (g_busy) return;
  g_busy = true;
  for (int i = g_count - 1; i >= 0; i--)
    if (g_sweaters[i]->pid == pid) sweater_update(g_sweaters[i], true);
  g_busy = false;
}

void knit_auto_yarn_ready(pid_t pid) { tracker_redraw_pid((DWORD)pid); }

void tracker_refresh(void) {
  if (g_busy) { post_reconcile(); return; }
  g_busy = true;
  for (int i = g_count - 1; i >= 0; i--)
    if (!eligible(g_sweaters[i]->target, false)) sweater_destroy_at(i);
  EnumWindows(enum_add, 0);
  g_busy = false;
  post_restack();
}

void tracker_restack(void) {
  g_restack_posted = false;
  if (g_busy) return;
  g_busy = true;
  for (int i = 0; i < g_count; i++) sweater_stack(g_sweaters[i], false);
  g_busy = false;
}

void tracker_reconcile(void) {
  if (g_busy) return;
  g_reconcile_posted = false;
  static unsigned ticks;
  g_busy = true;
  for (int i = g_count - 1; i >= 0; i--) {
    struct sweater* s = g_sweaters[i];
    if (!eligible(s->target, false)) { sweater_destroy_at(i); continue; }
    if (s->resize_hidden && g_movesize == s->target) continue;
    sweater_update(s, false);
    sweater_follow_desktop(s);
  }
  EnumWindows(enum_add, 0);
  g_busy = false;
  if (++ticks % 30 == 0) proc_info_prune();
}

void tracker_display_changed(void) {
  tracker_reconcile();
  tracker_redraw_all();
}

struct owners { DWORD* pids; int count, capacity; };

static BOOL CALLBACK enum_owner(HWND h, LPARAM lp) {
  struct owners* o = (struct owners*)lp;
  if (!eligible(h, true)) return TRUE;
  DWORD pid = window_owner_pid(h);
  for (int i = 0; i < o->count; i++) if (o->pids[i] == pid) return TRUE;
  if (o->count < o->capacity) o->pids[o->count++] = pid;
  return TRUE;
}

int tracker_owners(DWORD* pids, int capacity) {
  struct owners o = { pids, 0, capacity };
  EnumWindows(enum_owner, (LPARAM)&o);
  return o.count;
}
