#include "room.h"
#include "border.h"
#include <ApplicationServices/ApplicationServices.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <os/log.h>

extern struct settings g_settings;
extern bool g_knit_on;
// `log stream --level debug --predicate 'category == "room"'` shows these.
static os_log_t room_log(void) {
  static os_log_t log;
  static dispatch_once_t once;
  dispatch_once(&once, ^{ log = os_log_create("local.knitborders.app", "room"); });
  return log;
}
#define ROOM_TRACE(...) os_log_debug(room_log(), __VA_ARGS__)
extern void _AXUIElementGetWindow(CFTypeRef window, uint32_t* wid);

struct room_entry {
  uint32_t wid;
  pid_t pid;
  bool owed;          // we took `band` from every side
  float band;
  bool tried;         // an attempt finished (it may have been refused)
  bool busy;          // a job is queued or running
  bool seen_while_busy;
  CGRect last;        // the frame we last left it at
  CGRect seen;        // latest frame observed while busy
  CFAbsoluteTime window_start;
  int adjustments;    // within the current window, to spot a tug of war
  CFAbsoluteTime paused_until;
};

static pthread_mutex_t g_room_lock = PTHREAD_MUTEX_INITIALIZER;
static struct room_entry* g_entries;
static int g_count, g_capacity;
static bool g_was_active;

static bool room_check_trust(void) { return AXIsProcessTrusted(); }

static dispatch_queue_t room_queue(void) {
  static dispatch_queue_t queue;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    queue = dispatch_queue_create("window-sweaters.room", DISPATCH_QUEUE_SERIAL);
  });
  return queue;
}

static bool room_active(void) {
  return g_settings.inset && g_knit_on;
}

static struct room_entry* room_find(uint32_t wid) {
  for (int i = 0; i < g_count; i++) if (g_entries[i].wid == wid) return &g_entries[i];
  return NULL;
}

static struct room_entry* room_add(uint32_t wid, pid_t pid) {
  if (g_count == g_capacity) {
    int capacity = g_capacity ? g_capacity * 2 : 32;
    struct room_entry* grown = realloc(g_entries, sizeof(*grown) * capacity);
    if (!grown) return NULL;
    g_entries = grown;
    g_capacity = capacity;
  }
  struct room_entry* e = &g_entries[g_count++];
  *e = (struct room_entry){ .wid = wid, .pid = pid };
  return e;
}

static void room_remove(uint32_t wid) {
  for (int i = 0; i < g_count; i++) {
    if (g_entries[i].wid == wid) { g_entries[i] = g_entries[--g_count]; return; }
  }
}

// --- Accessibility (room queue only) ---------------------------------------

static AXUIElementRef room_copy_window(pid_t pid, uint32_t wid) {
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return NULL;
  AXUIElementSetMessagingTimeout(app, 0.25f);
  CFArrayRef windows = NULL;
  AXUIElementRef match = NULL;
  if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, (CFTypeRef*)&windows)
        == kAXErrorSuccess && windows && CFGetTypeID(windows) == CFArrayGetTypeID()) {
    for (CFIndex i = 0; i < CFArrayGetCount(windows) && !match; i++) {
      AXUIElementRef window = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
      uint32_t id = 0;
      _AXUIElementGetWindow(window, &id);
      if (id == wid) match = (AXUIElementRef)CFRetain(window);
    }
  }
  if (windows) CFRelease(windows);
  CFRelease(app);
  if (match) AXUIElementSetMessagingTimeout(match, 0.25f);
  return match;
}

static bool room_read_frame(AXUIElementRef window, CGRect* frame) {
  CFTypeRef position = NULL, size = NULL;
  bool ok = AXUIElementCopyAttributeValue(window, kAXPositionAttribute, &position) == kAXErrorSuccess
         && AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &size) == kAXErrorSuccess
         && position && size
         && AXValueGetValue(position, kAXValueTypeCGPoint, &frame->origin)
         && AXValueGetValue(size, kAXValueTypeCGSize, &frame->size);
  if (position) CFRelease(position);
  if (size) CFRelease(size);
  return ok;
}

static bool room_resizable(AXUIElementRef window) {
  CFTypeRef fullscreen = NULL;
  if (AXUIElementCopyAttributeValue(window, CFSTR("AXFullScreen"), &fullscreen) == kAXErrorSuccess
      && fullscreen) {
    bool full = CFGetTypeID(fullscreen) == CFBooleanGetTypeID() && CFBooleanGetValue(fullscreen);
    CFRelease(fullscreen);
    if (full) return false;
  }
  Boolean size_ok = false, position_ok = false;
  AXUIElementIsAttributeSettable(window, kAXSizeAttribute, &size_ok);
  AXUIElementIsAttributeSettable(window, kAXPositionAttribute, &position_ok);
  return size_ok && position_ok;
}

static void room_write_frame(AXUIElementRef window, CGRect frame) {
  AXValueRef size = AXValueCreate(kAXValueTypeCGSize, &frame.size);
  AXValueRef position = AXValueCreate(kAXValueTypeCGPoint, &frame.origin);
  // Size, position, size: shrinking first never pushes a window off screen,
  // and the second size catches apps that clamp size to where they sit.
  AXUIElementSetAttributeValue(window, kAXSizeAttribute, size);
  AXUIElementSetAttributeValue(window, kAXPositionAttribute, position);
  AXUIElementSetAttributeValue(window, kAXSizeAttribute, size);
  CFRelease(size);
  CFRelease(position);
}

// Grow a window back by `band` on every side.
static void room_give_back(pid_t pid, uint32_t wid, float band) {
  dispatch_async(room_queue(), ^{
    AXUIElementRef window = room_copy_window(pid, wid);
    if (!window) return;
    CGRect frame;
    if (room_resizable(window) && room_read_frame(window, &frame))
      room_write_frame(window, CGRectInset(frame, -band, -band));
    CFRelease(window);
  });
}

static void room_schedule(uint32_t wid);

static void room_job(uint32_t wid) {
  pthread_mutex_lock(&g_room_lock);
  struct room_entry* e = room_find(wid);
  if (!e) { pthread_mutex_unlock(&g_room_lock); return; }
  struct room_entry snapshot = *e;
  float band = g_settings.border_width;
  bool active = room_active();
  pthread_mutex_unlock(&g_room_lock);
  if (!active) {
    pthread_mutex_lock(&g_room_lock);
    if ((e = room_find(wid))) e->busy = false;
    pthread_mutex_unlock(&g_room_lock);
    return;
  }

  bool owed = snapshot.owed, changed = false;
  CGRect result = CGRectNull;
  bool trusted = room_check_trust();
  AXUIElementRef window = trusted ? room_copy_window(snapshot.pid, wid) : NULL;
  ROOM_TRACE("job wid=%u pid=%d trusted=%d found=%d owed=%d", wid, snapshot.pid, trusted, window != NULL, owed);
  if (window) {
    CGRect current;
    if (room_read_frame(window, &current)) {
      result = current;
      CGRect target;
      if (room_resizable(window)
          && room_target(owed, snapshot.last, snapshot.band, current, band, &target)) {
        ROOM_TRACE("wid=%u %.0f,%.0f %.0fx%.0f -> %.0f,%.0f %.0fx%.0f", wid,
                   current.origin.x, current.origin.y, current.size.width, current.size.height,
                   target.origin.x, target.origin.y, target.size.width, target.size.height);
        room_write_frame(window, target);
        if (!room_read_frame(window, &result)) result = target;
        owed = true;
        changed = true;
      }
    }
    CFRelease(window);
  }

  pthread_mutex_lock(&g_room_lock);
  bool again = false;
  if ((e = room_find(wid))) {
    e->busy = false;
    e->tried = trusted;
    if (!CGRectIsNull(result)) e->last = result;
    if (changed) {
      e->owed = owed;
      e->band = band;
      // A tiling tool that re-asserts its own frame would fight us forever.
      CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
      if (now - e->window_start > 3.0) { e->window_start = now; e->adjustments = 0; }
      if (++e->adjustments > 4) e->paused_until = now + 10.0;
    }
    // Something resized it while we worked: look again.
    again = e->seen_while_busy && !CGRectIsNull(result)
            && (!room_near(e->seen.size.width, result.size.width)
                || !room_near(e->seen.size.height, result.size.height));
    e->seen_while_busy = false;
  }
  pthread_mutex_unlock(&g_room_lock);
  if (again) room_schedule(wid);
}

static void room_schedule(uint32_t wid) {
  pthread_mutex_lock(&g_room_lock);
  struct room_entry* e = room_find(wid);
  if (!e || e->busy) { pthread_mutex_unlock(&g_room_lock); return; }
  e->busy = true;
  pthread_mutex_unlock(&g_room_lock);
  dispatch_async(room_queue(), ^{ room_job(wid); });
}

// --- Public ----------------------------------------------------------------

void room_observe(uint32_t wid, pid_t pid, CGRect frame) {
  if (!wid || pid <= 0 || !room_active()) return;
  pthread_mutex_lock(&g_room_lock);
  struct room_entry* e = room_find(wid);
  if (!e) e = room_add(wid, pid);
  if (!e) { pthread_mutex_unlock(&g_room_lock); return; }
  if (e->busy) {
    e->seen = frame;
    e->seen_while_busy = true;
    pthread_mutex_unlock(&g_room_lock);
    return;
  }
  bool same_size = room_near(frame.size.width, e->last.size.width)
                   && room_near(frame.size.height, e->last.size.height);
  bool settled = e->owed ? e->band == g_settings.border_width : e->tried;
  if (e->tried && same_size && settled) {
    // A move, or the echo of our own change: just remember where it is.
    e->last = frame;
    pthread_mutex_unlock(&g_room_lock);
    return;
  }
  if (CFAbsoluteTimeGetCurrent() < e->paused_until) {
    pthread_mutex_unlock(&g_room_lock);
    return;
  }
  pthread_mutex_unlock(&g_room_lock);
  room_schedule(wid);
}

void room_forget(uint32_t wid) {
  pthread_mutex_lock(&g_room_lock);
  struct room_entry* e = room_find(wid);
  if (e) {
    if (e->owed) room_give_back(e->pid, e->wid, e->band);
    room_remove(wid);
  }
  pthread_mutex_unlock(&g_room_lock);
}

static void room_release_all(void) {
  pthread_mutex_lock(&g_room_lock);
  for (int i = 0; i < g_count; i++)
    if (g_entries[i].owed) room_give_back(g_entries[i].pid, g_entries[i].wid, g_entries[i].band);
  g_count = 0;
  pthread_mutex_unlock(&g_room_lock);
}

void room_settings_changed(void) {
  bool active = room_active();
  if (g_was_active && !active) room_release_all();
  // Resizing other apps' windows needs Accessibility access: ask once, when
  // the mode is switched on, with the system's own prompt.
  if (active && !g_was_active && !room_check_trust()) {
    const void* key = kAXTrustedCheckOptionPrompt;
    const void* yes = kCFBooleanTrue;
    CFDictionaryRef options = CFDictionaryCreate(NULL, &key, &yes, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
  }
  g_was_active = active;
}

void room_release_all_and_wait(void) {
  room_release_all();
  dispatch_sync(room_queue(), ^{});
}
