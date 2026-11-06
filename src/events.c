#include "events.h"
#include "reconcile.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "misc/window.h"
#include <time.h>
#include <stddef.h>

// Weak so test targets that compile this file without reconcile.c still link;
// src/reconcile.c provides the real implementation in the app build.
__attribute__((weak)) void reconcile_kick(void) {}

extern struct table g_windows;
extern pid_t g_pid;

static void schedule_order_check(void) {
  if (!pthread_main_np()) {
    dispatch_async(dispatch_get_main_queue(), ^{ schedule_order_check(); });
    return;
  }
  static bool pending = false;
  if (pending) return;
  pending = true;
  // A reorder notification can precede the final app-group ordering. Repair
  // all visible pairs once afterwards, even when keyboard focus stays put.
  // Only stack events use this path; title edits and movement never start it.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    pending = false;
    windows_reorder_all(&g_windows);
  });
}

static void schedule_focus_check(int64_t delay_us) {
  if (!pthread_main_np()) {
    dispatch_async(dispatch_get_main_queue(), ^{ schedule_focus_check(delay_us); });
    return;
  }
  static uint64_t scheduled_deadline = 0, generation = 0;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t deadline = (uint64_t)now.tv_sec * NSEC_PER_SEC + now.tv_nsec
                      + delay_us * NSEC_PER_USEC;
  if (scheduled_deadline && scheduled_deadline <= deadline) return;
  scheduled_deadline = deadline;
  uint64_t request = ++generation;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_us * NSEC_PER_USEC),
                 dispatch_get_main_queue(), ^{
    if (request != generation) return;
    scheduled_deadline = 0;
    windows_determine_and_focus_active_window(&g_windows);
  });
}

#ifdef DEBUG
static void dump_event(void* data, size_t data_length) {
  for (int i = 0; i < data_length; i++) {
    printf("%02x ", *((unsigned char*)data + i));
  }
  printf("\n");
}

static void event_watcher(uint32_t event, void* data, size_t data_length, void* context) {
  static int count = 0;
  printf("(%d) Event: %d; Payload:\n", ++count, event);
  dump_event(data, data_length);
}
#endif

struct window_spawn_data {
  uint64_t sid;
  uint32_t wid;
};

static bool is_own_window(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  return pid == g_pid;
}

static void window_spawn_handler(uint32_t event, struct window_spawn_data* data, size_t length, int cid) {
  if (!data || length < offsetof(struct window_spawn_data, wid) + sizeof(data->wid)) return;
  struct table* windows = &g_windows;
  uint32_t wid = data->wid;
  uint64_t sid = data->sid;

  // This Space-scoped removal also arrives when a live window changes
  // displays. Hide immediately, but let full-snapshot liveness retire the
  // tracked entry. An onscreen destination can restore it on the next pass.
  if (wid && event == EVENT_WINDOW_DESTROY && table_find(windows, &wid)) {
    windows_window_hide(windows, wid);
    schedule_order_check();
    reconcile_kick();
    schedule_focus_check(0);
    return;
  }
  if (!wid || !sid || is_own_window(cid, wid)) return;
  schedule_order_check();
  reconcile_kick();

  if (event == EVENT_WINDOW_CREATE) {
    if (table_find(windows, &wid)) return; // Already discovered by reconciliation.
    if (windows_window_create(windows, wid, sid)) {
      debug("Window Created: %d %d\n", wid, sid);
      windows_determine_and_focus_active_window(windows);
    }
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    windows_determine_and_focus_active_window(windows);
  }
}

static void window_modify_handler(uint32_t event, uint32_t* window_id, size_t length, int cid) {
  if (!window_id || length < sizeof(*window_id)) return;
  uint32_t wid = *window_id;
  struct table* windows = &g_windows;

  if (!wid) return;
  // Only foreign windows enter this table. Resolve tracked targets locally;
  // querying their owner on every geometry event adds synchronous server work.
  if (!table_find(windows, &wid)) {
    if (event == EVENT_WINDOW_MOVE || event == EVENT_WINDOW_RESIZE) return;
    // Keep foreign stack/lifecycle notifications, including untracked windows
    // which can change the ordering of tracked ones.
    if (is_own_window(cid, wid)) return;
  }

  if (event == EVENT_WINDOW_MOVE) {
    debug("Window Move: %d\n", wid);
    windows_window_move(windows, wid);
    reconcile_kick(); // only reached for a tracked window
  } else if (event == EVENT_WINDOW_RESIZE) {
    debug("Window Resize: %d\n", wid);
    windows_window_resize(windows, wid);
    reconcile_kick();
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    windows_window_update(windows, wid);
    schedule_focus_check(10000);
    schedule_order_check();
    reconcile_kick();
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_update(windows, wid);
    schedule_order_check();
    reconcile_kick();
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    // Focus is fully event-driven. Kicking here would pin the loop fast
    // under any busy app that retitles or posts updates.
    debug("Window Focus\n");
    schedule_focus_check(50000);
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
    schedule_order_check();
    reconcile_kick();
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
    schedule_order_check();
    reconcile_kick();
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
    schedule_order_check();
    reconcile_kick();
  }
}

static void front_app_handler() {
  debug("Window Focus\n");
  schedule_focus_check(50000);
  schedule_order_check();
  reconcile_kick();
}

static void space_handler() {
  reconcile_kick();
  // Not all native-fullscreen windows have yet updated their space id...
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    windows_draw_borders_on_current_spaces(&g_windows);
  });
}

void events_register(int cid) {
  void* cid_ctx = (void*)(intptr_t)cid;

  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_CLOSE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_MOVE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_RESIZE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_LEVEL, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UNHIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_HIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_TITLE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_REORDER, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UPDATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_CREATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_DESTROY, cid_ctx);

  SLSRegisterNotifyProc(space_handler, EVENT_SPACE_CHANGE, cid_ctx);

  SLSRegisterNotifyProc(front_app_handler, EVENT_FRONT_CHANGE, cid_ctx);

#ifdef DEBUG
  for (int i = 0; i < 2000; i++) {
    SLSRegisterNotifyProc(event_watcher, i, NULL);
  }
#endif
}
