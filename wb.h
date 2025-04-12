#ifndef WB_H
#define WB_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "wayland.h"

struct wb_config {
    char font[128];
    bool bottom;
    uint32_t height;
    uint32_t bg_color, fg_color; // ARGB
};

struct wb {
    struct wayland *wl;
    struct wb_config config;
    bool exit;

    struct fcft_font *font;
    char status[BUFSIZ];
};

void wb_run(struct wb_config config);

#endif
