#ifndef WL_H
#define WL_H

#include <stdint.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

struct wl_ctx {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct zwlr_layer_shell_v1 *layer_shell;

    int scale;
    struct wl_output *output;

    // drawing related stuff
    uint32_t width, height;
    void *shm_data;
    struct wl_buffer *buffer;

    cairo_surface_t *cairo_surface;
};

typedef void (*draw_callback_t)(struct wl_ctx *, cairo_t *);

struct wl_ctx *wl_ctx_create();

void wl_ctx_destroy(struct wl_ctx *ctx);

void render(struct wl_ctx *ctx, draw_callback_t);

#endif
