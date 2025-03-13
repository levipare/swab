#ifndef WL_H
#define WL_H

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdint.h>

#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct wb_output {
    struct wl_output *output;
    uint32_t output_width, output_height;
    int32_t scale;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct pool_buffer buffer;
    cairo_surface_t *cairo_surface;
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

struct render_ctx {
    cairo_t *cr;
    PangoContext *pango;
    uint32_t width, height;
};

typedef void (*draw_func)(struct render_ctx *);

void render(struct wl_ctx *ctx, struct wb_output *output, draw_func draw);

struct wl_ctx *wl_ctx_create();

void wl_ctx_destroy(struct wl_ctx *ctx);

#endif
