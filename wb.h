#ifndef WB_H
#define WB_H

#include "wl.h"
#include <stdbool.h>

struct wb_config {
    char font[128];
    uint32_t height;
    uint32_t bg_color, fg_color;
};

struct wb {
    struct wl_ctx *wl;
    struct wb_config config;
    bool exit;

    char content[1024];
};

void wb_run(struct wb_config config);

#endif
