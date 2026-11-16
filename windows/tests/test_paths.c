// Tests never read or write the user's own settings: their data folder is
// a fresh directory under %TEMP%.
#include <windows.h>
#include <stdio.h>

const char* knit_data_dir(void) {
  static char dir[MAX_PATH];
  if (dir[0]) return dir;
  char temp[MAX_PATH];
  GetTempPathA(MAX_PATH, temp);
  snprintf(dir, sizeof dir, "%swindow-sweaters-test-%lu", temp, GetCurrentProcessId());
  CreateDirectoryA(dir, NULL);
  return dir;
}
