// Which apps wear sweaters, as chosen in the Apps menu. A port of src/hidden.m:
// a default (every app on, or every app off) plus the apps ticked the other
// way. On Windows an app is keyed by its lower-case executable name, the most
// stable identity a desktop or packaged app has across updates.
#include "app.h"
#include "src/misc/apps.h"
#include <stdlib.h>
#include <string.h>

static bool g_on_by_default = true;
static char** g_exceptions;
static int g_exception_count, g_exception_capacity;

static int exception_index(const char* key) {
  if (!key || !*key) return -1;
  for (int i = 0; i < g_exception_count; i++)
    if (_stricmp(g_exceptions[i], key) == 0) return i;
  return -1;
}

bool knit_apps_on_by_default(void) { return g_on_by_default; }

bool knit_apps_set_all(bool on) {
  bool changed = g_on_by_default != on || g_exception_count;
  g_on_by_default = on;
  for (int i = 0; i < g_exception_count; i++) free(g_exceptions[i]);
  g_exception_count = 0;
  return changed;
}

bool knit_app_hidden(const char* key) {
  return (exception_index(key) >= 0) == g_on_by_default;
}

bool knit_app_set_hidden(const char* key, bool hidden) {
  if (!key || !*key) return false;
  int index = exception_index(key);
  bool listed = hidden == g_on_by_default;   // listed means "differs from the default"
  if (listed) {
    if (index >= 0) return false;
    if (g_exception_count == g_exception_capacity) {
      int capacity = g_exception_capacity ? g_exception_capacity * 2 : 16;
      char** grown = realloc(g_exceptions, sizeof(*grown) * (size_t)capacity);
      if (!grown) return false;
      g_exceptions = grown;
      g_exception_capacity = capacity;
    }
    char* copy = _strdup(key);
    if (!copy) return false;
    g_exceptions[g_exception_count++] = copy;
    return true;
  }
  if (index < 0) return false;
  free(g_exceptions[index]);
  memmove(g_exceptions + index, g_exceptions + index + 1,
          sizeof(*g_exceptions) * (size_t)(g_exception_count - index - 1));
  g_exception_count--;
  return true;
}

int knit_app_exception_count(void) { return g_exception_count; }

const char* knit_app_exception(int index) {
  return index >= 0 && index < g_exception_count ? g_exceptions[index] : NULL;
}

bool knit_pid_hidden(int pid) {
  if (g_on_by_default && !g_exception_count) return false;   // the usual case
  const struct proc_info* info = pid > 0 ? proc_info_get((DWORD)pid) : NULL;
  if (!info || !info->exe[0]) return !g_on_by_default;
  return knit_app_hidden(info->key);
}
