// Apps switched off from the menu: the list, and how a window's owner is named.
#import <AppKit/AppKit.h>
#include "misc/apps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_list(void) {
  knit_apps_set_all(true);
  assert(knit_apps_on_by_default() && knit_app_exception_count() == 0);
  assert(!knit_app_exception(0) && !knit_app_exception(-1));
  assert(!knit_app_hidden("com.apple.Safari"));
  assert(!knit_apps_set_all(true));                              // nothing to change

  assert(knit_app_set_hidden("com.apple.Safari", true));
  assert(!knit_app_set_hidden("com.apple.Safari", true));        // no duplicates
  assert(knit_app_hidden("com.apple.Safari"));
  assert(!knit_app_hidden("com.apple.safari"));                  // identifiers are exact
  assert(!knit_app_hidden("com.apple.Safari.helper"));           // and never prefixes
  assert(knit_app_set_hidden("com.figma.Desktop", true));
  assert(knit_app_set_hidden("com.tinyspeck.slackmacgap", true));

  // Turning one back on keeps the rest in the order they were chosen.
  assert(knit_app_set_hidden("com.figma.Desktop", false));
  assert(!knit_app_set_hidden("com.figma.Desktop", false));
  assert(knit_app_exception_count() == 2);
  assert(!strcmp(knit_app_exception(0), "com.apple.Safari"));
  assert(!strcmp(knit_app_exception(1), "com.tinyspeck.slackmacgap"));

  // No length limit: a long identifier is kept whole, never cut into another app's.
  char long_id[1024];
  memset(long_id, 'a', sizeof long_id - 1);
  long_id[sizeof long_id - 1] = 0;
  assert(knit_app_set_hidden(long_id, true) && knit_app_hidden(long_id));
  long_id[600] = 0;
  assert(!knit_app_hidden(long_id));
  long_id[600] = 'a';
  assert(knit_app_set_hidden(long_id, false) && knit_app_exception_count() == 2);
  assert(!knit_app_set_hidden("", true) && !knit_app_set_hidden(NULL, true));

  // All off is a default, not a list: it covers apps never seen before, and
  // the apps ticked back on become the exceptions.
  assert(knit_apps_set_all(false));
  assert(!knit_apps_on_by_default() && knit_app_exception_count() == 0);
  assert(knit_app_hidden("com.apple.Safari") && knit_app_hidden("com.example.never-seen"));
  assert(!knit_app_set_hidden("com.apple.Safari", true));        // already off
  assert(knit_app_set_hidden("com.apple.Safari", false));
  assert(!knit_app_hidden("com.apple.Safari") && knit_app_hidden("com.figma.Desktop"));
  assert(knit_app_exception_count() == 1 && !strcmp(knit_app_exception(0), "com.apple.Safari"));
  assert(knit_app_set_hidden("com.apple.Safari", true) && knit_app_exception_count() == 0);
  assert(!knit_apps_set_all(false));

  // Back to all on forgets every individual choice.
  knit_app_set_hidden("com.apple.Safari", false);
  assert(knit_apps_set_all(true));
  assert(knit_app_exception_count() == 0 && !knit_app_hidden("com.example.never-seen"));

  // No count limit either, and removal keeps working at any size.
  char id[32];
  for (int i = 0; i < 1000; i++) {
    snprintf(id, sizeof id, "com.example.app%d", i);
    assert(knit_app_set_hidden(id, true));
  }
  assert(knit_app_exception_count() == 1000 && knit_app_hidden("com.example.app999"));
  assert(knit_app_set_hidden("com.example.app0", false));
  assert(!strcmp(knit_app_exception(0), "com.example.app1"));
  assert(knit_app_set_hidden("com.example.one-more", true) && knit_app_exception_count() == 1000);
  knit_apps_set_all(true);
}

static void test_owners(void) {
  // This test is a bare executable: no bundle identifier, so it follows the default.
  knit_app_set_hidden("com.apple.calculator", true);
  assert(!knit_pid_hidden(getpid()) && !knit_pid_hidden(0) && !knit_pid_hidden(-1));
  knit_apps_set_all(false);
  assert(knit_pid_hidden(getpid()) && knit_pid_hidden(0));
  knit_apps_set_all(true);

  // A real app, named the way the Apps menu names it. The owner is asked of
  // AppKit, never parsed from a path: Chrome runs from "…/Google Chrome.app.bundle"
  // after a background update, which path parsing failed to recognise.
  NSRunningApplication* finder = [NSRunningApplication
    runningApplicationsWithBundleIdentifier:@"com.apple.finder"].firstObject;
  if (!finder) { puts("SKIP: Finder is not running; owner lookup not exercised"); return; }
  pid_t pid = finder.processIdentifier;
  assert(!knit_pid_hidden(pid));                  // everything on: never hidden
  knit_app_set_hidden("com.apple.finder", true);
  assert(knit_pid_hidden(pid));
  knit_app_set_hidden("com.apple.finder", false);
  assert(!knit_pid_hidden(pid));
  knit_apps_set_all(false);
  assert(knit_pid_hidden(pid));
  knit_app_set_hidden("com.apple.finder", false);
  assert(!knit_pid_hidden(pid));
  knit_apps_set_all(true);
}

int main(void) {
  test_list();
  test_owners();
  puts("PASS: per-app choices, all on/all off defaults, exact identifiers, no silent limits, window owners resolved through AppKit");
  return 0;
}
