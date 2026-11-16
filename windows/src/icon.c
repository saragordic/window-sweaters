// The yarn mark, drawn with the same geometry as assets/yarn-menu-icon.svg
// and src/misc/status_icon.h, for the notification area and the app icon.
#include "app.h"
#include <stdio.h>
#include <stdlib.h>

// Coordinates are the 18 x 18 y-up space of status_icon.h.
static void yarn_mark(CGContextRef c, float size, uint32_t ink, uint32_t ball, uint32_t strand,
                      uint32_t needle, float alpha) {
  float k = size / 18.f;
  CGContextScaleCTM(c, k, k);
  CGContextSetAlpha(c, alpha);
  CGContextSetLineCap(c, kCGLineCapRound);
  CGContextSetLineJoin(c, kCGLineJoinRound);
#define RGBA(v) ((v) >> 16 & 255) / 255.0, ((v) >> 8 & 255) / 255.0, ((v) & 255) / 255.0, ((v) >> 24) / 255.0

  CGContextSetRGBStrokeColor(c, RGBA(needle));
  CGContextSetRGBFillColor(c, RGBA(needle));
  CGContextSetLineWidth(c, 1.35);
  CGContextMoveToPoint(c, 3.2, 15.8);
  CGContextAddLineToPoint(c, 5.7, 12.65);
  CGContextMoveToPoint(c, 14.8, 15.8);
  CGContextAddLineToPoint(c, 12.3, 12.65);
  CGContextStrokePath(c);
  CGContextFillEllipseInRect(c, CGRectMake(1.8, 14.4, 2.8, 2.8));
  CGContextFillEllipseInRect(c, CGRectMake(13.4, 14.4, 2.8, 2.8));

  CGRect orb = CGRectMake(2.8, 1.2, 12.4, 12.4);
  if (ball >> 24) {
    CGContextSetRGBFillColor(c, RGBA(ball));
    CGContextFillEllipseInRect(c, orb);
  }
  CGContextSaveGState(c);
  CGContextAddEllipseInRect(c, orb);
  CGContextClip(c);
  CGContextSetRGBStrokeColor(c, RGBA(strand));
  CGContextSetLineWidth(c, 1.3);
  CGContextMoveToPoint(c, 5.5, 13.4);
  CGContextAddCurveToPoint(c, 11, 10.4, 11, 4.4, 5.5, 1.4);
  CGContextMoveToPoint(c, 9.5, 13.8);
  CGContextAddCurveToPoint(c, 14.3, 10.3, 14.3, 4.5, 9.5, 1);
  CGContextMoveToPoint(c, 2.2, 6.7);
  CGContextAddCurveToPoint(c, 4.4, 8.8, 6.9, 9.6, 9.1, 9.7);
  CGContextMoveToPoint(c, 3.5, 3.2);
  CGContextAddCurveToPoint(c, 5.1, 4.7, 7, 5.4, 9.2, 5.3);
  CGContextStrokePath(c);
  CGContextRestoreGState(c);
  CGContextSetRGBStrokeColor(c, RGBA(ink));
  CGContextSetLineWidth(c, 1.5);
  CGContextAddEllipseInRect(c, orb);
  CGContextStrokePath(c);
#undef RGBA
}

static CGContextRef canvas(int size) {
  CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
  CGContextRef c = CGBitmapContextCreate(NULL, (size_t)size, (size_t)size, 8, 0, rgb,
                                         kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(rgb);
  return c;
}

static HICON icon_from_pixels(const uint32_t* px, int size) {
  uint32_t* bits = NULL;
  HBITMAP color = knit_dib(size, size, &bits);
  if (!color) return NULL;
  memcpy(bits, px, (size_t)size * size * 4);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
  ICONINFO ii = { TRUE, 0, 0, mask, color };
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return icon;
}

HICON yarn_tray_icon(int size, bool dark_mark, bool dimmed) {
  CGContextRef c = canvas(size);
  if (!c) return NULL;
  uint32_t ink = dark_mark ? 0xff1b1b1bu : 0xffffffffu;
  yarn_mark(c, (float)size, ink, 0, ink, ink, dimmed ? 0.45f : 1.f);
  HICON icon = icon_from_pixels(CGBitmapContextGetData(c), size);
  CGContextRelease(c);
  return icon;
}

// Full colour: a rust ball of yarn, cream strands, wooden needles.
CGImageRef yarn_app_image(int size) {
  CGContextRef c = canvas(size);
  if (!c) return NULL;
  yarn_mark(c, (float)size, 0xff6b3a2cu, 0xffc8644au, 0xfff6e7cfu, 0xff9b6b43u, 1.f);
  CGImageRef img = CGBitmapContextCreateImage(c);
  CGContextRelease(c);
  return img;
}

// An .ico with 32-bit images at the usual shell sizes.
bool yarn_write_ico(const char* path) {
  static const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };
  enum { N = sizeof sizes / sizeof sizes[0] };
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  uint8_t* blobs[N];
  uint32_t lens[N];
  for (int i = 0; i < N; i++) {
    int s = sizes[i];
    CGImageRef img = yarn_app_image(s);
    const uint32_t* px = CGImageGetPixels(img);
    uint32_t mask_stride = ((s + 31) / 32) * 4;
    uint32_t len = 40 + (uint32_t)s * s * 4 + mask_stride * s;
    uint8_t* b = calloc(1, len);
    // BITMAPINFOHEADER; height counts the colour and mask halves.
    uint32_t* h = (uint32_t*)b;
    h[0] = 40; h[1] = (uint32_t)s; h[2] = (uint32_t)(2 * s);
    *(uint16_t*)(b + 12) = 1; *(uint16_t*)(b + 14) = 32;
    for (int y = 0; y < s; y++)
      for (int x = 0; x < s; x++) {
        uint32_t p = px[(size_t)(s - 1 - y) * s + x], a = p >> 24;   // bottom-up rows
        uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255;
        if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; bl = bl * 255 / a; }
        ((uint32_t*)(b + 40))[(size_t)y * s + x] = (a << 24) | (r << 16) | (g << 8) | bl;
        if (!a) b[40 + s * s * 4 + y * mask_stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
      }
    CGImageRelease(img);
    blobs[i] = b;
    lens[i] = len;
  }
  uint16_t header[3] = { 0, 1, N };
  fwrite(header, 2, 3, f);
  uint32_t offset = 6 + 16 * N;
  for (int i = 0; i < N; i++) {
    uint8_t entry[16] = { 0 };
    entry[0] = (uint8_t)(sizes[i] >= 256 ? 0 : sizes[i]);
    entry[1] = entry[0];
    *(uint16_t*)(entry + 4) = 1;
    *(uint16_t*)(entry + 6) = 32;
    *(uint32_t*)(entry + 8) = lens[i];
    *(uint32_t*)(entry + 12) = offset;
    fwrite(entry, 1, 16, f);
    offset += lens[i];
  }
  for (int i = 0; i < N; i++) { fwrite(blobs[i], 1, lens[i], f); free(blobs[i]); }
  return fclose(f) == 0;
}
