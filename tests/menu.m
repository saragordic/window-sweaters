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
@end

#define NSUserDefaults KnitTestDefaults
#include "../src/menubar.m"
#undef NSUserDefaults

void knit_reveal_configure(bool (*allowed)(void), void (*schedule)(void)) {}
bool knit_reveal_step(float progress) { return false; }

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
    if (!item.enabled) {
      assert([item.title isEqual:@"Weather mode is off"] || [item.title isEqual:@"Check Weather Now"]);
      continue;
    }
    if (item.submenu) { assert(item.submenu.numberOfItems > 0); check_actions(item.submenu); }
    else assert(item.action && item.target);
  }
}

// Exercise the real mode lifecycle without invoking location or networking.
@interface KnitMenuTestWeather : KnitWeather
@end
@implementation KnitMenuTestWeather
- (void)refresh {}
@end

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

    KnitMenu* controller = [KnitMenu new];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Test"];
    menu.autoenablesItems = NO;
    [controller rebuild:menu];
    NSArray* expected = @[@"Show Sweater Borders", @"Weather", @"Pattern", @"Border Width", @"Stitch Size",
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
    assert(![customs itemWithTitle:@"Too Many Yarns"] && ![customs itemWithTitle:@"Atelier Finder"]);
    NSMenu* widths = submenu(menu, @"Border Width");
    assert([widths itemWithTitle:@"Custom · 10 pt"].state == NSControlStateValueOn);
    assert([widths itemWithTitle:@"Regular · 14 pt"].state == NSControlStateValueOff);
    assert(checked_count(widths) == 1);
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
    [controller startWeather];
    KnitWeather* fake = [KnitMenuTestWeather new];
    fake.temperatureChanged = controller.weather.temperatureChanged;
    controller.weather = fake;
    [[KnitTestDefaults standardUserDefaults] setBool:YES forKey:@"on"];
    [controller toggleWeather:nil];
    assert(fake.enabled);
    fake.temperatureChanged(20);
    assert(!g_knit_on);
    knit_save_prefs();
    assert([[KnitTestDefaults standardUserDefaults] boolForKey:@"on"]);
    [controller toggleWeather:nil];
    assert(!fake.enabled && g_knit_on); // Restore manual preference.
    [controller toggleWeather:nil];
    fake.temperatureChanged(10);
    assert(g_knit_on);
    [controller toggle:nil];
    assert(!fake.enabled && !g_knit_on);
    assert(![[KnitTestDefaults standardUserDefaults] boolForKey:@"weatherAutomatic"]);
    assert(![[KnitTestDefaults standardUserDefaults] boolForKey:@"on"]);
    puts("PASS: native menu structure, truthful state, working selections, chart filtering, gauge limits, cached swatches");
  }
  return 0;
}
