// Window Sweaters for Windows: a notification-area app that knits a border
// around every window. The renderer, colourways and patterns are the shared
// sources in ../src; this file is the app around them.
//
//   WindowSweaters.exe                     start (or do nothing if running)
//   WindowSweaters.exe width=18 chart=posy change a running instance's settings
//   WindowSweaters.exe --version | --help
#include "app.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/misc/knit.h"
#include "src/misc/chart.h"
#include "src/misc/apps.h"

HINSTANCE g_instance;
HWND g_msgwnd;

static float g_width = 12.f;
static UINT g_taskbar_created;
static NOTIFYICONDATAW g_nid;
static HICON g_tray_icon;
static bool g_tray_added;

#define RECONCILE_TIMER 0x5357
#define MSG_CLASS L"WindowSweatersMessage"
#define SETTINGS_SECTION L"Window Sweaters"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

float knit_current_width(void) { return g_width; }
float knit_menu_minimum_rows(void);   // menu.c

// --- settings: the same key=value language as the macOS app ------------------------

enum { APPLY_NONE = 0, APPLY_REDRAW = 1, APPLY_REFRESH = 2 };

static int apply_one(const char* a) {
  float v;
  if (strncmp(a, "chart=", 6) == 0) {
    if (!knit_pattern_select(a + 6)) return APPLY_NONE;
    knit_flush_cache();
    return APPLY_REDRAW;
  }
  if (strcmp(a, "apps=reload") == 0) { knit_apps_load(); knit_flush_cache(); return APPLY_REDRAW; }
  if (strcmp(a, "charts=reload") == 0) {
    knit_charts_load(knit_charts_dir());
    knit_flush_cache();
    return APPLY_REDRAW;
  }
  if (strcmp(a, "anchor=corner") == 0) { g_knit_anchor = KNIT_ANCHOR_CORNER; return APPLY_REDRAW; }
  if (strcmp(a, "anchor=centre") == 0 || strcmp(a, "anchor=center") == 0) {
    g_knit_anchor = KNIT_ANCHOR_CENTRE;
    return APPLY_REDRAW;
  }
  if (strcmp(a, "inactive=colour") == 0 || strcmp(a, "inactive=color") == 0) {
    g_knit_dim = 0.f; g_knit_grey = 0.f; return APPLY_REDRAW;
  }
  if (strcmp(a, "inactive=dim") == 0) { g_knit_dim = KNIT_INACTIVE_DIM; g_knit_grey = 0.f; return APPLY_REDRAW; }
  if (strcmp(a, "inactive=grey") == 0 || strcmp(a, "inactive=gray") == 0) {
    g_knit_dim = 0.f; g_knit_grey = 1.f; return APPLY_REDRAW;
  }
  if (sscanf(a, "dim=%f", &v) == 1) {
    if (isfinite(v)) { g_knit_dim = fminf(fmaxf(v, 0.f), 1.f); return APPLY_REDRAW; }
    return APPLY_NONE;
  }
  if (sscanf(a, "grey=%f", &v) == 1 || sscanf(a, "gray=%f", &v) == 1) {
    if (isfinite(v)) { g_knit_grey = fminf(fmaxf(v, 0.f), 1.f); return APPLY_REDRAW; }
    return APPLY_NONE;
  }
  if (sscanf(a, "width=%f", &v) == 1) {
    if (isfinite(v) && v >= 2.f && v <= 60.f) { g_width = v; return APPLY_REDRAW; }
    return APPLY_NONE;
  }
  if (sscanf(a, "gauge=%f", &v) == 1) {
    if (isfinite(v) && v >= 1.5f && v <= 40.f) { g_knit.rows = v; knit_flush_cache(); return APPLY_REDRAW; }
    return APPLY_NONE;
  }
  if (sscanf(a, "ground=%f", &v) == 1) { g_knit.ground = v; knit_flush_cache(); return APPLY_REDRAW; }
  if (sscanf(a, "ambient=%f", &v) == 1) { g_knit.ambient = v; knit_flush_cache(); return APPLY_REDRAW; }
  if (sscanf(a, "relief=%f", &v) == 1) { g_knit.relief = v; knit_flush_cache(); return APPLY_REDRAW; }
  if (sscanf(a, "sheen=%f", &v) == 1) { g_knit.sheen = v; knit_flush_cache(); return APPLY_REDRAW; }
  if (sscanf(a, "tuck=%f", &v) == 1) { g_knit.tuck = v; return APPLY_REDRAW; }
  if (strcmp(a, "knit=on") == 0) { g_knit_on = true; return APPLY_REFRESH; }
  if (strcmp(a, "knit=off") == 0) { g_knit_on = false; return APPLY_REFRESH; }
  if (strncmp(a, "yarn=", 5) == 0) {
    for (int k = 0; k < KNIT_STITCH_COUNT; k++)
      if (strcmp(a + 5, g_knit_stitch_names[k]) == 0) g_knit_stitch = k;
    knit_flush_cache();
    return APPLY_REDRAW;
  }
  if (strncmp(a, "basket=", 7) == 0) {
    for (int k = 0; k < g_knit_basket_count; k++)
      if (strcmp(a + 7, g_knit_baskets[k].name) == 0) g_knit_basket = k;
    knit_flush_cache();
    return APPLY_REDRAW;
  }
  return APPLY_NONE;
}

static bool g_started;   // the tracker is running; before that nothing to redraw

void knit_apply(const char* arg) {
  int result = apply_one(arg);
  if (!g_started) return;
  if (result & APPLY_REFRESH) { tracker_refresh(); knit_update_tray(); }
  else if (result & APPLY_REDRAW) tracker_redraw_all();
}

// --- preferences (settings.ini) -----------------------------------------------------

static void pref_get(const wchar_t* key, const char* fallback, char* out, int cap) {
  wchar_t wfallback[256], value[2048];
  knit_widen(fallback, wfallback, 256);
  GetPrivateProfileStringW(SETTINGS_SECTION, key, wfallback, value, 2048, knit_settings_path());
  knit_narrow(value, out, cap);
}

static void pref_set(const wchar_t* key, const char* value) {
  wchar_t wvalue[2048];
  knit_widen(value, wvalue, 2048);
  WritePrivateProfileStringW(SETTINGS_SECTION, key, wvalue, knit_settings_path());
}

static char g_inactive[32];

void knit_save_prefs(void) {
  char buf[64];
  pref_set(L"on", g_knit_on ? "1" : "0");
  snprintf(buf, sizeof buf, "%s", g_knit_stitch_names[g_knit_stitch]); pref_set(L"yarn", buf);
  pref_set(L"basket", g_knit_baskets[g_knit_basket].name);
  snprintf(buf, sizeof buf, "%g", g_width); pref_set(L"width", buf);
  snprintf(buf, sizeof buf, "%g", g_knit.rows); pref_set(L"gauge", buf);
  pref_set(L"chart", g_chart_active >= 0 && g_chart_active < g_chart_count
                     && !g_charts[g_chart_active].generated ? g_charts[g_chart_active].name : "none");
  pref_set(L"patternByApp", g_knit_pattern_by_app ? "1" : "0");
  pref_set(L"anchor", g_knit_anchor == KNIT_ANCHOR_CENTRE ? "centre" : "corner");
  const char* inactive = g_knit_grey > 0.001f ? "grey" : g_knit_dim > 0.001f ? "dim" : "colour";
  if (g_inactive[0] || strcmp(inactive, "colour") != 0) pref_set(L"inactive", inactive);
  pref_set(L"appsOnByDefault", knit_apps_on_by_default() ? "1" : "0");
  // Executable names cannot contain '|'.
  size_t cap = 64;
  for (int i = 0; i < knit_app_exception_count(); i++) cap += strlen(knit_app_exception(i)) + 1;
  char* list = calloc(1, cap);
  if (list) {
    for (int i = 0; i < knit_app_exception_count(); i++) {
      if (i) strcat(list, "|");
      strcat(list, knit_app_exception(i));
    }
    pref_set(L"appExceptions", list);
    free(list);
  }
}

static void load_prefs(void) {
  char v[2048], buf[128];
  pref_get(L"yarn", "stockinette", v, sizeof v);
  snprintf(buf, sizeof buf, "yarn=%s", v); knit_apply(buf);
  pref_get(L"basket", "wool", v, sizeof v);
  snprintf(buf, sizeof buf, "basket=%s", v); knit_apply(buf);
  pref_get(L"width", "12", v, sizeof v);
  float w = (float)atof(v);
  if (w < 2.f || w > 60.f) w = 12.f;
  snprintf(buf, sizeof buf, "width=%g", w); knit_apply(buf);
  pref_get(L"gauge", "6", v, sizeof v);
  snprintf(buf, sizeof buf, "gauge=%s", v); knit_apply(buf);
  pref_get(L"chart", "none", v, sizeof v);
  if (!v[0] || knit_chart_index(v) < 0) snprintf(v, sizeof v, "none");
  snprintf(buf, sizeof buf, "chart=%s", v); knit_apply(buf);
  pref_get(L"patternByApp", "1", v, sizeof v);
  if (atoi(v)) knit_apply("chart=by-app");
  if ((g_knit_pattern_by_app || g_chart_active >= 0) && g_knit.rows < knit_menu_minimum_rows()) {
    snprintf(buf, sizeof buf, "gauge=%g", knit_menu_minimum_rows());
    knit_apply(buf);
  }
  pref_get(L"anchor", "corner", v, sizeof v);
  knit_apply(strcmp(v, "centre") == 0 ? "anchor=centre" : "anchor=corner");
  pref_get(L"inactive", "", v, sizeof v);
  snprintf(g_inactive, sizeof g_inactive, "%s", v);
  if (strcmp(v, "colour") == 0 || strcmp(v, "dim") == 0 || strcmp(v, "grey") == 0) {
    snprintf(buf, sizeof buf, "inactive=%s", v);
    knit_apply(buf);
  }
  pref_get(L"on", "1", v, sizeof v);
  knit_apply(atoi(v) ? "knit=on" : "knit=off");

  pref_get(L"appsOnByDefault", "1", v, sizeof v);
  bool on = atoi(v) != 0;
  knit_apps_set_all(on);
  pref_get(L"appExceptions", "", v, sizeof v);
  for (char* key = strtok(v, "|"); key; key = strtok(NULL, "|"))
    if (*key) knit_app_set_hidden(key, on);   // the other way from the default
}

// --- start with Windows ----------------------------------------------------------------

static void exe_command(wchar_t* out, int cap) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  _snwprintf(out, (size_t)cap, L"\"%s\"", exe);
  out[cap - 1] = 0;
}

bool knit_startup_enabled(void) {
  wchar_t value[MAX_PATH + 8], want[MAX_PATH + 8];
  DWORD size = sizeof value;
  if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, KNIT_APP_NAME, RRF_RT_REG_SZ, NULL, value, &size)
      != ERROR_SUCCESS)
    return false;
  exe_command(want, MAX_PATH + 8);
  return _wcsicmp(value, want) == 0;
}

void knit_set_startup(bool on) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (on) {
    wchar_t cmd[MAX_PATH + 8];
    exe_command(cmd, MAX_PATH + 8);
    RegSetValueExW(key, KNIT_APP_NAME, 0, REG_SZ, (const BYTE*)cmd,
                   (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, KNIT_APP_NAME);
  }
  RegCloseKey(key);
}

// --- notification area ---------------------------------------------------------------

static bool taskbar_is_light(void) {
  DWORD light = 0, size = sizeof light;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size);
  return light != 0;
}

void knit_update_tray(void) {
  if (!g_msgwnd) return;
  bool no_apps = !knit_apps_on_by_default() && knit_app_exception_count() == 0;
  bool dimmed = !g_knit_on || no_apps;
  UINT dpi = GetDpiForWindow(g_msgwnd);
  int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96);
  HICON icon = yarn_tray_icon(size, taskbar_is_light(), dimmed);
  const wchar_t* status = !g_knit_on ? L"sweaters are paused"
                        : no_apps    ? L"sweaters are off for all apps"
                                     : L"sweaters are on";
  g_nid.cbSize = sizeof g_nid;
  g_nid.hWnd = g_msgwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
  g_nid.uCallbackMessage = WM_KNIT_TRAY;
  g_nid.hIcon = icon;
  _snwprintf(g_nid.szTip, sizeof g_nid.szTip / sizeof g_nid.szTip[0], L"Window Sweaters \x2014 %s", status);
  if (!g_tray_added) {
    g_tray_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != 0;
    if (g_tray_added) {
      g_nid.uVersion = NOTIFYICON_VERSION_4;
      Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    }
  } else {
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
  }
  if (g_tray_icon) DestroyIcon(g_tray_icon);
  g_tray_icon = icon;
}

static void tray_remove(void) {
  if (g_tray_added) Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_tray_added = false;
  if (g_tray_icon) DestroyIcon(g_tray_icon);
  g_tray_icon = NULL;
}

// --- the message window ------------------------------------------------------------------

static void apply_message(const char* message, size_t len) {
  // NUL-separated key=value arguments, ending with an empty one.
  size_t i = 0;
  bool any = false;
  while (i < len && message[i]) {
    const char* arg = message + i;
    size_t n = strnlen(arg, len - i);
    if (n == len - i) break;
    knit_apply(arg);
    any = true;
    i += n + 1;
  }
  if (any) knit_save_prefs();
}

static LRESULT CALLBACK msg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_taskbar_created && g_taskbar_created) {
    g_tray_added = false;
    knit_update_tray();
    return 0;
  }
  switch (msg) {
    case WM_KNIT_TRAY:
      switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU:
          menu_show(h);
          knit_update_tray();
          break;
      }
      return 0;
    case WM_KNIT_RESTACK: tracker_restack(); return 0;
    case WM_TIMER:
      if (wp == RECONCILE_TIMER) tracker_reconcile();
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      tracker_display_changed();
      knit_update_tray();
      return 0;
    case WM_SETTINGCHANGE:
      if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) knit_update_tray();
      if (wp == SPI_SETWORKAREA) tracker_reconcile();
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_APMRESUMEAUTOMATIC) tracker_display_changed();
      return TRUE;
    case WM_COPYDATA: {
      const COPYDATASTRUCT* cd = (const COPYDATASTRUCT*)lp;
      if (cd && cd->dwData == 0x4b4e4954 && cd->lpData && cd->cbData) {   // 'KNIT'
        apply_message(cd->lpData, cd->cbData);
        return TRUE;
      }
      return FALSE;
    }
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      tracker_stop();
      tray_remove();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// --- command line ----------------------------------------------------------------------

static void console_print(const char* text) {
  // Redirected to a file or pipe: write there.
  HANDLE std = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD type = std && std != INVALID_HANDLE_VALUE ? GetFileType(std) : FILE_TYPE_UNKNOWN;
  if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) {
    DWORD written;
    WriteFile(std, text, (DWORD)strlen(text), &written, NULL);
    return;
  }
  // A GUI app has no console; borrow the parent's if it was run from one.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleA(out, "\r\n", 2, &written, NULL);
    WriteConsoleA(out, text, (DWORD)strlen(text), &written, NULL);
    FreeConsole();
  } else {
    wchar_t w[1024];
    MessageBoxW(NULL, knit_widen(text, w, 1024), KNIT_APP_NAME, MB_OK | MB_ICONINFORMATION);
  }
}

// Pack the key=value arguments as the running instance expects them.
static char* pack_args(int argc, wchar_t** argv, size_t* len) {
  size_t cap = 2;
  for (int i = 1; i < argc; i++) cap += wcslen(argv[i]) * 3 + 1;
  char* buf = calloc(1, cap);
  if (!buf) return NULL;
  size_t n = 0;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == L'-') continue;
    char arg[1024];
    knit_narrow(argv[i], arg, sizeof arg);
    if (!strchr(arg, '=')) continue;
    size_t l = strlen(arg);
    memcpy(buf + n, arg, l + 1);
    n += l + 1;
  }
  buf[n++] = 0;
  *len = n;
  return buf;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmdline, int show) {
  (void)prev; (void)cmdline; (void)show;
  g_instance = instance;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; i++) {
    if (!wcscmp(argv[i], L"--version") || !wcscmp(argv[i], L"-v")) {
      console_print("Window Sweaters " KNIT_VERSION "\r\n");
      return 0;
    }
    if (!wcscmp(argv[i], L"--help") || !wcscmp(argv[i], L"-h")) {
      console_print("Window Sweaters: knitted borders for your windows.\r\n"
                    "Run it to add the yarn icon to the notification area.\r\n"
                    "Settings such as width=18 or chart=zigzag change a running copy.\r\n"
                    "Developer options: --version, --write-icon <file.ico>. See README.md.\r\n");
      return 0;
    }
    if (!wcscmp(argv[i], L"--write-icon") && i + 1 < argc) {
      char path[1024];
      knit_narrow(argv[i + 1], path, sizeof path);
      return yarn_write_ico(path) ? 0 : 1;
    }
  }

  size_t packed_len = 0;
  char* packed = argv ? pack_args(argc, argv, &packed_len) : NULL;
  HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\WindowSweaters.Instance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Hand any settings to the copy that is already running, and leave.
    HWND running = FindWindowW(MSG_CLASS, NULL);
    if (running && packed && packed_len > 1) {
      COPYDATASTRUCT cd = { 0x4b4e4954, (DWORD)packed_len, packed };
      SendMessageTimeoutW(running, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000, NULL);
    }
    free(packed);
    if (argv) LocalFree(argv);
    if (mutex) CloseHandle(mutex);
    return 0;
  }

  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc = { sizeof wc };
  wc.lpfnWndProc = msg_proc;
  wc.hInstance = instance;
  wc.lpszClassName = MSG_CLASS;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  RegisterClassExW(&wc);
  // A real (hidden) top-level window: message-only windows miss TaskbarCreated.
  g_msgwnd = CreateWindowExW(WS_EX_TOOLWINDOW, MSG_CLASS, KNIT_APP_NAME, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, instance, NULL);
  if (!g_msgwnd) return 1;

  // Restore the collection and preferences before any sweater is knitted.
  knit_charts_load(knit_charts_dir());
  knit_apps_load();
  load_prefs();
  if (packed && packed_len > 1) apply_message(packed, packed_len);
  free(packed);
  if (argv) LocalFree(argv);

  knit_update_tray();
  g_started = true;
  tracker_start();

  MSG m;
  while (GetMessageW(&m, NULL, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  CoUninitialize();
  if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
  return 0;
}
