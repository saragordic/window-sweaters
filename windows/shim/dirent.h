// Just enough of <dirent.h> for chart.c to list the user's PNG charts.
// Names are UTF-8. Each entry and the list itself are released with free().
#pragma once

struct dirent { char d_name[520]; };

int scandir(const char* dir, struct dirent*** namelist,
            int (*filter)(const struct dirent*),
            int (*compar)(const struct dirent**, const struct dirent**));
