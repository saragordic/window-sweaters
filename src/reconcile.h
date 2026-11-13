#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>
struct table;

void windows_reconcile_start(void);
// A complete front-to-back onscreen snapshot. NULL means the query failed.
// True when the pass mutated a border; heal-only callers ignore the result.
bool windows_reconcile_snapshot(struct table* windows, CFArrayRef snapshot);
// Main-thread only. Any window/menu activity: next pass runs at fast cadence.
void reconcile_kick(void);
// Main-thread only. True while the loop is at fast cadence (and knit is on).
bool reconcile_is_fast(void);
