#pragma once
// "Inside Window" placement: make room for the sweater by shrinking the
// window itself, so window and sweater together fill the frame the window had.
// Nothing is covered (the band sits outside the shrunken window, as usual) and
// nothing spills past the original frame onto the menu bar or a neighbour.
//
// Invariant for every window we have shrunk ("owed"): its outer frame, the
// window plus `band` on every side, is the space the user (or a zoom, or a
// tiling tool) gave it.
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <sys/types.h>
#include <math.h>

#define ROOM_EPSILON 1.0
// Below this we leave the window alone rather than squash it.
#define ROOM_MIN_WIDTH 80.0
#define ROOM_MIN_HEIGHT 60.0

static inline bool room_near(double a, double b) { return fabs(a - b) < ROOM_EPSILON; }

// The frame `seen` should become. `owed` says whether the window currently
// gives `owed_band` on every side; `last` is where we last left it. Returns
// false when nothing should change.
//
// - New window: inset every edge by `band`.
// - Moved only (same size): leave it.
// - Resized by someone else: every edge that moved is the new outer edge, so
//   inset just that edge; edges that stayed put already have room.
// - Band changed: grow or shrink every edge by the difference.
static inline bool room_target(bool owed, CGRect last, float owed_band,
                               CGRect seen, float band, CGRect* out) {
  double l = CGRectGetMinX(seen), r = CGRectGetMaxX(seen);
  double t = CGRectGetMinY(seen), b = CGRectGetMaxY(seen);
  if (!owed) {
    l += band; r -= band; t += band; b -= band;
  } else {
    bool resized = !room_near(seen.size.width, last.size.width)
                   || !room_near(seen.size.height, last.size.height);
    double extra = band - owed_band;
    if (!resized && room_near(extra, 0)) return false;
    if (resized) {
      if (!room_near(l, CGRectGetMinX(last))) l += owed_band;
      if (!room_near(r, CGRectGetMaxX(last))) r -= owed_band;
      if (!room_near(t, CGRectGetMinY(last))) t += owed_band;
      if (!room_near(b, CGRectGetMaxY(last))) b -= owed_band;
    }
    l += extra; r -= extra; t += extra; b -= extra;
  }
  if (r - l < ROOM_MIN_WIDTH || b - t < ROOM_MIN_HEIGHT) return false;
  *out = CGRectMake(l, t, r - l, b - t);
  return !CGRectEqualToRect(*out, seen);
}

// Main-thread or worker; cheap. Called with every observed window frame.
void room_observe(uint32_t wid, pid_t pid, CGRect frame);
// The window's sweater is going away (closed, or its app switched off).
void room_forget(uint32_t wid);
// Call after any settings change; gives every window its space back when the
// mode (or the sweaters) are switched off.
void room_settings_changed(void);
// Give every window its space back and wait for it (quitting).
void room_release_all_and_wait(void);
