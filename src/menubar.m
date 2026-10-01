// The menu bar extra: a status item that drives the running border daemon.
//
// It lives in the same process as the renderer, so it changes the renderer's
// globals directly and then asks every border to redraw — no IPC, no second
// app to keep in sync.

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "misc/knit.h"
#include "misc/chart.h"
#include "misc/apps.h"
#include "misc/status_icon.h"
#include <stdio.h>

extern void knit_apply(const char* arg);   // main.c: feeds one "key=value"
extern void knit_apps_filter_changed(void); // main.c: re-run the app gate
extern bool g_knit_on;

enum { KNIT_MENU_HEADER_TAG = 0x6864 };   // a label, not a command

// One running app, as the Apps menu sees it. Read from AppKit, faked in tests.
@interface KnitMenuApp : NSObject
@property(copy) NSString* bundleID;
@property(copy) NSString* name;
@property(strong) NSImage* icon;
@property pid_t pid;
@property BOOL regular;   // has a Dock icon
@end
@implementation KnitMenuApp
@end

static NSArray<KnitMenuApp*>* knit_menu_candidates(void);   // every running app
static NSSet<NSNumber*>* knit_menu_window_owners(void);     // pids with eligible windows

// Which running apps the Apps menu lists, sorted by name: those in the Dock,
// any app owning a window that could wear a sweater (worn now or not), and any
// app ticked the other way, so a choice can always be undone while it runs.
static NSArray<KnitMenuApp*>* knit_menu_apps(NSArray<KnitMenuApp*>* candidates,
                                             NSSet<NSNumber*>* owners, pid_t own) {
  NSMutableArray<KnitMenuApp*>* apps = [NSMutableArray array];
  NSMutableSet<NSString*>* seen = [NSMutableSet set];
  for (KnitMenuApp* app in candidates) {
    if (app.pid == own || !app.bundleID.length || [seen containsObject:app.bundleID]) continue;
    bool chosen = knit_app_hidden(app.bundleID.UTF8String) == knit_apps_on_by_default();
    if (!app.regular && ![owners containsObject:@(app.pid)] && !chosen) continue;
    [seen addObject:app.bundleID];
    if (!app.name.length) app.name = app.bundleID;
    [apps addObject:app];
  }
  [apps sortUsingComparator:^NSComparisonResult(KnitMenuApp* a, KnitMenuApp* b) {
    return [a.name localizedStandardCompare:b.name];
  }];
  return apps;
}

#ifndef KNIT_MENU_TEST_APPS
extern int knit_window_owners(int* pids, int capacity);  // main.c

static NSImage* knit_menu_app_icon(NSImage* icon) {
  if (!icon) icon = [NSWorkspace.sharedWorkspace iconForContentType:UTTypeApplicationBundle];
  NSImage* small = [icon copy];     // never resize the shared system icon
  small.size = NSMakeSize(16, 16);
  return small;
}

static NSArray<KnitMenuApp*>* knit_menu_candidates(void) {
  NSMutableArray<KnitMenuApp*>* apps = [NSMutableArray array];
  for (NSRunningApplication* running in NSWorkspace.sharedWorkspace.runningApplications) {
    KnitMenuApp* app = [KnitMenuApp new];
    app.bundleID = running.bundleIdentifier;
    app.name = running.localizedName;
    app.icon = knit_menu_app_icon(running.icon);
    app.pid = running.processIdentifier;
    app.regular = running.activationPolicy == NSApplicationActivationPolicyRegular;
    [apps addObject:app];
  }
  return apps;
}

static NSSet<NSNumber*>* knit_menu_window_owners(void) {
  int pids[1024];
  int count = knit_window_owners(pids, 1024);
  NSMutableSet<NSNumber*>* owners = [NSMutableSet set];
  for (int i = 0; i < count; i++) [owners addObject:@(pids[i])];
  return owners;
}
#endif

static bool knit_menu_chart_valid(int index) {
  return index >= 0 && index < g_chart_count && g_charts[index].px
         && g_charts[index].w > 0 && g_charts[index].h > 0;
}

static bool knit_menu_chart_selectable(int index) {
  if (!knit_menu_chart_valid(index) || g_charts[index].h > 14) return false;
  if (g_charts[index].generated) return false;  // app-icon charts are not choices
  const struct knit_chart* chart = &g_charts[index];
  uint32_t yarns[16];
  int count = 0;
  bool opaque = false;
  for (size_t i = 0; i < (size_t)chart->w * chart->h; i++) {
    uint32_t pixel = chart->px[i];
    bool solid = (pixel >> 24) >= 128;
    opaque |= solid;
    // Transparent pixels are one app-colour yarn, independent of their RGB.
    uint32_t yarn = solid ? pixel | 0xff000000 : 0;
    int match = 0;
    while (match < count && yarns[match] != yarn) match++;
    if (match == count) {
      if (count == 16) return false;
      yarns[count++] = yarn;
    }
  }
  return opaque;
}

static bool knit_menu_colourwork(void) {
  return g_knit_pattern_by_app || knit_menu_chart_valid(g_chart_active);
}

static float knit_menu_minimum_rows(void) {
  if (!knit_menu_colourwork()) return 3.f;
  if (!g_knit_pattern_by_app && knit_menu_chart_selectable(g_chart_active))
    return MAX(6, g_charts[g_chart_active].h);
  return 6.f;
}

static NSString* knit_menu_chart_title(const char* name) {
  NSString* title = [NSString stringWithUTF8String:name];
  return [[[title stringByReplacingOccurrencesOfString:@"_" withString:@" "]
                   stringByReplacingOccurrencesOfString:@"-" withString:@" "] capitalizedString];
}

static bool knit_menu_legacy_chart(const char* name) {
  static const char* legacy[] = { "awning", "sprig", "diagonal", "braid",
    "chrome", "heart", "trim", "carnival", "checker", "pinstripe",
    "snowflake", "blockstripe", "seedling" };
  if (strncmp(name, "atelier-", 8) == 0) return true;
  for (size_t i = 0; i < sizeof legacy / sizeof legacy[0]; i++) {
    if (strcmp(name, legacy[i]) == 0) return true;
  }
  return false;
}

static const struct { const char* name; const char* title; } knit_menu_patterns[] = {
  { "zigzag", "Zigzag" },
  { "picnic", "Picnic Checks" }, { "ribbon", "Ribbon Stripes" },
  { "posy", "Little Bows" }, { "twinkle", "Tiny Stars" },
  { "candy-stripe", "Candy Stripes" }
};

static bool knit_menu_featured_chart(const char* name) {
  for (size_t i = 0; i < sizeof knit_menu_patterns / sizeof knit_menu_patterns[0]; i++) {
    if (strcmp(name, knit_menu_patterns[i].name) == 0) return true;
  }
  return false;
}

static bool knit_menu_path_available(const char* path, bool directory) {
  if (!path || !path[0]) return false;
  NSString* value = [NSString stringWithUTF8String:path];
  BOOL isDirectory = NO;
  return value && [[NSFileManager defaultManager] fileExistsAtPath:value isDirectory:&isDirectory]
         && isDirectory == directory;
}

// Menu choices survive a restart, the way a menu bar app should.
static void knit_save_prefs(void) {
  NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
  [d setBool:g_knit_on forKey:@"on"];
  [d setInteger:g_knit_stitch forKey:@"yarn"];
  [d setInteger:g_knit_basket forKey:@"basket"];
  [d setFloat:knit_current_width() forKey:@"width"];
  [d setFloat:g_knit.rows forKey:@"gauge"];
  [d setObject:(g_chart_active >= 0 && g_chart_active < g_chart_count
               ? @(g_charts[g_chart_active].name) : @"none")
        forKey:@"chart"];
  [d setBool:g_knit_pattern_by_app forKey:@"patternByApp"];
  [d setInteger:g_knit_anchor forKey:@"anchor"];
  NSMutableArray* exceptions = [NSMutableArray array];
  for (int i = 0; i < knit_app_exception_count(); i++)
    [exceptions addObject:@(knit_app_exception(i))];
  [d setBool:knit_apps_on_by_default() forKey:@"appsOnByDefault"];
  [d setObject:exceptions forKey:@"appExceptions"];
}

static void knit_load_prefs(void) {
  NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
  // Older versions saved a global chart even while app profiles overrode it.
  // A missing mode therefore migrates to By App, preserving those sweaters.
  [d registerDefaults:@{ @"on": @YES, @"yarn": @0, @"basket": @3, @"width": @12.0f, @"gauge": @6.0f, @"chart": @"none", @"patternByApp": @YES, @"anchor": @0, @"appsOnByDefault": @YES, @"appExceptions": @[] }];
  char buf[128];

  NSInteger y = [d integerForKey:@"yarn"];
  if (y < 0 || y >= KNIT_STITCH_COUNT) y = 0;
  snprintf(buf, sizeof buf, "yarn=%s", g_knit_stitch_names[y]);
  knit_apply(buf);

  NSInteger b = [d integerForKey:@"basket"];
  if (b < 0 || b >= g_knit_basket_count) b = 3;
  snprintf(buf, sizeof buf, "basket=%s", g_knit_baskets[b].name);
  knit_apply(buf);

  float w = [d floatForKey:@"width"];
  if (w < 2.f || w > 60.f) w = 12.f;
  snprintf(buf, sizeof buf, "width=%g", w);
  knit_apply(buf);

  float g = [d floatForKey:@"gauge"];
  if (g >= 1.5f && g <= 40.f) { snprintf(buf, sizeof buf, "gauge=%g", g); knit_apply(buf); }

  NSString* chart = [d stringForKey:@"chart"];
  if (!chart.length || knit_chart_index(chart.UTF8String) < 0) chart = @"none";
  snprintf(buf, sizeof buf, "chart=%s", chart.UTF8String);
  knit_apply(buf);
  if ([d boolForKey:@"patternByApp"]) knit_apply("chart=by-app");
  if (knit_menu_colourwork() && g_knit.rows < knit_menu_minimum_rows()) {
    snprintf(buf, sizeof buf, "gauge=%g", knit_menu_minimum_rows());
    knit_apply(buf);
    [d setFloat:g_knit.rows forKey:@"gauge"];
  }

  knit_apply([d integerForKey:@"anchor"] == KNIT_ANCHOR_CENTRE
             ? "anchor=centre" : "anchor=corner");

  knit_apply([d boolForKey:@"on"] ? "knit=on" : "knit=off");

  // Restored before any window is discovered, so a switched-off app never
  // flashes a sweater at launch. Anything malformed is skipped, not trusted.
  bool on = [d boolForKey:@"appsOnByDefault"];
  knit_apps_set_all(on);
  id exceptions = [d objectForKey:@"appExceptions"];
  if ([exceptions isKindOfClass:NSArray.class]) {
    for (id bundleID in exceptions)
      if ([bundleID isKindOfClass:NSString.class])
        knit_app_set_hidden([bundleID UTF8String], on);   // the other way from the default
  }
}


@interface KnitMenu : NSObject <NSMenuDelegate>
@property(strong) NSStatusItem* item;
@property(strong) NSMutableDictionary<NSString*, NSImage*>* swatches;
@property(strong) id activity;
@end

@implementation KnitMenu

- (void)apply:(NSMenuItem*)sender {
  NSString* argument = sender.representedObject;
  if ([argument hasPrefix:@"chart="] && ![argument isEqualToString:@"chart=none"]) {
    int chart = knit_chart_index([argument substringFromIndex:6].UTF8String);
    float minimum = knit_menu_chart_selectable(chart) ? MAX(6, g_charts[chart].h) : 6;
    if (g_knit.rows < minimum) {
      NSString* gauge = [NSString stringWithFormat:@"gauge=%g", minimum];
      knit_apply(gauge.UTF8String);
    }
  }
  knit_apply(argument.UTF8String);
  if ([argument isEqualToString:@"apps=reload"] ||
      [argument isEqualToString:@"charts=reload"]) {
    [self.swatches removeAllObjects];
    if (knit_menu_colourwork() && g_knit.rows < knit_menu_minimum_rows()) {
      NSString* gauge = [NSString stringWithFormat:@"gauge=%g", knit_menu_minimum_rows()];
      knit_apply(gauge.UTF8String);
    }
  }
  knit_save_prefs();
}

- (void)selectPlain:(NSMenuItem*)sender {
  knit_apply("chart=none");
  NSString* argument = sender.representedObject;
  knit_apply(argument.UTF8String);
  knit_save_prefs();
}

- (void)updateStatus {
  // These transparent SkyLight windows are not ordinary AppKit content.
  // Tell macOS that enabled sweaters are ongoing user-visible work, while
  // still allowing the display and computer to sleep normally.
  if (g_knit_on && !self.activity) {
    self.activity = [NSProcessInfo.processInfo
      beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
      reason:@"Keep window sweaters synchronized"];
  } else if (!g_knit_on && self.activity) {
    [NSProcessInfo.processInfo endActivity:self.activity];
    self.activity = nil;
  }
  // "Every app off" looks the same on screen as paused, so say which it is,
  // in the tooltip and to VoiceOver, never by dimming alone.
  bool noApps = !knit_apps_on_by_default() && knit_app_exception_count() == 0;
  NSString* status = !g_knit_on ? @"Sweaters are paused"
                   : noApps     ? @"Sweaters are off for all apps"
                                : @"Sweaters are on";
  self.item.button.alphaValue = g_knit_on && !noApps ? 1.0 : 0.45;
  self.item.button.toolTip = [@"Window Sweaters — " stringByAppendingString:status.lowercaseString];
  [self.item.button setAccessibilityValue:status];
}

- (void)toggle:(NSMenuItem*)sender {
  knit_apply(g_knit_on ? "knit=off" : "knit=on");
  knit_save_prefs();
  [self updateStatus];
}

- (void)toggleApp:(NSMenuItem*)sender {
  const char* bundleID = [sender.representedObject UTF8String];
  if (!knit_app_set_hidden(bundleID, !knit_app_hidden(bundleID))) return;
  knit_save_prefs();
  knit_apps_filter_changed();
  [self updateStatus];
}

- (void)setAllApps:(NSMenuItem*)sender {
  if (!knit_apps_set_all([sender.representedObject boolValue])) return;
  knit_save_prefs();
  knit_apps_filter_changed();
  [self updateStatus];
}

- (NSMenuItem*)addApp:(KnitMenuApp*)app to:(NSMenu*)menu {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:app.name
                                                action:@selector(toggleApp:)
                                         keyEquivalent:@""];
  item.target = self;
  item.representedObject = app.bundleID;
  item.image = app.icon;
  item.state = knit_app_hidden(app.bundleID.UTF8String) ? NSControlStateValueOff
                                                        : NSControlStateValueOn;
  item.enabled = YES;
  [menu addItem:item];
  return item;
}

- (void)addHeader:(NSString*)title to:(NSMenu*)menu {
  NSMenuItem* header;
  if (@available(macOS 14.0, *)) header = [NSMenuItem sectionHeaderWithTitle:title];
  else header = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  header.enabled = NO;
  header.tag = KNIT_MENU_HEADER_TAG;
  [menu addItem:header];
}

// Every open app, ticked while it wears a sweater, then the two ways to reset
// them all. Each reset is dimmed when it would change nothing, so the pair
// also shows at a glance whether everything is on or off.
- (void)buildApps:(NSMenu*)menu {
  NSArray<KnitMenuApp*>* apps = knit_menu_apps(knit_menu_candidates(), knit_menu_window_owners(),
                                               NSProcessInfo.processInfo.processIdentifier);
  for (KnitMenuApp* app in apps) [self addApp:app to:menu];
  if (!apps.count) [self addHeader:@"No Available Apps" to:menu];

  [menu addItem:[NSMenuItem separatorItem]];
  bool uniform = knit_app_exception_count() == 0;
  NSMenuItem* allOn = [self add:menu title:@"Turn On for All Apps" arg:nil on:NO];
  allOn.action = @selector(setAllApps:);
  allOn.representedObject = @YES;
  allOn.enabled = !(uniform && knit_apps_on_by_default());
  NSMenuItem* allOff = [self add:menu title:@"Turn Off for All Apps" arg:nil on:NO];
  allOff.action = @selector(setAllApps:);
  allOff.representedObject = @NO;
  allOff.enabled = !(uniform && !knit_apps_on_by_default());
}

- (void)quit:(id)sender { [NSApp terminate:nil]; }

- (void)openApps:(id)sender {
  const char* path = knit_apps_path();
  if (!knit_menu_path_available(path, false)) return;
  NSString* p = [NSString stringWithUTF8String:path];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p]];
}

- (void)openCharts:(id)sender {
  const char* path = knit_charts_dir();
  if (!knit_menu_path_available(path, true)) return;
  NSString* p = [NSString stringWithUTF8String:path];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p]];
}

- (NSMenuItem*)add:(NSMenu*)menu title:(NSString*)t arg:(NSString*)a on:(BOOL)on {
  NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:t
                                              action:@selector(apply:)
                                       keyEquivalent:@""];
  mi.target = self;
  mi.representedObject = a;
  mi.state = on ? NSControlStateValueOn : NSControlStateValueOff;
  mi.enabled = YES;
  [menu addItem:mi];
  return mi;
}

- (NSMenu*)submenu:(NSMenu*)menu title:(NSString*)title {
  NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
  submenu.autoenablesItems = NO;
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  item.submenu = submenu;
  item.enabled = YES;
  [menu addItem:item];
  return submenu;
}

- (void)addAction:(NSMenu*)menu title:(NSString*)title selector:(SEL)selector {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:selector keyEquivalent:@""];
  item.target = self;
  item.enabled = YES;
  [menu addItem:item];
}

// Small, cached colour samples keep the native menu easy to scan. Their
// stitches are illustrative; the renderer remains the source of the border.
- (NSImage*)swatchForBasket:(int)index {
  const struct knit_basket* basket = &g_knit_baskets[index];
  NSString* key = [NSString stringWithUTF8String:basket->name];
  if (!self.swatches) self.swatches = [NSMutableDictionary dictionary];
  if (self.swatches[key]) return self.swatches[key];
  NSImage* image = [NSImage imageWithSize:NSMakeSize(30, 18) flipped:NO
                         drawingHandler:^BOOL(NSRect bounds) {
    [[NSColor colorWithSRGBRed:0.94 green:0.92 blue:0.88 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                                     xRadius:3 yRadius:3] fill];
    for (int x = 0; x < 5; x++) {
      uint32_t color = basket->colors[x % basket->len];
      [[NSColor colorWithSRGBRed:((color >> 16) & 255) / 255.0
                          green:((color >> 8) & 255) / 255.0
                           blue:(color & 255) / 255.0 alpha:1] setStroke];
      for (int y = 0; y < 3; y++) {
        CGFloat cx = 5 + x * 5, cy = 3.5 + y * 4.5;
        NSBezierPath* stitch = [NSBezierPath bezierPath];
        stitch.lineWidth = 1.6;
        stitch.lineCapStyle = NSLineCapStyleRound;
        [stitch moveToPoint:NSMakePoint(cx - 1.5, cy + 2.5)];
        [stitch lineToPoint:NSMakePoint(cx, cy)];
        [stitch lineToPoint:NSMakePoint(cx + 1.5, cy + 2.5)];
        [stitch stroke];
      }
    }
    return YES;
  }];
  self.swatches[key] = image;
  return image;
}

- (NSImage*)swatchForChart:(int)index {
  if (!knit_menu_chart_valid(index)) return nil;
  const struct knit_chart* chart = &g_charts[index];
  const int columns = MIN(24, MAX(12, chart->w));
  const int rows = MIN(8, MAX(6, chart->h));
  const uint32_t base = 0xff638575;
  uint32_t colors[24 * 8];
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < columns; x++) {
      uint32_t pixel = chart->px[(y % chart->h) * chart->w + x % chart->w];
      // Match the renderer's transparent-stitch rule exactly.
      colors[y * columns + x] = (pixel >> 24) < 128 ? base : pixel;
    }
  }
  // Copy the small sample so cached NSImage drawing blocks cannot retain a
  // chart pointer after Reload Patterns releases the chart's pixel buffer.
  NSData* sample = [NSData dataWithBytes:colors length:rows * columns * sizeof(uint32_t)];
  NSString* key = [NSString stringWithFormat:@"chart:%s:%d:%d:%@", chart->name,
                   columns, rows, [sample base64EncodedStringWithOptions:0]];
  if (!self.swatches) self.swatches = [NSMutableDictionary dictionary];
  if (self.swatches[key]) return self.swatches[key];
  NSImage* image = [NSImage imageWithSize:NSMakeSize(40, 18) flipped:NO
                         drawingHandler:^BOOL(NSRect bounds) {
    NSBezierPath* outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                                                          xRadius:3 yRadius:3];
    [NSGraphicsContext saveGraphicsState];
    [outline addClip];
    [[NSColor colorWithSRGBRed:((base >> 16) & 255) / 255.0 * 0.72
                        green:((base >> 8) & 255) / 255.0 * 0.72
                         blue:(base & 255) / 255.0 * 0.72 alpha:1] setFill];
    [outline fill];
    const uint32_t* pixels = sample.bytes;
    CGFloat stepX = 38.0 / columns, stepY = 16.0 / rows;
    for (int y = 0; y < rows; y++) {
      for (int x = 0; x < columns; x++) {
        uint32_t color = pixels[y * columns + x];
        [[NSColor colorWithSRGBRed:((color >> 16) & 255) / 255.0
                            green:((color >> 8) & 255) / 255.0
                             blue:(color & 255) / 255.0 alpha:1] setStroke];
        CGFloat cx = 1 + (x + 0.5) * stepX, cy = 17 - (y + 0.85) * stepY;
        NSBezierPath* stitch = [NSBezierPath bezierPath];
        stitch.lineWidth = MIN(1.15, stepX * 0.5);
        stitch.lineCapStyle = NSLineCapStyleRound;
        [stitch moveToPoint:NSMakePoint(cx - stepX * 0.26, cy + stepY * 0.65)];
        [stitch lineToPoint:NSMakePoint(cx, cy)];
        [stitch lineToPoint:NSMakePoint(cx + stepX * 0.26, cy + stepY * 0.65)];
        [stitch stroke];
      }
    }
    [NSGraphicsContext restoreGraphicsState];
    return YES;
  }];
  self.swatches[key] = image;
  return image;
}

// Rebuilt each time it opens, so the ticks always show the live state.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  [self updateStatus];
  [self rebuild:menu];
}

- (void)rebuild:(NSMenu*)menu {
  [menu removeAllItems];
  [self addAction:menu title:@"Show Sweater Borders" selector:@selector(toggle:)];
  [menu itemAtIndex:0].state = g_knit_on ? NSControlStateValueOn : NSControlStateValueOff;

  [self buildApps:[self submenu:menu title:@"Apps"]];

  NSMenu* pattern = [self submenu:menu title:@"Pattern"];
  NSMenuItem* byApp = [self add:pattern title:@"By App" arg:@"chart=by-app"
                           on:g_knit_pattern_by_app];
  byApp.toolTip = @"Each app uses its own pattern and yarn colours.";
  [pattern addItem:[NSMenuItem separatorItem]];
  NSArray* plainNames = @[@"Stockinette", @"Rib"];
  const enum knit_stitch plainStitches[] = { KNIT_STOCKINETTE, KNIT_RIB };
  for (NSUInteger i = 0; i < plainNames.count; i++) {
    enum knit_stitch stitch = plainStitches[i];
    NSMenuItem* plain = [self add:pattern title:plainNames[i]
        arg:[NSString stringWithFormat:@"yarn=%s", g_knit_stitch_names[stitch]]
        on:(!g_knit_pattern_by_app && g_chart_active < 0 && g_knit_stitch == stitch)];
    plain.action = @selector(selectPlain:);
    plain.toolTip = @"Plain knitting in each app's yarn colour.";
  }

  BOOL hasFeatured = NO;
  for (size_t i = 0; i < sizeof knit_menu_patterns / sizeof knit_menu_patterns[0]; i++) {
    int index = knit_chart_index(knit_menu_patterns[i].name);
    if (!knit_menu_chart_selectable(index)) continue;
    if (!hasFeatured) [pattern addItem:[NSMenuItem separatorItem]];
    hasFeatured = YES;
    NSMenuItem* choice = [self add:pattern
        title:[NSString stringWithUTF8String:knit_menu_patterns[i].title]
        arg:[NSString stringWithFormat:@"chart=%s", knit_menu_patterns[i].name]
        on:(!g_knit_pattern_by_app && g_chart_active == index)];
    choice.image = [self swatchForChart:index];
    choice.toolTip = strcmp(knit_menu_patterns[i].name, "zigzag") == 0
        ? @"Uses colours from app icons, with cream or deeper zigzags for contrast."
        : @"Pattern preview. Each app keeps its own main yarn colour.";
  }

  NSMenu* custom = nil;
  for (int i = 0; i < g_chart_count; i++) {
    if (!knit_menu_chart_selectable(i) || knit_menu_legacy_chart(g_charts[i].name)
        || knit_menu_featured_chart(g_charts[i].name)) continue;
    if (!custom) {
      [pattern addItem:[NSMenuItem separatorItem]];
      custom = [self submenu:pattern title:@"Custom Patterns"];
    }
    NSMenuItem* choice = [self add:custom title:knit_menu_chart_title(g_charts[i].name)
        arg:[NSString stringWithFormat:@"chart=%s", g_charts[i].name]
        on:(!g_knit_pattern_by_app && g_chart_active == i)];
    choice.image = [self swatchForChart:i];
    choice.toolTip = [NSString stringWithFormat:@"Each app keeps its main yarn colour. Uses at least %d stitch rows to show the full pattern.",
                      MAX(6, g_charts[i].h)];
  }
  if (!g_knit_pattern_by_app && g_chart_active >= 0 && g_chart_active < g_chart_count
      && (knit_menu_legacy_chart(g_charts[g_chart_active].name)
          || !knit_menu_chart_selectable(g_chart_active))) {
    [pattern addItem:[NSMenuItem separatorItem]];
    NSMenuItem* current = [self add:pattern
        title:[@"Current: " stringByAppendingString:knit_menu_chart_title(g_charts[g_chart_active].name)]
        arg:[NSString stringWithFormat:@"chart=%s", g_charts[g_chart_active].name] on:YES];
    current.toolTip = @"Your existing pattern is preserved. Choose another pattern to change it.";
  }

  NSMenu* widths = [self submenu:menu title:@"Border Width"];
  float width = knit_current_width();
  const struct { const char* name; float value; } widthChoices[] = {
    { "Slim", 6 }, { "Narrow", 8 }, { "Medium", 12 }, { "Regular", 14 }, { "Wide", 18 }, { "Extra Wide", 28 }
  };
  BOOL standardWidth = NO;
  for (size_t i = 0; i < sizeof widthChoices / sizeof widthChoices[0]; i++) {
    BOOL selected = fabsf(width - widthChoices[i].value) < 0.001f;
    standardWidth |= selected;
    [self add:widths title:[NSString stringWithFormat:@"%s · %g pt", widthChoices[i].name, widthChoices[i].value]
        arg:[NSString stringWithFormat:@"width=%g", widthChoices[i].value] on:selected];
  }
  if (!standardWidth) {
    [widths addItem:[NSMenuItem separatorItem]];
    [self add:widths title:[NSString stringWithFormat:@"Custom · %g pt", width]
        arg:[NSString stringWithFormat:@"width=%g", width] on:YES];
  }

  NSMenu* sizes = [self submenu:menu title:@"Stitch Size"];
  float minimum = knit_menu_minimum_rows();
  const struct { const char* name; float rows; } sizeChoices[] = {
    { "Chunky", 3 }, { "Medium", 6 }, { "Fine", 10 }, { "Very Fine", 14 }
  };
  BOOL standardSize = NO;
  for (size_t i = 0; i < sizeof sizeChoices / sizeof sizeChoices[0]; i++) {
    if (sizeChoices[i].rows < minimum) continue;
    BOOL selected = fabsf(g_knit.rows - sizeChoices[i].rows) < 0.001f;
    standardSize |= selected;
    [self add:sizes title:[NSString stringWithFormat:@"%s · %g rows", sizeChoices[i].name, sizeChoices[i].rows]
        arg:[NSString stringWithFormat:@"gauge=%g", sizeChoices[i].rows] on:selected];
  }
  if (!standardSize) {
    [sizes addItem:[NSMenuItem separatorItem]];
    [self add:sizes title:[NSString stringWithFormat:@"Custom · %g rows", g_knit.rows]
        arg:[NSString stringWithFormat:@"gauge=%g", g_knit.rows] on:YES];
  }

  [menu addItem:[NSMenuItem separatorItem]];
  NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit Window Sweaters"
                                             action:@selector(quit:) keyEquivalent:@"q"];
  quit.target = self;
  quit.enabled = YES;
  [menu addItem:quit];
}
@end

static KnitMenu* g_menu = nil;

// The status item MUST be created after the application has finished
// launching. Built before [NSApp run], it draws its icon but its menu never
// opens — which looks exactly like a dead click.
@interface KnitAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation KnitAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)note {
  g_menu.item = [[NSStatusBar systemStatusBar]
                  statusItemWithLength:NSSquareStatusItemLength];
  g_menu.item.button.image = knit_status_icon();
  [g_menu.item.button setAccessibilityLabel:@"Window Sweaters"];

  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Window Sweaters"];
  menu.autoenablesItems = NO;
  [g_menu rebuild:menu];
  menu.delegate = g_menu;
  g_menu.item.menu = menu;

  // Show at a glance whether sweaters are on. Without this the icon looks
  // identical either way, so "switched off" is indistinguishable from "broken".
  [g_menu updateStatus];
}
@end

static KnitAppDelegate* g_delegate = nil;

void knit_application_prepare(void) {
  [NSApplication.sharedApplication setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void knit_menubar_prepare(void) {
  @autoreleasepool {
    knit_charts_load(knit_charts_dir());
    knit_apps_load();
    knit_load_prefs();
  }
}

void knit_menubar_start(void) {
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    g_menu = [[KnitMenu alloc] init];
    g_delegate = [[KnitAppDelegate alloc] init];
    app.delegate = g_delegate;
    // accessory: menu bar only, no Dock icon
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      if (getenv("KNIT_DISPLAY_TRACE")) {
        fprintf(stderr, "MENU running=%d item=%d visible=%d button=%d frame=%s\n", NSApp.running, g_menu.item != nil, g_menu.item.visible, g_menu.item.button != nil, NSStringFromRect(g_menu.item.button.window.frame).UTF8String);
      }
    });
    [app run];   // installs the status item via the delegate, then pumps
  }
}
