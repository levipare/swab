#ifndef WL_H
#define WL_H

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct render_ctx {
    uint32_t width, height;
    pixman_image_t *pix;
};

struct wl_output_ctx {
    struct wl_ctx *ctx;

    struct wl_output *output;
    char *name;
    int32_t scale;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t width, height; // dimensions of surface

    struct pool_buffer buffer;

    struct wl_list link;
};

struct wl_ctx {
    int fd;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_list outputs;

    void (*draw_callback)(void *, struct render_ctx *);
    void *draw_data;
};

void schedule_frame(struct wl_ctx *ctx);

struct wl_ctx *wl_ctx_create(bool bottom, uint32_t height,
                             void (*draw)(void *, struct render_ctx *),
                             void *data);

void wl_ctx_destroy(struct wl_ctx *ctx);

#endif
