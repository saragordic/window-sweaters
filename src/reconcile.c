#include "reconcile.h"
#include "windows.h"
#include "misc/extern.h"
#include "misc/knit.h"
#include <math.h>

extern struct table g_windows;
extern pid_t g_pid;
static const double snapshot_interval = .05;

// Weak so test targets that compile this file without the AppKit module get a
// deterministic no-op, exactly like border.c's knit_auto_yarn stubs.
// src/menubar.m provides the real implementations in the app build.
__attribute__((weak)) bool knit_power_low(void) { return false; }
__attribute__((weak)) bool knit_screens_asleep(void) { return false; }
__attribute__((weak)) void knit_set_fast(bool fast) { (void)fast; }

static const double reconcile_fast_interval = .05;
// The slow heartbeat must still fall inside a Dock minimize/unminimize
// animation, which emits no window-server events at all: the snapshot is the
// only signal those transitions ever produce, so this cap bounds how late
// the ring can react to one.
static const double reconcile_slow_interval = .2;
static double g_reconcile_interval = .05;
static int g_quiet_streak = 0;
static bool g_reconcile_kick = false;
// Refreshed from the weak hooks at the top of every timer pass.
static bool g_power_low = false;
static bool g_screens_asleep = false;

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

bool windows_reconcile_snapshot(struct table* windows, CFArrayRef snapshot) {
  assert(pthread_main_np());
  if (!snapshot) return false;
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
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
  if (!observed) return false;
  bool worked = false;
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
    if (!previous || previous->alpha <= 0 || previous->tracked) {
      source->just_created = windows_window_create(windows, source->wid,
                                                   window_space_id(SLSMainConnectionID(), source->wid));
      if (source->just_created) worked = true;
    }
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
        if (border->visible || (overlay && overlay->alpha > 0)) {
          windows_window_hide(windows, wid);
          worked = true;
        }
        if (++border->missing_snapshots >= 10) {
          border->missing_snapshots = 0;
          if (!window_exists(wid)) {
            windows_window_destroy(windows, wid, 0);
            worked = true;
          }
        }
        bucket = next;
        continue;
      }
      border->missing_snapshots = 0;
      if (now >= border->space_check_after) {
        border->space_check_after = now + .20;
        if (border_refresh_space(border)) worked = true;
      }
      // Use observed size here: an app may defer model bounds/notifications
      // while the user is inside its native mouse-tracking resize loop.
      if (!border->resize_suppressed && border_suppress_live_resize(border, source->bounds)) {
        worked = true;
        bucket = next;
        continue;
      }
      // The snapshot already carries presentation bounds and alpha; a
      // synchronous model-geometry query is only needed when something could
      // have moved, or periodically, as a drift backstop. A hidden ring is
      // included too: restoring it uses model geometry, and a lost UNHIDE can
      // leave the overlay listed at the same bounds.
      bool needs_model = border->resize_suppressed
        || !border->visible
        || !border->geometry_valid || border->needs_redraw || border->metadata_dirty
        || border->native_transform
        || !overlay || overlay->alpha <= 0
        // Tolerant, like the settle checks: a scaled display can carry a
        // persistent sub-pixel offset between presentation and model bounds,
        // and an exact compare would disable the skip on it forever.
        || fabs(source->bounds.origin.x - border->target_bounds.origin.x) > 1
        || fabs(source->bounds.origin.y - border->target_bounds.origin.y) > 1
        || fabs(source->bounds.size.width - border->target_bounds.size.width) > 1
        || fabs(source->bounds.size.height - border->target_bounds.size.height) > 1
        || fabs(source->alpha - border->opacity) > .001;
      if (!needs_model && ++border->quiet_audit_count >= 16) {
        needs_model = true;    // periodic audit catches model/presentation drift
      }
      if (needs_model) {
        border->quiet_audit_count = 0;
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
        if (border->resize_suppressed) {
          bool drifting = fabs(source->bounds.size.width - geometry.size.width) > 1
                       || fabs(source->bounds.size.height - geometry.size.height) > 1;
          // Sizes that already disagree are still the in-progress resize, so
          // the mouse query runs only once sizes agree, as the old short-circuit did.
          bool still_resizing = !drifting && border_suppress_live_resize(border, geometry);
          if (drifting || still_resizing) {
            if (still_resizing) worked = true;
            bucket = next;
            continue;
          }
        }
        // A separate ring cannot undergo the Dock's nonrectangular Genie
        // transform. Hide it as soon as EITHER presentation dimension is
        // scaled: the genie shrinks one axis first, and waiting for both
        // leaves the full-size ring floating over the animation. Restore
        // only when both dimensions have settled back to within 1 pt.
        bool scaled = geometry.size.width > 0 && geometry.size.height > 0
          && (fabs(source->bounds.size.width / geometry.size.width - 1) > .08
              || fabs(source->bounds.size.height / geometry.size.height - 1) > .08);
        bool settled = fabs(source->bounds.size.width - geometry.size.width) <= 1
          && fabs(source->bounds.size.height - geometry.size.height) <= 1;
        border->native_transform = scaled || (border->native_transform && !settled);
        if (border->native_transform) {
          if (border->visible) {
            windows_window_hide(windows, wid);
            worked = true;
          }
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
            worked = true;
            border->missing_overlay_snapshots = 0;
            border->overlay_rebuild_after = now + 1;
            border->space_check_after = 0;
          }
        } else border->missing_overlay_snapshots = 0;
        if (!border->visible || !overlay || overlay->alpha <= 0) {
          border->metadata_dirty = true;
          border_update_geometry_from_snapshot(border, geometry, source->alpha);
          worked = true;
        } else if (!CGRectEqualToRect(geometry, border->target_bounds)
                   || fabs(source->alpha - border->opacity) > .001
                   || !border->geometry_valid || border->needs_redraw || border->metadata_dirty) {
          border_update_geometry_from_snapshot(border, geometry, source->alpha);
          worked = true;
        } else {
          int order = border_get_settings(border)->border_order;
          bool placed = order == BORDER_ORDER_BELOW
            ? overlay->rank > source->rank && overlay->rank < source->next_foreign
            : overlay->rank < source->rank && overlay->rank > source->previous_foreign;
          if (!placed) {
            border_reorder(border);
            worked = true;
          }
        }
      } else {
        // The model path clears this when it sees the overlay. The skip path
        // must too, or a returned ring leaves a stale missing-overlay count.
        border->missing_overlay_snapshots = 0;
        int order = border_get_settings(border)->border_order;
        bool placed = order == BORDER_ORDER_BELOW
          ? overlay->rank > source->rank && overlay->rank < source->next_foreign
          : overlay->rank < source->rank && overlay->rank > source->previous_foreign;
        if (!placed) {
          border_reorder(border);
          worked = true;
        }
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
  return worked;
}

static CFRunLoopTimerRef reconcile_timer;

void reconcile_kick(void) {
  g_reconcile_kick = true;
  // A kick must not wait out the slow cadence: pull the next pass forward.
  // The timer is null in tests and before windows_reconcile_start.
  if (reconcile_timer && g_reconcile_interval > reconcile_fast_interval)
    CFRunLoopTimerSetNextFireDate(reconcile_timer,
                                  CFAbsoluteTimeGetCurrent() + reconcile_fast_interval);
}

bool reconcile_is_fast(void) {
  return g_knit_on && g_reconcile_interval <= reconcile_fast_interval;
}

// Main-run-loop owned, unit-testable: the next interval, given whether the
// snapshot just did work. Quiet snapshots double the interval up to the slow
// cap; a kick or real work snaps back to fast. Sleeping screens poll at a 1 s
// heartbeat and ignore kicks until the wake notification arrives.
double reconcile_step_interval(bool snapshot_did_work) {
  if (!g_knit_on) {
    g_quiet_streak = 0;
    g_reconcile_kick = false;
    return 1.0;
  }
  if (g_screens_asleep) {
    g_quiet_streak = 0;
    g_reconcile_kick = false;
    return 1.0;
  }
  if (g_reconcile_kick || snapshot_did_work) {
    g_quiet_streak = 0;
    g_reconcile_kick = false;
    return reconcile_fast_interval;
  }
  double next = reconcile_fast_interval;
  for (int i = 0; i < g_quiet_streak; i++) {
    next *= 2;
    if (next >= reconcile_slow_interval) break;
  }
  g_quiet_streak++;
  if (g_power_low) next = fmax(next, .2);
  return fmin(next, reconcile_slow_interval);
}

static void reconcile_refresh_power(void) {
  g_power_low = knit_power_low();
  g_screens_asleep = knit_screens_asleep();
}

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
          bool worked = g_knit_on ? windows_reconcile_snapshot(&g_windows, snapshot) : false;
          if (snapshot) CFRelease(snapshot);
          pending = false;
          // The list is collected off the main thread, so power and sleep can
          // change before this decision. This is the path that actually decays.
          reconcile_refresh_power();
          g_reconcile_interval = reconcile_step_interval(worked);
          knit_set_fast(reconcile_is_fast());
          CFRunLoopTimerSetNextFireDate(timer,
            CFAbsoluteTimeGetCurrent() + g_reconcile_interval);
        });
      });
    } else if (pending) {
      // Completion owns the reschedule. Stepping here would clear a kick that
      // arrived mid-snapshot, and the completion pass would then decay over it.
      CFRunLoopTimerSetNextFireDate(timer,
        CFAbsoluteTimeGetCurrent() + reconcile_fast_interval);
    } else {
      // Paused. Yields 1.0 s, same as today.
      reconcile_refresh_power();
      g_reconcile_interval = reconcile_step_interval(false);
      knit_set_fast(reconcile_is_fast());
      CFRunLoopTimerSetNextFireDate(timer,
        CFAbsoluteTimeGetCurrent() + g_reconcile_interval);
    }
  });
  CFRunLoopAddTimer(CFRunLoopGetMain(), reconcile_timer, kCFRunLoopCommonModes);
}
