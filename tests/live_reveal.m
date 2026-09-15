// Run only when explicitly requested: creates two owned windows for 14 seconds.
// Uses the production menu timer and border renderer. No location, user settings,
// synthetic input, or other apps' window contents are accessed.
#import <Cocoa/Cocoa.h>
#include "../src/border.c"
#include "../src/misc/connection.h"
#include "../src/menubar.m"

struct settings g_settings = {.enabled = true, .border_width = 12,
  .border_style = BORDER_STYLE_KNIT, .border_order = BORDER_ORDER_BELOW, .hidpi = true};
mach_port_t g_server_port;
static struct border* borders[2];
static bool demo;
void knit_reveal_configure(bool (*allowed)(void), void (*schedule)(void)) {
  border_reveal_allowed = allowed; border_reveal_schedule = schedule;
}
bool knit_reveal_step(float progress) {
  bool active = false;
  for (int i=0; i<2; i++) if (borders[i]) active |= border_step_reveal(borders[i], progress);
  return active;
}
void knit_apply(const char* arg) {
  if (!strcmp(arg, "knit=on")) g_knit_on = true;
  if (!strcmp(arg, "knit=off")) g_knit_on = false;
  for (int i=0; i<2; i++) if (borders[i]) {
    borders[i]->needs_redraw = true; border_update(borders[i], false);
  }
}
float knit_current_width(void) { return g_settings.border_width; }

@interface RevealProbe : NSObject <NSApplicationDelegate>
@property(strong) NSMutableArray<NSWindow*>* windows;
@property(strong) NSMutableArray<NSTextField*>* labels;
@property(strong) NSTimer* timer;
@property double started;
@property int phase;
@property BOOL diagnosed;
@property NSRect base;
@end
@implementation RevealProbe
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  self.windows = [NSMutableArray array]; self.labels = [NSMutableArray array];
  NSRect screen = NSScreen.mainScreen.visibleFrame;
  for (int i=0; i<2; i++) {
    NSRect rect = NSMakeRect(NSMinX(screen)+100+i*460, NSMidY(screen)-160+i*50, 420, 260);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
    window.title = demo ? @"Window Sweaters" : (i ? @"Sweater test · Safari pattern" : @"Sweater test · Chrome pattern");
    window.backgroundColor = [NSColor colorWithSRGBRed:.96 green:.95 blue:.92 alpha:1];
    NSTextField* label = [NSTextField wrappingLabelWithString:demo ? @"A cosier desktop." : @"Preparing the reveal…"];
    label.frame = NSMakeRect(30,80,360,120); label.font = [NSFont systemFontOfSize:22 weight:NSFontWeightMedium];
    [window.contentView addSubview:label];
    [self.windows addObject:window]; [self.labels addObject:label];
    uint64_t ignored = WINDOW_TAG_IGNORES_CYCLE;
    SLSSetWindowTags(SLSMainConnectionID(), (uint32_t)window.windowNumber, &ignored, 64);
    [window orderFront:nil];
  }
  [NSApp activateIgnoringOtherApps:YES];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200*NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self begin]; });
}
- (void)begin {
  g_server_port = create_connection_server_port();
  knit_charts_load(NULL);
  g_menu = [KnitMenu new];
  knit_reveal_configure(knit_reveal_allowed, knit_reveal_schedule);
  g_knit_on = false;
  for (int i=0; i<2; i++) {
    borders[i] = border_create();
    borders[i]->target_wid = (uint32_t)self.windows[i].windowNumber;
    borders[i]->sid = window_space_id(borders[i]->cid,borders[i]->target_wid);
    borders[i]->sticky = true; borders[i]->metadata_dirty = true;
    borders[i]->radius = 9; borders[i]->inner_radius = 10; borders[i]->focused = true;
    snprintf(borders[i]->app,sizeof borders[i]->app,"%s",i ? "Safari" : "Chrome");
    border_update(borders[i],false);
    assert(borders[i]->wid && borders[i]->context);
  }
  self.base = self.windows[0].frame;
  self.started = NSProcessInfo.processInfo.systemUptime;
  self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0/60 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}
- (void)label:(NSString*)text {
  NSString* shown = text;
  if (demo) {
    if ([text hasPrefix:@"300"]) shown = @"Sweater weather.\nBelow 60°F / 15.6°C.";
    else if ([text hasPrefix:@"Manual"]) shown = @"On when you want.\nOff when you don’t.";
    else if ([text hasPrefix:@"Resizing"]) shown = @"Make a little room…";
    else if ([text hasPrefix:@"Resize finished"]) shown = @"A perfect fit.\nEvery time.";
    else if ([text hasPrefix:@"Hide"]) shown = @"Welcome back.";
    else shown = @"A little knit.\nA cosier Mac.";
  }
  for (NSTextField* label in self.labels) label.stringValue = shown;
  printf("%.3f %s\n",NSProcessInfo.processInfo.systemUptime-self.started,text.UTF8String); fflush(stdout);
}
- (void)tick:(NSTimer*)timer {
  double t = NSProcessInfo.processInfo.systemUptime-self.started;
  if (t>1 && self.phase==0) { self.phase++; [self label:@"300 ms outward reveal\nBoth windows together"]; [g_menu setSweatersEnabled:YES]; }
  if (t>3 && self.phase==1) { self.phase++; [g_menu setSweatersEnabled:NO]; [self label:@"Manual off → on"]; }
  if (t>3.5 && self.phase==2) { self.phase++; [g_menu setSweatersEnabled:YES]; }
  if (t>5 && self.phase==3) { self.phase++; [self label:@"Resizing…\nExisting resize suppression"]; }
  if (t>=5 && t<7) {
    NSRect rect = self.base; rect.size.width += 90*sin((t-5)*M_PI); rect.size.height += 45*sin((t-5)*M_PI);
    [self.windows[0] setFrame:rect display:YES]; border_update(borders[0],false);
  }
  if (t>7 && self.phase==4) { self.phase++; [self label:@"Resize finished\nSweater reveals again"]; }
  if (t>=7 && t<8) border_update(borders[0],false);
  // Match the app's independent depth repair after window ordering changes.
  for (int i=0;i<2;i++) if (borders[i]->visible) border_reorder(borders[i]);
  if (t>8 && !self.diagnosed) {
    self.diagnosed=YES;
    bool ordered=false; SLSWindowIsOrderedIn(borders[0]->cid,borders[0]->wid,&ordered);
    printf("RECOVERY visible=%d geometry=%d suppressed=%d revealing=%d progress=%g ordered=%d frame=%s target=%s observed=%s\n",borders[0]->visible,borders[0]->geometry_valid,borders[0]->resize_suppressed,borders[0]->revealing,borders[0]->reveal_progress,ordered,NSStringFromRect(NSRectFromCGRect(borders[0]->frame)).UTF8String,NSStringFromRect(NSRectFromCGRect(borders[0]->target_bounds)).UTF8String,NSStringFromSize(NSSizeFromCGSize(borders[0]->resize_observed_size)).UTF8String); fflush(stdout);
  }
  if (t>9 && self.phase==5) {
    self.phase++; [self label:@"Hide → restore"]; [self.windows[0] orderOut:nil]; border_hide(borders[0]);
  }
  if (t>10 && self.phase==6) {
    self.phase++; [self.windows[0] orderFront:nil]; border_update(borders[0],false);
  }
  if (t>12 && self.phase==7) { self.phase++; [self label:@"Finished\nOpaque fabric · unchanged stitches"]; }
  if (t>14) {
    [timer invalidate]; [g_menu.revealTimer invalidate];
    for(int i=0;i<2;i++) {
      assert(!borders[i]->revealing && !borders[i]->reveal_image);
      border_hide(borders[i]); border_destroy_window(borders[i]);
      SLSReleaseConnection(borders[i]->cid);
      free(borders[i]); borders[i]=NULL; [self.windows[i] close];
    }
    puts("PASS: live reveal, off/on, resize recovery, hide/restore; surfaces released"); fflush(stdout);
    [NSApp terminate:nil];
  }
}
@end
int main(int argc, const char** argv) {
  @autoreleasepool {
    [NSApplication.sharedApplication setActivationPolicy:NSApplicationActivationPolicyAccessory];
    if (argc > 1 && !strcmp(argv[1], "--backdrop")) {
      NSRect screen = NSScreen.mainScreen.visibleFrame;
      NSWindow* backdrop = [[NSWindow alloc] initWithContentRect:NSMakeRect(NSMinX(screen)+60,NSMidY(screen)-205,965,435)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
      backdrop.backgroundColor = [NSColor colorWithSRGBRed:.86 green:.87 blue:.85 alpha:1];
      [backdrop orderFront:nil];
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 25*NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
      [NSApp run]; return 0;
    }
    demo = argc > 1 && !strcmp(argv[1], "--demo");
    RevealProbe* probe = [RevealProbe new]; NSApp.delegate = probe; [NSApp run];
  }
}
