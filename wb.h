#ifndef WB_H
#define WB_H

#include "wl.h"

struct wb {
    struct wl_ctx *wl;
    const char *output_name;
};

struct wb *wb_create();

void wb_destroy(struct wb *bar);

void wb_run(struct wb *bar);

#endif
