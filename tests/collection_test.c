// clang -std=c99 -Wall -Wextra -Werror -fsanitize=address -g -Isrc \
//   tests/collection_test.c src/apps.c src/chart.c -o /tmp/knit-collection-test \
//   -framework ApplicationServices -framework ImageIO -framework CoreFoundation
#include "misc/apps.h"
#include "misc/chart.h"
#include "catalogue.h"
#include <ApplicationServices/ApplicationServices.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* const collection_names[] = {
  "atelier-finder", "atelier-terminal", "atelier-grok", "atelier-teams", "atelier-claude", "atelier-codex",
  "atelier-spotify", "atelier-notion", "atelier-whatsapp", "atelier-figma",
  "atelier-chrome", "atelier-paper",
  "atelier-safari", "atelier-firefox", "atelier-cursor", "atelier-slack", "atelier-zoom", "atelier-telegram", "atelier-messages", "atelier-mail", "atelier-notes", "atelier-calendar", "atelier-reminders", "atelier-music", "atelier-photos", "atelier-preview", "atelier-word", "atelier-excel", "atelier-powerpoint", "atelier-outlook", "atelier-vscode", "atelier-photoshop", "atelier-illustrator",
  "atelier-granola", "atelier-chatgpt", "atelier-ghostty", "atelier-opencode", "atelier-beeper", "atelier-willow", "atelier-antigravity", "atelier-ytmusic", "atelier-protonvpn", "atelier-xcode", "atelier-appstore", "atelier-androidstudio", "atelier-settings", "atelier-weather", "braid", "blockstripe", "checker", "seedling", "trim", "picnic", "ribbon", "posy", "twinkle", "candy-stripe", "zigzag"
};
enum { COLLECTION_COUNT = sizeof collection_names / sizeof collection_names[0] };

static void make_directory(const char* path) {
  assert(mkdir(path, 0700) == 0);
}

static void write_png(const char* dir, const char* name) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.png", dir, name);
  uint32_t pixels[] = {0xff102030, 0xff405060, 0xff708090, 0xffa0b0c0};
  CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(pixels, 2, 2, 8, 8, colors,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  assert(context);
  CGImageRef image = CGBitmapContextCreateImage(context);
  CFStringRef string = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
  CFURLRef url = CFURLCreateWithFileSystemPath(NULL, string, kCFURLPOSIXPathStyle, false);
  CGImageDestinationRef output = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
  assert(output);
  CGImageDestinationAddImage(output, image, NULL);
  assert(CGImageDestinationFinalize(output));
  CFRelease(output);
  CFRelease(url);
  CFRelease(string);
  CGImageRelease(image);
  CGContextRelease(context);
  CGColorSpaceRelease(colors);
}

static void remove_png(const char* dir, const char* name) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.png", dir, name);
  assert(unlink(path) == 0);
}

static void test_apps(const char* home) {
  assert(setenv("HOME", home, 1) == 0);
  const char* path = knit_apps_path();
  assert(knit_apps_load() == 0); // new config inherits the collection
  assert(knit_app_rule("Finder")->color == 0xff258de0);
  assert(strcmp(knit_app_rule("msteams Helper")->chart, "atelier-teams") == 0);
  assert(strcmp(knit_app_rule("Microsoft Teams")->chart, "atelier-teams") == 0);
  assert(strcmp(knit_app_rule("Google Chrome Helper")->chart, "atelier-chrome") == 0);
  assert(strcmp(knit_app_rule("Chrome")->chart, "atelier-chrome") == 0);
  assert(strcmp(knit_app_rule("Chromium")->chart, "atelier-chrome") == 0);
  assert(CATALOGUE_COUNT == 48);
  assert(strcmp(knit_app_rule("OpenCode Helper (Renderer)")->chart, "atelier-opencode") == 0);
  assert(strcmp(knit_app_rule("Beeper Desktop")->chart, "atelier-beeper") == 0);
  assert(strcmp(knit_app_rule("Beeper Helper")->chart, "atelier-beeper") == 0);
  assert(strcmp(knit_app_rule("Willow Voice")->chart, "atelier-willow") == 0);
  assert(strcmp(knit_app_rule("Antigravity Helper")->chart, "atelier-antigravity") == 0);
  assert(strcmp(knit_app_rule("YT Music")->chart, "atelier-ytmusic") == 0);
  assert(strcmp(knit_app_rule("YouTube Music")->chart, "atelier-ytmusic") == 0);
  assert(strcmp(knit_app_rule("Music")->chart, "atelier-music") == 0);
  assert(strcmp(knit_app_rule("ProtonVPN")->chart, "atelier-protonvpn") == 0);
  assert(strcmp(knit_app_rule("Proton VPN")->chart, "atelier-protonvpn") == 0);
  assert(knit_app_rule("Proton Mail") == NULL);
  assert(strcmp(knit_app_rule("Xcode")->chart, "atelier-xcode") == 0);
  assert(strcmp(knit_app_rule("Code")->chart, "atelier-vscode") == 0);
  assert(strcmp(knit_app_rule("App Store")->chart, "atelier-appstore") == 0);
  assert(strcmp(knit_app_rule("Android Studio")->chart, "atelier-androidstudio") == 0);
  assert(strcmp(knit_app_rule("studio")->chart, "atelier-androidstudio") == 0);
  assert(strcmp(knit_app_rule("System Settings")->chart, "atelier-settings") == 0);
  assert(strcmp(knit_app_rule("System Preferences")->chart, "atelier-settings") == 0);
  assert(strcmp(knit_app_rule("Weather")->chart, "atelier-weather") == 0);
  for (int i = 0; i < CATALOGUE_COUNT; i++) {
    const struct app_rule* rule = knit_app_rule(catalogue[i].match);
    assert(rule && strcmp(rule->chart, catalogue[i].chart) == 0);
  }
  assert(strcmp(knit_app_rule("Zoom")->chart, "atelier-zoom") == 0);
  assert(strcmp(knit_app_rule("Visual Studio Code")->chart, "atelier-vscode") == 0);
  assert(strcmp(knit_app_rule("Adobe Photoshop 2026")->chart, "atelier-photoshop") == 0);
  assert(knit_app_rule("Electron") == NULL);

  char identity[64] = "unchanged";
  assert(knit_app_name_from_executable("/Applications/Cursor.app/Contents/MacOS/Cursor", identity, sizeof identity));
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-cursor") == 0);
  assert(knit_app_name_from_executable("/Applications/Cursor.app/Contents/MacOS/Electron", identity, sizeof identity));
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-cursor") == 0);
  assert(knit_app_name_from_executable("/Applications/Android Studio.app/Contents/MacOS/studio", identity, sizeof identity));
  assert(strcmp(identity, "Android Studio") == 0);
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-androidstudio") == 0);
  assert(knit_app_name_from_executable(
      "/Applications/Visual Studio Code.app/Contents/MacOS/Electron", identity, sizeof identity));
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-vscode") == 0);
  assert(knit_app_name_from_executable(
      "/Volumes/Disk/Other App.app/Contents/MacOS/Electron", identity, sizeof identity));
  assert(strcmp(identity, "Other App") == 0 && knit_app_rule(identity) == NULL);
  assert(!knit_app_name_from_executable("/usr/bin/Electron", identity, sizeof identity));
  assert(!knit_app_name_from_executable("/A.app/Contents/MacOS/", identity, sizeof identity));
  assert(!knit_app_name_from_executable(NULL, identity, sizeof identity));
  assert(!knit_app_name_from_executable("/Long.app/Contents/MacOS/Electron", identity, 2));
  assert(strcmp(identity, "Other App") == 0);
  assert(knit_app_name_from_bundle("/Users/keith/Applications/YT Music.app", identity, sizeof identity));
  assert(strcmp(identity, "YT Music") == 0);
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-ytmusic") == 0);
  assert(knit_app_name_from_bundle("/Applications/YouTube Music.app/", identity, sizeof identity));
  assert(strcmp(knit_app_rule(identity)->chart, "atelier-ytmusic") == 0);
  assert(!knit_app_name_from_bundle("/usr/bin/Web App", identity, sizeof identity));
  assert(!knit_app_name_from_webapp(0, identity, sizeof identity));
  assert(knit_app_rule(NULL) == NULL);
  assert(knit_app_rule("") == NULL);
  assert(knit_app_rule("Unknown App") == NULL);

  FILE* file = fopen(path, "w");
  assert(file);
  fputs("Short = #abc\nLong = #1234567\nBadHex = #12345G\n"
        "Suffix = #123456z\nSign = #-12345\nJunk = #112233 a b\n"
        "App = #aBcDeF\nApp Helper = #334455 my-chart\n"
        "f = #010203 personal\nMST = #445566\nClaude = #776655\n"
        "Notion = #AABBCC # an inline comment\n", file);
  for (int i = 0; i < 600; i++) fputc('x', file);
  fputs(" = #000000\n", file);
  fclose(file);
  assert(knit_apps_load() == 6);
  assert(knit_app_rule("App")->color == 0xffabcdef);
  assert(strcmp(knit_app_rule("APP Helper Renderer")->chart, "my-chart") == 0);
  assert(knit_app_rule("Finder")->color == 0xff010203); // user prefix beats built-in
  assert(knit_app_rule("Figma")->color == 0xff010203);
  assert(knit_app_rule("MSTeams")->color == 0xff445566);
  assert(knit_app_rule("Claude")->chart[0] == '\0'); // explicit plain profile
  assert(knit_app_rule("Notion")->chart[0] == '\0');
  assert(strcmp(knit_app_rule("Spotify")->chart, "atelier-spotify") == 0);
  assert(knit_app_rule("Short") == NULL);
  assert(knit_app_rule("BadHex") == NULL);
  assert(unlink(path) == 0);
  assert(knit_apps_load() == 0);
  assert(knit_app_rule("Finder")->color == 0xff258de0); // missing file still works
}

static void test_patterns(void) {
  assert(knit_charts_load(NULL) == COLLECTION_COUNT);
  assert(g_knit_pattern_by_app); // first launch/migration default
  int finder = knit_chart_index("atelier-finder");
  int chrome = knit_chart_index("atelier-chrome");
  int figma = knit_chart_index("atelier-figma");
  assert(knit_pattern_for_app("Finder") == finder);
  assert(knit_pattern_for_app("Google Chrome") == chrome);
  assert(knit_pattern_for_app("Unknown") == -1);
  assert(knit_pattern_for_app(NULL) == -1);

  assert(knit_pattern_select("atelier-figma"));
  assert(!g_knit_pattern_by_app);
  assert(knit_pattern_for_app("Finder") == figma);
  assert(knit_pattern_for_app("Google Chrome") == figma);
  assert(knit_pattern_for_app("Unknown") == figma);
  assert(knit_app_rule("Google Chrome")->color == 0xfff4f0e6); // app yarn stays
  assert(!knit_pattern_select("missing-chart"));
  assert(!knit_pattern_select(NULL));
  assert(!g_knit_pattern_by_app && g_chart_active == figma);

  assert(knit_pattern_select("none"));
  assert(!g_knit_pattern_by_app && g_chart_active == -1);
  assert(knit_pattern_for_app("Finder") == -1);
  assert(knit_pattern_for_app("Google Chrome") == -1);
  assert(knit_pattern_select("by-app"));
  assert(g_knit_pattern_by_app);
  assert(knit_pattern_for_app("Finder") == finder);
  assert(knit_pattern_for_app("Google Chrome") == chrome);

  g_app_rules[0] = (struct app_rule){"Google Chrome", 0xff112233, "missing-chart"};
  g_app_rule_count = 1;
  assert(knit_pattern_for_app("Google Chrome") == -1); // invalid profile -> plain
  g_app_rules[0].chart[0] = '\0';
  assert(knit_pattern_for_app("Google Chrome") == -1); // plain profile
  snprintf(g_app_rules[0].chart, sizeof g_app_rules[0].chart, "atelier-finder");
  assert(knit_pattern_for_app("Google Chrome") == finder); // user profile wins
  assert(knit_pattern_select("atelier-figma"));
  assert(knit_pattern_for_app("Google Chrome") == figma); // explicit global wins
  g_app_rule_count = 0;
  assert(knit_pattern_select("by-app"));
  assert(knit_pattern_for_app("Google Chrome") == chrome);
}

static void test_preserved_charts(void) {
  const char* names[] = {"braid", "blockstripe", "checker", "seedling", "trim"};
  struct knit_chart saved[5];
  assert(knit_charts_load(NULL) == COLLECTION_COUNT);
  for (int i = 0; i < 5; i++) {
    saved[i] = g_charts[knit_chart_index(names[i])];
    size_t size = (size_t)saved[i].w * saved[i].h * sizeof(uint32_t);
    saved[i].px = malloc(size); assert(saved[i].px);
    memcpy(saved[i].px, g_charts[knit_chart_index(names[i])].px, size);
  }
  assert(knit_charts_load("charts") >= COLLECTION_COUNT);
  for (int i = 0; i < 5; i++) {
    struct knit_chart* actual = &g_charts[knit_chart_index(names[i])];
    assert(actual->w == saved[i].w && actual->h == saved[i].h);
    assert(memcmp(actual->px, saved[i].px,
                  (size_t)actual->w * actual->h * sizeof(uint32_t)) == 0);
    free(saved[i].px);
  }
}

static void test_charts(const char* dir) {
  assert(COLLECTION_COUNT == 58);
  assert(knit_charts_load("/does-not-exist/knit-test") == COLLECTION_COUNT);
  for (int i = 0; i < COLLECTION_COUNT; i++) {
    assert(knit_chart_index(collection_names[i]) == i); // names are unique and stable
    const struct knit_chart* chart = &g_charts[i];
    assert(chart->h > 0 && chart->h <= 12 && chart->w > 0 && chart->w <= 256 && chart->px);
    bool has_base = false, has_contrast = false;
    uint32_t yarns[16];
    int yarn_count = 0;
    for (int pixel = 0; pixel < chart->w * chart->h; pixel++) {
      uint32_t yarn = chart->px[pixel];
      assert((yarn >> 24) == 0 || (yarn >> 24) == 255);
      if ((yarn >> 24) == 0) {
        has_base = true;
        yarn = 0; // all transparent pixels use one app base yarn
      } else has_contrast = true;
      int index = 0;
      while (index < yarn_count && yarns[index] != yarn) index++;
      if (index == yarn_count) {
        assert(yarn_count < 16); // renderer supports sixteen total yarn colours
        yarns[yarn_count++] = yarn;
      }
    }
    assert(has_base && (has_contrast || chart->cuff_color));
  }
  assert(g_charts[knit_chart_index("atelier-whatsapp")].solid_corners);
  assert(!g_charts[knit_chart_index("atelier-claude")].solid_corners);
  assert(g_charts[knit_chart_index("atelier-messages")].round_dots);
  assert(g_charts[knit_chart_index("atelier-weather")].round_dots);
  assert(g_charts[knit_chart_index("atelier-chrome")].corner_color == 0);
  assert(g_charts[knit_chart_index("atelier-spotify")].sculpted_yarn);
  write_png(dir, "atelier-spotify");
  assert(knit_charts_load(dir) == COLLECTION_COUNT);
  assert(g_charts[knit_chart_index("atelier-spotify")].sculpted_yarn);
  remove_png(dir, "atelier-spotify");
  write_png(dir, "atelier-chrome");
  assert(knit_charts_load(dir) == COLLECTION_COUNT);
  assert(!g_charts[knit_chart_index("atelier-chrome")].solid_corners);
  assert(g_charts[knit_chart_index("atelier-chrome")].corner_color == 0);
  remove_png(dir, "atelier-chrome");
  write_png(dir, "atelier-whatsapp");
  assert(knit_charts_load(dir) == COLLECTION_COUNT);
  assert(g_charts[knit_chart_index("atelier-whatsapp")].solid_corners);
  remove_png(dir, "atelier-whatsapp");
  assert(knit_charts_load(NULL) == COLLECTION_COUNT);
  assert(knit_chart_index(NULL) == -1);
  write_png(dir, "z-last");
  write_png(dir, "a-first");
  write_png(dir, "atelier-finder");
  assert(knit_charts_load(dir) == COLLECTION_COUNT + 2);
  assert(knit_chart_index("atelier-finder") == 0);
  assert(g_charts[0].w == 2 && g_charts[0].h == 2); // user PNG replaces built-in
  assert(knit_chart_index("a-first") == COLLECTION_COUNT);
  assert(knit_chart_index("z-last") == COLLECTION_COUNT + 1);
  assert(knit_pattern_select("z-last"));
  write_png(dir, "middle");
  assert(knit_charts_load(dir) == COLLECTION_COUNT + 3);
  assert(g_chart_active == knit_chart_index("z-last"));
  assert(g_chart_active == COLLECTION_COUNT + 2); // follows name across index changes
  assert(!g_knit_pattern_by_app);
  assert(knit_pattern_for_app("Google Chrome") == COLLECTION_COUNT + 2);
  for (int i = 0; i < 100; i++) {
    assert(knit_charts_load(dir) == COLLECTION_COUNT + 3);
    assert(g_chart_active == COLLECTION_COUNT + 2);
  }
  remove_png(dir, "z-last");
  assert(knit_charts_load(dir) == COLLECTION_COUNT + 2);
  assert(g_chart_active == -1);
  assert(!g_knit_pattern_by_app && knit_pattern_for_app("Finder") == -1);
  assert(knit_pattern_select("atelier-claude"));
  assert(knit_charts_load(NULL) == COLLECTION_COUNT);
  assert(g_chart_active == knit_chart_index("atelier-claude"));
  assert(knit_pattern_for_app("Google Chrome") == g_chart_active);
  assert(knit_pattern_select("by-app"));
  assert(knit_charts_load(NULL) == COLLECTION_COUNT);
  assert(g_knit_pattern_by_app);
  assert(knit_pattern_for_app("Google Chrome") == knit_chart_index("atelier-chrome"));
  remove_png(dir, "a-first");
  remove_png(dir, "middle");
  remove_png(dir, "atelier-finder");
  for (int i = 0; i < g_chart_count; i++) free(g_charts[i].px);
  g_chart_count = 0;
  g_chart_active = -1;
}

int main(void) {
  char temporary[] = "/tmp/knit-collection-XXXXXX";
  char* home = mkdtemp(temporary);
  assert(home);
  char library[1024], support[1024], app[1024], charts[1024];
  snprintf(library, sizeof library, "%s/Library", home);
  snprintf(support, sizeof support, "%s/Library/Application Support", home);
  snprintf(app, sizeof app, "%s/Library/Application Support/Knit Borders", home);
  snprintf(charts, sizeof charts, "%s/charts", home);
  make_directory(library);
  make_directory(support);
  make_directory(charts);
  test_apps(home);
  test_patterns();
  test_preserved_charts();
  test_charts(charts);
  assert(rmdir(charts) == 0);
  assert(rmdir(app) == 0);
  assert(rmdir(support) == 0);
  assert(rmdir(library) == 0);
  assert(rmdir(home) == 0);
  puts("Collection tests passed: 48 app profiles, 58 valid charts, strict parsing, app precedence, By App/global/plain patterns, Chrome, overrides, stable reloads.");
  return 0;
}
