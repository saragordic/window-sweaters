#include "windows.h"
#include "misc/apps.h"
#include "misc/extern.h"
#include <time.h>

extern int g_knit_trace;
#include "hashtable.h"
#include "border.h"
#include "misc/ax.h"
#include <string.h>
#include <libproc.h>

extern pid_t g_pid;
extern struct settings g_settings;

// Loaded via dlsym in main.c
extern CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);

static bool window_in_list(struct table* list, char* app_name) {
  if (table_find(list, app_name)) return true;
  return false;
}

// The one gate every border passes before it exists: new windows, existing
// windows, reconciliation and the menu's per-app switch all come through here.
static bool app_allowed(struct settings* settings, char* app_name, pid_t pid) {
  if (knit_pid_hidden(pid)) return false;
  if (settings->whitelist_enabled
      && !window_in_list(&settings->whitelist, app_name)) {
    return false;
  }
  if (settings->blacklist_enabled
      && window_in_list(&settings->blacklist, app_name)) {
    return false;
  }
  return true;
}

bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid) {
  bool window_created = false;
  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);

  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  char pid_name_buffer[PROC_PIDPATHINFO_MAXSIZE] = {0};
  if (proc_name(pid, pid_name_buffer, sizeof(pid_name_buffer)) <= 0) return false;
  // VS Code and some other Electron apps share a generic process name. Resolve
  // that name once at discovery, never while moving/resizing or drawing yarn.
  if (strcmp(pid_name_buffer, "Electron") == 0) {
    char executable[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if (proc_pidpath(pid, executable, sizeof executable) > 0)
      knit_app_name_from_executable(executable, pid_name_buffer, sizeof pid_name_buffer);
  }


  if (pid == g_pid || !app_allowed(&g_settings, pid_name_buffer, pid)) return false;

  CFArrayRef target_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type);

  if (!target_ref) return false;

  CFTypeRef query = SLSWindowQueryWindows(cid, target_ref, 0x0);
  if (query) {
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (iterator && SLSWindowIteratorGetCount(iterator) > 0) {
      if (SLSWindowIteratorAdvance(iterator)) {
        if (window_suitable(iterator)) {
          struct border* border = table_find(windows, &wid);
          if (!border) {
            border = border_create();
            table_add(windows, &wid, border);
            window_created = true;
          }

          int32_t radius = 0;

          // Determine window corner radius
          if (JBSLSWindowIteratorGetCornerRadii) {
            CFArrayRef radii_ref = JBSLSWindowIteratorGetCornerRadii(iterator);
            if (radii_ref && CFArrayGetCount(radii_ref) > 0) {
              CFNumberRef value = CFArrayGetValueAtIndex(radii_ref, 0);
              CFNumberGetValue(value, kCFNumberSInt32Type, &radius);
            }
            if (radii_ref) CFRelease(radii_ref);
          }
          radius = radius > 0 ? radius : 9;

          border->radius = radius;
          border->inner_radius = radius + 1;
          border->target_wid = wid;
          border->owner_pid = pid;
          snprintf(border->app, sizeof border->app, "%s", pid_name_buffer);
          border->sid = sid;
          border->metadata_dirty = true;
          border_update(border, false);
          windows_update_notifications(windows);
        }
      }
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
  }
  CFRelease(target_ref);

  return window_created;
}

void windows_add_missing_windows(struct table* windows);

static void windows_remove_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        border_destroy(border);
      }
      bucket = bucket->next;
    }
  }
  table_clear(windows);
  windows_update_notifications(windows);
}

// Apply a changed app list without touching anyone else's border: drop the
// borders that no longer pass the gate, then add only windows not yet tracked.
// Rebuilding everything would make every sweater on screen flicker.
void windows_apply_app_filter(struct table* windows) {
  uint32_t* removed = windows->count ? malloc(sizeof(*removed) * windows->count) : NULL;
  if (windows->count && !removed) return;
  int count = 0;
  for (int i = 0; i < windows->capacity; ++i) {
    for (struct bucket* bucket = windows->buckets[i]; bucket; bucket = bucket->next) {
      struct border* border = bucket->value;
      if (border && !border->is_proxy
          && !app_allowed(&g_settings, border->app, border->owner_pid))
        removed[count++] = *(uint32_t*)bucket->key;
    }
  }
  for (int i = 0; i < count; i++) windows_window_destroy(windows, removed[i], 0);
  free(removed);
  windows_add_missing_windows(windows);
  // New borders start unfocused; nothing else will correct that until the
  // next window event, so a re-enabled front window would look inactive.
  windows_determine_and_focus_active_window(windows);
}

void windows_recreate_all_borders(struct table* windows) {
  windows_remove_all(windows);
  windows_add_existing_windows(windows);
}

void windows_update_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_active(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_inactive(struct table* windows) {
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && !border->focused) {
          border->needs_redraw = true;
          border_update(border, true);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_window_update(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) {
    border->metadata_dirty = true;
    border_update(border, true);
  }
}

void windows_reorder_all(struct table* windows) {
  for (int i = 0; i < windows->capacity; i++) {
    for (struct bucket* bucket = windows->buckets[i]; bucket; bucket = bucket->next) {
      if (bucket->value) border_reorder(bucket->value);
    }
  }
}

static uint64_t geometry_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

static void schedule_resize_followup(struct table* windows, uint32_t wid,
                                     uint64_t followup_id, int64_t delay_ns) {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ns),
                 dispatch_get_main_queue(), ^{
    uint32_t target_wid = wid;
    struct border* current = table_find(windows, &target_wid);
    if (!current || !current->resize_followup_pending
        || current->resize_followup_id != followup_id) return;
    uint64_t now = geometry_time_ns();
    if (now < current->resize_followup_deadline) {
      schedule_resize_followup(windows, wid, followup_id,
                               current->resize_followup_deadline - now);
      return;
    }
    current->resize_followup_pending = false;
    border_update_geometry(current);
  });
}

void windows_window_resize(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (!border) return;
  if (g_knit_trace) {
    static uint64_t prev = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    uint64_t gap = prev ? now - prev : 0;
    prev = now;
    border_update_geometry(border);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t done = ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    fprintf(stderr, "SIZE  gap %6.1f ms   our work %6.2f ms\n",
            gap / 1000.0, (done - now) / 1000.0);
  } else {
    border_update_geometry(border);
  }

  // Notifications may precede the final WindowServer geometry. Read it once
  // more after 32ms without another resize. There is at most one pending
  // check per window, with an ID token that rejects checks from before a
  // hide/close. The table is the application-lifetime g_windows; no border
  // pointer is retained across this delay.
  border->resize_followup_deadline = geometry_time_ns() + 32 * NSEC_PER_MSEC;
  if (!border->resize_followup_pending) {
    static uint64_t next_followup_id = 0;
    border->resize_followup_pending = true;
    border->resize_followup_id = ++next_followup_id;
    schedule_resize_followup(windows, wid, border->resize_followup_id,
                             32 * NSEC_PER_MSEC);
  }
}

static bool windows_window_focus(struct table* windows, uint32_t wid) {
  bool found_window = false;
  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket* bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused && border->target_wid != wid) {
          border->focused = false;
          border->metadata_dirty = true;
          // With dimming off, knit focus changes do not change any pixels.
          // Still update ordering, but avoid repainting both entire borders.
          if (border_get_settings(border)->border_style != BORDER_STYLE_KNIT
              || g_knit_dim > 0.f) border->needs_redraw = true;
          border_update(border, true);
        }

        if (!border->focused && border->target_wid == wid) {
          border->focused = true;
          border->metadata_dirty = true;
          if (border_get_settings(border)->border_style != BORDER_STYLE_KNIT
              || g_knit_dim > 0.f) border->needs_redraw = true;
          border_update(border, true);
        }

        if (border->target_wid == wid) found_window = true;
      }
      bucket = bucket->next;
    }
  }

  return found_window;
}

// Diagnostic: report the interval between window-move notifications and how
// long we take to act on each. If the intervals are long, the WindowServer is
// not telling us promptly; if our own time is long, the fault is ours.
void windows_window_move(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (g_knit_trace) {
    static uint64_t prev = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    uint64_t gap = prev ? now - prev : 0;
    prev = now;
    if (border) border_update_geometry(border);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t done = ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    fprintf(stderr, "move  gap %6.1f ms   our work %5.2f ms\n",
            gap / 1000.0, (done - now) / 1000.0);
    return;
  }
  if (border) border_update_geometry(border);
}

void windows_window_hide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) {
    border->resize_followup_pending = false;
    border_hide(border);
  }
}

void windows_window_unhide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) {
    // Hidden windows can resize or move to another space before reappearing.
    border->metadata_dirty = true;
    border_update(border, false);
  }
}

bool windows_window_destroy(struct table* windows, uint32_t wid, uint64_t sid) {
  struct border* border = table_find(windows, &wid);
  if (border && (border->sid == sid || border->sticky || sid == 0)) {
    table_remove(windows, &wid);
    border_destroy(border);
    windows_update_notifications(windows);
    return true;
  }
  return false;
}

void windows_update_notifications(struct table* windows) {
  int window_count = 0;
  uint32_t empty = 0;
  uint32_t* window_list = windows->count ? malloc(sizeof(*window_list) * windows->count) : NULL;
  if (windows->count && !window_list) return;

  for (int i = 0; i < windows->capacity; ++i) {
    struct bucket *bucket = windows->buckets[i];
    while (bucket) {
      if (bucket->value) {
        uint32_t wid = *(uint32_t *) bucket->key;
        window_list[window_count++] = wid;
      }
      bucket = bucket->next;
    }
  }

  int cid = SLSMainConnectionID();
  SLSRequestNotificationsForWindows(cid, window_count ? window_list : &empty, window_count);
  free(window_list);
}

void windows_determine_and_focus_active_window(struct table* windows) {
  int cid = SLSMainConnectionID();
  uint32_t front_wid = g_settings.ax_focus
                       ? ax_get_front_window(cid)
                       : get_front_window(cid);
  if (!front_wid && g_settings.ax_focus) front_wid = get_front_window(cid);

  debug("Front window: %d\n", front_wid);
  if (!windows_window_focus(windows, front_wid)) {
    debug("Taking slow window focus path: %d\n", front_wid);
    if (front_wid && windows_window_create(windows,
                                           front_wid,
                                           window_space_id(cid, front_wid))) {
      windows_window_focus(windows, front_wid);
    }
  }
}

void windows_draw_borders_on_current_spaces(struct table* windows) {
  debug("Space Change: Consistency check\n");
  int cid = SLSMainConnectionID();
  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  if (!displays) return;
  uint32_t space_count = CFArrayGetCount(displays);
  uint64_t space_list[space_count];

  for (int i = 0; i < space_count; i++) {
    space_list[i] = SLSManagedDisplayGetCurrentSpace(cid,
                                          CFArrayGetValueAtIndex(displays, i));
  }

  CFRelease(displays);

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            0,
                                                            space_list_ref,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );

  if (window_list) {
    CFTypeRef query = SLSWindowQueryWindows(cid, window_list, 0x0);
    if (query) {
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
      if (iterator) {
        while(SLSWindowIteratorAdvance(iterator)) {
          if (window_suitable(iterator)) {
            uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
            struct border* border = table_find(windows, &wid);
            if (border) {
              border->metadata_dirty = true;
              border_update(border, true);
            }
            else {
              debug("Creating Missing Window: %d\n", wid);
              windows_window_create(windows, wid, window_space_id(cid, wid));
            }
          }
        }
        CFRelease(iterator);
      }
      CFRelease(query);
    }
    CFRelease(window_list);
  }
  CFRelease(space_list_ref);
}

// Every window on every Space that could wear a sweater, before any app is
// asked whether it wants one. Returns the count; the caller frees *out.
static int windows_copy_suitable(int cid, uint32_t** out) {
  *out = NULL;
  int found = 0;
  uint64_t* space_list = NULL;
  int space_count = 0;

  CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(cid);
  if (display_spaces_ref) {
    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
      CFDictionaryRef display_ref
                               = CFArrayGetValueAtIndex(display_spaces_ref, i);
      CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref,
                                                   CFSTR("Spaces"));
      int spaces_count = CFArrayGetCount(spaces_ref);

      space_list = (uint64_t*)realloc(space_list,
                                      sizeof(uint64_t)*(space_count
                                                        + spaces_count));

      for (int j = 0; j < spaces_count; ++j) {
        CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
        CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
        CFNumberGetValue(sid_ref,
                         CFNumberGetType(sid_ref),
                         space_list + space_count + j);
      }
      space_count += spaces_count;
    }
    CFRelease(display_spaces_ref);
  }

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  CFArrayRef window_list_ref = SLSCopyWindowsWithOptionsAndTags(cid,
                                                                0,
                                                                space_list_ref,
                                                                0x2,
                                                                &set_tags,
                                                                &clear_tags  );
  if (window_list_ref) {
    int count = CFArrayGetCount(window_list_ref);
    if (count > 0 && (*out = malloc(sizeof(**out) * (size_t)count))) {
      CFTypeRef query = SLSWindowQueryWindows(cid, window_list_ref, 0x0);
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);

      while (SLSWindowIteratorAdvance(iterator) && found < count) {
        if (window_suitable(iterator))
          (*out)[found++] = SLSWindowIteratorGetWindowID(iterator);
      }

      CFRelease(query);
      CFRelease(iterator);
    }
    CFRelease(window_list_ref);
  }
  CFRelease(space_list_ref);
  free(space_list);
  return found;
}

static void windows_add_windows(struct table* windows, bool only_missing) {
  int cid = SLSMainConnectionID();
  uint32_t* suitable;
  int count = windows_copy_suitable(cid, &suitable);
  for (int i = 0; i < count; i++) {
    uint32_t wid = suitable[i];
    if (!only_missing || !table_find(windows, &wid))
      windows_window_create(windows, wid, window_space_id(cid, wid));
  }
  if (count) windows_update_notifications(windows);
  free(suitable);
}

// The processes owning a window that could wear a sweater, whether or not it
// wears one now. The Apps menu lists these, so an app switched off never drops
// out of the list, however it presents itself (no Dock icon, say).
int windows_eligible_owners(int* pids, int capacity) {
  int cid = SLSMainConnectionID();
  uint32_t* suitable;
  int count = windows_copy_suitable(cid, &suitable);
  int owners = 0;
  for (int i = 0; i < count; i++) {
    int owner_cid = 0;
    pid_t pid = 0;
    SLSGetWindowOwner(cid, suitable[i], &owner_cid);
    SLSConnectionGetPID(owner_cid, &pid);
    if (pid <= 0 || pid == g_pid) continue;
    int seen = 0;
    while (seen < owners && pids[seen] != pid) seen++;
    if (seen == owners && owners < capacity) pids[owners++] = pid;
  }
  free(suitable);
  return owners;
}

void windows_add_existing_windows(struct table* windows) {
  windows_add_windows(windows, false);
}

void windows_add_missing_windows(struct table* windows) {
  windows_add_windows(windows, true);
}
