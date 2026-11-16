// The Win32 half of the shim: image files through WIC, a scandir for chart.c,
// and the mutex the tile cache locks.
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdlib.h>
#include <stdio.h>
#include "cg.h"
#include "dirent.h"
#include "pthread.h"

int pthread_mutex_lock(pthread_mutex_t* m) { AcquireSRWLockExclusive((PSRWLOCK)m); return 0; }
int pthread_mutex_unlock(pthread_mutex_t* m) { ReleaseSRWLockExclusive((PSRWLOCK)m); return 0; }

static wchar_t* widen(const char* s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0) return NULL;
  wchar_t* w = malloc(sizeof(wchar_t) * (size_t)n);
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

int scandir(const char* dir, struct dirent*** namelist,
            int (*filter)(const struct dirent*),
            int (*compar)(const struct dirent**, const struct dirent**)) {
  *namelist = NULL;
  char pattern[1100];
  snprintf(pattern, sizeof pattern, "%s\\*", dir);
  wchar_t* wpattern = widen(pattern);
  if (!wpattern) return -1;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(wpattern, &fd);
  free(wpattern);
  if (h == INVALID_HANDLE_VALUE) return -1;
  struct dirent** list = NULL;
  int count = 0, cap = 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    struct dirent entry;
    if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, entry.d_name, sizeof entry.d_name, NULL, NULL))
      continue;
    if (filter && !filter(&entry)) continue;
    if (count == cap) {
      cap = cap ? cap * 2 : 16;
      struct dirent** grown = realloc(list, sizeof *list * (size_t)cap);
      if (!grown) break;
      list = grown;
    }
    struct dirent* copy = malloc(sizeof *copy);
    if (!copy) break;
    *copy = entry;
    list[count++] = copy;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (compar && count > 1)
    qsort(list, (size_t)count, sizeof *list, (int (*)(const void*, const void*))compar);
  *namelist = list;
  return count;
}

static IWICImagingFactory* wic(void) {
  static IWICImagingFactory* factory;
  if (factory) return factory;
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  (void)hr;   // already initialised (in any mode) is fine
  if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IWICImagingFactory, (void**)&factory)))
    factory = NULL;
  return factory;
}

CGImageRef cg_image_load_file(const char* path) {
  IWICImagingFactory* f = wic();
  if (!f || !path) return NULL;
  wchar_t* wpath = widen(path);
  if (!wpath) return NULL;
  IWICBitmapDecoder* dec = NULL;
  IWICBitmapFrameDecode* frame = NULL;
  IWICBitmapSource* conv = NULL;
  CGImageRef img = NULL;
  if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(f, wpath, NULL, GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand, &dec))
      && SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame))
      && SUCCEEDED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppPBGRA,
                                          (IWICBitmapSource*)frame, &conv))) {
    UINT w = 0, h = 0;
    IWICBitmapSource_GetSize(conv, &w, &h);
    if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
      uint32_t* px = malloc(sizeof(uint32_t) * w * h);
      if (px && SUCCEEDED(IWICBitmapSource_CopyPixels(conv, NULL, w * 4, w * h * 4, (BYTE*)px)))
        img = CGImageCreateWithPixels(px, (int)w, (int)h);
      free(px);
    }
  }
  if (conv) IWICBitmapSource_Release(conv);
  if (frame) IWICBitmapFrameDecode_Release(frame);
  if (dec) IWICBitmapDecoder_Release(dec);
  free(wpath);
  return img;
}

bool cg_image_write_png(CGImageRef image, const char* path) {
  IWICImagingFactory* f = wic();
  if (!f || !image || !path) return false;
  wchar_t* wpath = widen(path);
  if (!wpath) return false;
  UINT w = (UINT)CGImageGetWidth(image), h = (UINT)CGImageGetHeight(image);
  IWICStream* stream = NULL;
  IWICBitmapEncoder* enc = NULL;
  IWICBitmapFrameEncode* frame = NULL;
  bool ok = false;
  if (SUCCEEDED(IWICImagingFactory_CreateStream(f, &stream))
      && SUCCEEDED(IWICStream_InitializeFromFilename(stream, wpath, GENERIC_WRITE))
      && SUCCEEDED(IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &enc))
      && SUCCEEDED(IWICBitmapEncoder_Initialize(enc, (IStream*)stream, WICBitmapEncoderNoCache))
      && SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(enc, &frame, NULL))
      && SUCCEEDED(IWICBitmapFrameEncode_Initialize(frame, NULL))
      && SUCCEEDED(IWICBitmapFrameEncode_SetSize(frame, w, h))) {
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
    // PNG stores straight alpha; un-premultiply if the encoder asked for it.
    const uint32_t* src = CGImageGetPixels(image);
    uint32_t* px = malloc(sizeof(uint32_t) * w * h);
    if (px) {
      bool straight = IsEqualGUID(&fmt, &GUID_WICPixelFormat32bppBGRA);
      for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t p = src[i], a = p >> 24;
        if (straight && a && a < 255) {
          uint32_t r = ((p >> 16) & 255) * 255 / a, g = ((p >> 8) & 255) * 255 / a, b = (p & 255) * 255 / a;
          p = (a << 24) | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
        }
        px[i] = p;
      }
      ok = SUCCEEDED(IWICBitmapFrameEncode_WritePixels(frame, h, w * 4, w * h * 4, (BYTE*)px))
        && SUCCEEDED(IWICBitmapFrameEncode_Commit(frame))
        && SUCCEEDED(IWICBitmapEncoder_Commit(enc));
      free(px);
    }
  }
  if (frame) IWICBitmapFrameEncode_Release(frame);
  if (enc) IWICBitmapEncoder_Release(enc);
  if (stream) IWICStream_Release(stream);
  free(wpath);
  return ok;
}
