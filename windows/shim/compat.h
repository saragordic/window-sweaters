// Force-included into every Windows translation unit (see CMakeLists.txt).
// Lets the shared macOS sources build with MSVC or clang-cl unchanged.
#pragma once

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string.h>
#include <math.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

typedef int pid_t;
