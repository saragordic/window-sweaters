// The notification-area menu, rebuilt every time it opens so the ticks show
// the live state. Mirrors the macOS menu in src/menubar.m.
#include "app.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/misc/knit.h"
#include "src/misc/chart.h"
#include "src/misc/apps.h"

enum {
  ID_TOGGLE = 1, ID_QUIT, ID_ALL_ON, ID_ALL_OFF, ID_EDIT_APPS, ID_OPEN_CHARTS,
  ID_RELOAD, ID_STARTUP,
  ID_ARG_BASE = 1000,     // + index into g_args
  ID_APP_BASE = 5000,     // + index into g_apps
};

#define MAX_ARGS 256
#define MAX_APPS 256

struct menu_arg { char arg[96]; bool plain; };
static struct menu_arg g_args[MAX_ARGS];
static int g_arg_count;

struct menu_app { char key[64]; char name[64]; DWORD pid; };
static struct menu_app g_apps[MAX_APPS];
static int g_app_count;

static HBITMAP g_bitmaps[MAX_ARGS];
static int g_bitmap_count;

// --- helpers shared with the macOS menu's rules ----------------------------------

static bool chart_valid(int index) {
  return index >= 0 && index < g_chart_count && g_charts[index].px
         && g_charts[index].w > 0 && g_charts[index].h > 0;
}

static bool chart_selectable(int index) {
  if (!chart_valid(index) || g_charts[index].h > 14) return false;
  if (g_charts[index].generated) return false;
  const struct knit_chart* chart = &g_charts[index];
  uint32_t yarns[16];
  int count = 0;
  bool opaque = false;
  for (size_t i = 0; i < (size_t)chart->w * chart->h; i++) {
    uint32_t pixel = chart->px[i];
    bool solid = (pixel >> 24) >= 128;
    opaque |= solid;
    uint32_t yarn = solid ? pixel | 0xff000000 : 0;
    int match = 0;
    while (match < count && yarns[match] != yarn) match++;
    if (match == count) {
      if (count == 16) return false;
      yarns[count++] = yarn;
    }
  }
  return opaque;
}

static bool colourwork(void) {
  return g_knit_pattern_by_app || chart_valid(g_chart_active);
}

float knit_menu_minimum_rows(void) {
  if (!colourwork()) return 3.f;
  if (!g_knit_pattern_by_app && chart_selectable(g_chart_active))
    return (float)(g_charts[g_chart_active].h > 6 ? g_charts[g_chart_active].h : 6);
  return 6.f;
}

static bool legacy_chart(const char* name) {
  static const char* legacy[] = { "awning", "sprig", "diagonal", "braid",
    "chrome", "heart", "trim", "carnival", "checker", "pinstripe",
    "snowflake", "blockstripe", "seedling" };
  if (strncmp(name, "atelier-", 8) == 0) return true;
  for (size_t i = 0; i < sizeof legacy / sizeof legacy[0]; i++)
    if (strcmp(name, legacy[i]) == 0) return true;
  return false;
}

static const struct { const char* name; const char* title; } k_patterns[] = {
  { "zigzag", "Zigzag" },
  { "picnic", "Picnic Checks" }, { "ribbon", "Ribbon Stripes" },
  { "posy", "Little Bows" }, { "twinkle", "Tiny Stars" },
  { "candy-stripe", "Candy Stripes" }
};

static bool featured_chart(const char* name) {
  for (size_t i = 0; i < sizeof k_patterns / sizeof k_patterns[0]; i++)
    if (strcmp(name, k_patterns[i].name) == 0) return true;
  return false;
}

static void chart_title(const char* name, char* out, size_t cap) {
  snprintf(out, cap, "%s", name);
  bool start = true;
  for (char* p = out; *p; p++) {
    if (*p == '_' || *p == '-') { *p = ' '; start = true; continue; }
    if (start && *p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    start = false;
  }
}

static int menu_dpi(void) {
  HDC dc = GetDC(NULL);
  int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(NULL, dc);
  return dpi ? dpi : 96;
}

// A small knitted swatch of the chart, drawn like the macOS menu's.
static HBITMAP chart_swatch(int index) {
  if (!chart_valid(index) || g_bitmap_count >= MAX_ARGS) return NULL;
  const struct knit_chart* chart = &g_charts[index];
  float k = menu_dpi() / 96.f;
  int W = (int)lroundf(40 * k), H = (int)lroundf(18 * k);
  uint32_t* bits = NULL;
  HBITMAP bmp = knit_dib(W, H, &bits);
  if (!bmp) return NULL;
  CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
  CGContextRef c = CGBitmapContextCreate(bits, (size_t)W, (size_t)H, 8, (size_t)W * 4, rgb,
                                         kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(rgb);
  if (!c) { DeleteObject(bmp); return NULL; }
  CGContextScaleCTM(c, k, k);
  const int columns = chart->w < 12 ? 12 : chart->w > 24 ? 24 : chart->w;
  const int rows = chart->h < 6 ? 6 : chart->h > 8 ? 8 : chart->h;
  const uint32_t base = 0xff638575;
  CGMutablePathRef outline = CGPathCreateMutable();
  CGPathAddRoundedRect(outline, NULL, CGRectMake(0.5, 0.5, 39, 17), 3, 3);
  CGContextAddPath(c, outline);
  CGContextClip(c);
  CGContextSetRGBFillColor(c, ((base >> 16) & 255) / 255.0 * 0.72, ((base >> 8) & 255) / 255.0 * 0.72,
                           (base & 255) / 255.0 * 0.72, 1);
  CGContextFillRect(c, CGRectMake(0, 0, 40, 18));
  CFRelease(outline);
  double step_x = 38.0 / columns, step_y = 16.0 / rows;
  CGContextSetLineCap(c, kCGLineCapRound);
  CGContextSetLineWidth(c, step_x * 0.5 < 1.15 ? step_x * 0.5 : 1.15);
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < columns; x++) {
      uint32_t color = chart->px[(y % chart->h) * chart->w + x % chart->w];
      if ((color >> 24) < 128) color = base;
      CGContextSetRGBStrokeColor(c, ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0,
                                 (color & 255) / 255.0, 1);
      double cx = 1 + (x + 0.5) * step_x, cy = 17 - (y + 0.85) * step_y;
      CGContextMoveToPoint(c, cx - step_x * 0.26, cy + step_y * 0.65);
      CGContextAddLineToPoint(c, cx, cy);
      CGContextAddLineToPoint(c, cx + step_x * 0.26, cy + step_y * 0.65);
      CGContextStrokePath(c);
    }
  CGContextRelease(c);
  g_bitmaps[g_bitmap_count++] = bmp;
  return bmp;
}

static void free_bitmaps(void) {
  for (int i = 0; i < g_bitmap_count; i++) DeleteObject(g_bitmaps[i]);
  g_bitmap_count = 0;
}

// --- building ----------------------------------------------------------------------

static void add_item(HMENU menu, UINT id, const char* title, bool checked, bool enabled, HBITMAP bmp) {
  wchar_t wtitle[160];
  knit_widen(title, wtitle, 160);
  MENUITEMINFOW mi = { sizeof mi };
  mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE | (bmp ? MIIM_BITMAP : 0);
  mi.fType = MFT_STRING;
  mi.fState = (checked ? MFS_CHECKED : 0) | (enabled ? 0 : MFS_DISABLED);
  mi.wID = id;
  mi.dwTypeData = wtitle;
  mi.hbmpItem = bmp;
  InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &mi);
}

static UINT arg_id(const char* arg, bool plain) {
  if (g_arg_count >= MAX_ARGS) return 0;
  snprintf(g_args[g_arg_count].arg, sizeof g_args[0].arg, "%s", arg);
  g_args[g_arg_count].plain = plain;
  return ID_ARG_BASE + g_arg_count++;
}

static void add_arg(HMENU menu, const char* title, const char* arg, bool checked, HBITMAP bmp) {
  UINT id = arg_id(arg, false);
  if (id) add_item(menu, id, title, checked, true, bmp);
}

static void add_separator(HMENU menu) { AppendMenuW(menu, MF_SEPARATOR, 0, NULL); }

static HMENU add_submenu(HMENU menu, const char* title) {
  HMENU sub = CreatePopupMenu();
  wchar_t wtitle[160];
  knit_widen(title, wtitle, 160);
  AppendMenuW(menu, MF_POPUP | MF_STRING, (UINT_PTR)sub, wtitle);
  return sub;
}

static int cmp_app(const void* a, const void* b) {
  return _stricmp(((const struct menu_app*)a)->name, ((const struct menu_app*)b)->name);
}

static void build_apps(HMENU menu) {
  g_app_count = 0;
  DWORD pids[512];
  int count = tracker_owners(pids, 512);
  for (int i = 0; i < count && g_app_count < MAX_APPS; i++) {
    const struct proc_info* info = proc_info_get(pids[i]);
    if (info->own || !info->exe[0]) continue;
    bool seen = false;
    for (int j = 0; j < g_app_count; j++) if (_stricmp(g_apps[j].key, info->key) == 0) seen = true;
    if (seen) continue;
    struct menu_app* app = &g_apps[g_app_count++];
    snprintf(app->key, sizeof app->key, "%s", info->key);
    snprintf(app->name, sizeof app->name, "%s", info->name);
    app->pid = pids[i];
  }
  // Apps ticked the other way stay listed, so a choice can always be undone.
  for (int i = 0; i < knit_app_exception_count() && g_app_count < MAX_APPS; i++) {
    const char* key = knit_app_exception(i);
    bool seen = false;
    for (int j = 0; j < g_app_count; j++) if (_stricmp(g_apps[j].key, key) == 0) seen = true;
    if (seen) continue;
    struct menu_app* app = &g_apps[g_app_count++];
    snprintf(app->key, sizeof app->key, "%s", key);
    snprintf(app->name, sizeof app->name, "%s", key);
    char* dot = strrchr(app->name, '.');
    if (dot && _stricmp(dot, ".exe") == 0) *dot = 0;
    app->pid = 0;
  }
  qsort(g_apps, (size_t)g_app_count, sizeof g_apps[0], cmp_app);
  for (int i = 0; i < g_app_count; i++)
    add_item(menu, ID_APP_BASE + i, g_apps[i].name, !knit_app_hidden(g_apps[i].key), true, NULL);
  if (!g_app_count) add_item(menu, 0, "No Available Apps", false, false, NULL);

  add_separator(menu);
  bool uniform = knit_app_exception_count() == 0;
  add_item(menu, ID_ALL_ON, "Turn On for All Apps", false, !(uniform && knit_apps_on_by_default()), NULL);
  add_item(menu, ID_ALL_OFF, "Turn Off for All Apps", false, !(uniform && !knit_apps_on_by_default()), NULL);
}

static void build(HMENU menu) {
  g_arg_count = 0;
  free_bitmaps();
  char title[160], arg[96];

  add_item(menu, ID_TOGGLE, "Show Sweater Borders", g_knit_on, true, NULL);
  build_apps(add_submenu(menu, "Apps"));

  HMENU pattern = add_submenu(menu, "Pattern");
  add_arg(pattern, "By App", "chart=by-app", g_knit_pattern_by_app, NULL);
  add_separator(pattern);
  const char* plain_names[] = { "Stockinette", "Rib" };
  const int plain_stitches[] = { KNIT_STOCKINETTE, KNIT_RIB };
  for (int i = 0; i < 2; i++) {
    snprintf(arg, sizeof arg, "yarn=%s", g_knit_stitch_names[plain_stitches[i]]);
    UINT id = arg_id(arg, true);
    if (id) add_item(pattern, id, plain_names[i],
                     !g_knit_pattern_by_app && g_chart_active < 0 && g_knit_stitch == plain_stitches[i],
                     true, NULL);
  }
  bool featured = false;
  for (size_t i = 0; i < sizeof k_patterns / sizeof k_patterns[0]; i++) {
    int index = knit_chart_index(k_patterns[i].name);
    if (!chart_selectable(index)) continue;
    if (!featured) add_separator(pattern);
    featured = true;
    snprintf(arg, sizeof arg, "chart=%s", k_patterns[i].name);
    add_arg(pattern, k_patterns[i].title, arg, !g_knit_pattern_by_app && g_chart_active == index,
            chart_swatch(index));
  }
  HMENU custom = NULL;
  for (int i = 0; i < g_chart_count; i++) {
    if (!chart_selectable(i) || legacy_chart(g_charts[i].name) || featured_chart(g_charts[i].name))
      continue;
    if (!custom) { add_separator(pattern); custom = add_submenu(pattern, "Custom Patterns"); }
    chart_title(g_charts[i].name, title, sizeof title);
    snprintf(arg, sizeof arg, "chart=%s", g_charts[i].name);
    add_arg(custom, title, arg, !g_knit_pattern_by_app && g_chart_active == i, chart_swatch(i));
  }
  if (!g_knit_pattern_by_app && g_chart_active >= 0 && g_chart_active < g_chart_count
      && (legacy_chart(g_charts[g_chart_active].name) || !chart_selectable(g_chart_active))) {
    add_separator(pattern);
    char name[96];
    chart_title(g_charts[g_chart_active].name, name, sizeof name);
    snprintf(title, sizeof title, "Current: %s", name);
    snprintf(arg, sizeof arg, "chart=%s", g_charts[g_chart_active].name);
    add_arg(pattern, title, arg, true, NULL);
  }

  HMENU widths = add_submenu(menu, "Border Width");
  float width = knit_current_width();
  const struct { const char* name; float value; } width_choices[] = {
    { "Slim", 6 }, { "Narrow", 8 }, { "Medium", 12 }, { "Regular", 14 }, { "Wide", 18 }, { "Extra Wide", 28 }
  };
  bool standard = false;
  for (size_t i = 0; i < sizeof width_choices / sizeof width_choices[0]; i++) {
    bool selected = fabsf(width - width_choices[i].value) < 0.001f;
    standard |= selected;
    snprintf(title, sizeof title, "%s \xc2\xb7 %g pt", width_choices[i].name, width_choices[i].value);
    snprintf(arg, sizeof arg, "width=%g", width_choices[i].value);
    add_arg(widths, title, arg, selected, NULL);
  }
  if (!standard) {
    add_separator(widths);
    snprintf(title, sizeof title, "Custom \xc2\xb7 %g pt", width);
    snprintf(arg, sizeof arg, "width=%g", width);
    add_arg(widths, title, arg, true, NULL);
  }

  HMENU sizes = add_submenu(menu, "Stitch Size");
  float minimum = knit_menu_minimum_rows();
  const struct { const char* name; float rows; } size_choices[] = {
    { "Chunky", 3 }, { "Medium", 6 }, { "Fine", 10 }, { "Very Fine", 14 }
  };
  standard = false;
  for (size_t i = 0; i < sizeof size_choices / sizeof size_choices[0]; i++) {
    if (size_choices[i].rows < minimum) continue;
    bool selected = fabsf(g_knit.rows - size_choices[i].rows) < 0.001f;
    standard |= selected;
    snprintf(title, sizeof title, "%s \xc2\xb7 %g rows", size_choices[i].name, size_choices[i].rows);
    snprintf(arg, sizeof arg, "gauge=%g", size_choices[i].rows);
    add_arg(sizes, title, arg, selected, NULL);
  }
  if (!standard) {
    add_separator(sizes);
    snprintf(title, sizeof title, "Custom \xc2\xb7 %g rows", g_knit.rows);
    snprintf(arg, sizeof arg, "gauge=%g", g_knit.rows);
    add_arg(sizes, title, arg, true, NULL);
  }

  HMENU inactive = add_submenu(menu, "Inactive Windows");
  bool grey = g_knit_grey > 0.001f, dimmed = !grey && g_knit_dim > 0.001f;
  add_arg(inactive, "Full Colour", "inactive=colour", !grey && !dimmed, NULL);
  add_arg(inactive, "Dimmed", "inactive=dim", dimmed, NULL);
  add_arg(inactive, "Grey", "inactive=grey", grey, NULL);

  HMENU custom_files = add_submenu(menu, "Your Own Colourways");
  add_item(custom_files, ID_EDIT_APPS, "Edit Colourways (apps.conf)", false, true, NULL);
  add_item(custom_files, ID_OPEN_CHARTS, "Open Patterns Folder", false, true, NULL);
  add_separator(custom_files);
  add_item(custom_files, ID_RELOAD, "Reload Colourways and Patterns", false, true, NULL);

  add_separator(menu);
  add_item(menu, ID_STARTUP, "Start with Windows", knit_startup_enabled(), true, NULL);
  add_separator(menu);
  add_item(menu, ID_QUIT, "Quit Window Sweaters", false, true, NULL);
}

void menu_show(HWND owner) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  build(menu);
  POINT at;
  GetCursorPos(&at);
  // Required for the menu to close when the user clicks elsewhere.
  SetForegroundWindow(owner);
  UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
  flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  UINT id = (UINT)TrackPopupMenuEx(menu, flags, at.x, at.y, owner, NULL);
  PostMessageW(owner, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (id) menu_command(id);
  free_bitmaps();
}

static void open_path(const char* utf8, bool folder) {
  wchar_t path[MAX_PATH * 2];
  knit_widen(utf8, path, MAX_PATH * 2);
  if (!path[0]) return;
  DWORD attr = GetFileAttributesW(path);
  if (attr == INVALID_FILE_ATTRIBUTES || ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) != folder) return;
  if (folder) ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
  else ShellExecuteW(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);
}

static void apply_choice(const struct menu_arg* choice) {
  char buf[128];
  if (choice->plain) {
    knit_apply("chart=none");
    knit_apply(choice->arg);
    knit_save_prefs();
    return;
  }
  const char* a = choice->arg;
  if (strncmp(a, "chart=", 6) == 0 && strcmp(a, "chart=none") != 0) {
    int chart = knit_chart_index(a + 6);
    float minimum = chart_selectable(chart) ? (float)(g_charts[chart].h > 6 ? g_charts[chart].h : 6) : 6.f;
    if (g_knit.rows < minimum) {
      snprintf(buf, sizeof buf, "gauge=%g", minimum);
      knit_apply(buf);
    }
  }
  knit_apply(a);
  knit_save_prefs();
}

void menu_command(UINT id) {
  if (id >= ID_APP_BASE && id < ID_APP_BASE + (UINT)g_app_count) {
    const char* key = g_apps[id - ID_APP_BASE].key;
    if (!knit_app_set_hidden(key, !knit_app_hidden(key))) return;
    knit_save_prefs();
    tracker_refresh();
    knit_update_tray();
    return;
  }
  if (id >= ID_ARG_BASE && id < ID_ARG_BASE + (UINT)g_arg_count) {
    apply_choice(&g_args[id - ID_ARG_BASE]);
    return;
  }
  switch (id) {
    case ID_TOGGLE:
      knit_apply(g_knit_on ? "knit=off" : "knit=on");
      knit_save_prefs();
      break;
    case ID_ALL_ON:
    case ID_ALL_OFF:
      if (!knit_apps_set_all(id == ID_ALL_ON)) return;
      knit_save_prefs();
      tracker_refresh();
      knit_update_tray();
      break;
    case ID_EDIT_APPS: open_path(knit_apps_path(), false); break;
    case ID_OPEN_CHARTS: open_path(knit_charts_dir(), true); break;
    case ID_RELOAD: {
      knit_apply("charts=reload");
      knit_apply("apps=reload");
      if (colourwork() && g_knit.rows < knit_menu_minimum_rows()) {
        char buf[64];
        snprintf(buf, sizeof buf, "gauge=%g", knit_menu_minimum_rows());
        knit_apply(buf);
      }
      knit_save_prefs();
      break;
    }
    case ID_STARTUP: knit_set_startup(!knit_startup_enabled()); break;
    case ID_QUIT: PostMessageW(g_msgwnd, WM_CLOSE, 0, 0); break;
  }
}
