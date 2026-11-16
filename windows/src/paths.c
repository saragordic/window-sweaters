// Where Window Sweaters keeps its files on Windows:
//   %APPDATA%\Window Sweaters\apps.conf, charts\ and settings.ini
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include "app.h"

const char* knit_data_dir(void) {
  static char dir[1024];
  if (dir[0]) return dir;
  PWSTR base = NULL;
  if (FAILED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL, &base))) return "";
  wchar_t wdir[MAX_PATH * 2];
  _snwprintf(wdir, sizeof wdir / sizeof wdir[0], L"%s\\Window Sweaters", base);
  wdir[sizeof wdir / sizeof wdir[0] - 1] = 0;
  CoTaskMemFree(base);
  CreateDirectoryW(wdir, NULL);
  if (!WideCharToMultiByte(CP_UTF8, 0, wdir, -1, dir, sizeof dir, NULL, NULL)) dir[0] = 0;
  return dir;
}

const wchar_t* knit_settings_path(void) {
  static wchar_t path[MAX_PATH * 2];
  if (path[0]) return path;
  MultiByteToWideChar(CP_UTF8, 0, knit_data_dir(), -1, path, MAX_PATH);
  wcscat_s(path, sizeof path / sizeof path[0], L"\\settings.ini");
  return path;
}

wchar_t* knit_widen(const char* s, wchar_t* out, int capacity) {
  if (!out || capacity <= 0) return out;
  out[0] = 0;
  if (s) MultiByteToWideChar(CP_UTF8, 0, s, -1, out, capacity);
  out[capacity - 1] = 0;
  return out;
}

char* knit_narrow(const wchar_t* s, char* out, int capacity) {
  if (!out || capacity <= 0) return out;
  out[0] = 0;
  if (s) WideCharToMultiByte(CP_UTF8, 0, s, -1, out, capacity, NULL, NULL);
  out[capacity - 1] = 0;
  return out;
}
