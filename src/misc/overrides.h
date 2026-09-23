#pragma once
#include <stdint.h>
#include <sys/types.h>

enum { KNIT_OVERRIDE_COLOR = 1, KNIT_OVERRIDE_CHART = 2 };

// Main-thread lookup of the UI's saved bundle-ID rule. Fields are independent.
// Returns a mask of the values it changed. The menu module owns persistence.
unsigned knit_app_override(pid_t pid, uint32_t* yarn, int* chart);
