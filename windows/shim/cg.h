// A small software implementation of the CoreGraphics subset the knit
// renderer uses, so src/knit.c, src/chart.c and src/apps.c build on Windows
// unchanged and knit exactly the same stitches.
//
// Conventions follow CoreGraphics:
//   - bitmap contexts are y-up, origin at the bottom left, while the pixel
//     memory starts at the TOP row (so it can be handed straight to a
//     top-down Windows DIB);
//   - 32-bit contexts are premultiplied BGRA in memory (0xAARRGGBB as a
//     uint32_t), the layout UpdateLayeredWindow expects;
//   - paths are transformed by the CTM when they are added to a context.
//
// Not implemented: dashes, miter/bevel joins (drawn round), shadows,
// gradients and colour management. The renderer uses none of them; the few
// declared for src/misc/drawing.h are inert stubs.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef double CGFloat;
typedef struct CGPoint { CGFloat x, y; } CGPoint;
typedef struct CGSize { CGFloat width, height; } CGSize;
typedef struct CGRect { CGPoint origin; CGSize size; } CGRect;
typedef struct CGAffineTransform { CGFloat a, b, c, d, tx, ty; } CGAffineTransform;

typedef const void* CFTypeRef;
typedef long CFIndex;
typedef struct CGPath* CGMutablePathRef;
typedef const struct CGPath* CGPathRef;
typedef struct CGContext* CGContextRef;
typedef struct CGImage* CGImageRef;
typedef struct CGColorSpace* CGColorSpaceRef;
typedef struct CGColor* CGColorRef;
typedef struct CGGradient* CGGradientRef;
typedef struct CGImageSource* CGImageSourceRef;
typedef const struct __CFString* CFStringRef;
typedef const struct __CFURL* CFURLRef;
typedef const struct __CFArray* CFArrayRef;
typedef const struct __CFDictionary* CFDictionaryRef;
typedef const void* CFAllocatorRef;
typedef uint32_t CGBitmapInfo;
typedef uint32_t CFStringEncoding;
typedef int32_t CGError;

// One enum, so `alpha | byte order` combines without a mixed-enum warning.
enum { kCGImageAlphaNone = 0, kCGImageAlphaPremultipliedLast = 1,
       kCGImageAlphaPremultipliedFirst = 2, kCGImageAlphaLast = 3,
       kCGImageAlphaFirst = 4, kCGImageAlphaNoneSkipLast = 5,
       kCGImageAlphaNoneSkipFirst = 6,
       kCGBitmapByteOrderDefault = 0, kCGBitmapByteOrder32Little = 2 << 12,
       kCGBitmapByteOrder32Host = kCGBitmapByteOrder32Little };
typedef enum { kCGLineCapButt, kCGLineCapRound, kCGLineCapSquare } CGLineCap;
typedef enum { kCGLineJoinMiter, kCGLineJoinRound, kCGLineJoinBevel } CGLineJoin;
typedef enum { kCGInterpolationDefault = 0, kCGInterpolationNone = 1,
               kCGInterpolationLow = 2, kCGInterpolationMedium = 4,
               kCGInterpolationHigh = 3 } CGInterpolationQuality;
enum { kCFStringEncodingUTF8 = 0x08000100 };
enum { kCFURLPOSIXPathStyle = 0, kCFURLWindowsPathStyle = 2 };
enum { kCGErrorSuccess = 0 };
#define kCGColorSpaceSRGB ((CFStringRef)"sRGB")
#define kCGColorSpaceGenericRGB ((CFStringRef)"genericRGB")
extern const int kCFTypeArrayCallBacks;

static const CGPoint CGPointZero = { 0, 0 };
static const CGSize CGSizeZero = { 0, 0 };
static const CGRect CGRectZero = { { 0, 0 }, { 0, 0 } };
static const CGAffineTransform CGAffineTransformIdentity = { 1, 0, 0, 1, 0, 0 };

static inline CGPoint CGPointMake(CGFloat x, CGFloat y) { CGPoint p = { x, y }; return p; }
static inline CGSize CGSizeMake(CGFloat w, CGFloat h) { CGSize s = { w, h }; return s; }
static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  CGRect r = { { x, y }, { w, h } }; return r;
}
static inline CGFloat CGRectGetMinX(CGRect r) { return r.size.width < 0 ? r.origin.x + r.size.width : r.origin.x; }
static inline CGFloat CGRectGetMinY(CGRect r) { return r.size.height < 0 ? r.origin.y + r.size.height : r.origin.y; }
static inline CGFloat CGRectGetMaxX(CGRect r) { return r.size.width < 0 ? r.origin.x : r.origin.x + r.size.width; }
static inline CGFloat CGRectGetMaxY(CGRect r) { return r.size.height < 0 ? r.origin.y : r.origin.y + r.size.height; }
static inline CGFloat CGRectGetMidX(CGRect r) { return r.origin.x + r.size.width * 0.5; }
static inline CGFloat CGRectGetMidY(CGRect r) { return r.origin.y + r.size.height * 0.5; }
static inline CGFloat CGRectGetWidth(CGRect r) { return fabs(r.size.width); }
static inline CGFloat CGRectGetHeight(CGRect r) { return fabs(r.size.height); }
static inline CGRect CGRectStandardize(CGRect r) {
  return CGRectMake(CGRectGetMinX(r), CGRectGetMinY(r), CGRectGetWidth(r), CGRectGetHeight(r));
}
static inline CGRect CGRectInset(CGRect r, CGFloat dx, CGFloat dy) {
  r = CGRectStandardize(r);
  r.origin.x += dx; r.origin.y += dy; r.size.width -= 2 * dx; r.size.height -= 2 * dy;
  if (r.size.width < 0 || r.size.height < 0) return CGRectZero;
  return r;
}
static inline bool CGRectIsEmpty(CGRect r) { return r.size.width == 0 || r.size.height == 0; }

static inline CGAffineTransform CGAffineTransformMake(CGFloat a, CGFloat b, CGFloat c,
                                                      CGFloat d, CGFloat tx, CGFloat ty) {
  CGAffineTransform t = { a, b, c, d, tx, ty }; return t;
}
static inline CGAffineTransform CGAffineTransformMakeTranslation(CGFloat tx, CGFloat ty) {
  return CGAffineTransformMake(1, 0, 0, 1, tx, ty);
}
static inline CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy) {
  return CGAffineTransformMake(sx, 0, 0, sy, 0, 0);
}
static inline CGAffineTransform CGAffineTransformMakeRotation(CGFloat angle) {
  CGFloat c = cos(angle), s = sin(angle);
  return CGAffineTransformMake(c, s, -s, c, 0, 0);
}
// t1 first, then t2.
static inline CGAffineTransform CGAffineTransformConcat(CGAffineTransform t1, CGAffineTransform t2) {
  return CGAffineTransformMake(t1.a * t2.a + t1.b * t2.c, t1.a * t2.b + t1.b * t2.d,
                               t1.c * t2.a + t1.d * t2.c, t1.c * t2.b + t1.d * t2.d,
                               t1.tx * t2.a + t1.ty * t2.c + t2.tx,
                               t1.tx * t2.b + t1.ty * t2.d + t2.ty);
}
static inline CGAffineTransform CGAffineTransformTranslate(CGAffineTransform t, CGFloat tx, CGFloat ty) {
  return CGAffineTransformConcat(CGAffineTransformMakeTranslation(tx, ty), t);
}
static inline CGAffineTransform CGAffineTransformScale(CGAffineTransform t, CGFloat sx, CGFloat sy) {
  return CGAffineTransformConcat(CGAffineTransformMakeScale(sx, sy), t);
}
static inline CGAffineTransform CGAffineTransformRotate(CGAffineTransform t, CGFloat angle) {
  return CGAffineTransformConcat(CGAffineTransformMakeRotation(angle), t);
}
static inline CGAffineTransform CGAffineTransformInvert(CGAffineTransform t) {
  CGFloat det = t.a * t.d - t.b * t.c;
  if (det == 0) return t;
  CGFloat ia = t.d / det, ib = -t.b / det, ic = -t.c / det, id = t.a / det;
  return CGAffineTransformMake(ia, ib, ic, id, -(t.tx * ia + t.ty * ic), -(t.tx * ib + t.ty * id));
}
static inline CGPoint CGPointApplyAffineTransform(CGPoint p, CGAffineTransform t) {
  return CGPointMake(t.a * p.x + t.c * p.y + t.tx, t.b * p.x + t.d * p.y + t.ty);
}

// --- objects ---------------------------------------------------------------
CFTypeRef CFRetain(CFTypeRef object);
void CFRelease(CFTypeRef object);

CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name);
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
void CGColorSpaceRelease(CGColorSpaceRef space);

// --- paths -----------------------------------------------------------------
CGMutablePathRef CGPathCreateMutable(void);
CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform* m);
CGPathRef CGPathCreateWithRoundedRect(CGRect rect, CGFloat rw, CGFloat rh, const CGAffineTransform* m);
void CGPathRelease(CGPathRef path);
void CGPathMoveToPoint(CGMutablePathRef p, const CGAffineTransform* m, CGFloat x, CGFloat y);
void CGPathAddLineToPoint(CGMutablePathRef p, const CGAffineTransform* m, CGFloat x, CGFloat y);
void CGPathAddQuadCurveToPoint(CGMutablePathRef p, const CGAffineTransform* m,
                               CGFloat cpx, CGFloat cpy, CGFloat x, CGFloat y);
void CGPathAddCurveToPoint(CGMutablePathRef p, const CGAffineTransform* m,
                           CGFloat cp1x, CGFloat cp1y, CGFloat cp2x, CGFloat cp2y,
                           CGFloat x, CGFloat y);
void CGPathCloseSubpath(CGMutablePathRef p);
void CGPathAddRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect rect);
void CGPathAddRoundedRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect rect,
                          CGFloat rw, CGFloat rh);
void CGPathAddEllipseInRect(CGMutablePathRef p, const CGAffineTransform* m, CGRect rect);
void CGPathAddPath(CGMutablePathRef p, const CGAffineTransform* m, CGPathRef other);

// --- bitmap contexts -------------------------------------------------------
CGContextRef CGBitmapContextCreate(void* data, size_t width, size_t height,
                                   size_t bitsPerComponent, size_t bytesPerRow,
                                   CGColorSpaceRef space, CGBitmapInfo info);
void* CGBitmapContextGetData(CGContextRef c);
size_t CGBitmapContextGetWidth(CGContextRef c);
size_t CGBitmapContextGetHeight(CGContextRef c);
CGImageRef CGBitmapContextCreateImage(CGContextRef c);
void CGContextRelease(CGContextRef c);
void CGContextFlush(CGContextRef c);

void CGContextSaveGState(CGContextRef c);
void CGContextRestoreGState(CGContextRef c);
void CGContextScaleCTM(CGContextRef c, CGFloat sx, CGFloat sy);
void CGContextTranslateCTM(CGContextRef c, CGFloat tx, CGFloat ty);
void CGContextRotateCTM(CGContextRef c, CGFloat angle);
void CGContextConcatCTM(CGContextRef c, CGAffineTransform t);

void CGContextSetRGBFillColor(CGContextRef c, CGFloat r, CGFloat g, CGFloat b, CGFloat a);
void CGContextSetRGBStrokeColor(CGContextRef c, CGFloat r, CGFloat g, CGFloat b, CGFloat a);
void CGContextSetGrayFillColor(CGContextRef c, CGFloat gray, CGFloat a);
void CGContextSetGrayStrokeColor(CGContextRef c, CGFloat gray, CGFloat a);
void CGContextSetLineWidth(CGContextRef c, CGFloat width);
void CGContextSetLineCap(CGContextRef c, CGLineCap cap);
void CGContextSetLineJoin(CGContextRef c, CGLineJoin join);
void CGContextSetAlpha(CGContextRef c, CGFloat alpha);
void CGContextSetShouldAntialias(CGContextRef c, bool antialias);
void CGContextSetInterpolationQuality(CGContextRef c, CGInterpolationQuality q);

void CGContextBeginPath(CGContextRef c);
void CGContextAddPath(CGContextRef c, CGPathRef path);
void CGContextMoveToPoint(CGContextRef c, CGFloat x, CGFloat y);
void CGContextAddLineToPoint(CGContextRef c, CGFloat x, CGFloat y);
void CGContextAddCurveToPoint(CGContextRef c, CGFloat cp1x, CGFloat cp1y,
                              CGFloat cp2x, CGFloat cp2y, CGFloat x, CGFloat y);
void CGContextClosePath(CGContextRef c);
void CGContextAddRect(CGContextRef c, CGRect rect);
void CGContextAddEllipseInRect(CGContextRef c, CGRect rect);

void CGContextFillPath(CGContextRef c);
void CGContextEOFillPath(CGContextRef c);
void CGContextStrokePath(CGContextRef c);
void CGContextFillRect(CGContextRef c, CGRect rect);
void CGContextFillEllipseInRect(CGContextRef c, CGRect rect);
void CGContextClearRect(CGContextRef c, CGRect rect);
void CGContextClip(CGContextRef c);
void CGContextEOClip(CGContextRef c);
void CGContextClipToRect(CGContextRef c, CGRect rect);

void CGContextDrawImage(CGContextRef c, CGRect rect, CGImageRef image);
void CGContextDrawTiledImage(CGContextRef c, CGRect rect, CGImageRef image);

// --- images ----------------------------------------------------------------
CGImageRef CGImageRetain(CGImageRef image);
void CGImageRelease(CGImageRef image);
size_t CGImageGetWidth(CGImageRef image);
size_t CGImageGetHeight(CGImageRef image);
// Windows extensions: premultiplied BGRA pixels, top row first.
CGImageRef CGImageCreateWithPixels(const uint32_t* pixels, int width, int height);
const uint32_t* CGImageGetPixels(CGImageRef image);

// --- image files (WIC) and strings -----------------------------------------
CFStringRef CFStringCreateWithCString(CFAllocatorRef alloc, const char* s, CFStringEncoding enc);
CFURLRef CFURLCreateWithFileSystemPath(CFAllocatorRef alloc, CFStringRef path, int style, bool dir);
CGImageSourceRef CGImageSourceCreateWithURL(CFURLRef url, CFDictionaryRef options);
CGImageRef CGImageSourceCreateImageAtIndex(CGImageSourceRef src, size_t index, CFDictionaryRef options);
// Windows extensions for tests and the icon generator.
CGImageRef cg_image_load_file(const char* utf8_path);
bool cg_image_write_png(CGImageRef image, const char* utf8_path);

// --- inert stubs, only for src/misc/drawing.h ------------------------------
CGColorRef CGColorCreateGenericRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a);
CGColorRef CGColorCreateSRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a);
void CGColorRelease(CGColorRef color);
void CGContextSetShadowWithColor(CGContextRef c, CGSize offset, CGFloat blur, CGColorRef color);
void CGContextReplacePathWithStrokedPath(CGContextRef c);
CFArrayRef CFArrayCreate(CFAllocatorRef alloc, const void** values, CFIndex count, const void* callbacks);
CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef space, CFArrayRef colors, const CGFloat* locations);
void CGContextDrawLinearGradient(CGContextRef c, CGGradientRef g, CGPoint start, CGPoint end, uint32_t options);

#ifdef __cplusplus
}
#endif
