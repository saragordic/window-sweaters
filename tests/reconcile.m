// Fault-injected complete WindowServer snapshots; no real windows are changed.
#import <Cocoa/Cocoa.h>
CFAbsoluteTime mock_now(void);
#define CFAbsoluteTimeGetCurrent mock_now
#define SLSGetWindowBounds mock_bounds
#define SLSMainConnectionID mock_connection
#include "../src/windows.h"
static CFArrayRef mock_snapshot(CGWindowListOption options, CGWindowID wid);
#define CGWindowListCopyWindowInfo mock_snapshot
static uint64_t mock_space(int cid, uint32_t wid) { return 1; }
#define window_space_id mock_space
#include "../src/reconcile.c"

struct table g_windows;
pid_t g_pid = 100;
bool g_knit_on = true;
static struct settings settings = {.border_order = BORDER_ORDER_BELOW};
static struct border target, discovered;
static int hides, restores, geometry, orders, destroys, creates, surface_resets;
void border_reset_surface(struct border* b) { surface_resets++; b->visible=false; b->geometry_valid=false; }
static bool gone;
static CFAbsoluteTime test_now = 1;
static int space_checks, space_repairs;
static uint64_t target_space = 1;
CFAbsoluteTime mock_now(void) { return test_now; }
bool border_refresh_space(struct border* b) {
  space_checks++;
  if (b->sid != target_space) {
    b->sid = target_space; b->metadata_dirty = true; space_repairs++;
    return true;
  }
  return false;
}
int mock_connection(void) { return 1; }
CGError mock_bounds(int cid, uint32_t wid, CGRect* bounds) { *bounds=CGRectMake(10,10,300,200);return gone ? kCGErrorFailure : kCGErrorSuccess; }
struct settings* border_get_settings(struct border* b) { return &settings; }
void border_update_geometry(struct border* b) { geometry++; b->target_bounds=CGRectMake(10,10,300,200); }
void border_update_geometry_from_snapshot(struct border* b,CGRect frame,double opacity) {
  if(!b->visible)restores++;else if(!CGRectEqualToRect(frame,b->target_bounds))geometry++;
  b->visible=true;b->geometry_valid=true;b->metadata_dirty=false;b->target_bounds=frame;b->opacity=opacity;
}
bool border_suppress_live_resize(struct border* b,CGRect bounds) { return b->resize_suppressed; }
void border_reorder(struct border* b) { orders++; }
void windows_window_hide(struct table* t,uint32_t wid) { hides++; ((struct border*)table_find(t,&wid))->visible=false; }
void windows_window_unhide(struct table* t,uint32_t wid) { restores++; ((struct border*)table_find(t,&wid))->visible=true; }
bool windows_window_destroy(struct table* t,uint32_t wid,uint64_t sid) { destroys++; table_remove(t,&wid);return true; }
bool windows_window_create(struct table* t,uint32_t wid,uint64_t sid) {
  if(wid!=7)return false;
  creates++;discovered=(struct border){.target_wid=7,.wid=8,.visible=true,.geometry_valid=true,
    .target_bounds=CGRectMake(10,10,300,200)};
  table_add(t,&wid,&discovered);return true;
}
static unsigned long hash(void* p) { return *(uint32_t*)p; }
static int equal(void* a,void* b) { return *(uint32_t*)a==*(uint32_t*)b; }
static NSDictionary* window(uint32_t wid,pid_t pid,double alpha) {
  return @{(__bridge NSString*)kCGWindowNumber:@(wid),(__bridge NSString*)kCGWindowOwnerPID:@(pid),
    (__bridge NSString*)kCGWindowAlpha:@(alpha),(__bridge NSString*)kCGWindowLayer:@0,
    (__bridge NSString*)kCGWindowBounds:CFBridgingRelease(CGRectCreateDictionaryRepresentation(CGRectMake(10,10,300,200)))};
}
static void observe(NSArray* a) { windows_reconcile_snapshot(&g_windows,(__bridge CFArrayRef)a); }
static void test_adaptive(void) {
  g_quiet_streak = 0; g_reconcile_interval = .05; g_reconcile_kick = false;
  g_power_low = false; g_screens_asleep = false; g_knit_on = true;
  assert(reconcile_step_interval(false) == .05);
  assert(reconcile_step_interval(false) == .1);
  assert(reconcile_step_interval(false) == .2);
  assert(reconcile_step_interval(false) == .4);
  assert(reconcile_step_interval(false) == .8);
  assert(reconcile_step_interval(false) == .8);
  assert(reconcile_step_interval(true) == .05);
  assert(reconcile_step_interval(false) == .05);
  assert(reconcile_step_interval(false) == .1);
  while (reconcile_step_interval(false) < .8) {}
  reconcile_kick();
  assert(reconcile_step_interval(false) == .05 && !g_reconcile_kick);
  // Flag cleared: the next quiet steps decay. A sticky kick would stay at .05.
  assert(reconcile_step_interval(false) == .05);
  assert(reconcile_step_interval(false) == .1);
  g_screens_asleep = true;
  reconcile_kick();
  assert(reconcile_step_interval(false) == 1.0);
  assert(reconcile_step_interval(true) == 1.0);
  g_screens_asleep = false;
  g_quiet_streak = 0; g_reconcile_kick = false; g_power_low = true;
  // fmax floors every quiet step, including the first, so this is .2 .2 .2 .4 .8.
  double low0 = reconcile_step_interval(false), low1 = reconcile_step_interval(false);
  double low2 = reconcile_step_interval(false), low3 = reconcile_step_interval(false);
  double low4 = reconcile_step_interval(false);
  assert(low1 >= .2 && low0 == .2 && low1 == .2 && low2 == .2 && low3 == .4 && low4 == .8);
  g_power_low = false; g_quiet_streak = 0; g_reconcile_kick = false;
  g_reconcile_interval = reconcile_step_interval(false);
  assert(g_reconcile_interval == .05 && reconcile_is_fast());
  reconcile_step_interval(false);
  g_reconcile_interval = reconcile_step_interval(false);
  assert(g_reconcile_interval == .2 && !reconcile_is_fast());
  gone = false; settings.border_order = BORDER_ORDER_BELOW;
  live_ids = NULL; live_count = 0; live_ids_expiry = 0;
  table_init(&g_windows, 16, hash, equal);
  uint32_t wid = 3;
  target = (struct border){.target_wid=3,.wid=2,.sid=1,.visible=true,.geometry_valid=true,
    .opacity=1,.target_bounds=CGRectMake(10,10,300,200),.space_check_after=0};
  table_add(&g_windows, &wid, &target);
  NSDictionary* s=window(3,200,1), *o=window(2,100,1), *rear=window(4,201,1);
  g_quiet_streak = 4; g_reconcile_kick = false; g_power_low = false;
  target_space = 1;                    // the border's Space is unchanged: a no-op check
  int checks_before = space_checks, repairs_before = space_repairs;
  bool quiet = windows_reconcile_snapshot(&g_windows, (__bridge CFArrayRef)@[s,o,rear]);
  assert(!quiet && space_checks == checks_before + 1 && space_repairs == repairs_before);
  assert(reconcile_step_interval(quiet) == .8); // a no-op space check is not work
  bool repaired = windows_reconcile_snapshot(&g_windows, (__bridge CFArrayRef)@[s,rear,o]);
  assert(repaired);
  assert(reconcile_step_interval(repaired) == .05);
  table_free(&g_windows); free(previous_snapshot); previous_snapshot=NULL; previous_count=0;
  puts("PASS: adaptive reconcile interval decay, kick, work reset, sleep and low-power");
}
static CFArrayRef mock_snapshot(CGWindowListOption options, CGWindowID wid) {
  return CFRetain((__bridge CFArrayRef)(gone ? @[] : @[window(3,200,1)]));
}
int main(void) {@autoreleasepool {
  table_init(&g_windows,16,hash,equal);
  uint32_t wid=3;target=(struct border){.target_wid=3,.wid=2,.sid=1,.visible=true,.geometry_valid=true,
    .target_bounds=CGRectMake(10,10,300,200)};table_add(&g_windows,&wid,&target);
  NSDictionary*s=window(3,200,1),*o=window(2,100,1),*rear=window(4,201,1);
  observe(@[s,o,rear]);assert(!hides&&!orders&&!geometry&&!restores);
  assert(space_checks == 1 && space_repairs == 0);
  target_space = 2; // display changes, but target/overlay stay listed at identical bounds
  observe(@[s,o,rear]); assert(space_checks == 1); // bounded checks
  test_now += .21;
  observe(@[s,o,rear]);
  assert(space_checks == 2 && space_repairs == 1 && target.sid == 2 && !target.metadata_dirty);
  target.resize_suppressed=true;
  observe(@[s,rear]);assert(!restores&&!geometry); // Recovery must not resurrect the hidden resize ring.
  target.resize_suppressed=false;
  observe(nil);assert(!hides); // A failed query is not an empty desktop.
  observe(@[s,rear,o]);assert(orders==1); // Border stranded behind a rear window.
  settings.border_order=BORDER_ORDER_ABOVE;
  observe(@[o,s,rear]);assert(orders==1);
  observe(@[s,o,rear]);assert(orders==2);
  settings.border_order=BORDER_ORDER_BELOW;
  target.target_bounds.size.width=280;
  observe(@[s,o,rear]);assert(geometry==1);
  observe(@[o,rear]);assert(hides==1&&!target.visible); // Lost HIDE notification.
  observe(@[s,rear]);assert(restores==1&&target.visible); // Lost UNHIDE.
  observe(@[window(3,200,0),o,rear]);assert(hides==2); // Alpha-zero retained source.
  observe(@[s,o,rear]);assert(restores==2);
  NSMutableDictionary* transformed=[s mutableCopy];
  transformed[(__bridge NSString*)kCGWindowBounds]=CFBridgingRelease(CGRectCreateDictionaryRepresentation(CGRectMake(10,10,90,60)));
  observe(@[transformed,o,rear]);assert(target.native_transform&&!target.visible);
  observe(@[s,o,rear]);assert(!target.native_transform&&target.visible);
  // Ordering the same missing surface must eventually escalate to recreation.
  int old_resets=surface_resets;
  test_now += 2;
  observe(@[s,rear]); observe(@[s,rear]); observe(@[s,rear]);
  assert(surface_resets==old_resets+1);
  for(int i=0;i<20;i++)observe(@[s,rear]);
  assert(surface_resets==old_resets+1); // rate limited even if restoration fails
  observe(@[s,o,rear]); assert(target.missing_overlay_snapshots==0);
  target.resize_suppressed=true; test_now+=2;
  for(int i=0;i<5;i++)observe(@[s,rear]);
  assert(surface_resets==old_resets+1);
  target.resize_suppressed=false;
  for(int i=0;i<5;i++)observe(@[rear]);
  assert(surface_resets==old_resets+1); // hidden source never recreated
  gone=true;
  for(int i=0;i<10;i++)observe(@[rear]);
  assert(destroys==1&&!table_find(&g_windows,&wid)); // Lost DESTROY notification.
  observe(@[window(7,202,1),window(8,100,1)]);
  assert(creates==1&&table_find(&g_windows,&(uint32_t){7})); // Lost CREATE.
  observe(@[window(7,202,1),window(8,100,1)]);assert(creates==1);
  // A Space-removal/CLOSE race must not permanently lose a still-visible ID.
  uint32_t transferred=7;
  table_remove(&g_windows,&transferred);
  observe(@[window(7,202,1),window(8,100,1)]);
  assert(creates==2 && table_find(&g_windows,&transferred));
  observe(@[window(7,202,1),window(8,100,1)]); assert(creates==2);
  table_free(&g_windows);free(previous_snapshot);previous_snapshot=NULL;previous_count=0;free(live_ids);
  puts("PASS: missing create/hide/show/destroy, transparent source, geometry, above/below depth, failed snapshot");
  test_adaptive();
}return 0;}
