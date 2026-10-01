#pragma once
#include <pthread.h>
#include "misc/helpers.h"
#include "misc/window.h"
#include "misc/drawing.h"
#include "misc/knit.h"
#include "animation.h"
#include "hashtable.h"

#define BORDER_ORDER_ABOVE 1
#define BORDER_ORDER_BELOW -1
#define BORDER_STYLE_ROUND  'r'
#define BORDER_STYLE_ROUND_UNIFORM 'u'
#define BORDER_STYLE_SQUARE 's'
#define BORDER_STYLE_KNIT   'k'
// Slack between the knit's outer edge and the edge of the border window.
// During a live resize the border window's shape is reshaped a frame behind
// the content we draw for the new size, so a window growing outward gets its
// knit clipped by a stale shape — visible as the band detaching from whichever
// edge is growing. This slack is transparent, costs nothing to draw, and gives
// the reshape a frame of headroom.
#define BORDER_PADDING 8.0

#define BORDER_TSMN 3.27f

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
#define BORDER_TSMW 52.f
#else
#define BORDER_TSMW 8.f
#endif

struct color_style {
  enum { COLOR_STYLE_GRADIENT, COLOR_STYLE_SOLID, COLOR_STYLE_GLOW } stype;
  union {
    uint32_t color;
    struct gradient gradient;
  };
};

struct settings {
  bool enabled;
  uint32_t apply_to;

  struct color_style active_window;
  struct color_style inactive_window;
  struct color_style corner_mask;
  struct color_style background;

  float border_width;
  float blur_radius;
  char border_style;
  bool hidpi;
  bool show_background;
  int border_order;
  bool ax_focus;

  bool blacklist_enabled;
  struct table blacklist;

  bool whitelist_enabled;
  struct table whitelist;
};

struct event_buffer {
  bool disable_coalescing;
  volatile bool is_coalescing;
  int64_t last_coalesce_attempt;
};

struct border {
  pthread_mutex_t mutex;
  int cid;

  bool focused;
  bool needs_redraw;
  bool too_small;
  bool sticky;
  bool metadata_dirty;
  // target_bounds is safe to deduplicate only after a complete geometry update.
  bool geometry_valid;
  bool visible;
  bool native_transform;
  bool resize_suppressed;
  CGSize resize_observed_size;
  CFAbsoluteTime resize_settle_after;
  double opacity;
  unsigned missing_snapshots;
  unsigned missing_overlay_snapshots;
  CFAbsoluteTime overlay_rebuild_after;
  CFAbsoluteTime space_check_after;
  bool resize_followup_pending;
  uint64_t resize_followup_id;
  uint64_t resize_followup_deadline;
  int level;
  int sub_level;

  uint64_t sid;
  uint32_t wid;
  uint32_t target_wid;
  pid_t owner_pid;       // authoritative owner for icon lookup
  char app[64];          // owning app's process name, for per-app colourways

  float radius;
  float inner_radius;

  CGPoint origin;
  CGRect frame;
  CGRect target_bounds;
  CGRect drawing_bounds;
  CGContextRef context;

  struct animation animation;
  struct event_buffer event_buffer;

  bool is_proxy;
  struct border* proxy;
  volatile uint32_t external_proxy_wid;

  struct settings setting_override;
};

void border_refresh_space(struct border* border);
void border_reset_surface(struct border* border);

struct border* border_create();
void border_destroy(struct border* border);

// Main-thread gate shared by event updates and snapshot recovery.
bool border_suppress_live_resize(struct border* border, CGRect bounds);
void border_update_geometry(struct border* border);
// Bounds from a complete onscreen WindowServer snapshot, independent of notifications.
void border_update_geometry_from_snapshot(struct border* border, CGRect bounds, double opacity);
void border_update(struct border* border, bool try_async);
void border_reorder(struct border* border);
void border_hide(struct border* border);
void border_unhide(struct border* border);

struct settings* border_get_settings(struct border* border);
