// Names and icons for the processes that own windows.
//
// The display name is the executable's FileDescription ("Google Chrome",
// "Microsoft Word", "Visual Studio Code"), which is what the shared colourway
// collection matches against, with a few aliases for Windows executables
// whose description does not say which app they are.
#include "app.h"
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define PROC_CACHE_MAX 256

struct proc_slot {
  struct proc_info info;
  FILETIME created;
  DWORD checked;      // GetTickCount of the last liveness check
  bool used;
};
static struct proc_slot g_procs[PROC_CACHE_MAX];

// Executables whose FileDescription is missing, generic or unlike the app's
// name. Matched on the lower-case file name; the value is the name used for
// colourways, so these apps wear the collection's sweater.
static const struct { const char* exe; const char* name; } k_aliases[] = {
  { "code.exe", "Visual Studio Code" },
  { "code - insiders.exe", "Visual Studio Code" },
  { "windowsterminal.exe", "Terminal" },
  { "olk.exe", "Microsoft Outlook" },
  { "ms-teams.exe", "Microsoft Teams" },
  { "msteams.exe", "Microsoft Teams" },
  { "microsoft.photos.exe", "Photos" },
  { "photosapp.exe", "Photos" },
  { "telegram.exe", "Telegram" },
  { "whatsapp.exe", "WhatsApp" },
  { "whatsapp.root.exe", "WhatsApp" },
  { "zoom.exe", "Zoom" },
  { "spotify.exe", "Spotify" },
  { "chatgpt.exe", "ChatGPT" },
  { "claude.exe", "Claude" },
  { "notion.exe", "Notion" },
  { "figma.exe", "Figma" },
  { "slack.exe", "Slack" },
  { "discord.exe", "Discord" },
  { "cursor.exe", "Cursor" },
  { "winword.exe", "Microsoft Word" },
  { "excel.exe", "Microsoft Excel" },
  { "powerpnt.exe", "Microsoft PowerPoint" },
  { "outlook.exe", "Microsoft Outlook" },
  { "chrome.exe", "Google Chrome" },
  { "firefox.exe", "Firefox" },
};

static void file_description(const wchar_t* exe, wchar_t* out, int capacity) {
  out[0] = 0;
  DWORD handle = 0, size = GetFileVersionInfoSizeW(exe, &handle);
  if (!size) return;
  void* data = malloc(size);
  if (!data) return;
  if (GetFileVersionInfoW(exe, 0, size, data)) {
    struct { WORD lang, cp; }* tr = NULL;
    UINT trlen = 0;
    wchar_t query[64];
    wchar_t* value = NULL;
    UINT len = 0;
    if (VerQueryValueW(data, L"\\VarFileInfo\\Translation", (void**)&tr, &trlen) && trlen >= 4) {
      _snwprintf(query, 64, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].lang, tr[0].cp);
      VerQueryValueW(data, query, (void**)&value, &len);
    }
    if (!value || !len) {
      // Common fallbacks: US English in Unicode, then in Windows-1252.
      VerQueryValueW(data, L"\\StringFileInfo\\040904b0\\FileDescription", (void**)&value, &len);
      if (!value || !len)
        VerQueryValueW(data, L"\\StringFileInfo\\040904e4\\FileDescription", (void**)&value, &len);
    }
    if (value && len) {
      wcsncpy(out, value, (size_t)capacity - 1);
      out[capacity - 1] = 0;
      // Trim trailing spaces some vendors leave in.
      size_t n = wcslen(out);
      while (n && out[n - 1] == L' ') out[--n] = 0;
    }
  }
  free(data);
}

static void fill_info(struct proc_info* info, DWORD pid) {
  memset(info, 0, sizeof *info);
  info->pid = pid;
  info->own = pid == GetCurrentProcessId();
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (h) {
    DWORD n = MAX_PATH;
    if (!QueryFullProcessImageNameW(h, 0, info->exe, &n)) info->exe[0] = 0;
    CloseHandle(h);
  }
  const wchar_t* base = wcsrchr(info->exe, L'\\');
  base = base ? base + 1 : info->exe;
  char exe_name[128];
  knit_narrow(base, exe_name, sizeof exe_name);
  for (char* p = exe_name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
  snprintf(info->key, sizeof info->key, "%s", exe_name[0] ? exe_name : "unknown");

  for (size_t i = 0; i < sizeof k_aliases / sizeof k_aliases[0]; i++)
    if (strcmp(exe_name, k_aliases[i].exe) == 0) {
      snprintf(info->name, sizeof info->name, "%s", k_aliases[i].name);
      return;
    }
  wchar_t desc[128];
  if (info->exe[0]) file_description(info->exe, desc, 128);
  else desc[0] = 0;
  if (desc[0]) {
    knit_narrow(desc, info->name, sizeof info->name);
    return;
  }
  // No description: the file name without ".exe".
  wchar_t plain[MAX_PATH];
  wcsncpy(plain, base, MAX_PATH - 1);
  plain[MAX_PATH - 1] = 0;
  wchar_t* dot = wcsrchr(plain, L'.');
  if (dot && _wcsicmp(dot, L".exe") == 0) *dot = 0;
  knit_narrow(plain[0] ? plain : L"Unknown", info->name, sizeof info->name);
}

static bool creation_time(DWORD pid, FILETIME* out) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;
  FILETIME exit_time, kernel, user;
  BOOL ok = GetProcessTimes(h, out, &exit_time, &kernel, &user);
  CloseHandle(h);
  return ok != 0;
}

const struct proc_info* proc_info_get(DWORD pid) {
  DWORD now = GetTickCount();
  int free_slot = -1, oldest = 0;
  for (int i = 0; i < PROC_CACHE_MAX; i++) {
    struct proc_slot* s = &g_procs[i];
    if (!s->used) { if (free_slot < 0) free_slot = i; continue; }
    if (s->info.pid == pid) {
      // Re-check the identity at most once a second: pids are recycled.
      if (now - s->checked > 1000) {
        FILETIME t;
        s->checked = now;
        if (creation_time(pid, &t) && CompareFileTime(&t, &s->created) != 0) {
          s->created = t;
          fill_info(&s->info, pid);
        }
      }
      return &s->info;
    }
    if (s->checked < g_procs[oldest].checked || !g_procs[oldest].used) oldest = i;
  }
  int slot = free_slot >= 0 ? free_slot : oldest;
  struct proc_slot* s = &g_procs[slot];
  s->used = true;
  s->checked = now;
  if (!creation_time(pid, &s->created)) memset(&s->created, 0, sizeof s->created);
  fill_info(&s->info, pid);
  return &s->info;
}

void proc_info_prune(void) {
  for (int i = 0; i < PROC_CACHE_MAX; i++) {
    if (!g_procs[i].used) continue;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, g_procs[i].info.pid);
    bool alive = h && WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    if (h) CloseHandle(h);
    if (!alive) g_procs[i].used = false;
  }
}

struct uwp_search { DWORD host; DWORD found; };

static BOOL CALLBACK find_uwp_child(HWND child, LPARAM lp) {
  struct uwp_search* s = (struct uwp_search*)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(child, &pid);
  if (pid && pid != s->host) { s->found = pid; return FALSE; }
  return TRUE;
}

DWORD window_owner_pid(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  wchar_t cls[64];
  if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, L"ApplicationFrameWindow") == 0) {
    struct uwp_search s = { pid, 0 };
    EnumChildWindows(hwnd, find_uwp_child, (LPARAM)&s);
    if (s.found) return s.found;
  }
  return pid;
}

// --- icons --------------------------------------------------------------------

HBITMAP knit_dib(int w, int h, uint32_t** bits) {
  BITMAPINFO bi = { 0 };
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;   // top-down, like the CoreGraphics shim
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* p = NULL;
  HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &p, NULL, 0);
  if (bits) *bits = p;
  if (bmp && p) memset(p, 0, (size_t)w * h * 4);
  return bmp;
}

// Draw an icon into a premultiplied top-down buffer. Drawing it once on black
// and once on white recovers alpha from how much background shows through,
// which works the same for alpha icons and old masked ones.
static bool icon_to_pixels(HICON icon, int size, uint32_t* out) {
  uint32_t *black = NULL, *white = NULL;
  HBITMAP bb = knit_dib(size, size, &black), wb = knit_dib(size, size, &white);
  bool ok = false;
  HDC dc = CreateCompatibleDC(NULL);
  if (bb && wb && dc) {
    HGDIOBJ old = SelectObject(dc, bb);
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);
    SelectObject(dc, wb);
    for (int i = 0; i < size * size; i++) white[i] = 0x00ffffffu;
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);
    SelectObject(dc, old);
    GdiFlush();
    for (int i = 0; i < size * size; i++) {
      uint32_t p = black[i], q = white[i];
      int show = 0;
      for (int sh = 0; sh < 24; sh += 8) {
        int d = (int)((q >> sh) & 255) - (int)((p >> sh) & 255);
        if (d > show) show = d;
      }
      int a = 255 - show;
      if (a < 0) a = 0;
      uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
      if (r > (uint32_t)a) r = a;
      if (g > (uint32_t)a) g = a;
      if (b > (uint32_t)a) b = a;
      out[i] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
    }
    ok = true;
  }
  if (dc) DeleteDC(dc);
  if (bb) DeleteObject(bb);
  if (wb) DeleteObject(wb);
  return ok;
}

static HICON load_icon(DWORD pid, HWND hint, int size) {
  const struct proc_info* info = proc_info_get(pid);
  HICON icon = NULL;
  if (info->exe[0]) {
    if (FAILED(SHDefExtractIconW(info->exe, 0, 0, &icon, NULL, MAKELONG(size, size)))) icon = NULL;
  }
  if (!icon && hint) {
    // The window's own icon. Borrowed, so copy it before the caller destroys it.
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(hint, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result)
        && result)
      icon = CopyIcon((HICON)result);
    if (!icon) {
      HICON cls = (HICON)GetClassLongPtrW(hint, GCLP_HICON);
      if (cls) icon = CopyIcon(cls);
    }
  }
  return icon;
}

bool proc_icon_pixels(DWORD pid, HWND hint, int size, uint32_t* out) {
  HICON icon = load_icon(pid, hint, size);
  if (!icon) return false;
  bool ok = icon_to_pixels(icon, size, out);
  DestroyIcon(icon);
  return ok;
}

HBITMAP proc_icon_bitmap(DWORD pid, int size) {
  HICON icon = load_icon(pid, NULL, size);
  if (!icon) return NULL;
  uint32_t* bits = NULL;
  HBITMAP bmp = knit_dib(size, size, &bits);
  if (bmp && !icon_to_pixels(icon, size, bits)) { DeleteObject(bmp); bmp = NULL; }
  DestroyIcon(icon);
  return bmp;
}
