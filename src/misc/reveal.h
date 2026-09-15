#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>

// Reveal the finished fabric without changing its gauge, phase or opacity.
// The caller saves/restores the graphics state and still draws the normal ring.
static inline void knit_reveal_clip(CGContextRef ctx, CGRect win, float radius,
                                    float band, float tuck, float progress) {
  if (progress >= 1) return;
  if (!isfinite(progress) || progress <= 0) {
    CGContextClipToRect(ctx, CGRectZero);
    return;
  }
  radius = fmaxf(0, fminf(radius, fminf(win.size.width, win.size.height) * .5f));
  float half = fminf(win.size.width, win.size.height) * .5f - 2;
  tuck = fminf(fmaxf(1, tuck), fmaxf(1, half));
  float inset = tuck - (tuck + band) * progress;
  CGRect edge = CGRectInset(win, inset, inset);
  float r = fmaxf(0, radius - inset);
  CGPathRef path = CGPathCreateWithRoundedRect(edge, r, r, NULL);
  CGContextAddPath(ctx, path);
  CGContextClip(ctx);
  CGPathRelease(path);
}
