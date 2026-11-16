// The one pthread feature the shared renderer uses: a statically initialised
// mutex around the tile cache. Backed by an SRWLOCK (same layout: one pointer),
// so this header never has to pull in <windows.h>.
#pragma once

typedef struct { void* opaque; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }

int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);
