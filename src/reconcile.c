#include "reconcile.h"
#include "windows.h"
#include "misc/extern.h"
#include "misc/knit.h"
#include <math.h>

extern struct table g_windows;
extern pid_t g_pid;
static const double snapshot_interval = .05;
// WindowServer may round presentation bounds outward during app transitions.
static const double presentation_size_tolerance = 2;

static bool presentation_size_matches(CGSize observed, CGSize expected) {
  return fabs(observed.width - expected.width) <= presentation_size_tolerance
      && fabs(observed.height - expected.height) <= presentation_size_tolerance;
}

struct observed_window {
  uint32_t wid;
  pid_t pid;
  CGRect bounds;
  double alpha;
  int layer;
  bool just_created;
  bool tracked;
  CFIndex rank;
  CFIndex previous_foreign, next_foreign;
};
static struct observed_window* previous_snapshot;
static size_t previous_count;
static uint32_t* live_ids;
static size_t live_count;
static CFAbsoluteTime live_ids_expiry;

static int compare_id(const void* a, const void* b) {
  uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
  return (x > y) - (x < y);
}

static bool window_exists(uint32_t wid) {
  // Bounds and owner helpers may still return cached data after a missed
  // DESTROY. Query the complete server list, including legitimate hidden windows.
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  if (now >= live_ids_expiry) {
    CFArrayRef all = CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, 0);
    if (!all) return true;
    CFIndex count = CFArrayGetCount(all);
    uint32_t* ids = calloc((size_t)count + 1, sizeof(*ids));
    if (!ids) { CFRelease(all); return true; }
    size_t valid = 0;
    for (CFIndex i = 0; i < count; i++) {
      CFDictionaryRef info = CFArrayGetValueAtIndex(all, i);
      CFNumberRef number = CFDictionaryGetValue(info, kCGWindowNumber);
      if (number && CFNumberGetValue(number, kCFNumberSInt32Type, ids + valid)) valid++;
    }
    CFRelease(all);
    qsort(ids, valid, sizeof(*ids), compare_id);
    free(live_ids); live_ids = ids; live_count = valid;
    live_ids_expiry = now + .25; // Share one liveness query across hidden targets.
  }
  return bsearch(&wid, live_ids, live_count, sizeof(*live_ids), compare_id) != NULL;
}

static int compare_window(const void* a, const void* b) {
  uint32_t x = ((const struct observed_window*)a)->wid;
  uint32_t y = ((const struct observed_window*)b)->wid;
  return (x > y) - (x < y);
}

static const struct observed_window* find_window(struct observed_window* windows,
                                                 size_t count, uint32_t wid) {
  struct observed_window key = {.wid = wid};
  return bsearch(&key, windows, count, sizeof(*windows), compare_window);
}

void windows_reconcile_snapshot(struct table* windows, CFArrayRef snapshot) {
  assert(pthread_main_np());
  if (!snapshot) return;
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  bool mouse_held = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
  // Opt-in, short-lived transfer diagnostics: IDs and geometry only, no titles.
  static CFAbsoluteTime trace_after, trace_end;
  bool trace = false;
  if (getenv("KNIT_DISPLAY_TRACE")) {
    if (!trace_end) trace_end = now + 600;
    trace = now < trace_end && now >= trace_after;
    if (trace) trace_after = now + .25;
  }
  CFIndex count = CFArrayGetCount(snapshot);
  struct observed_window* observed = calloc((size_t)count + 1, sizeof(*observed));
  if (!observed) return;
  size_t valid = 0;
  CFIndex previous_foreign = -1;
  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef info = CFArrayGetValueAtIndex(snapshot, i);
    struct observed_window w = {.rank = i, .alpha = 1, .previous_foreign = previous_foreign,
                                 .next_foreign = count};
    CFNumberRef number = CFDictionaryGetValue(info, kCGWindowNumber);
    CFNumberRef pid = CFDictionaryGetValue(info, kCGWindowOwnerPID);
    CFDictionaryRef bounds = CFDictionaryGetValue(info, kCGWindowBounds);
    if (!number || !pid || !bounds
        || !CFNumberGetValue(number, kCFNumberSInt32Type, &w.wid)
        || !CFNumberGetValue(pid, kCFNumberSInt32Type, &w.pid)
        || !CGRectMakeWithDictionaryRepresentation(bounds, &w.bounds)) continue;
    CFNumberRef alpha = CFDictionaryGetValue(info, kCGWindowAlpha);
    if (alpha) CFNumberGetValue(alpha, kCFNumberDoubleType, &w.alpha);
    CFNumberRef layer = CFDictionaryGetValue(info, kCGWindowLayer);
    if (layer) CFNumberGetValue(layer, kCFNumberIntType, &w.layer);
    observed[valid++] = w;
    if (w.pid != g_pid && w.alpha > 0) previous_foreign = i;
  }
  CFIndex next_foreign = count;
  for (size_t i = valid; i-- > 0;) {
    observed[i].next_foreign = next_foreign;
    if (observed[i].pid != g_pid && observed[i].alpha > 0) next_foreign = observed[i].rank;
  }
  qsort(observed, valid, sizeof(*observed), compare_window);

  // Recover a missed CREATE without repeatedly querying every unsuitable
  // panel. Only newly visible normal-level foreign windows need discovery.
  for (size_t i = 0; i < valid; i++) {
    struct observed_window* source = observed + i;
    if (source->pid == g_pid || source->layer != 0 || source->alpha <= 0
        || table_find(windows, &source->wid)) continue;
    const struct observed_window* previous = previous_count
      ? find_window(previous_snapshot, previous_count, source->wid) : NULL;
    if (!previous || previous->alpha <= 0 || previous->tracked)
      source->just_created = windows_window_create(windows, source->wid,
                                                   window_space_id(SLSMainConnectionID(), source->wid));
  }

  for (int i = 0; i < windows->capacity; i++) {
    for (struct bucket* bucket = windows->buckets[i]; bucket;) {
      struct bucket* next = bucket->next;
      struct border* border = bucket->value;
      if (!border || border->is_proxy || border->external_proxy_wid) { bucket = next; continue; }
      uint32_t wid = border->target_wid;
      const struct observed_window* source = find_window(observed, valid, wid);
      const struct observed_window* overlay = find_window(observed, valid, border->wid);
      if (trace) {
        CGRect model = CGRectZero;
        SLSGetWindowBounds(border->cid, wid, &model);
        CGRect shown = source ? source->bounds : CGRectZero;
        CGRect ring = overlay ? overlay->bounds : CGRectZero;
        fprintf(stderr, "DISPLAY %.2f app=%s id=%u overlay=%u source=%d ring=%d visible=%d resize=%d transform=%d space=%llu/%llu/%llu model=%.0f,%.0f,%.0f,%.0f shown=%.0f,%.0f,%.0f,%.0f ringbounds=%.0f,%.0f,%.0f,%.0f\n",
          now, border->app, wid, border->wid, source != NULL, overlay != NULL,
          border->visible, border->resize_suppressed, border->native_transform,
          border->sid, window_space_id(border->cid,wid), window_space_id(border->cid,border->wid),
          model.origin.x,model.origin.y,model.size.width,model.size.height,
          shown.origin.x,shown.origin.y,shown.size.width,shown.size.height,
          ring.origin.x,ring.origin.y,ring.size.width,ring.size.height);
      }
      if (source && source->just_created) { bucket = next; continue; }
      if (!source || source->alpha <= 0) {
        border->missing_overlay_snapshots = 0;
        // Ordered-in flags alone can outlive actual onscreen visibility. Hide
        // first; keep a hidden/minimized target available for restoration.
        if (border->visible || (overlay && overlay->alpha > 0)) windows_window_hide(windows, wid);
        if (++border->missing_snapshots >= 10) {
          border->missing_snapshots = 0;
          if (!window_exists(wid)) windows_window_destroy(windows, wid, 0);
        }
        bucket = next;
        continue;
      }
      border->missing_snapshots = 0;
      if (now >= border->space_check_after) {
        border->space_check_after = now + .20;
        border_refresh_space(border);
      }
      // A drag may change presentation bounds before model bounds update.
      // Stage Manager also changes presentation bounds when switching apps.
      if (!border->resize_suppressed && mouse_held
          && !presentation_size_matches(source->bounds.size, border->drawing_bounds.size)
          && border_suppress_live_resize(border, source->bounds)) {
        bucket = next;
        continue;
      }
      // Match the notification path's model geometry. Presentation bounds can
      // shrink during open/close animations; mixing the two makes the border
      // bounce between sizes and repeatedly repaints it.
      CGRect geometry = border->target_bounds;
      if (SLSGetWindowBounds(border->cid, wid, &geometry) != kCGErrorSuccess) {
        bucket = next;
        continue;
      }
      // Do not alternate presentation/model sizes in the settling timer.
      // Wait for them to agree before restoring a suppressed resize border.
      bool settled = presentation_size_matches(source->bounds.size, geometry.size);
      if (border->resize_suppressed
          && (!settled || border_suppress_live_resize(border, geometry))) {
        bucket = next;
        continue;
      }
      // A separate ring cannot undergo the Dock's nonrectangular Genie
      // transform. Hide it while both presentation dimensions are scaled;
      // restore when the target returns to its normal rectangular frame.
      bool scaled = geometry.size.width > 0 && geometry.size.height > 0
        && fabs(source->bounds.size.width / geometry.size.width - 1) > .08
        && fabs(source->bounds.size.height / geometry.size.height - 1) > .08;
      border->native_transform = scaled || (border->native_transform && !settled);
      if (border->native_transform) {
        if (border->visible) windows_window_hide(windows, wid);
        bucket = next;
        continue;
      }
      // A successful order transaction is not proof that an overlay became
      // onscreen. Try the cheap restore first, then replace a stranded surface
      // after three settled snapshots. Never rebuild during resize/animation,
      // while the source is hidden, or more than once per second.
      if (!overlay || overlay->alpha <= 0) {
        if (++border->missing_overlay_snapshots >= 3 && now >= border->overlay_rebuild_after) {
          border_reset_surface(border);
          border->missing_overlay_snapshots = 0;
          border->overlay_rebuild_after = now + 1;
          border->space_check_after = 0;
        }
      } else border->missing_overlay_snapshots = 0;
      if (!border->visible || !overlay || overlay->alpha <= 0) {
        border->metadata_dirty = true;
        border_update_geometry_from_snapshot(border, geometry, source->alpha);
      } else if (!CGRectEqualToRect(geometry, border->target_bounds)
                 || fabs(source->alpha - border->opacity) > .001
                 || !border->geometry_valid || border->needs_redraw || border->metadata_dirty) {
        border_update_geometry_from_snapshot(border, geometry, source->alpha);
      } else {
        int order = border_get_settings(border)->border_order;
        bool placed = order == BORDER_ORDER_BELOW
          ? overlay->rank > source->rank && overlay->rank < source->next_foreign
          : overlay->rank < source->rank && overlay->rank > source->previous_foreign;
        if (!placed) border_reorder(border);
      }
      bucket = next;
    }
  }
  // Remember which visible candidates were accepted. A Space-removal event
  // can retire a tracked entry while its window stays in consecutive snapshots.
  // Reconsider those entries; continue caching rejected panels to avoid AX work.
  for (size_t i = 0; i < valid; i++)
    observed[i].tracked = table_find(windows, &observed[i].wid) != NULL;
  free(previous_snapshot);
  previous_snapshot = observed;
  previous_count = valid;
}

static CFRunLoopTimerRef reconcile_timer;

void windows_reconcile_start(void) {
  if (reconcile_timer) return;
  reconcile_timer = CFRunLoopTimerCreateWithHandler(NULL, CFAbsoluteTimeGetCurrent() + snapshot_interval,
                                                   snapshot_interval, 0, 0, ^(CFRunLoopTimerRef timer) {
    static bool pending = false; // Main-run-loop owned; at most one snapshot in flight.
    if (g_knit_on && !pending) {
      pending = true;
      dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        // Window-list collection is read-only and can take several milliseconds.
        // Keep that IPC/dictionary work off the geometry/painting run loop.
        CFArrayRef snapshot = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly
                                                        | kCGWindowListExcludeDesktopElements, 0);
        dispatch_async(dispatch_get_main_queue(), ^{
          if (g_knit_on) windows_reconcile_snapshot(&g_windows, snapshot);
          if (snapshot) CFRelease(snapshot);
          pending = false;
        });
      });
    }
    CFRunLoopTimerSetNextFireDate(timer, CFAbsoluteTimeGetCurrent() + (g_knit_on ? snapshot_interval : 1));
  });
  CFRunLoopAddTimer(CFRunLoopGetMain(), reconcile_timer, kCFRunLoopCommonModes);
}
