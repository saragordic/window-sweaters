// Which apps wear sweaters, as chosen in the Apps menu, and how a window's
// owner is named for it.
#import <AppKit/AppKit.h>
#include "misc/apps.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// A starting point plus exceptions: every app follows g_on_by_default unless
// it is listed, and a listed app is the opposite. "Turn Off for All Apps" is
// therefore a real default that also covers apps opened later. The list grows
// as needed: a limit here would only turn a click into a silent no-op.
static bool g_on_by_default = true;
static char** g_exceptions;
static int g_exception_count, g_exception_capacity;

static int exception_index(const char* bundle_id) {
  if (!bundle_id || !*bundle_id) return -1;
  for (int i = 0; i < g_exception_count; i++)
    if (strcmp(g_exceptions[i], bundle_id) == 0) return i;
  return -1;
}

bool knit_apps_on_by_default(void) { return g_on_by_default; }

bool knit_apps_set_all(bool on) {
  assert(pthread_main_np());
  bool changed = g_on_by_default != on || g_exception_count;
  g_on_by_default = on;
  for (int i = 0; i < g_exception_count; i++) free(g_exceptions[i]);
  g_exception_count = 0;
  return changed;
}

bool knit_app_hidden(const char* bundle_id) {
  return (exception_index(bundle_id) >= 0) == g_on_by_default;
}

bool knit_app_set_hidden(const char* bundle_id, bool hidden) {
  assert(pthread_main_np());
  if (!bundle_id || !*bundle_id) return false;
  int index = exception_index(bundle_id);
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
    char* copy = strdup(bundle_id);
    if (!copy) return false;
    g_exceptions[g_exception_count++] = copy;
    return true;
  }
  if (index < 0) return false;
  // Keep insertion order so the saved list is stable across launches.
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

// Ask AppKit, the same source the Apps menu lists, rather than guessing from
// the executable's path: a running app need not live at an ".app" path. Chrome,
// for one, runs from a temporary "Google Chrome.app.bundle" clone after an update.
bool knit_pid_hidden(int pid) {
  assert(pthread_main_np());
  if (g_on_by_default && !g_exception_count) return false;   // the usual case
  @autoreleasepool {
    NSString* identifier = pid > 0
      ? [NSRunningApplication runningApplicationWithProcessIdentifier:pid].bundleIdentifier
      : nil;
    // Without an identifier it cannot be listed, so it follows the default.
    if (!identifier.length) return !g_on_by_default;
    return knit_app_hidden(identifier.UTF8String);
  }
}
