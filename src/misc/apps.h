#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

// Per-app colourways.
//
// Some apps deserve their own yarn regardless of which colour the window would
// otherwise be handed. Rules live in a plain text file the user owns:
//
//   Claude   = #C68468 atelier-claude
//   Spotify  = #3B6450 atelier-spotify
//
// The name on the left is matched as a case-insensitive prefix of the app's
// process name, so "Claude" also covers "Claude Helper (Renderer)". An optional
// chart name after the colour gives that app its own pattern too.

#define KNIT_APP_RULES_MAX 64

struct app_rule {
  char     match[64];
  uint32_t color;
  char     chart[64];   // empty = plain in By App mode
};

extern struct app_rule g_app_rules[KNIT_APP_RULES_MAX];
extern int g_app_rule_count;
extern bool g_knit_pattern_by_app; // true = app profiles; false = global selection

/// Path to apps.conf, created and seeded with defaults on first run.
const char* knit_apps_path(void);

/// (Re)read apps.conf. Returns the number of user rules loaded. Built-in app
/// colourways remain available independently of this file.
int knit_apps_load(void);

/// Longest-prefix user match, then built-in app match, or NULL.
const struct app_rule* knit_app_rule(const char* app_name);

/// Resolve the effective chart (-1 = plain), independently of the app's yarn.
/// By App uses the profile, falling back to plain; global mode overrides it.
int knit_pattern_for_app(const char* app_name);

/// Select "by-app", "none" (global plain), or a loaded chart (global pattern).
/// Returns false for an unknown name without changing the current selection.
bool knit_pattern_select(const char* name);

/// Resolve a generic Electron executable to its containing app's filename.
/// Leaves output untouched on failure. Called only when a window is discovered.
bool knit_app_name_from_executable(const char* path, char* output, size_t capacity);

/// Filename of a .app bundle, from a path to the bundle or something inside it.
bool knit_app_name_from_bundle(const char* path, char* output, size_t capacity);

/// Resolve a Safari web app's "Web App" process to its bundle name via argv.
bool knit_app_name_from_webapp(pid_t pid, char* output, size_t capacity);
