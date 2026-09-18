// The Apps menu's live switch, through the production window code: the real
// src/windows.c and src/hidden.m against a scripted WindowServer. Borders are
// plain records here; no real window is created, moved or changed.
#import <AppKit/AppKit.h>
#include "../src/windows.h"
#include "../src/misc/apps.h"
#include "../src/misc/extern.h"
#include "../src/misc/ax.h"
#include <assert.h>

// ---- A scripted WindowServer: windows, their owners, and the front window.
enum { MAX_WINDOWS = 16 };
struct fake_window { uint32_t wid; pid_t pid; const char* name; bool open; };
static struct fake_window fake_windows[MAX_WINDOWS];
static int fake_count;
static uint32_t front_wid;
static int notified_count;

static struct fake_window* fake_find(uint32_t wid) {
  for (int i = 0; i < fake_count; i++) if (fake_windows[i].wid == wid) return fake_windows + i;
  return NULL;
}
static int mock_cid(void) { return 1; }
static CGError mock_owner(int cid, uint32_t wid, int* owner) {
  (void)cid; struct fake_window* w = fake_find(wid); *owner = w ? w->pid : 0; return 0;
}
static CGError mock_connection_pid(int cid, pid_t* pid) { *pid = cid; return 0; }
static int mock_proc_name(int pid, void* buffer, uint32_t size) {
  for (int i = 0; i < fake_count; i++)
    if (fake_windows[i].pid == pid) return snprintf(buffer, size, "%s", fake_windows[i].name);
  return 0;
}
static int mock_proc_pidpath(int pid, void* buffer, uint32_t size) { (void)pid; (void)buffer; (void)size; return 0; }

// Queries and iterators are arrays of window numbers with a cursor.
@interface FakeIterator : NSObject
@property(strong) NSArray<NSNumber*>* wids;
@property NSInteger index;
@end
@implementation FakeIterator
@end
static CFTypeRef mock_query(int cid, CFArrayRef windows, int options) {
  (void)cid; (void)options; return CFRetain(windows);
}
static CFTypeRef mock_query_windows(CFTypeRef query) {
  FakeIterator* iterator = [FakeIterator new];
  iterator.wids = (__bridge NSArray*)query;
  iterator.index = -1;
  return (__bridge_retained CFTypeRef)iterator;
}
static int mock_iterator_count(CFTypeRef it) { return (int)((__bridge FakeIterator*)it).wids.count; }
static bool mock_iterator_advance(CFTypeRef it) {
  FakeIterator* iterator = (__bridge FakeIterator*)it;
  return ++iterator.index < (NSInteger)iterator.wids.count;
}
static uint32_t mock_iterator_wid(CFTypeRef it) {
  FakeIterator* iterator = (__bridge FakeIterator*)it;
  return iterator.wids[iterator.index].unsignedIntValue;
}
static bool mock_suitable(CFTypeRef it) { (void)it; return true; }
static CFArrayRef mock_display_spaces(int cid) {
  (void)cid;
  return (__bridge_retained CFArrayRef)@[@{@"Spaces": @[@{@"id64": @1}]}];
}
static CFArrayRef mock_windows_on_spaces(int cid, uint32_t owner, CFArrayRef spaces,
                                         uint32_t options, uint64_t* set, uint64_t* clear) {
  (void)cid; (void)owner; (void)spaces; (void)options; (void)set; (void)clear;
  NSMutableArray* wids = [NSMutableArray array];
  for (int i = 0; i < fake_count; i++) if (fake_windows[i].open) [wids addObject:@(fake_windows[i].wid)];
  return (__bridge_retained CFArrayRef)wids;
}
static CGError mock_notify(int cid, uint32_t* list, int count) {
  (void)cid; (void)list; notified_count = count; return 0;
}
// Used only by the Space-change pass, which this test never drives.
static CFArrayRef mock_displays(int cid) { (void)cid; return NULL; }
static uint64_t mock_display_space(int cid, CFStringRef display) { (void)cid; (void)display; return 1; }
static uint64_t mock_space(int cid, uint32_t wid) { (void)cid; (void)wid; return 1; }
static uint32_t mock_front(int cid) { (void)cid; return front_wid; }

#define SLSMainConnectionID mock_cid
#define SLSGetWindowOwner mock_owner
#define SLSConnectionGetPID mock_connection_pid
#define proc_name mock_proc_name
#define proc_pidpath mock_proc_pidpath
#define SLSWindowQueryWindows mock_query
#define SLSWindowQueryResultCopyWindows mock_query_windows
#define SLSWindowIteratorGetCount mock_iterator_count
#define SLSWindowIteratorAdvance mock_iterator_advance
#define SLSWindowIteratorGetWindowID mock_iterator_wid
#define window_suitable mock_suitable
#define SLSCopyManagedDisplaySpaces mock_display_spaces
#define SLSCopyWindowsWithOptionsAndTags mock_windows_on_spaces
#define SLSRequestNotificationsForWindows mock_notify
#define SLSCopyManagedDisplays mock_displays
#define SLSManagedDisplayGetCurrentSpace mock_display_space
#define window_space_id mock_space
#define get_front_window mock_front
#define ax_get_front_window mock_front
#include "../src/windows.c"
#undef proc_name
#undef proc_pidpath

// ---- The real menu model, with AppKit's app lookup scripted by pid.
static NSDictionary<NSNumber*, NSString*>* fake_bundles;
@interface FakeRunningApplication : NSObject
@property(copy) NSString* bundleIdentifier;
+ (instancetype)runningApplicationWithProcessIdentifier:(pid_t)pid;
@end
@implementation FakeRunningApplication
+ (instancetype)runningApplicationWithProcessIdentifier:(pid_t)pid {
  FakeRunningApplication* app = [self new];
  app.bundleIdentifier = fake_bundles[@(pid)];
  return app;
}
@end
#define NSRunningApplication FakeRunningApplication
#include "../src/hidden.m"
#undef NSRunningApplication

// ---- Everything windows.c expects from the rest of the app.
struct table g_windows;
struct settings g_settings;
pid_t g_pid = 1;
int g_knit_trace;
float g_knit_dim;
CFArrayRef (*JBSLSWindowIteratorGetCornerRadii)(CFTypeRef);
static int created, destroyed;

struct border* border_create(void) { created++; return calloc(1, sizeof(struct border)); }
void border_destroy(struct border* border) { destroyed++; free(border); }
void border_update(struct border* border, bool try_async) { (void)border; (void)try_async; }
void border_update_geometry(struct border* border) { (void)border; }
void border_reorder(struct border* border) { (void)border; }
void border_hide(struct border* border) { (void)border; }
struct settings* border_get_settings(struct border* border) { (void)border; return &g_settings; }
bool knit_app_name_from_executable(const char* path, char* output, size_t capacity) {
  (void)path; (void)output; (void)capacity; return false;
}

static unsigned long hash_wid(void* key) { return *(uint32_t*)key; }
static int equal_wid(void* a, void* b) { return *(uint32_t*)a == *(uint32_t*)b; }
static unsigned long hash_name(void* key) {
  unsigned long h = 5381;
  for (const char* p = key; *p; p++) h = h * 33 + (unsigned char)*p;
  return h;
}
static int equal_name(void* a, void* b) { return strcmp(a, b) == 0; }

static struct border* border_of(uint32_t wid) { return table_find(&g_windows, &wid); }
static int border_count(void) { return g_windows.count; }

int main(void) {
  @autoreleasepool {
    table_init(&g_windows, 64, hash_wid, equal_wid);
    table_init(&g_settings.blacklist, 8, hash_name, equal_name);
    table_init(&g_settings.whitelist, 8, hash_name, equal_name);
    // Two apps with two and one windows, plus a script window with no bundle.
    fake_windows[0] = (struct fake_window){11, 501, "Alpha", true};
    fake_windows[1] = (struct fake_window){12, 501, "Alpha", true};
    fake_windows[2] = (struct fake_window){21, 502, "Beta", true};
    fake_windows[3] = (struct fake_window){31, 503, "python3", true};
    fake_count = 4;
    fake_bundles = @{@501: @"com.example.alpha", @502: @"com.example.beta"};
    front_wid = 11;

    windows_add_existing_windows(&g_windows);
    windows_determine_and_focus_active_window(&g_windows);
    assert(border_count() == 4 && notified_count == 4 && border_of(11)->focused);
    struct border* beta = border_of(21);
    struct border* script = border_of(31);

    // Every eligible owner, whatever its state.
    int pids[8];
    assert(windows_eligible_owners(pids, 8) == 3);

    // Off for Alpha: both its borders go; nobody else's is touched.
    created = destroyed = 0;
    knit_app_set_hidden("com.example.alpha", true);
    windows_apply_app_filter(&g_windows);
    assert(!border_of(11) && !border_of(12) && destroyed == 2 && created == 0);
    assert(border_of(21) == beta && border_of(31) == script && notified_count == 2);
    // Still listed while off, and a new Alpha window stays bare.
    assert(windows_eligible_owners(pids, 8) == 3);
    fake_windows[fake_count++] = (struct fake_window){13, 501, "Alpha", true};
    assert(!windows_window_create(&g_windows, 13, 1) && !border_of(13));

    // Back on: all three Alpha windows return, and the front one is focused
    // at once rather than looking inactive until the next window event.
    created = destroyed = 0;
    knit_app_set_hidden("com.example.alpha", false);
    windows_apply_app_filter(&g_windows);
    assert(border_of(11) && border_of(12) && border_of(13) && created == 3 && destroyed == 0);
    assert(border_of(11)->focused && !border_of(12)->focused);
    assert(border_of(21) == beta && border_of(31) == script && notified_count == 5);

    // All off takes everyone, including a window no app can be named for.
    knit_apps_set_all(false);
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 0 && notified_count == 0);
    assert(windows_eligible_owners(pids, 8) == 3);   // nothing becomes unreachable
    // Then one app ticked back on.
    knit_app_set_hidden("com.example.beta", false);
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 1 && border_of(21));
    beta = border_of(21);
    knit_apps_set_all(true);
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 5 && border_of(21) == beta && border_of(11)->focused);

    // The startup script still has the last word: ticking an app in the menu
    // never overrides blacklist= or whitelist=.
    _table_add(&g_settings.blacklist, "Beta", 5, (void*)true);
    g_settings.blacklist_enabled = true;
    windows_apply_app_filter(&g_windows);
    assert(!border_of(21) && border_count() == 4);
    knit_app_set_hidden("com.example.beta", false);          // already on: no change
    windows_apply_app_filter(&g_windows);
    assert(!border_of(21));
    g_settings.blacklist_enabled = false;
    _table_add(&g_settings.whitelist, "Alpha", 6, (void*)true);
    g_settings.whitelist_enabled = true;
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 3 && border_of(11) && !border_of(21) && !border_of(31));
    knit_app_set_hidden("com.example.alpha", true);          // menu and script together
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 0);
    g_settings.whitelist_enabled = false;
    knit_apps_set_all(true);
    windows_apply_app_filter(&g_windows);
    assert(border_count() == 5);

    // Closed windows are not resurrected by a change.
    fake_windows[1].open = false;
    windows_window_destroy(&g_windows, 12, 0);
    knit_app_set_hidden("com.example.beta", true);
    windows_apply_app_filter(&g_windows);
    assert(!border_of(12) && !border_of(21) && border_count() == 3);
    knit_apps_set_all(true);
    windows_apply_app_filter(&g_windows);
    assert(!border_of(12) && border_count() == 4);
  }
  puts("PASS: live per-app switching through the production window code: targeted removal and "
       "restoration, focus restored, all off/on, script filters respected, closed windows stay closed");
  return 0;
}
