// Native menu and action checks. Preferences are an in-memory fake; no app runs.
#import <Cocoa/Cocoa.h>
#include <assert.h>

@interface KnitTestDefaults : NSObject
@property(strong) NSMutableDictionary* values;
+ (instancetype)standardUserDefaults;
- (void)registerDefaults:(NSDictionary*)defaults;
- (void)setBool:(BOOL)value forKey:(NSString*)key;
- (void)setInteger:(NSInteger)value forKey:(NSString*)key;
- (void)setFloat:(float)value forKey:(NSString*)key;
- (void)setObject:(id)value forKey:(NSString*)key;
- (BOOL)boolForKey:(NSString*)key;
- (NSInteger)integerForKey:(NSString*)key;
- (float)floatForKey:(NSString*)key;
- (NSString*)stringForKey:(NSString*)key;
- (id)objectForKey:(NSString*)key;
@end
@implementation KnitTestDefaults
+ (instancetype)standardUserDefaults {
  static KnitTestDefaults* defaults;
  if (!defaults) { defaults = [self new]; defaults.values = [NSMutableDictionary dictionary]; }
  return defaults;
}
- (void)registerDefaults:(NSDictionary*)defaults {
  for (NSString* key in defaults) if (!self.values[key]) self.values[key] = defaults[key];
}
- (void)setBool:(BOOL)value forKey:(NSString*)key { self.values[key] = @(value); }
- (void)setInteger:(NSInteger)value forKey:(NSString*)key { self.values[key] = @(value); }
- (void)setFloat:(float)value forKey:(NSString*)key { self.values[key] = @(value); }
- (void)setObject:(id)value forKey:(NSString*)key { self.values[key] = value; }
- (BOOL)boolForKey:(NSString*)key { return [self.values[key] boolValue]; }
- (NSInteger)integerForKey:(NSString*)key { return [self.values[key] integerValue]; }
- (float)floatForKey:(NSString*)key { return [self.values[key] floatValue]; }
- (NSString*)stringForKey:(NSString*)key { return self.values[key]; }
- (id)objectForKey:(NSString*)key { return self.values[key]; }
@end

#define NSUserDefaults KnitTestDefaults
#define KNIT_MENU_TEST_APPS
#include "../src/menubar.m"
#undef NSUserDefaults

// Apps are faked: tests must not depend on what happens to be running.
static NSMutableArray<KnitMenuApp*>* running_apps;
static NSMutableSet<NSNumber*>* window_owners;
static int filter_changes;
static pid_t next_pid = 1000;
static KnitMenuApp* app_with(NSString* bundleID, NSString* name, BOOL regular) {
  KnitMenuApp* app = [KnitMenuApp new];
  app.bundleID = bundleID;
  app.name = name;
  app.regular = regular;
  app.pid = next_pid++;
  return app;
}
static KnitMenuApp* fake_app(NSString* bundleID, NSString* name) { return app_with(bundleID, name, YES); }
static NSArray<KnitMenuApp*>* knit_menu_candidates(void) { return running_apps; }
static NSSet<NSNumber*>* knit_menu_window_owners(void) { return window_owners; }
void knit_apps_filter_changed(void) { filter_changes++; }
static NSArray<NSString*>* titles(NSMenu* menu) {
  NSMutableArray* result = [NSMutableArray array];
  for (NSMenuItem* item in menu.itemArray) [result addObject:item.isSeparatorItem ? @"-" : item.title];
  return result;
}
static NSArray* saved_exceptions(void) {
  return [[KnitTestDefaults standardUserDefaults] objectForKey:@"appExceptions"];
}
static bool saved_on_by_default(void) {
  return [[KnitTestDefaults standardUserDefaults] boolForKey:@"appsOnByDefault"];
}

struct knit_gauge g_knit = {.rows = 6};
int g_knit_stitch, g_knit_basket, g_knit_anchor;
bool g_knit_on = true, g_knit_pattern_by_app = true;
const char* g_knit_stitch_names[] = {"stockinette", "rib", "garter"};
static const uint32_t basket[] = {0xff123456, 0xffabcdef};
const struct knit_basket g_knit_baskets[] = {
  {"first", basket, 2}, {"second", basket, 2}, {"third", basket, 2}, {"fourth", basket, 2}
};
const int g_knit_basket_count = 4;
struct knit_chart g_charts[KNIT_CHART_MAX];
int g_chart_count, g_chart_active = -1;
static float current_width = 10;
static bool paths_available;
static int reload_height;
static uint32_t chart_pixels[KNIT_CHART_MAX][256];

float knit_current_width(void) { return current_width; }
int knit_chart_index(const char* name) {
  for (int i = 0; i < g_chart_count; i++) if (!strcmp(name, g_charts[i].name)) return i;
  return -1;
}
const char* knit_apps_path(void) { return paths_available ? "tests/menu.m" : ""; }
const char* knit_charts_dir(void) { return paths_available ? "/private/tmp" : ""; }
int knit_apps_load(void) { return 0; }
int knit_charts_load(const char* directory) { (void)directory; return g_chart_count; }
void knit_apply(const char* argument) {
  if (!strcmp(argument, "charts=reload")) {
    if (reload_height && g_chart_active >= 0) g_charts[g_chart_active].h = reload_height;
  } else if (!strncmp(argument, "chart=", 6)) {
    const char* name = argument + 6;
    if (!strcmp(name, "by-app")) g_knit_pattern_by_app = true;
    else { g_knit_pattern_by_app = false; g_chart_active = knit_chart_index(name); }
  } else if (!strncmp(argument, "yarn=", 5)) {
    for (int i = 0; i < KNIT_STITCH_COUNT; i++)
      if (!strcmp(argument + 5, g_knit_stitch_names[i])) g_knit_stitch = i;
  } else if (sscanf(argument, "gauge=%f", &g_knit.rows) == 1) {
  } else if (sscanf(argument, "width=%f", &current_width) == 1) {
  } else if (!strncmp(argument, "knit=", 5)) g_knit_on = !strcmp(argument + 5, "on");
}

static int add_chart(const char* name, int height) {
  int i = g_chart_count++;
  snprintf(g_charts[i].name, sizeof g_charts[i].name, "%s", name);
  g_charts[i].w = 2;
  g_charts[i].h = height;
  g_charts[i].px = chart_pixels[i];
  for (int p = 0; p < 2 * height; p++) chart_pixels[i][p] = p % 2 ? 0 : 0xffe35e77;
  return i;
}
static NSMenu* submenu(NSMenu* menu, NSString* title) {
  NSMenuItem* item = [menu itemWithTitle:title];
  assert(item && item.submenu);
  return item.submenu;
}
static int checked_count(NSMenu* menu) {
  int count = 0;
  for (NSMenuItem* item in menu.itemArray) {
    count += item.state == NSControlStateValueOn;
    if (item.submenu) count += checked_count(item.submenu);
  }
  return count;
}
static void check_actions(NSMenu* menu) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.isSeparatorItem) continue;
    if (item.tag == KNIT_MENU_HEADER_TAG) { assert(!item.enabled && !item.action); continue; }
    // Only the two resets may be dimmed, and only when they would change nothing.
    assert(item.enabled || item.action == @selector(setAllApps:));
    if (item.submenu) { assert(item.submenu.numberOfItems > 0); check_actions(item.submenu); }
    else assert(item.action && item.target);
  }
}

static NSArray<NSString*>* listed(NSArray<KnitMenuApp*>* apps) {
  NSMutableArray* names = [NSMutableArray array];
  for (KnitMenuApp* app in apps) [names addObject:app.name];
  return names;
}

static void test_listing(void) {
  knit_apps_set_all(true);
  KnitMenuApp* safari = app_with(@"com.apple.Safari", @"Safari", YES);
  KnitMenuApp* raycast = app_with(@"com.raycast.macos", @"Raycast", NO);   // menu bar only
  KnitMenuApp* agent = app_with(@"com.example.agent", @"Agent", NO);       // no windows
  KnitMenuApp* nameless = app_with(@"com.example.nameless", @"", YES);
  KnitMenuApp* anonymous = app_with(nil, @"Script", YES);                  // no identifier
  KnitMenuApp* second = app_with(@"com.apple.Safari", @"Safari", YES);     // another copy
  KnitMenuApp* itself = app_with(@"local.knitborders.app", @"Window Sweaters", YES);
  NSArray* all = @[safari, raycast, agent, nameless, anonymous, second, itself];
  NSSet* owners = [NSSet setWithObjects:@(raycast.pid), nil];

  // Dock apps and window owners; never ourselves, never twice, never unnamed.
  assert([listed(knit_menu_apps(all, owners, itself.pid))
          isEqualToArray:(@[@"com.example.nameless", @"Raycast", @"Safari"])]);
  // A menu bar app is found through its window, even with no sweater on it:
  // turning everything off must not make it unreachable.
  knit_apps_set_all(false);
  assert([listed(knit_menu_apps(all, owners, itself.pid)) containsObject:@"Raycast"]);
  // Once its window closes, a choice made for it keeps it listed while it runs.
  knit_app_set_hidden("com.raycast.macos", false);
  assert([listed(knit_menu_apps(all, [NSSet set], itself.pid)) containsObject:@"Raycast"]);
  assert(![listed(knit_menu_apps(all, [NSSet set], itself.pid)) containsObject:@"Agent"]);
  knit_apps_set_all(true);
  assert(![listed(knit_menu_apps(all, [NSSet set], itself.pid)) containsObject:@"Raycast"]);
}

static void test_apps(KnitMenu* controller, NSMenu* menu) {
  KnitTestDefaults* defaults = [KnitTestDefaults standardUserDefaults];
  knit_apps_set_all(true);
  running_apps = [NSMutableArray arrayWithArray:@[
    fake_app(@"com.apple.Safari", @"Safari"), fake_app(@"com.figma.Desktop", @"Figma"),
    fake_app(@"com.apple.finder", @"finder")]];
  [controller rebuild:menu];
  assert([menu indexOfItemWithTitle:@"Apps"] == 1);   // directly under the master switch
  NSMenu* apps = submenu(menu, @"Apps");
  // Case-insensitive, localised order; every app starts on; nothing to turn on.
  assert([titles(apps) isEqualToArray:(@[@"Figma", @"finder", @"Safari", @"-",
                                          @"Turn On for All Apps", @"Turn Off for All Apps"])]);
  assert(checked_count(apps) == 3);
  assert(![apps itemWithTitle:@"Turn On for All Apps"].enabled);
  assert([apps itemWithTitle:@"Turn Off for All Apps"].enabled);
  check_actions(menu);

  filter_changes = 0;
  [controller toggleApp:[apps itemWithTitle:@"Safari"]];
  assert(knit_app_hidden("com.apple.Safari") && filter_changes == 1);
  assert(saved_on_by_default() && [saved_exceptions() isEqualToArray:@[@"com.apple.Safari"]]);
  [controller rebuild:menu];
  apps = submenu(menu, @"Apps");
  assert([apps itemWithTitle:@"Safari"].state == NSControlStateValueOff);
  assert(checked_count(apps) == 2);
  assert([apps itemWithTitle:@"Turn On for All Apps"].enabled);   // both now do something
  assert([apps itemWithTitle:@"Turn Off for All Apps"].enabled);
  check_actions(menu);

  // Turning it back on is the same click, and is saved the same way.
  [controller toggleApp:[apps itemWithTitle:@"Safari"]];
  assert(!knit_app_hidden("com.apple.Safari") && filter_changes == 2);
  assert([saved_exceptions() count] == 0);

  // All off: every app unticked, then pick the few that should keep theirs.
  [controller setAllApps:[apps itemWithTitle:@"Turn Off for All Apps"]];
  assert(filter_changes == 3 && !knit_apps_on_by_default() && !saved_on_by_default());
  [controller rebuild:menu];
  apps = submenu(menu, @"Apps");
  assert(checked_count(apps) == 0);
  assert([apps itemWithTitle:@"Turn On for All Apps"].enabled);
  assert(![apps itemWithTitle:@"Turn Off for All Apps"].enabled);
  check_actions(menu);
  [controller toggleApp:[apps itemWithTitle:@"Figma"]];
  assert(!knit_app_hidden("com.figma.Desktop") && knit_app_hidden("com.apple.Safari"));
  assert(!saved_on_by_default() && [saved_exceptions() isEqualToArray:@[@"com.figma.Desktop"]]);
  [controller rebuild:menu];
  apps = submenu(menu, @"Apps");
  assert(checked_count(apps) == 1 && [apps itemWithTitle:@"Figma"].state == NSControlStateValueOn);

  // Restored at launch, in both modes; anything malformed is ignored.
  knit_apps_set_all(true);
  knit_load_prefs();
  assert(!knit_apps_on_by_default() && !knit_app_hidden("com.figma.Desktop")
         && knit_app_hidden("com.apple.Safari"));

  filter_changes = 0;
  [controller setAllApps:[apps itemWithTitle:@"Turn On for All Apps"]];
  assert(knit_apps_on_by_default() && knit_app_exception_count() == 0 && filter_changes == 1);
  assert(saved_on_by_default() && [saved_exceptions() count] == 0);
  [controller setAllApps:[apps itemWithTitle:@"Turn On for All Apps"]];   // no change, no redraw
  assert(filter_changes == 1);

  [defaults setObject:@[@"com.apple.Safari", @42, @"", @"com.apple.Safari"] forKey:@"appExceptions"];
  knit_load_prefs();
  assert(knit_app_exception_count() == 1 && knit_app_hidden("com.apple.Safari"));
  [defaults setObject:@"not a list" forKey:@"appExceptions"];
  knit_load_prefs();
  assert(knit_app_exception_count() == 0 && !knit_app_hidden("com.apple.Safari"));

  running_apps = [NSMutableArray array];
  [controller rebuild:menu];
  assert([titles(submenu(menu, @"Apps")) isEqualToArray:(@[@"No Available Apps", @"-",
          @"Turn On for All Apps", @"Turn Off for All Apps"])]);
  knit_apps_set_all(true);
}

int main(void) {
  @autoreleasepool {
    for (size_t i = 0; i < sizeof knit_menu_patterns / sizeof knit_menu_patterns[0]; i++)
      add_chart(knit_menu_patterns[i].name, 6);
    int custom = add_chart("my-flowers", 8);
    int legacy = add_chart("awning", 6);
    add_chart("atelier-finder", 6);
    add_chart("too-tall", 15);
    int empty = add_chart("empty", 6); g_charts[empty].px = NULL;
    int transparent = add_chart("invisible", 6); memset(chart_pixels[transparent], 0, sizeof chart_pixels[transparent]);
    int excessive = add_chart("too-many-yarns", 9);
    for (int i = 0; i < 18; i++) chart_pixels[excessive][i] = 0xff000000 | i;
    int budget = add_chart("palette-limit", 8);
    for (int i = 0; i < 16; i++) chart_pixels[budget][i] = i ? 0xff000000 | i : 0;
    assert(knit_menu_chart_selectable(budget));
    chart_pixels[excessive][0] = 0; // Base yarn also counts against the limit.
    assert(!knit_menu_chart_selectable(excessive));
    // A chart built at runtime from an app icon is not a pattern the user can
    // pick, so it must never reach the menu however ordinary it otherwise looks.
    int generated = add_chart("auto-Numbers", 6);
    assert(knit_menu_chart_selectable(generated));   // ordinary in every other way
    g_charts[generated].generated = true;
    assert(!knit_menu_chart_selectable(generated));

    KnitMenu* controller = [KnitMenu new];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Test"];
    menu.autoenablesItems = NO;
    [controller rebuild:menu];
    NSArray* expected = @[@"Show Sweater Borders", @"Apps", @"Pattern", @"Border Width", @"Stitch Size",
                         @"", @"Quit Window Sweaters"];
    assert(menu.numberOfItems == expected.count);
    for (NSInteger i = 0; i < menu.numberOfItems; i++)
      assert([[menu itemAtIndex:i].title isEqualToString:expected[i]]);
    check_actions(menu);
    NSMenu* patterns = submenu(menu, @"Pattern");
    assert(checked_count(patterns) == 1);
    assert(![patterns itemWithTitle:@"Stitch Style"] && ![patterns itemWithTitle:@"Awning"]);
    NSMenu* customs = submenu(patterns, @"Custom Patterns");
    assert([customs itemWithTitle:@"My Flowers"] && ![customs itemWithTitle:@"Too Tall"]);
    assert(![customs itemWithTitle:@"Empty"] && ![customs itemWithTitle:@"Invisible"]);
    for (NSMenuItem* item in customs.itemArray)
      assert([item.title rangeOfString:@"Auto" options:NSCaseInsensitiveSearch].location == NSNotFound);
    assert(![customs itemWithTitle:@"Too Many Yarns"] && ![customs itemWithTitle:@"Atelier Finder"]);
    NSMenu* widths = submenu(menu, @"Border Width");
    assert([widths itemWithTitle:@"Custom · 10 pt"].state == NSControlStateValueOn);
    assert([widths itemWithTitle:@"Regular · 14 pt"].state == NSControlStateValueOff);
    assert(checked_count(widths) == 1);
    // Every width, thinnest first, including the 8 pt option between Slim and Medium.
    NSMutableArray* widthTitles = [NSMutableArray array];
    for (NSMenuItem* item in widths.itemArray) if (!item.isSeparatorItem) [widthTitles addObject:item.title];
    assert([widthTitles isEqualToArray:(@[@"Slim · 6 pt", @"Narrow · 8 pt", @"Medium · 12 pt",
      @"Regular · 14 pt", @"Wide · 18 pt", @"Extra Wide · 28 pt", @"Custom · 10 pt"])]);
    [controller apply:[widths itemWithTitle:@"Narrow · 8 pt"]];
    assert(current_width == 8 && [[KnitTestDefaults standardUserDefaults] floatForKey:@"width"] == 8);
    [controller rebuild:menu];
    widths = submenu(menu, @"Border Width");
    assert([widths itemWithTitle:@"Narrow · 8 pt"].state == NSControlStateValueOn);
    assert(![widths itemWithTitle:@"Custom · 10 pt"] && checked_count(widths) == 1);
    current_width = 10;
    [controller rebuild:menu];
    assert(![submenu(menu, @"Stitch Size") itemWithTitle:@"Chunky · 3 rows"]);
    assert(![menu itemWithTitle:@"Customize"]);

    [controller selectPlain:[patterns itemWithTitle:@"Rib"]];
    assert(!g_knit_pattern_by_app && g_chart_active == -1 && g_knit_stitch == KNIT_RIB);
    assert([[KnitTestDefaults standardUserDefaults] integerForKey:@"yarn"] == KNIT_RIB);
    assert(![[KnitTestDefaults standardUserDefaults] boolForKey:@"patternByApp"]);
    [controller rebuild:menu];
    assert([submenu(menu, @"Stitch Size") itemWithTitle:@"Chunky · 3 rows"]);
    g_knit.rows = 3;
    [controller apply:[submenu(menu, @"Pattern") itemWithTitle:@"Picnic Checks"]];
    assert(g_chart_active == knit_chart_index("picnic") && g_knit.rows == 6);
    g_knit.rows = 3;
    [controller apply:[submenu(menu, @"Pattern") itemWithTitle:@"By App"]];
    assert(g_knit_pattern_by_app && g_knit.rows == 6);
    [controller rebuild:menu];
    [controller apply:[submenu(submenu(menu, @"Pattern"), @"Custom Patterns") itemWithTitle:@"My Flowers"]];
    assert(g_chart_active == custom && g_knit.rows == 8);
    [controller rebuild:menu];
    NSMenu* sizes = submenu(menu, @"Stitch Size");
    assert(![sizes itemWithTitle:@"Medium · 6 rows"]);
    assert([sizes itemWithTitle:@"Custom · 8 rows"].state == NSControlStateValueOn);
    assert(checked_count(sizes) == 1);
    assert(checked_count(submenu(menu, @"Pattern")) == 1);

    g_chart_active = legacy;
    paths_available = true;
    [controller rebuild:menu];
    patterns = submenu(menu, @"Pattern");
    assert([patterns itemWithTitle:@"Current: Awning"].state == NSControlStateValueOn);
    assert(checked_count(patterns) == 1);
    assert(![menu itemWithTitle:@"Customize"]);
    check_actions(menu);

    g_chart_active = custom;
    g_charts[custom].h = 6;
    g_knit.rows = 6;
    reload_height = 8;
    // Exercise retained reload handling independently of the simplified menu.
    NSMenuItem* reload = [NSMenuItem new];
    reload.representedObject = @"charts=reload";
    [controller apply:reload];
    assert(g_chart_active == custom && g_charts[custom].h == 8 && g_knit.rows == 8);
    assert([[KnitTestDefaults standardUserDefaults] floatForKey:@"gauge"] == 8);
    g_knit_pattern_by_app = true;
    g_knit.rows = 3;
    reload.representedObject = @"apps=reload";
    [controller apply:reload];
    assert(g_knit_pattern_by_app && g_knit.rows == 6);
    assert([[KnitTestDefaults standardUserDefaults] floatForKey:@"gauge"] == 6);

    // Cached images must outlive the chart buffer that produced them.
    uint32_t* temporary = malloc(12 * sizeof *temporary);
    assert(temporary);
    for (int i = 0; i < 12; i++) temporary[i] = i % 2 ? 0 : 0xff987654;
    g_charts[0].px = temporary;
    NSImage* image = [controller swatchForChart:0];
    free(temporary);
    g_charts[0].px = NULL;
    assert(image.TIFFRepresentation.length > 0);
    [[KnitTestDefaults standardUserDefaults] setObject:@"my-flowers" forKey:@"chart"];
    [[KnitTestDefaults standardUserDefaults] setBool:NO forKey:@"patternByApp"];
    [[KnitTestDefaults standardUserDefaults] setFloat:3 forKey:@"gauge"];
    knit_load_prefs();
    assert(g_chart_active == custom && g_knit.rows == 8);
    assert([[KnitTestDefaults standardUserDefaults] floatForKey:@"gauge"] == 8);
    [[KnitTestDefaults standardUserDefaults] setBool:YES forKey:@"patternByApp"];
    [[KnitTestDefaults standardUserDefaults] setFloat:3 forKey:@"gauge"];
    knit_load_prefs();
    assert(g_knit_pattern_by_app && g_knit.rows == 6);
    assert([[KnitTestDefaults standardUserDefaults] floatForKey:@"gauge"] == 6);
    test_listing();
    test_apps(controller, menu);
    puts("PASS: per-app switches: listing rules, sorted, truthful, persisted, reversible, all on and all off");
    puts("PASS: native menu structure, truthful state, working selections, chart filtering, gauge limits, cached swatches");
  }
  return 0;
}
