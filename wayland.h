#ifndef WAYLAND_H
#define WAYLAND_H

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

struct wayland_monitor {
    struct wayland *wl;

    struct wl_output *output;
    char *name;
    int32_t scale;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t width, height; // dimensions of surface
    struct pool_buffer buffer;

    struct wl_list link;
};

typedef void (*scale_callback_t)(void *data, struct wayland_monitor *mon,
                                 int32_t scale);

struct wayland_layer_surface_config {
    uint32_t layer;
    uint32_t width, height;
    uint32_t anchor;
    int32_t top, right, bottom, left;
    int32_t zone;
};

struct wayland {
    int fd;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_list monitors;

    struct wayland_layer_surface_config ls_config;
    void *user_data;
    scale_callback_t user_scale_callback;
};

typedef void (*draw_callback_t)(void *, struct render_ctx *);

void render(struct wayland_monitor *mon, draw_callback_t draw, void *draw_data);

struct wayland *wayland_create(struct wayland_layer_surface_config ls_config,
                               scale_callback_t user_scale_callback,
                               void *user_data);

void wayland_destroy(struct wayland *ctx);

#endif
