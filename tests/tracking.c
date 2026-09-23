// Run with make test.
// Fault-inject the WindowServer boundary while executing the real border code.
#define CGEventSourceButtonState mock_button
#define CFAbsoluteTimeGetCurrent mock_time
#define SLSGetWindowBounds mock_bounds
#define SLSWindowIsOrderedIn mock_shown
#define SLSTransactionCreate mock_transaction
#define SLSTransactionMoveWindowWithGroup mock_move
#define SLSTransactionOrderWindow mock_order
#define SLSTransactionCommit mock_commit
#define SLSTransactionSetWindowTransform mock_transform
#define SLSTransactionSetWindowLevel mock_level
#define SLSTransactionSetWindowSubLevel mock_sublevel
#define SLSDisableUpdate mock_disable
#define SLSReenableUpdate mock_enable
#define SLSWindowFreezeWithOptions mock_freeze
#define SLSWindowThaw mock_thaw
#define SLSSetWindowShape mock_shape
#define SLWindowContextCreate mock_context
#define CGSNewRegionWithRect mock_region
#define SLSSetWindowTags mock_tags
#define SLSClearWindowTags mock_clear_tags
#define SLSFlushWindowContentRegion mock_flush
#define SLSSetWindowAlpha mock_alpha
#define SLSReleaseWindow mock_release_window
#include "../src/border.h"
static int mock_window_level(int cid, uint32_t wid);
static int mock_window_sub_level(int cid, uint32_t wid);
static bool mock_space_visible(int cid, uint64_t sid);
static uint64_t mock_window_space(int cid, uint32_t wid);
static void mock_send_space(int cid, uint32_t wid, uint64_t sid);
#define window_space_id mock_window_space
#define window_send_to_space mock_send_space
#define window_level mock_window_level
#define window_sub_level mock_window_sub_level
#define is_space_visible mock_space_visible
#include "../src/border.c"

struct settings g_settings = {.border_width = 10, .border_style = BORDER_STYLE_KNIT,
                              .border_order = BORDER_ORDER_BELOW};
struct knit_gauge g_knit;
bool g_knit_on = false;
float g_knit_dim;
int g_chart_active;
mach_port_t g_server_port;
static int draws;
static CGRect last_knit_rect;
static float last_knit_radius, last_knit_width;
uint32_t knit_color_for_app(const char* app) { return 0; }
const struct app_rule* knit_app_rule(const char* app) { return NULL; }
int knit_chart_index(const char* name) { return -1; }
int knit_pattern_for_app(const char* name) { return -1; }
void knit_draw(CGContextRef c, CGRect r, float radius, float width, uint32_t color,
               int chart, float dim, float tuck) {
  draws++; last_knit_rect = r; last_knit_radius = radius; last_knit_width = width;
}
void knit_draw_inside(CGContextRef c, CGRect r, float radius, float width,
                      uint32_t color, int chart, float dim) {
  draws++; last_knit_rect = r; last_knit_radius = radius; last_knit_width = width;
}

static int released_surfaces;
CGError mock_release_window(int cid, uint32_t wid) { assert(cid==1 && wid==2); released_surfaces++; return kCGErrorSuccess; }
static bool mouse_held;
static CFAbsoluteTime test_time = 1;
bool mock_button(CGEventSourceStateID state, CGMouseButton button) { return mouse_held; }
CFAbsoluteTime mock_time(void) { return test_time; }
static CGRect target;
static CGPoint moved_to;
static int moves, shapes, disabled, frozen, context_creations;
static int bounds_queries, flushes, orders, commits, levels, sublevels;
static int level_queries, sublevel_queries, server_level = 11, server_sublevel = 23;
static int applied_level, applied_sublevel, applied_order;
static uint32_t level_wid, sublevel_wid, ordered_wid, relative_wid;
static uint32_t hidden_wid, failed_shown_wid;
static bool fail_bounds, fail_transaction, fail_shape, fail_context, opaque_commit_return, space_hidden;
static int mock_window_level(int cid, uint32_t wid) {
  assert(cid == 1 && wid == 3);
  level_queries++;
  return server_level;
}
static int mock_window_sub_level(int cid, uint32_t wid) {
  assert(cid == 1 && wid == 3);
  sublevel_queries++;
  return server_sublevel;
}
static uint64_t source_space = 1, overlay_space = 1;
static int space_moves;
static bool fail_space_move;
static uint64_t mock_window_space(int cid, uint32_t wid) {
  return wid == 3 ? source_space : overlay_space;
}
static void mock_send_space(int cid, uint32_t wid, uint64_t sid) {
  assert(wid == 2); // only the owned border may be reassigned
  space_moves++;
  if (!fail_space_move) overlay_space = sid;
}
static bool mock_space_visible(int cid, uint64_t sid) { return !space_hidden; }
CGError mock_bounds(int cid, uint32_t wid, CGRect* out) {
  bounds_queries++;
  if (fail_bounds) return kCGErrorFailure;
  *out = target; return kCGErrorSuccess;
}
CGError mock_shown(int cid, uint32_t wid, bool* out) {
  if (wid == failed_shown_wid) return kCGErrorFailure;
  *out = wid != hidden_wid;
  return kCGErrorSuccess;
}
CFTypeRef mock_transaction(int cid) { return fail_transaction ? NULL : CFRetain(CFSTR("transaction")); }
CGError mock_move(CFTypeRef t, uint32_t wid, CGPoint p) { moves++; moved_to = p; return 0; }
CGError mock_order(CFTypeRef t, uint32_t wid, int order, uint32_t relative) {
  orders++; ordered_wid = wid; applied_order = order; relative_wid = relative;
  return 0;
}
CGError mock_commit(CFTypeRef t, int sync) { commits++; return opaque_commit_return ? kCGErrorFailure : 0; }
CGError mock_transform(CFTypeRef t, uint32_t wid, int a, int b, CGAffineTransform tx) { return 0; }
CGError mock_level(CFTypeRef t, uint32_t wid, int level) {
  levels++; level_wid = wid; applied_level = level; return 0;
}
CGError mock_sublevel(CFTypeRef t, uint32_t wid, int level) {
  sublevels++; sublevel_wid = wid; applied_sublevel = level; return 0;
}
CGError mock_disable(int cid) { disabled++; return 0; }
CGError mock_enable(int cid) { disabled--; return 0; }
CGError mock_freeze(int cid, uint32_t wid, CFTypeRef options) { frozen++; return 0; }
CGError mock_thaw(int cid, uint32_t wid) { if (frozen) frozen--; return 0; }
CGError mock_shape(int cid, uint32_t wid, float x, float y, CFTypeRef region) {
  shapes++; return fail_shape ? kCGErrorFailure : kCGErrorSuccess;
}
CGContextRef mock_context(int cid, uint32_t wid, CFDictionaryRef options) {
  context_creations++;
  if (fail_context) return NULL;
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(NULL, 512, 512, 8, 0, color_space,
                                               kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(color_space);
  return context;
}
CGError mock_region(CGRect* rect, CFTypeRef* out) { *out = CFRetain(CFSTR("region")); return 0; }
CGError mock_tags(int cid, uint32_t wid, uint64_t* tags, int size) { return 0; }
CGError mock_clear_tags(int cid, uint32_t wid, uint64_t* tags, int size) { return 0; }
CGError mock_flush(int cid, uint32_t wid, void* region) { flushes++; return 0; }
CGError mock_alpha(int cid, uint32_t wid, float alpha) { return 0; }


static void check_reorder(struct border* border, int expected_queries, int expected_orders) {
  int old_moves = moves, old_shapes = shapes, old_contexts = context_creations;
  int old_bounds = bounds_queries, old_draws = draws, old_flushes = flushes;
  int old_orders = orders, old_commits = commits, old_levels = levels, old_sublevels = sublevels;
  int old_level_queries = level_queries, old_sublevel_queries = sublevel_queries;
  int old_level = border->level, old_sublevel = border->sub_level;
  bool old_dirty = border->metadata_dirty, old_redraw = border->needs_redraw;
  CGRect old_frame = border->frame, old_drawing = border->drawing_bounds;
  CGPoint old_origin = border->origin;
  CGContextRef old_context = border->context;
  border_reorder(border);
  assert(level_queries == old_level_queries + expected_queries);
  assert(sublevel_queries == old_sublevel_queries + expected_queries);
  assert(orders == old_orders + expected_orders && commits == old_commits + expected_orders);
  int metadata_updates = expected_orders && expected_queries;
  assert(levels == old_levels + metadata_updates && sublevels == old_sublevels + metadata_updates);
  if (!expected_orders)
    assert(border->level == old_level && border->sub_level == old_sublevel);
  assert(moves == old_moves && shapes == old_shapes && context_creations == old_contexts);
  assert(bounds_queries == old_bounds && draws == old_draws && flushes == old_flushes);
  assert(border->metadata_dirty == old_dirty && border->needs_redraw == old_redraw);
  assert(CGRectEqualToRect(border->frame, old_frame));
  assert(CGRectEqualToRect(border->drawing_bounds, old_drawing));
  assert(CGPointEqualToPoint(border->origin, old_origin) && border->context == old_context);
  assert(!disabled && !frozen);
}

static void check_display_transfer(void) {
  struct border b = {.cid=1, .wid=2, .target_wid=3, .sid=1};
  pthread_mutex_init(&b.mutex, NULL);
  border_refresh_space(&b);
  assert(space_moves == 0 && !b.metadata_dirty);
  // Both desktops remain visible; no geometry or Space notification arrives.
  source_space = 2;
  border_refresh_space(&b);
  assert(space_moves == 1 && overlay_space == 2 && b.sid == 2 && b.metadata_dirty);
  assert(b.needs_redraw && !b.geometry_valid);
  b.metadata_dirty = false;
  border_refresh_space(&b); assert(space_moves == 1 && !b.metadata_dirty);
  // Failed transfer: updating the cache must not prevent a subsequent retry.
  source_space = 3; fail_space_move = true;
  border_refresh_space(&b); assert(space_moves == 2 && overlay_space == 2);
  fail_space_move = false; b.metadata_dirty = false;
  border_refresh_space(&b);
  assert(space_moves == 3 && overlay_space == 3 && b.metadata_dirty);
  source_space = 0; b.metadata_dirty = false;
  border_refresh_space(&b); assert(space_moves == 3 && b.sid == 3 && !b.metadata_dirty);
  source_space = 1; b.sticky = true;
  border_refresh_space(&b); assert(space_moves == 3);
  b.sticky = false; b.external_proxy_wid = 9;
  border_refresh_space(&b); assert(space_moves == 3);
  b.external_proxy_wid = 0; b.opacity = .2;
  border_reset_surface(&b);
  assert(released_surfaces==1 && !b.wid && !b.context && !b.visible);
  assert(b.needs_redraw && b.metadata_dirty && !b.geometry_valid && b.opacity==1);
  assert(b.target_wid==3); // never close or replace the application window
  pthread_mutex_destroy(&b.mutex);
  source_space = overlay_space = 1;
  puts("PASS: display Space transfer, same-Space no-op, retry after failed transfer, unknown Space and proxy guards");
}

int main(void) {
  check_display_transfer();
  struct border border;
  border_init(&border, 1);
  border.metadata_dirty = false;
  border.geometry_valid = true; // This fixture starts with an existing surface.
  border.sticky = true;
  border.wid = 2;
  border.target_wid = 3;
  border.radius = 9;
  border.inner_radius = 10;
  border.context = mock_context(1, 2, NULL);
  border.frame = CGRectMake(0, 0, 200, 150);
  border.drawing_bounds = CGRectMake(0, 0, 200, 150);
  target = CGRectMake(100, 90, 200, 150);

  border_update_geometry(&border);
  assert(moves == 1 && shapes == 0 && moved_to.x == 100 && moved_to.y == 90);
  // A size change arriving via MOVE must reshape, including a top-left resize.
  target = CGRectMake(80, 70, 220, 170);
  border_update_geometry(&border);
  assert(shapes == 1 && moves == 2);
  assert(CGSizeEqualToSize(border.drawing_bounds.size, target.size));
  assert(moved_to.x == 80 && moved_to.y == 70 && !disabled && !frozen);

  // The matching RESIZE and settling check see the same bounds. Neither may
  // submit another transaction, reshape, rebind or paint the existing sweater.
  int old_moves = moves, old_shapes = shapes, old_contexts = context_creations;
  int old_commits = commits, old_flushes = flushes, old_bounds = bounds_queries;
  border_update_geometry(&border);
  border_update_geometry(&border);
  assert(bounds_queries == old_bounds + 2);
  assert(moves == old_moves && shapes == old_shapes && commits == old_commits);
  assert(context_creations == old_contexts && flushes == old_flushes);

  // A missing transaction must not leave the display update lock held.
  target.size.width = 250;
  fail_transaction = true;
  border_update_geometry(&border);
  assert(!disabled && !frozen && border.needs_redraw && !border.geometry_valid);
  fail_transaction = false;
  fail_shape = true;
  border_update_geometry(&border);
  assert(!disabled && !frozen && border.frame.size.width == 220);
  fail_shape = false;
  fail_context = true;
  border_update_geometry(&border);
  assert(!disabled && !frozen && border.context && border.needs_redraw);
  fail_context = false;
  border_update_geometry(&border);
  assert(border.frame.size.width == 250 && !disabled && !frozen && !border.needs_redraw && border.geometry_valid);

  // Explicit appearance work still runs at unchanged geometry.
  old_flushes = flushes;
  g_knit_on = true;
  border.needs_redraw = true;
  border_update_geometry(&border);
  assert(flushes == old_flushes + 1 && !border.needs_redraw);
  // The painted ring's outer edge is exactly the native window rectangle.
  assert(CGRectEqualToRect(CGRectInset(last_knit_rect, -last_knit_width, -last_knit_width),
                           border.drawing_bounds));
  assert(last_knit_radius == border.radius);
  g_knit_on = false;

  // Hiding invalidates the shortcut, so unchanged bounds can be restored.
  border_hide(&border);
  assert(!border.geometry_valid);
  old_commits = commits;
  border_update_geometry(&border);
  assert(commits == old_commits + 1 && border.geometry_valid);

  // An authoritative snapshot must not be replaced with stale cached bounds.
  CGRect fresh = CGRectMake(60, 50, 260, 190);
  old_bounds = bounds_queries;
  border_update_geometry_from_snapshot(&border, fresh, 1);
  assert(bounds_queries == old_bounds && CGRectEqualToRect(border.target_bounds, fresh));
  old_flushes = flushes;
  border_update_geometry_from_snapshot(&border, fresh, .25);
  assert(border.opacity == .25 && flushes == old_flushes);

  // Real dragging differs from programmatic setFrame: suppress all drawing
  // while the button remains down, even if the user pauses mid-resize.
  target = fresh;
  g_knit_on = true;
  mouse_held = true;
  target.origin.x += 10; // Moving the window without resizing stays visible.
  border_update_geometry(&border);
  assert(border.visible && !border.resize_suppressed);
  target.size.height += 30;
  old_flushes = flushes;
  border_update_geometry(&border);
  assert(border.resize_suppressed && !border.visible && flushes == old_flushes);
  test_time += 1;
  border_update_geometry_from_snapshot(&border, target, 1);
  border_update(&border, false);
  border_unhide(&border);
  border_reorder(&border);
  assert(!border.visible && flushes == old_flushes);
  mouse_held = false;
  test_time += .03;
  border_update_geometry(&border);
  assert(!border.visible && flushes == old_flushes);
  target.size.height += 2; // Final geometry can land after mouse-up.
  border_update_geometry(&border);
  test_time += .07;
  border_update_geometry(&border);
  assert(border.visible && !border.resize_suppressed && flushes == old_flushes + 1);
  assert(CGRectEqualToRect(border.target_bounds, target));

  // Some apps deliver the first size update only after mouse-up. Hide that
  // intermediate frame too, then restore after the same stable-size interval.
  old_flushes = flushes;
  target.size.width += 12;
  border_update_geometry(&border);
  assert(border.resize_suppressed && !border.visible && flushes == old_flushes);
  test_time += .07;
  border_update_geometry(&border);
  assert(border.visible && !border.resize_suppressed && flushes == old_flushes + 1);
  g_knit_on = false;

  // Late geometry notifications must not resurrect a ring during Genie.
  border.native_transform = true;
  old_bounds = bounds_queries;
  border_update_geometry(&border);
  assert(!border.visible && bounds_queries == old_bounds);
  border.native_transform = false;

  // Window disappearance must not read uninitialized geometry or move a ghost.
  fail_bounds = true;
  int previous_moves = moves;
  border_update_geometry(&border);
  border_update(&border, true);
  assert(moves == previous_moves && !disabled && !frozen);
  fail_bounds = false;
  target.size.width = NAN;
  border_update(&border, true);
  assert(moves == previous_moves && !disabled && !frozen);

  // Repair stale depth independently of focus or geometry, even with pending
  // metadata/redraw work. Neither pending flag belongs to the depth repair.
  border.metadata_dirty = true;
  border.needs_redraw = true;
  border.level = -7;
  border.sub_level = -9;
  check_reorder(&border, 1, 1);
  assert(border.level == 11 && border.sub_level == 23);
  assert(applied_level == 11 && applied_sublevel == 23 && level_wid == 2 && sublevel_wid == 2);
  assert(ordered_wid == 2 && relative_wid == 3 && applied_order == BORDER_ORDER_ABOVE);
  // A window override, including above/below preference, remains authoritative.
  border.setting_override = g_settings;
  border.setting_override.enabled = true;
  border.setting_override.border_order = BORDER_ORDER_ABOVE;
  server_level = 14;
  server_sublevel = 27;
  check_reorder(&border, 1, 1);
  assert(border.level == 14 && border.sub_level == 27 && applied_order == BORDER_ORDER_ABOVE);
  border.setting_override.enabled = false;

  hidden_wid = 3; check_reorder(&border, 0, 1); assert(applied_order == 0);
  hidden_wid = 2; check_reorder(&border, 0, 0);
  hidden_wid = 0;
  failed_shown_wid = 3; check_reorder(&border, 0, 1); assert(applied_order == 0);
  failed_shown_wid = 2; check_reorder(&border, 0, 0);
  failed_shown_wid = 0;
  border.too_small = true; check_reorder(&border, 0, 0); border.too_small = false;
  CGContextRef saved_context = border.context;
  border.context = NULL; check_reorder(&border, 0, 0); border.context = saved_context;
  border.wid = 0; check_reorder(&border, 0, 0); border.wid = 2;
  border.is_proxy = true; check_reorder(&border, 0, 0); border.is_proxy = false;
  border.external_proxy_wid = 99; check_reorder(&border, 0, 0); border.external_proxy_wid = 0;
  border.sticky = false; space_hidden = true; check_reorder(&border, 0, 0);
  border.sticky = true; space_hidden = false;
  server_level = 19; server_sublevel = 31;
  fail_transaction = true;
  check_reorder(&border, 1, 0);
  assert(border.level == 14 && border.sub_level == 27);
  fail_transaction = false;
  // Private commit calls can return a nonzero value even when ordering applies.
  opaque_commit_return = true;
  check_reorder(&border, 1, 1);
  assert(border.level == 19 && border.sub_level == 31);
  opaque_commit_return = false;
  check_reorder(&border, 1, 1);
  assert(border.level == 19 && border.sub_level == 31);
  CGContextRelease(border.context);
  pthread_mutex_destroy(&border.mutex);
  puts("PASS: movement/resize, failure recovery, isolated depth repair, visibility/proxy guards, transaction failures");
  return 0;
}
