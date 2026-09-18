#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

// Which apps wear sweaters, chosen in the Apps menu.
//
// A default for every app (on, or off after "Turn Off for All Apps") plus the
// apps ticked the other way. Keyed by bundle identifier, the one name macOS
// keeps stable across localisation, renames and self-updates. Kept apart from
// the startup script's blacklist=/whitelist= so neither can overwrite the
// other; a window must pass both. Main thread only; asserted.

bool knit_apps_on_by_default(void);
/// Every app on, or every app off, forgetting individual choices. Returns
/// true only if this changed anything.
bool knit_apps_set_all(bool on);
bool knit_app_hidden(const char* bundle_id);
/// Returns true only if this changed anything. No length or count limit, so
/// a click on any listed app always takes effect.
bool knit_app_set_hidden(const char* bundle_id, bool hidden);
/// The apps that differ from the default, in the order they were chosen.
int knit_app_exception_count(void);
const char* knit_app_exception(int index);

/// Whether a window owned by this process should go without a sweater. Free
/// while every app is on, the usual case. The owner is asked of AppKit, never
/// parsed from a path. A process without a bundle identifier cannot be
/// listed, so it follows the default.
bool knit_pid_hidden(int pid);
