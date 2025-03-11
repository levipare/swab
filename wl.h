#ifndef WL_H
#define WL_H

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdint.h>

#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct wb_output {
    struct wl_output *output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    uint32_t output_width, output_height;
    int32_t scale;

    struct pool_buffer buffer;
    uint32_t width, height;
};

struct wl_ctx {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wb_output *output;
};

typedef void (*draw_callback_t)(struct wb_output *);

struct wl_ctx *wl_ctx_create();

void wl_ctx_destroy(struct wl_ctx *ctx);

void render(struct wb_output *output, draw_callback_t draw);

#endif
