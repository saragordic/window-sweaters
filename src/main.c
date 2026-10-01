#include "border.h"
#include "misc/autoyarn.h"
#include "hashtable.h"
#include "events.h"
#include "reconcile.h"
#include "misc/extern.h"
#include "windows.h"
#include "mach.h"
#include "parse.h"
#include "misc/connection.h"
#include "misc/ax.h"
#include "misc/yabai.h"
#include <stdio.h>
#include <dlfcn.h>

#define VERSION_OPT_LONG "--version"
#define VERSION_OPT_SHRT "-v"

#define HELP_OPT_LONG "--help"
#define HELP_OPT_SHRT "-h"

#define MAJOR 1
#define MINOR 5
#define PATCH 0

// Resolved via dlsym because of availability
CFArrayRef (* JBSLSWindowIteratorGetCornerRadii)(CFTypeRef) = NULL;

pid_t g_pid;
int g_knit_trace = 0;
static int g_no_menu = 0;
mach_port_t g_server_port;
struct table g_windows;
struct mach_server g_mach_server;
struct settings g_settings = { .enabled = true,
                               .active_window = { .stype = COLOR_STYLE_SOLID,
                                                  .color = 0xffe1e3e4 },
                               .inactive_window = { .stype = COLOR_STYLE_SOLID,
                                                    .color =  0x00000000 },
                               .background = { .stype = COLOR_STYLE_SOLID,
                                               .color = 0x00000000         },
                               .border_width = 12.f,
                               .blur_radius = 0,
                               .border_style = BORDER_STYLE_KNIT,
                               .hidpi = true,
                               .show_background = false,
                               .border_order = BORDER_ORDER_BELOW,
                               .ax_focus = false,
                               .blacklist_enabled = false,
                               .whitelist_enabled = false                    };

static TABLE_HASH_FUNC(hash_windows) {
  return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(cmp_windows) {
  return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

static TABLE_HASH_FUNC(hash_blacklist) {
  // djb2 by Dan Bernstein
  unsigned long hash = 5381;
  char c;
  while((c = *((char*)key++))) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static TABLE_COMPARE_FUNC(cmp_blacklist) {
  return strcmp((char*)key_a, (char*)key_b) == 0;
}

// Icon jobs never retain border pointers: the target may close before the
// answer arrives. Resolve surviving owners on the main thread instead.
void knit_auto_yarn_ready(pid_t pid) {
  assert(pthread_main_np());
  for (int i = 0; i < g_windows.capacity; i++) {
    for (struct bucket* b = g_windows.buckets[i]; b; b = b->next) {
      struct border* border = b->value;
      if (border && border->owner_pid == pid) {
        border->needs_redraw = true;
        border_update(border, true);
      }
    }
  }
}

static void message_handler(void* data, uint32_t len) {
  char* message = data;
  uint32_t update_mask = 0;
  struct settings settings = g_settings;

  while(message && *message) {
    update_mask |= parse_settings(&settings, 1, &message);
    message += strlen(message) + 1;
  }

  if (settings.apply_to > 0) {
    struct border* border = table_find(&g_windows, &settings.apply_to);
    if (border) {
      border->setting_override = settings;
      border->setting_override.enabled = true;
      border->needs_redraw = true;
      border_update(border, true);
    }
    return;
  } else {
    g_settings = settings;
    for (int i = 0; i < g_windows.capacity; ++i) {
      struct bucket* bucket = g_windows.buckets[i];
      while (bucket) {
        if (bucket->value) {
          struct border* border = bucket->value;
          if (border->setting_override.enabled) {
            char* message = data;
            uint32_t window_update_mask = 0;
            while(message && *message) {
              window_update_mask |= parse_settings(&border->setting_override,
                                                   1,
                                                   &message                  );
              message += strlen(message) + 1;
            }

            if (window_update_mask
                && !((update_mask & BORDER_UPDATE_MASK_ALL)
                     || (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL))) {
              border->needs_redraw = true;
              border_update(border, true);
            }
          }
        }
        bucket = bucket->next;
      }
    }
  }

  if (update_mask & BORDER_UPDATE_MASK_RECREATE_ALL) {
    windows_recreate_all_borders(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_ALL) {
    windows_update_all(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_ACTIVE) {
    windows_update_active(&g_windows);
  } else if (update_mask & BORDER_UPDATE_MASK_INACTIVE) {
    windows_update_inactive(&g_windows);
  }
}

// Called by the menu bar: feed one "key=value" through the same path a
// command line invocation would take.
void knit_apply(const char* arg) {
  size_t n = strlen(arg);
  char buf[n + 2];
  memcpy(buf, arg, n);
  buf[n] = '\0';
  buf[n + 1] = '\0';
  message_handler(buf, (uint32_t)(n + 2));
}

float knit_current_width(void) { return g_settings.border_width; }

// Called by the menu after it turns an app on or off.
void knit_apps_filter_changed(void) { windows_apply_app_filter(&g_windows); }

// Owners of every window that could wear a sweater, for the Apps menu.
int knit_window_owners(int* pids, int capacity) {
  return windows_eligible_owners(pids, capacity);
}

extern void knit_menubar_start(void);
extern void knit_menubar_prepare(void);
extern void knit_application_prepare(void);

static void send_args_to_server(mach_port_t port, int argc, char** argv) {
  int message_length = argc;
  int argl[argc];

  for (int i = 1; i < argc; i++) {
    argl[i] = strlen(argv[i]);
    message_length += argl[i] + 1;
  }

  char message[(sizeof(char) * message_length)];
  char* temp = message;

  for (int i = 1; i < argc; i++) {
    memcpy(temp, argv[i], argl[i]);
    temp += argl[i];
    *temp++ = '\0';
  }
  *temp++ = '\0';

  mach_send_message(port, message, message_length);
}


void load_symbols() {
  if (__builtin_available(macOS 26.0, *)) {
    void* lib = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY | RTLD_LOCAL);
    if (lib) {
      JBSLSWindowIteratorGetCornerRadii = dlsym(lib, "SLSWindowIteratorGetCornerRadii");
    }
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && ((strcmp(argv[1], VERSION_OPT_LONG) == 0)
                   || (strcmp(argv[1], VERSION_OPT_SHRT) == 0))) {
    fprintf(stdout, "Window Sweaters %d.%d.%d\n", MAJOR, MINOR, PATCH);
    exit(EXIT_SUCCESS);
  }

  if (argc > 1 && ((strcmp(argv[1], HELP_OPT_LONG) == 0)
                   || (strcmp(argv[1], HELP_OPT_SHRT) == 0))) {
    fprintf(stdout, "Window Sweaters: launch the app for menu-bar controls.\nDeveloper options: --version, --trace, --no-menu. See README.md.\n");
    exit(EXIT_SUCCESS);
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--trace") == 0)   g_knit_trace = 1;
    if (strcmp(argv[i], "--no-menu") == 0) g_no_menu = 1;
  }

  table_init(&g_settings.blacklist, 64, hash_blacklist, cmp_blacklist);
  table_init(&g_settings.whitelist, 64, hash_blacklist, cmp_blacklist);
  g_settings.ax_focus = ax_check_trust(true);

  uint32_t update_mask = parse_settings(&g_settings, argc - 1, argv + 1);
  mach_port_t server_port = mach_get_bs_port(BS_NAME);
  if (server_port && update_mask) {
    send_args_to_server(server_port, argc, argv);
    return 0;
  } else if (server_port) {
    error("A Window Sweaters instance is already running and no valid arguments"
          " were provided. To modify properties of the running instance"
          " provide them as arguments.\n");
  }

  if (!g_no_menu) knit_application_prepare();
  load_symbols();
  pid_for_task(mach_task_self(), &g_pid);
  table_init(&g_windows, 1024, hash_windows, cmp_windows);

  g_server_port = create_connection_server_port();

  int cid = SLSMainConnectionID();
  events_register(cid);

  // NOTE: upstream JankyBorders drains and discards this connection's event
  // queue here, which is right for a headless daemon. We are an NSApplication,
  // so AppKit owns event delivery — draining it swallows the clicks meant for
  // our own menu bar item. Window notifications arrive via events_register()
  // above and are unaffected.

  // Restore the collection and preferences before any border is drawn.
  // Otherwise each preference application repaints every existing window.
  if (!g_no_menu) knit_menubar_prepare();
  windows_add_existing_windows(&g_windows);
  windows_reconcile_start();

  mach_server_begin(&g_mach_server, message_handler);
  if (!update_mask) execute_config_file("window-sweaters", "sweatersrc");

  #ifdef _YABAI_INTEGRATION
  yabai_register_mach_port(&g_windows);
  #endif
  if (g_no_menu) {
    // the plain daemon, exactly as it ran before any AppKit was involved
    CFRunLoopRun();
  } else {
    knit_menubar_start();   // installs the status item, then runs the loop
  }
  return 0;
}
