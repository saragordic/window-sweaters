/* Native integration probe; never reads or changes another application's window.
 * Build from the repository root (border.c is included below, not linked twice):
 * clang -O2 -g -fobjc-arc -Isrc tests/live_resize.m src/animation.c src/knit.c src/chart.c src/apps.c -o /tmp/knit-live-resize -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks -framework SkyLight
 * Run ONLY after reviewing the visible scope:
 * /tmp/knit-live-resize --mode notifications
 * /tmp/knit-live-resize --mode immediate
 * Creates "Window Sweaters Resize Probe" and its border, resizes only that window
 * for three seconds, prints JSON, then closes its windows and exits. No AX,
 * synthetic input, screenshots, user configuration writes, or window inventory.
 * Geometry queries do not establish that pixels were presented in sync.
 */
#import <Cocoa/Cocoa.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include "../src/border.h"
#include "../src/events.h"
#include "../src/misc/apps.h"
#include "../src/misc/chart.h"

static CGError timed_bounds(int, uint32_t, CGRect*);
static CGError timed_shape(int, uint32_t, float, float, CFTypeRef);
static CGContextRef timed_context(int, uint32_t, CFDictionaryRef);
static void timed_draw(CGContextRef, CGRect, float, float, uint32_t, int, float, float);
static void timed_draw_inside(CGContextRef, CGRect, float, float, uint32_t, int, float);
static CGError timed_flush(int, uint32_t, void*);
static CGError timed_commit(CFTypeRef, int);
#define SLSGetWindowBounds timed_bounds
#define SLSSetWindowShape timed_shape
#define SLWindowContextCreate timed_context
#define knit_draw timed_draw
#define knit_draw_inside timed_draw_inside
#define SLSFlushWindowContentRegion timed_flush
#define SLSTransactionCommit timed_commit
#include "../src/border.c"
#undef SLSGetWindowBounds
#undef SLSSetWindowShape
#undef SLWindowContextCreate
#undef knit_draw
#undef knit_draw_inside
#undef SLSFlushWindowContentRegion
#undef SLSTransactionCommit

struct settings g_settings = {.enabled = true, .border_width = 10,
    .border_style = BORDER_STYLE_KNIT, .border_order = BORDER_ORDER_BELOW, .hidpi = true};
mach_port_t g_server_port = MACH_PORT_NULL; // Metadata is supplied for our own window.
float knit_current_width(void) { return g_settings.border_width; }

enum metric { NATIVE_FRAME, BORDER_UPDATE, BOUNDS, SHAPE, CONTEXT, DRAW, FLUSH, COMMIT,
              QUERY, NOTIFY_DELAY, QUEUE_DELAY, METRIC_COUNT };
static const char* metric_names[] = {"native_set_frame", "border_update", "target_bounds",
    "window_shape", "context_create", "knit_draw", "window_flush", "transaction_commit",
    "alignment_queries", "latest_request_to_notification", "notification_main_queue"};
struct distribution { unsigned count, stored; double total, max, samples[2048]; };
static struct distribution metrics[METRIC_COUNT];
static struct { unsigned count, errors; double top_max, bottom_max, size_max; } alignment[2];
static struct border* probe_border;
static _Atomic(uint32_t) probe_wid;
static _Atomic(uint64_t) request_ns;
static bool running, notification_mode = true, move_only;
static unsigned requests, updates, move_events, resize_events, query_failures;
static double minimum_top_gap = INFINITY;
static uint64_t start_ns;
static NSDictionary* startup_details;

static uint64_t now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}
static void record(enum metric metric, uint64_t elapsed) {
  struct distribution* d = &metrics[metric];
  double ms = elapsed / 1e6;
  d->count++; d->total += ms; d->max = fmax(d->max, ms);
  if (d->stored < 2048) d->samples[d->stored++] = ms;
}
static CGError timed_bounds(int cid, uint32_t wid, CGRect* out) {
  uint64_t t = now_ns(); CGError e = SLSGetWindowBounds(cid, wid, out);
  record(BOUNDS, now_ns() - t); return e;
}
static CGError timed_shape(int cid, uint32_t wid, float x, float y, CFTypeRef shape) {
  uint64_t t = now_ns(); CGError e = SLSSetWindowShape(cid, wid, x, y, shape);
  record(SHAPE, now_ns() - t); return e;
}
static CGContextRef timed_context(int cid, uint32_t wid, CFDictionaryRef options) {
  uint64_t t = now_ns(); CGContextRef c = SLWindowContextCreate(cid, wid, options);
  record(CONTEXT, now_ns() - t); return c;
}
static void timed_draw(CGContextRef ctx, CGRect r, float radius, float width,
                        uint32_t color, int chart, float dim, float tuck) {
  uint64_t t = now_ns(); knit_draw(ctx, r, radius, width, color, chart, dim, tuck);
  record(DRAW, now_ns() - t);
}
static void timed_draw_inside(CGContextRef ctx, CGRect r, float radius, float width,
                              uint32_t color, int chart, float dim) {
  uint64_t t = now_ns(); knit_draw_inside(ctx, r, radius, width, color, chart, dim);
  record(DRAW, now_ns() - t);
}
static CGError timed_flush(int cid, uint32_t wid, void* region) {
  uint64_t t = now_ns(); CGError e = SLSFlushWindowContentRegion(cid, wid, region);
  record(FLUSH, now_ns() - t); return e;
}
static CGError timed_commit(CFTypeRef transaction, int sync) {
  uint64_t t = now_ns(); CGError e = SLSTransactionCommit(transaction, sync);
  record(COMMIT, now_ns() - t); return e;
}

// Query only the probe's two owned window IDs, never their contents.
static void sample_alignment(unsigned bucket) {
  if (!probe_border || !probe_border->wid) return;
  CGRect target = CGRectZero, border = CGRectZero;
  uint64_t t = now_ns();
  CGError a = SLSGetWindowBounds(probe_border->cid, probe_border->target_wid, &target);
  CGError b = SLSGetWindowBounds(probe_border->cid, probe_border->wid, &border);
  record(QUERY, now_ns() - t);
  if (a || b) { query_failures++; return; }
  double top = fabs(border.origin.y - target.origin.y);
  double bottom = fabs(CGRectGetMaxY(border) - CGRectGetMaxY(target));
  double size = fmax(fabs(border.size.width - target.size.width),
                     fabs(border.size.height - target.size.height));
  alignment[bucket].count++;
  alignment[bucket].errors += top > .5 || bottom > .5 || size > .5;
  alignment[bucket].top_max = fmax(alignment[bucket].top_max, top);
  alignment[bucket].bottom_max = fmax(alignment[bucket].bottom_max, bottom);
  alignment[bucket].size_max = fmax(alignment[bucket].size_max, size);
}
static void update_border(void) {
  if (!running || !probe_border) return;
  uint64_t t = now_ns(); border_update(probe_border, false);
  record(BORDER_UPDATE, now_ns() - t); updates++;
  sample_alignment(0);
}
static void handle_notification(uint32_t event, uint64_t arrival, uint64_t request) {
  if (!running) return;
  if (event == EVENT_WINDOW_MOVE) move_events++; else resize_events++;
  if (request && arrival >= request) record(NOTIFY_DELAY, arrival - request);
  record(QUEUE_DELAY, now_ns() - arrival);
  if (notification_mode) update_border();
}
static void window_notification(uint32_t event, void* data, size_t length, void* context) {
  if (!data || length < sizeof(uint32_t)) return;
  uint32_t wid; memcpy(&wid, data, sizeof(wid));
  if (wid != atomic_load(&probe_wid)) return;
  uint64_t arrival = now_ns(), request = atomic_load(&request_ns);
  if (pthread_main_np()) handle_notification(event, arrival, request);
  else dispatch_async(dispatch_get_main_queue(), ^{ handle_notification(event, arrival, request); });
}
static int compare_double(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}
static NSDictionary* report(double seconds, NSString* error) {
  NSMutableDictionary* phases = [NSMutableDictionary dictionary];
  for (unsigned i = 0; i < METRIC_COUNT; i++) {
    struct distribution* d = &metrics[i];
    qsort(d->samples, d->stored, sizeof(double), compare_double);
    double median = d->stored ? d->samples[(d->stored - 1)/2] : 0;
    double p95 = d->stored ? d->samples[(unsigned)ceil(.95*d->stored) - 1] : 0;
    phases[@(metric_names[i])] = @{ @"calls": @(d->count),
        @"mean_ms": @(d->count ? d->total/d->count : 0), @"p50_ms": @(median),
        @"p95_ms": @(p95), @"max_ms": @(d->max) };
  }
  NSMutableDictionary* geometry = [NSMutableDictionary dictionary];
  for (unsigned i = 0; i < 2; i++) {
    geometry[i ? (move_only ? @"next_timer_before_move" : @"next_timer_before_resize")
               : @"immediately_after_border_update"] = @{
        @"samples": @(alignment[i].count), @"mismatch_over_half_point": @(alignment[i].errors),
        @"max_top_error_points": @(alignment[i].top_max),
        @"max_bottom_error_points": @(alignment[i].bottom_max),
        @"max_size_error_points": @(alignment[i].size_max) };
  }
  return @{ @"probe": move_only ? @"Window Sweaters Move Probe" : @"Window Sweaters Resize Probe",
      @"mode": move_only ? @"move" : notification_mode ? @"notifications" : @"immediate",
      @"status": error ?: @"completed", @"elapsed_seconds": @(seconds),
      @"native_resize_requests": @(requests), @"border_updates": @(updates),
      @"move_notifications": @(move_events), @"resize_notifications": @(resize_events),
      @"query_failures": @(query_failures), @"phases": phases, @"geometry": geometry,
      @"minimum_visible_top_gap_points": @(isfinite(minimum_top_gap) ? minimum_top_gap : 0),
      @"top_boundary_reached": @(move_only && minimum_top_gap <= .5),
      @"startup": startup_details ?: @{},
      @"pattern": @"Chrome built-in app colourwork", @"border_width_points": @(g_settings.border_width),
      @"measurement_limit": @"Own-window geometry and CPU/API timings only; no pixel capture or presentation-synchronization proof. Target and border share this probe process. Notification delay is measured from the latest resize request, not a per-event correlation ID." };
}

@interface ResizeProbe : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property NSWindow* window;
@property NSTimer* timer;
@property NSRect baseFrame;
@property double amplitude;
@property BOOL finished;
@end
@implementation ResizeProbe
- (void)finish:(NSString*)error {
  if (self.finished) return;
  self.finished = YES; running = false;
  double seconds = start_ns ? (now_ns() - start_ns)/1e9 : 0;
  [self.timer invalidate]; self.timer = nil;
  atomic_store(&probe_wid, 0);
  NSDictionary* result = report(seconds, error);
  if (probe_border) { border_destroy(probe_border); probe_border = NULL; }
  [self.window orderOut:nil]; self.window.delegate = nil;
  [self.window close]; self.window = nil;
  // border_destroy releases its own resources on the main queue first.
  dispatch_async(dispatch_get_main_queue(), ^{
    NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
    if (json) { fwrite(json.bytes, 1, json.length, stdout); fputc('\n', stdout); fflush(stdout); }
    exit(error ? 2 : 0);
  });
}
- (void)windowWillClose:(NSNotification*)notification { [self finish:@"probe_closed_early"]; }
- (void)tick:(NSTimer*)timer {
  double elapsed = (now_ns() - start_ns)/1e9;
  if (elapsed >= 3.0) {
    sample_alignment(1);
    [self finish:(notification_mode && !updates) ? @"no_target_notifications_received" : nil];
    return;
  }
  sample_alignment(1);
  NSRect frame = self.baseFrame;
  double delta = round(self.amplitude * .5 * (1 - cos(2*M_PI*elapsed)));
  // Move mode checks the drag geometry without triggering resize suppression.
  // Resize mode keeps AppKit's bottom-left origin fixed.
  if (move_only) frame.origin.y += delta;
  else frame.size.height += delta;
  atomic_store(&request_ns, now_ns()); requests++;
  uint64_t t = now_ns(); [self.window setFrame:frame display:YES animate:NO];
  record(NATIVE_FRAME, now_ns() - t);
  if (move_only) minimum_top_gap = fmin(minimum_top_gap,
      NSMaxY(self.window.screen.visibleFrame) - NSMaxY(self.window.frame));
  if (!notification_mode) update_border();
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  NSScreen* screen = NSScreen.mainScreen;
  int cid = SLSMainConnectionID();
  if (!screen || !cid) { [self finish:@"window_server_unavailable"]; return; }
  NSRect visible = screen.visibleFrame;
  double width = fmin(1100, visible.size.width - 80);
  double height = fmin(600, visible.size.height - 180);
  if (width < 200 || height < 120) { [self finish:@"display_too_small"]; return; }
  NSRect content = NSMakeRect(NSMidX(visible) - width/2, NSMidY(visible) - height/2 - 50, width, height);
  self.window = [[NSWindow alloc] initWithContentRect:content
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  self.window.title = @"Window Sweaters Resize Probe";
  self.window.releasedWhenClosed = NO;
  self.window.delegate = self;
  self.window.collectionBehavior = NSWindowCollectionBehaviorIgnoresCycle;
  NSTextField* label = [NSTextField wrappingLabelWithString:@"Temporary native resize timing probe\nCloses automatically in three seconds."];
  label.frame = NSMakeRect(24, 24, width - 48, 56);
  [self.window.contentView addSubview:label];
  uint32_t wid = (uint32_t)self.window.windowNumber;
  // Keep the separately running Window Sweaters app from decorating this probe.
  // Its window_suitable filter excludes this bit; our explicit border still runs.
  uint64_t ignored_cycle = WINDOW_TAG_IGNORES_CYCLE;
  if (!wid || SLSSetWindowTags(cid, wid, &ignored_cycle, 64) != kCGErrorSuccess) {
    [self finish:@"own_window_isolation_unavailable"]; return;
  }
  [self.window orderFront:nil];
  // Give AppKit one main-runloop turn to publish its initial ordered state.
  // This is a single startup delay, never a retry loop or production workaround.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{ [self configureBorder]; });
}
- (void)configureBorder {
  if (self.finished || !self.window) return;
  int cid = SLSMainConnectionID();
  uint32_t wid = (uint32_t)self.window.windowNumber;
  CGRect bounds = CGRectZero;
  bool shown = false;
  CGError bounds_error = SLSGetWindowBounds(cid, wid, &bounds);
  CGError ordered_error = SLSWindowIsOrderedIn(cid, wid, &shown);
  NSMutableDictionary* details = [@{
      @"deferred_milliseconds": @100, @"connection_id": @(cid), @"target_window_id": @(wid),
      @"bounds_error": @(bounds_error), @"bounds_points": NSStringFromRect(NSRectFromCGRect(bounds)),
      @"ordered_in_error": @(ordered_error), @"ordered_in": @(shown),
      @"appkit_visible": @(self.window.visible) } mutableCopy];
  startup_details = details;
  if (!wid || bounds_error != kCGErrorSuccess) {
    [self finish:@"own_window_bounds_unavailable"]; return;
  }
  if (ordered_error != kCGErrorSuccess || !shown) {
    [self finish:@"own_window_not_ordered_after_startup_delay"]; return;
  }
  uint64_t sid = window_space_id(cid, wid);
  details[@"space_id"] = @(sid);
  if (!sid) { [self finish:@"own_window_space_unavailable"]; return; }
  probe_border = calloc(1, sizeof(struct border));
  border_init(probe_border, cid);
  probe_border->target_wid = wid; probe_border->sid = sid;
  probe_border->sticky = true; probe_border->metadata_dirty = false;
  probe_border->radius = 9; probe_border->inner_radius = 10;
  probe_border->focused = true; probe_border->level = (int)self.window.level;
  snprintf(probe_border->app, sizeof probe_border->app, "Chrome");
  knit_charts_load(NULL); // Built-in chart data only; does not touch user config.
  g_knit_pattern_by_app = true; g_chart_active = -1;
  border_update(probe_border, false);
  details[@"border_window_id"] = @(probe_border->wid);
  details[@"disable_mouse_events_error"] = @(SLSSetMouseEventEnableFlags(cid, probe_border->wid, false));
  details[@"border_context_created"] = @(probe_border->context != NULL);
  details[@"border_too_small"] = @(probe_border->too_small);
  details[@"border_target_bounds_points"] = NSStringFromRect(NSRectFromCGRect(probe_border->target_bounds));
  if (!probe_border->wid || !probe_border->context) {
    [self finish:@"border_window_unavailable"]; return;
  }
  atomic_store(&probe_wid, wid);
  CGError a = SLSRegisterNotifyProc(window_notification, EVENT_WINDOW_MOVE, NULL);
  CGError b = SLSRegisterNotifyProc(window_notification, EVENT_WINDOW_RESIZE, NULL);
  CGError c = SLSRequestNotificationsForWindows(cid, &wid, 1);
  if (a || b || c) { [self finish:@"own_window_notifications_unavailable"]; return; }
  self.baseFrame = self.window.frame;
  NSRect visible = self.window.screen.visibleFrame;
  self.amplitude = move_only ? fmax(0, NSMaxY(visible) - NSMaxY(self.baseFrame))
                             : fmin(120, NSMaxY(visible) - NSMaxY(self.baseFrame) - 30);
  memset(metrics, 0, sizeof(metrics)); // Exclude one-time creation and tile warmup.
  running = true; start_ns = now_ns();
  self.timer = [NSTimer timerWithTimeInterval:1.0/60 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
}
@end

int main(int argc, const char* argv[]) {
  if (argc == 3 && strcmp(argv[1], "--mode") == 0) {
    if (strcmp(argv[2], "immediate") == 0) notification_mode = false;
    else if (strcmp(argv[2], "move") == 0) { notification_mode = false; move_only = true; }
    else if (strcmp(argv[2], "notifications") != 0) {
      fputs("Mode must be notifications, immediate, or move.\n", stderr); return 2;
    }
  } else if (argc != 1) {
    fputs("Usage: knit-live-resize [--mode notifications|immediate|move]\nCreates only its own visible probe window for three seconds.\n", stderr);
    return 2;
  }
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    ResizeProbe* delegate = [ResizeProbe new];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
