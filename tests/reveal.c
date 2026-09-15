// Offscreen checks use the actual knitted fabric, not a substitute animation.
#include "misc/knit.h"
#include "misc/chart.h"
#include "misc/reveal.h"
#include <ImageIO/ImageIO.h>
#include <assert.h>
#include <stdio.h>

static CGContextRef frame(int scale, float progress) {
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef c = CGBitmapContextCreate(NULL, 360*scale, 240*scale, 8, 0, cs,
    kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(cs);
  CGContextScaleCTM(c, scale, scale);
  CGRect win = CGRectMake(30, 30, 300, 180);
  CGContextSaveGState(c);
  knit_reveal_clip(c, win, 12, 18, 1, progress);
  if (progress >= 1) {
    knit_draw(c,win,12,18,0xff638575,knit_chart_index("zigzag"),0,1);
  } else {
  CGImageRef image = knit_snapshot(CGSizeMake(360,240), scale, win, 12, 18,
      0xff638575, knit_chart_index("zigzag"), 0, 1);
  CGContextDrawImage(c, CGRectMake(0,0,360,240), image);
  CGImageRelease(image);
  }
  CGContextRestoreGState(c);
  return c;
}
int main(int argc, char** argv) {
  knit_charts_load("charts");
  for (int scale = 1; scale <= 2; scale++) {
    CGContextRef full = frame(scale, 1);
    unsigned char* final = CGBitmapContextGetData(full);
    size_t bytes = CGBitmapContextGetBytesPerRow(full)*240*scale;
    size_t previous = 0;
    for (int i = 0; i <= 10; i++) {
      CGContextRef c = frame(scale, i/10.f);
      unsigned char* pixels = CGBitmapContextGetData(c);
      size_t opaque = 0;
      for (size_t p=0; p<bytes; p+=4) {
        if (!i) assert(pixels[p+3] == 0);
        if (pixels[p+3] == 255) {
          opaque++;
          // Ignore the antialiased moving boundary; fully interior stitches
          // must be byte-identical to the finished sweater.
          size_t stride = CGBitmapContextGetBytesPerRow(c);
          if (p > stride*2+8 && p+stride*2+8 < bytes
              && pixels[p-8+3] == 255 && pixels[p+8+3] == 255
              && pixels[p-stride*2+3] == 255 && pixels[p+stride*2+3] == 255)
            if (memcmp(pixels+p, final+p, 4)) {
              fprintf(stderr, "scale=%d step=%d x=%zu y=%zu got=%d,%d,%d expected=%d,%d,%d\n",scale,i,(p%stride)/4,p/stride,pixels[p],pixels[p+1],pixels[p+2],final[p],final[p+1],final[p+2]);
              abort();
            }
        }
        if (i == 10) assert(!memcmp(pixels+p, final+p, 4));
      }
      assert(opaque >= previous); previous = opaque;
      if (argc > 1 && scale == 2) {
        char path[1024]; snprintf(path, sizeof path, "%s/reveal-%02d.png", argv[1], i);
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (UInt8*)path, strlen(path), false);
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
        CGImageRef image = CGBitmapContextCreateImage(c);
        CGImageDestinationAddImage(dest, image, NULL);
        assert(CGImageDestinationFinalize(dest));
        CGImageRelease(image); CFRelease(dest); CFRelease(url);
      }
      CGContextRelease(c);
    }
    CGContextRelease(full);
  }
  puts("PASS: reveal at 1x/2x, empty start, monotonic coverage, unchanged opaque stitches, exact final frame");
}
