/* Native stacking probe. Build from repository root:
 * clang -O2 -g -fobjc-arc -Isrc tests/live_order.m src/animation.c src/knit.c src/chart.c src/apps.c -o /tmp/knit-live-order -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks -framework SkyLight
 * Reviewed scope before running: /tmp/knit-live-order --mode immediate
 * or: /tmp/knit-live-order --mode deferred
 * Only two temporary owned NSWindows and their production borders are changed.
 * Six order swaps run after a 100ms startup delay; cleanup normally finishes
 * within two seconds. The external driver should retain its 12-second timeout.
 * No AX, input synthesis, screenshots, app activation, or configuration writes.
 */
// Reuse only the local production-border wrappers and support definitions.
// ResizeProbe is never instantiated and its renamed entry point is never called.
#include "../src/border.h"
static int32_t owned_order_sublevel(int cid, uint32_t wid);
#define window_sub_level owned_order_sublevel
#define main unused_resize_probe_main
#include "live_resize.m"
#undef main
#undef window_sub_level

static struct border* order_borders[2];
static _Atomic(uint32_t) order_sources[2];
static NSMutableArray* order_snapshots;
static NSMutableArray* order_notifications;
static NSMutableArray* order_startup;
static NSDictionary* order_injection;
static bool order_running, deferred_repair, repairing, injecting;
static unsigned order_generation, handled_generation[2], scheduled_generation;
static unsigned requested_front, repairs, suppressed_reentrant;
static uint64_t order_start_ns, order_request_ns;
static NSString* order_error;
static unsigned reorder_calls;
static uint64_t reorder_total_ns;

static int32_t owned_order_sublevel(int cid, uint32_t wid) {
  assert(wid && (wid == atomic_load(&order_sources[0]) || wid == atomic_load(&order_sources[1])));
  return SLSGetWindowSubLevel(cid, wid);
}

static double order_elapsed(void) { return order_start_ns ? (now_ns() - order_start_ns)/1e6 : 0; }

// This API returns the onscreen list in front-to-back order. For every dictionary,
// inspect only its numeric ID first. Never retain/log fields from unrelated windows.
static NSDictionary* order_snapshot(NSString* phase) {
  uint32_t owned[4] = {atomic_load(&order_sources[0]), atomic_load(&order_sources[1]),
      order_borders[0] ? order_borders[0]->wid : 0, order_borders[1] ? order_borders[1]->wid : 0};
  NSInteger rank[4] = {-1, -1, -1, -1};
  CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
                                                 kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
  if (!windows) return @{ @"phase": phase, @"error": @"onscreen_order_unavailable" };
  for (CFIndex i = 0, count = CFArrayGetCount(windows); i < count; i++) {
    CFDictionaryRef entry = CFArrayGetValueAtIndex(windows, i);
    CFNumberRef id = CFDictionaryGetValue(entry, kCGWindowNumber);
    int32_t value = 0;
    if (!id || !CFNumberGetValue(id, kCFNumberSInt32Type, &value)) continue;
    for (unsigned j = 0; j < 4; j++) if ((uint32_t)value == owned[j]) rank[j] = i;
  }
  CFRelease(windows);
  bool complete = rank[0] >= 0 && rank[1] >= 0 && rank[2] >= 0 && rank[3] >= 0;
  unsigned front = requested_front, rear = 1 - front;
  bool a_above = complete && rank[2] < rank[0];
  bool b_above = complete && rank[3] < rank[1];
  bool target_order = complete && rank[front] < rank[rear];
  bool foreground_border_clear = complete && rank[front + 2] < rank[rear];
  NSMutableArray* own = [NSMutableArray array];
  for (unsigned j = 0; j < 4; j++) {
    int64_t level = 0;
    CGError level_error = owned[j] ? SLSGetWindowLevel(SLSMainConnectionID(), owned[j], &level) : kCGErrorFailure;
    int32_t sublevel = owned[j] ? SLSGetWindowSubLevel(SLSMainConnectionID(), owned[j]) : 0;
    [own addObject:@{ @"role": @[@"target_a", @"target_b", @"border_a", @"border_b"][j],
                     @"window_id": @(owned[j]), @"front_to_back_index": @(rank[j]),
                     @"level_query_error": @(level_error), @"level": @(level), @"sublevel": @(sublevel) }];
  }
  return @{ @"phase": phase, @"stage": @(order_generation), @"elapsed_ms": @(order_elapsed()),
      @"requested_front": front ? @"target_b" : @"target_a", @"own_windows": own,
      @"all_own_windows_onscreen": @(complete), @"source_order_matches_request": @(target_order),
      @"border_a_above_owner": @(a_above), @"border_b_above_owner": @(b_above),
      @"foreground_border_above_rear_target": @(foreground_border_clear),
      @"own_sequence_valid": @(complete && target_order && a_above && b_above && foreground_border_clear) };
}

static bool update_order_border(unsigned index) {
  struct border* b = order_borders[index];
  if (!b) return false;
  int64_t level = 0;
  CGError error = SLSGetWindowLevel(b->cid, b->target_wid, &level);
  if (error) { order_error = @"own_level_query_failed"; return false; }
  // Direct accessor, never the version-specific raw-Mach window_sub_level helper.
  int32_t sublevel = SLSGetWindowSubLevel(b->cid, b->target_wid);
  b->level = (int)level; b->sub_level = sublevel;
  b->sticky = true; b->metadata_dirty = false;
  uint64_t t = now_ns();
  repairing = true; border_update(b, false); repairing = false;
  record(BORDER_UPDATE, now_ns() - t);
  repairs++;
  return true;
}
static void repair_order_border(unsigned index) {
  if (!order_borders[index]) return;
  uint64_t t = now_ns();
  repairing = true; border_reorder(order_borders[index]); repairing = false;
  reorder_total_ns += now_ns() - t; reorder_calls++;
}
static void handle_order_notification(unsigned index, uint64_t arrival) {
  if (!order_running) return;
  [order_notifications addObject:@{ @"stage": @(order_generation),
      @"window_id": @(atomic_load(&order_sources[index])), @"elapsed_ms": @(order_elapsed()),
      @"latest_swap_to_notification_ms": @(order_request_ns && arrival >= order_request_ns
                                             ? (arrival - order_request_ns)/1e6 : 0),
      @"main_queue_delay_ms": @((now_ns() - arrival)/1e6) }];
  if (repairing || injecting) { suppressed_reentrant++; return; }
  // The probe controls every source swap. Bound same-process self-notifications
  // to one immediate update per source per swap rather than an accidental loop.
  if (handled_generation[index] == order_generation) return;
  handled_generation[index] = order_generation;
  update_order_border(index);
  if (deferred_repair && scheduled_generation != order_generation) {
    unsigned generation = order_generation;
    scheduled_generation = generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      if (!order_running || generation != order_generation) return;
      repair_order_border(1 - requested_front); // Real production helper, rear first.
      repair_order_border(requested_front);
      [order_snapshots addObject:order_snapshot(@"after_deferred_all_repair")];
    });
  }
}
static void order_notification(uint32_t event, void* data, size_t length, void* context) {
  if (!data || length < sizeof(uint32_t)) return;
  uint32_t wid; memcpy(&wid, data, sizeof(wid));
  for (unsigned i = 0; i < 2; i++) {
    if (!wid || wid != atomic_load(&order_sources[i])) continue;
    uint64_t arrival = now_ns();
    if (pthread_main_np()) handle_order_notification(i, arrival);
    else dispatch_async(dispatch_get_main_queue(), ^{ handle_order_notification(i, arrival); });
    break;
  }
}

@interface OrderProbe : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property NSMutableArray<NSWindow*>* windows;
@property BOOL finished;
@end
@implementation OrderProbe
- (void)finish:(NSString*)error {
  if (self.finished) return;
  self.finished = YES; order_running = false;
  double duration = order_elapsed();
  NSMutableDictionary* phases = [NSMutableDictionary dictionary];
  for (unsigned i = 0; i < METRIC_COUNT; i++) if (metrics[i].count) {
    struct distribution* d = &metrics[i];
    phases[@(metric_names[i])] = @{ @"calls": @(d->count), @"mean_ms": @(d->total/d->count), @"max_ms": @(d->max) };
  }
  NSDictionary* result = @{ @"probe": @"Window Sweaters Order Probe",
      @"mode": deferred_repair ? @"immediate_plus_all_at_30ms" : @"immediate_per_source_notification",
      @"status": error ?: order_error ?: @"completed", @"elapsed_ms": @(duration),
      @"requested_swaps": @(order_generation), @"border_updates": @(repairs),
      @"production_reorder_calls": @(reorder_calls),
      @"production_reorder_mean_ms": @(reorder_calls ? reorder_total_ns/1e6/reorder_calls : 0),
      @"suppressed_reentrant_notifications": @(suppressed_reentrant),
      @"startup": order_startup ?: @[], @"notifications": order_notifications ?: @[],
      @"snapshots": order_snapshots ?: @[], @"phases": phases,
      @"explicit_fault_injection": order_injection ?: @{ @"performed": @NO },
      @"limits": @"Own synthetic windows share one process. Only owned IDs/ranks are retained from the public onscreen list. One immediate repair per source per requested swap bounds same-process feedback. Ranks measure stacking, not presented pixels. Direct SkyLight sublevel accessor has no separate error result." };
  for (unsigned i = 0; i < 2; i++) {
    atomic_store(&order_sources[i], 0);
    if (order_borders[i]) { border_destroy(order_borders[i]); order_borders[i] = NULL; }
  }
  for (NSWindow* window in self.windows) { window.delegate = nil; [window orderOut:nil]; [window close]; }
  [self.windows removeAllObjects];
  dispatch_async(dispatch_get_main_queue(), ^{
    NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
    if (json) { fwrite(json.bytes, 1, json.length, stdout); fputc('\n', stdout); fflush(stdout); }
    exit((error || order_error) ? 2 : 0);
  });
}
- (void)windowWillClose:(NSNotification*)notification { [self finish:@"probe_closed_early"]; }
- (void)injectStaleOrder {
  if (self.finished) return;
  unsigned front = requested_front, rear = 1 - front;
  CFTypeRef transaction = SLSTransactionCreate(SLSMainConnectionID());
  if (!transaction) { order_error = @"owned_injection_transaction_unavailable"; return; }
  injecting = true;
  CGError ordered = SLSTransactionOrderWindow(transaction, order_borders[front]->wid,
      BORDER_ORDER_BELOW, atomic_load(&order_sources[rear]));
  CGError committed = SLSTransactionCommit(transaction, 0);
  CFRelease(transaction);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    if (self.finished) return;
    NSDictionary* stale = order_snapshot(@"explicitly_injected_stale_order");
    [order_snapshots addObject:stale];
    unsigned old_shapes = metrics[SHAPE].count, old_contexts = metrics[CONTEXT].count;
    unsigned old_draws = metrics[DRAW].count, old_flushes = metrics[FLUSH].count;
    repair_order_border(rear); repair_order_border(front);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      if (self.finished) return;
      NSDictionary* recovered = order_snapshot(@"after_production_reorder_fault_recovery");
      [order_snapshots addObject:recovered];
      bool was_stale = [stale[@"all_own_windows_onscreen"] boolValue]
          && ![stale[@"foreground_border_above_rear_target"] boolValue];
      bool fixed = [recovered[@"own_sequence_valid"] boolValue];
      order_injection = @{ @"performed": @YES,
          @"description": @"Intentionally placed the owned foreground border below the owned rear target; separate from the six natural swaps.",
          @"raw_order_return": @(ordered), @"raw_commit_return": @(committed),
          @"stale_order_observed": @(was_stale), @"recovered_by_production_helper": @(fixed),
          @"shape_calls_during_repair": @(metrics[SHAPE].count - old_shapes),
          @"context_calls_during_repair": @(metrics[CONTEXT].count - old_contexts),
          @"draw_calls_during_repair": @(metrics[DRAW].count - old_draws),
          @"flush_calls_during_repair": @(metrics[FLUSH].count - old_flushes) };
      // These private transaction calls do not provide reliable CGError
      // acknowledgments on this runtime. Require the observed rank transition.
      if (!was_stale || !fixed) order_error = @"owned_fault_recovery_check_failed";
      injecting = false;
    });
  });
}
- (void)stage:(unsigned)stage {
  if (self.finished) return;
  if (stage == 6) { [self finish:order_notifications.count ? nil : @"no_own_reorder_notifications"]; return; }
  requested_front = stage % 2; order_generation = stage + 1;
  NSWindow* front = self.windows[requested_front];
  NSWindow* back = self.windows[1 - requested_front];
  order_request_ns = now_ns();
  [front orderWindow:NSWindowAbove relativeTo:back.windowNumber];
  [order_snapshots addObject:order_snapshot(@"immediately_after_source_swap")];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 70 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    if (!self.finished) [order_snapshots addObject:order_snapshot(@"70ms_after_source_swap")];
  });
  if (deferred_repair && stage == 5)
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self injectStaleOrder]; });
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self stage:stage + 1]; });
}
- (void)configureBorders {
  if (self.finished) return;
  int cid = SLSMainConnectionID();
  for (unsigned i = 0; i < 2; i++) {
    NSWindow* window = self.windows[i];
    uint32_t wid = (uint32_t)window.windowNumber;
    bool shown = false; CGRect bounds = CGRectZero;
    CGError a = SLSGetWindowBounds(cid, wid, &bounds);
    CGError b = SLSWindowIsOrderedIn(cid, wid, &shown);
    uint64_t sid = window_space_id(cid, wid);
    [order_startup addObject:@{ @"role": i ? @"target_b" : @"target_a", @"window_id": @(wid),
        @"bounds_error": @(a), @"bounds_points": NSStringFromRect(NSRectFromCGRect(bounds)),
        @"ordered_in_error": @(b), @"ordered_in": @(shown), @"space_id": @(sid) }];
    if (a || b || !shown || !sid) { [self finish:@"own_window_not_ready"]; return; }
    struct border* border = calloc(1, sizeof(*border)); order_borders[i] = border;
    border_init(border, cid); border->target_wid = wid; border->sid = sid;
    border->radius = 9; border->inner_radius = 10; border->focused = true;
    snprintf(border->app, sizeof border->app, "%s", i ? "Figma" : "Chrome");
    if (!update_order_border(i) || !border->wid || !border->context) {
      [self finish:@"own_border_not_ready"]; return;
    }
  }
  uint32_t source_ids[2] = {atomic_load(&order_sources[0]), atomic_load(&order_sources[1])};
  CGError a = SLSRegisterNotifyProc(order_notification, EVENT_WINDOW_REORDER, NULL);
  CGError b = SLSRequestNotificationsForWindows(cid, source_ids, 2);
  if (a || b) { [self finish:@"own_reorder_notifications_unavailable"]; return; }
  memset(metrics, 0, sizeof(metrics)); repairs = 0;
  order_start_ns = now_ns(); order_running = true;
  [self stage:0];
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  NSScreen* screen = NSScreen.mainScreen; int cid = SLSMainConnectionID();
  if (!screen || !cid) { [self finish:@"window_server_unavailable"]; return; }
  self.windows = [NSMutableArray array];
  order_startup = [NSMutableArray array]; order_snapshots = [NSMutableArray array];
  order_notifications = [NSMutableArray array];
  NSRect visible = screen.visibleFrame;
  double width = fmin(620, visible.size.width - 160), height = fmin(380, visible.size.height - 160);
  if (width < 200 || height < 120) { [self finish:@"display_too_small"]; return; }
  for (unsigned i = 0; i < 2; i++) {
    NSRect rect = NSMakeRect(NSMidX(visible) - width/2 + i*55 - 28,
                            NSMidY(visible) - height/2 + i*45 - 22, width, height);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    window.title = i ? @"Window Sweaters Order Probe B" : @"Window Sweaters Order Probe A";
    window.releasedWhenClosed = NO; window.delegate = self;
    window.collectionBehavior = NSWindowCollectionBehaviorIgnoresCycle;
    [self.windows addObject:window];
    uint32_t wid = (uint32_t)window.windowNumber; uint64_t tag = WINDOW_TAG_IGNORES_CYCLE;
    if (!wid || SLSSetWindowTags(cid, wid, &tag, 64)) { [self finish:@"own_window_isolation_unavailable"]; return; }
    atomic_store(&order_sources[i], wid);
    [window orderFront:nil];
  }
  knit_charts_load(NULL); g_knit_pattern_by_app = true;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self configureBorders]; });
}
@end

int main(int argc, const char* argv[]) {
  if (argc == 3 && !strcmp(argv[1], "--mode")) {
    if (!strcmp(argv[2], "deferred")) deferred_repair = true;
    else if (strcmp(argv[2], "immediate")) { fputs("Mode must be immediate or deferred.\n", stderr); return 2; }
  } else if (argc != 1) { fputs("Usage: knit-live-order [--mode immediate|deferred]\n", stderr); return 2; }
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    OrderProbe* delegate = [OrderProbe new]; app.delegate = delegate;
    [app run];
  }
  return 0;
}
