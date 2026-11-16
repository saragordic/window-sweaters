// Shared declarations for the Windows app.
#pragma once
#include <windows.h>
#include <objbase.h>
#include <stdbool.h>
#include <stdint.h>
#include "cg.h"

#define KNIT_VERSION "1.5.0"
#define KNIT_APP_NAME L"Window Sweaters"

#define WM_KNIT_TRAY     (WM_APP + 1)   // notification icon callback
#define WM_KNIT_RESTACK  (WM_APP + 2)   // coalesced z-order repair
#define WM_KNIT_REDRAW   (WM_APP + 3)   // wParam: pid whose sweaters to redraw

extern HINSTANCE g_instance;
extern HWND g_msgwnd;      // hidden message window: tray, timers, WM_COPYDATA

// paths.c --------------------------------------------------------------------
const char* knit_data_dir(void);            // UTF-8, created on first use
const wchar_t* knit_settings_path(void);    // settings.ini
wchar_t* knit_widen(const char* s, wchar_t* out, int capacity);
char* knit_narrow(const wchar_t* s, char* out, int capacity);

// procinfo.c -----------------------------------------------------------------
// What the app knows about one running process, cached by pid and creation
// time so a recycled pid never inherits another app's sweater.
struct proc_info {
  DWORD pid;
  char name[64];       // display name, used to match colourways (apps.conf)
  char key[64];        // stable identity for the Apps menu: lower-case exe name
  wchar_t exe[MAX_PATH];
  bool own;            // this process
};
const struct proc_info* proc_info_get(DWORD pid);
// The process a top-level window really belongs to. UWP apps are hosted in
// ApplicationFrameHost.exe; their content window names the real process.
DWORD window_owner_pid(HWND hwnd);
// The app's icon, premultiplied BGRA, top row first; false if it has none.
bool proc_icon_pixels(DWORD pid, HWND hint, int size, uint32_t* out);
// A small icon bitmap for a menu item (premultiplied 32-bit DIB), or NULL.
HBITMAP proc_icon_bitmap(DWORD pid, int size);
void proc_info_prune(void);

// tracker.c ------------------------------------------------------------------
void tracker_start(void);
void tracker_stop(void);
void tracker_redraw_all(void);        // a style setting changed
void tracker_redraw_pid(DWORD pid);   // an app's colours became known
void tracker_refresh(void);           // sweaters on/off or the app filter changed
void tracker_restack(void);           // WM_KNIT_RESTACK
void tracker_reconcile(void);         // periodic safety net
void tracker_display_changed(void);
int tracker_owners(DWORD* pids, int capacity);   // owners of eligible windows

// autoyarn_win.c ---------------------------------------------------------------
void knit_auto_yarn_ready(pid_t pid);  // implemented by tracker.c

// hidden_win.c ---------------------------------------------------------------
// knit_app_hidden() and friends are declared in src/misc/apps.h.

// main.c -----------------------------------------------------------------------
void knit_apply(const char* arg);
void knit_save_prefs(void);
void knit_update_tray(void);
bool knit_startup_enabled(void);
void knit_set_startup(bool on);

// menu.c -----------------------------------------------------------------------
void menu_show(HWND owner);
void menu_command(UINT id);

// icon.c -----------------------------------------------------------------------
// The yarn mark (assets/yarn-menu-icon.svg) for the notification area.
HICON yarn_tray_icon(int size, bool dark_mark, bool dimmed);
// The full-colour app icon, as an image of the given size.
CGImageRef yarn_app_image(int size);
bool yarn_write_ico(const char* path);
// A premultiplied top-down 32-bit DIB section for menus and icons.
HBITMAP knit_dib(int w, int h, uint32_t** bits);
