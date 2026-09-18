#include "misc/apps.h"
#include "misc/chart.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

struct app_rule g_app_rules[KNIT_APP_RULES_MAX];
int g_app_rule_count = 0;
bool g_knit_pattern_by_app = true;

// The collection stays available even when apps.conf is absent or unreadable.
// User rules are evaluated first, so personal colourways always take priority.
static const struct app_rule k_collection[] = {
  { "Finder",          0xff258de0u, "atelier-finder" },
  { "Microsoft Teams", 0xff9283c1u, "atelier-teams" },
  { "MSTeams",         0xff9283c1u, "atelier-teams" },
  { "Teams",           0xff9283c1u, "atelier-teams" },
  { "Claude",          0xffd58561u, "atelier-claude" },
  { "Codex",           0xff0a84ffu, "atelier-chatgpt" },
  { "Spotify",         0xff497641u, "atelier-spotify" },
  { "Notion",          0xfff5f5f2u, "atelier-notion" },
  { "WhatsApp",        0xff6ea77bu, "atelier-whatsapp" },
  { "Figma",           0xffad7197u, "atelier-figma" },
  { "Google Chrome",   0xfff4f0e6u, "atelier-chrome" },
  { "Chrome",          0xfff4f0e6u, "atelier-chrome" },
  { "Paper",           0xff83ade8u, "atelier-paper" },
  { "Chromium",        0xfff4f0e6u, "atelier-chrome" },
  { "Safari", 0xff268ed8u, "atelier-safari" },
  { "Firefox", 0xff643a9au, "atelier-firefox" },
  { "Cursor", 0xff26251eu, "atelier-cursor" },
  { "Slack", 0xff542a52u, "atelier-slack" },
  { "zoom.us", 0xff2877ebu, "atelier-zoom" },
  { "Zoom", 0xff2877ebu, "atelier-zoom" },
  { "Telegram", 0xff389eceu, "atelier-telegram" },
  { "Messages", 0xff55af51u, "atelier-messages" },
  { "Mail", 0xff2986ceu, "atelier-mail" },
  { "Notes", 0xfff6f0d9u, "atelier-notes" },
  { "Calendar", 0xfff7f3e9u, "atelier-calendar" },
  { "Reminders", 0xfff6f3ebu, "atelier-reminders" },
  { "Music", 0xffe64e70u, "atelier-music" },
  { "Apple Music", 0xffe64e70u, "atelier-music" },
  { "Photos", 0xfff8f0dbu, "atelier-photos" },
  { "Preview", 0xff597bafu, "atelier-preview" },
  { "Microsoft Word", 0xff2855a1u, "atelier-word" },
  { "Microsoft Excel", 0xff28674fu, "atelier-excel" },
  { "Microsoft PowerPoint", 0xffb9573du, "atelier-powerpoint" },
  { "Microsoft Outlook", 0xff176bb7u, "atelier-outlook" },
  { "Code", 0xff237cafu, "atelier-vscode" },
  { "Visual Studio Code", 0xff237cafu, "atelier-vscode" },
  { "Adobe Photoshop", 0xff182f45u, "atelier-photoshop" },
  { "Adobe Illustrator", 0xff4e3029u, "atelier-illustrator" },
  { "ChatGPT", 0xff0a84ffu, "atelier-chatgpt" },
  { "Grok Bot", 0xff34383bu, "atelier-grok" },
  { "Discord", 0xff5865f2u, "checker" },
  { "Granola", 0xff292e2au, "atelier-granola" },
  { "Terminal", 0xff303c35u, "atelier-terminal" },
  { "ghostty", 0xff2b3350u, "atelier-ghostty" },
};

static const char* k_default_conf =
  "# Window Sweaters — per-app colourways\n"
  "#\n"
  "# <app name> = #RRGGBB [chart]\n"
  "#\n"
  "# The name is matched as a case-insensitive prefix of the app's process\n"
  "# name, so \"Claude\" also covers \"Claude Helper\". The optional chart name\n"
  "# is a built-in chart or any file in the charts folder, without .png.\n"
  "# Personal rules take priority over the built-in app collection.\n"
  "#\n"
  "# Edit, then restart Window Sweaters to load your changes.\n"
  "\n"
  "# 37 apps already have their own yarn and chart; see docs/COLLECTION.md.\n"
  "# Uncomment to customize:\n"
  "# Claude = #D58561 atelier-claude\n";

const char* knit_apps_path(void) {
  static char path[1024];
  if (path[0]) return path;
  const char* home = getenv("HOME");
  if (!home || !*home) return "";
  snprintf(path, sizeof path, "%s/Library/Application Support/Knit Borders", home);
  mkdir(path, 0755);
  snprintf(path, sizeof path, "%s/Library/Application Support/Knit Borders/apps.conf", home);
  FILE* f = fopen(path, "r");
  if (f) { fclose(f); return path; }
  f = fopen(path, "w");                     // seed it the first time
  if (f) { fputs(k_default_conf, f); fclose(f); }
  return path;
}

// trim leading and trailing whitespace in place
static char* trim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
  *e = 0;
  return s;
}

int knit_apps_load(void) {
  g_app_rule_count = 0;
  FILE* f = fopen(knit_apps_path(), "r");
  if (!f) return 0;

  char line[512];
  while (fgets(line, sizeof line, f) && g_app_rule_count < KNIT_APP_RULES_MAX) {
    // Reject an overlong rule as a whole rather than parsing its fragments.
    if (!strchr(line, '\n') && !feof(f)) {
      int ch;
      while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
      continue;
    }
    char* s = trim(line);
    if (!*s || *s == '#') continue;
    char* eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char* name = trim(s);
    char* val  = trim(eq + 1);
    if (!*name || strlen(name) >= sizeof g_app_rules[0].match || *val != '#') continue;

    // sscanf("%6x") accepts short and partially malformed values. Require
    // exactly six hex digits followed by whitespace or end of line instead.
    size_t len = strlen(val);
    if (len < 7 || (val[7] && !isspace((unsigned char)val[7]))) continue;
    int valid = 1;
    for (int i = 1; i <= 6; i++)
      if (!isxdigit((unsigned char)val[i])) valid = 0;
    if (!valid) continue;
    unsigned long rgb = strtoul(val + 1, NULL, 16);
    char chart[64] = {0};
    char* tail = trim(val + 7);
    if (*tail && *tail != '#') {
      size_t n = strcspn(tail, " \t\r\n");
      if (n >= sizeof chart) continue;
      memcpy(chart, tail, n);
      tail = trim(tail + n);
      if (*tail && *tail != '#') continue;
    }

    struct app_rule* r = &g_app_rules[g_app_rule_count++];
    snprintf(r->match, sizeof r->match, "%s", name);
    r->color = 0xff000000u | (uint32_t)rgb;
    snprintf(r->chart, sizeof r->chart, "%s", chart);
  }
  fclose(f);
  return g_app_rule_count;
}

const struct app_rule* knit_app_rule(const char* app_name) {
  if (!app_name || !*app_name) return NULL;
  const struct app_rule* best = NULL;
  size_t best_len = 0;
  for (int i = 0; i < g_app_rule_count; i++) {
    size_t n = strlen(g_app_rules[i].match);
    if (n && strncasecmp(app_name, g_app_rules[i].match, n) == 0 && n > best_len) {
      best = &g_app_rules[i];
      best_len = n;                        // longest prefix wins
    }
  }
  if (best) return best;
  for (size_t i = 0; i < sizeof k_collection / sizeof k_collection[0]; i++) {
    size_t n = strlen(k_collection[i].match);
    if (strncasecmp(app_name, k_collection[i].match, n) == 0 && n > best_len) {
      best = &k_collection[i];
      best_len = n;
    }
  }
  return best;
}

bool knit_app_rule_personal(const struct app_rule* rule) {
  // Equality only: ordering pointers into two different arrays is undefined.
  for (int i = 0; i < g_app_rule_count; i++) if (rule == &g_app_rules[i]) return true;
  return false;
}

int knit_pattern_for_app(const char* app_name) {
  if (!g_knit_pattern_by_app)
    return g_chart_active >= 0 && g_chart_active < g_chart_count ? g_chart_active : -1;
  const struct app_rule* rule = knit_app_rule(app_name);
  return rule && rule->chart[0] ? knit_chart_index(rule->chart) : -1;
}

bool knit_pattern_select(const char* name) {
  if (!name) return false;
  if (strcmp(name, "by-app") == 0) {
    g_knit_pattern_by_app = true;
    return true;
  }
  int index = strcmp(name, "none") == 0 ? -1 : knit_chart_index(name);
  if (index < 0 && strcmp(name, "none") != 0) return false;
  g_chart_active = index;
  g_knit_pattern_by_app = false;
  return true;
}

bool knit_app_name_from_executable(const char* path, char* output, size_t capacity) {
  if (!path || !output || !capacity) return false;
  const char* marker = strstr(path, ".app/Contents/MacOS/");
  if (!marker || !marker[strlen(".app/Contents/MacOS/")]) return false;
  const char* start = marker;
  while (start > path && start[-1] != '/') start--;
  size_t length = (size_t)(marker - start);
  if (!length || length >= capacity) return false;
  memcpy(output, start, length);
  output[length] = '\0';
  return true;
}
