// The geometry behind "Inside Window": a window plus its band must exactly
// fill the frame it was given, and repeated events must never creep.
#include "../src/room.h"
#include <assert.h>
#include <stdio.h>

static bool eq(CGRect a, CGRect b) { return CGRectEqualToRect(a, b); }

int main(void) {
  CGRect out;
  // A new window gives the band on every side.
  CGRect given = CGRectMake(0, 25, 800, 600);
  assert(room_target(false, CGRectNull, 0, given, 12, &out));
  assert(eq(out, CGRectMake(12, 37, 776, 576)));
  assert(eq(CGRectInset(out, -12, -12), given));
  CGRect last = out;

  // Our own change echoing back, or a plain move, changes nothing.
  assert(!room_target(true, last, 12, last, 12, &out));
  assert(!room_target(true, last, 12, CGRectOffset(last, 140, -20), 12, &out));

  // Dragging the right edge out: only that edge makes room; the left stays.
  CGRect dragged = CGRectMake(12, 37, 900, 576);
  assert(room_target(true, last, 12, dragged, 12, &out));
  assert(eq(out, CGRectMake(12, 37, 888, 576)));
  // ...and the result is stable: seeing it again is a no-op.
  assert(!room_target(true, out, 12, out, 12, &out));

  // Dragging the top-left corner: both of those edges, nothing else.
  dragged = CGRectMake(-88, -63, 876, 676);
  assert(room_target(true, last, 12, dragged, 12, &out));
  assert(eq(out, CGRectMake(-76, -51, 864, 664)));

  // Zoom or a tiling tool sets every edge: the window fits inside the tile.
  CGRect tile = CGRectMake(0, 25, 720, 875);
  assert(room_target(true, last, 12, tile, 12, &out));
  assert(eq(CGRectInset(out, -12, -12), tile));

  // A new band width rebalances without moving the outer frame.
  assert(room_target(true, last, 12, last, 18, &out));
  assert(eq(CGRectInset(out, -18, -18), CGRectInset(last, -12, -12)));
  assert(room_target(true, last, 12, last, 6, &out));
  assert(eq(CGRectInset(out, -6, -6), CGRectInset(last, -12, -12)));

  // Tiny windows are left alone rather than squashed.
  assert(!room_target(false, CGRectNull, 0, CGRectMake(0, 0, 90, 70), 12, &out));

  printf("PASS: inside placement fits window + band to the given frame; moves, echoes and re-reads never creep; per-edge resize, tiles and width changes\n");
  return 0;
}
