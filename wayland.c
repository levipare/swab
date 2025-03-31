#include "wayland.h"

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "pixman.h"
#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void render(struct wayland_monitor *mon, draw_callback draw,
                   void *draw_data) {
    uint32_t width = mon->width * mon->scale;
    uint32_t height = mon->height * mon->scale;

    // check if buffer is out of date of output configuration
    if (width != mon->buffer.width || height != mon->buffer.height) {
        // destroy out of date buffer
        pool_buffer_destroy(&mon->buffer);

        // monitor new one
        pool_buffer_create(&mon->buffer, mon->wl->shm, width, height);
    }

    assert(mon->output);
    assert(mon->buffer.buffer);
    assert(draw);

    pixman_image_t *pix = pixman_image_create_bits_no_clear(
        PIXMAN_a8r8g8b8, width, height, mon->buffer.data,
        mon->buffer.width * 4);

    struct render_ctx rctx = {
        .pix = pix, .width = mon->buffer.width, .height = mon->buffer.height};

    // call the callback associated with an output
    draw(draw_data, &rctx);

    pixman_image_unref(pix);

    wl_surface_set_buffer_scale(mon->surface, mon->scale);
    wl_surface_attach(mon->surface, mon->buffer.buffer, 0, 0);
    wl_surface_damage(mon->surface, 0, 0, mon->width, mon->height);
    wl_surface_commit(mon->surface);
}
/* layer surface listener */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct wayland_monitor *mon = data;
    mon->width = w;
    mon->height = h;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
    log_info("layer surface closed");
    zwlr_layer_surface_v1_destroy(surface);
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* wl_output listener */
static void output_geometry(void *data, struct wl_output *wl_output, int32_t x,
                            int32_t y, int32_t physical_width,
                            int32_t physical_height, int32_t subpixel,
                            const char *make, const char *model,
                            int32_t transform) {
    struct wayland_monitor *mon = data;
    mon->dim.mm.width = physical_width;
    mon->dim.mm.height = physical_height;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    struct wayland_monitor *mon = data;
    mon->dim.px.width = width;
    mon->dim.px.height = width;
}

static void output_scale(void *data, struct wl_output *wl_output,
                         int32_t scale) {
    struct wayland_monitor *mon = data;
    mon->scale = scale;
}

static void output_name(void *data, struct wl_output *wl_output,
                        const char *name) {
    struct wayland_monitor *mon = data;
    mon->name = malloc(strlen(name) + 1);
    strcpy(mon->name, name);
}

static void output_description(void *data, struct wl_output *wl_output,
                               const char *desc) {
    // not needed
}

static void output_done(void *data, struct wl_output *wl_output) {
    struct wayland_monitor *mon = data;

    // calculate dpi
    const int32_t width_px = mon->dim.px.width;
    const int32_t height_px = mon->dim.px.height;
    const int32_t width_mm = mon->dim.mm.width;
    const int32_t height_mm = mon->dim.mm.height;
    const float diag_in =
        sqrt(pow(width_mm, 2) + pow(height_mm, 2)) * 0.03937008;
    const float diag_px = sqrt(pow(width_px, 2) + pow(height_px, 2));
    mon->dpi = width_mm == 0 && height_mm == 0 ? 96. : diag_px / diag_in;

    log_info("monitor %s configured, dpi %.f, scale %d", mon->name, mon->dpi,
             mon->scale);

    if (mon->surface) {
        render(mon, mon->wl->draw, mon->wl->draw_data);
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
    .done = output_done,
};

/* registry listener */
static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    // printf("%s %d\n", interface, version);
    struct wayland *wl = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor =
            wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wayland_monitor *mon = calloc(1, sizeof(struct wayland_monitor));
        mon->wl = wl;
        mon->output =
            wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_add_listener(mon->output, &output_listener, mon);

        wl_list_init(&mon->link);
        wl_list_insert(&wl->monitors, &mon->link);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        wl->layer_shell =
            wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface,
                             version < 4 ? version : 4);
    }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry,
                                   uint32_t name) {
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void schedule_frame(struct wayland *wl) {
    struct wayland_monitor *mon;
    wl_list_for_each(mon, &wl->monitors, link) {
        render(mon, wl->draw, wl->draw_data);
    }
}

struct wayland *wayland_create(bool bottom, uint32_t height, draw_callback draw,
                               void *draw_data) {
    struct wayland *wl = calloc(1, sizeof(*wl));
    wl_list_init(&wl->monitors);
    wl->draw = draw;
    wl->draw_data = draw_data;

    wl->display = wl_display_connect(NULL);
    wl->fd = wl_display_get_fd(wl->display);

    wl->registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(wl->registry, &wl_registry_listener, wl);
    wl_display_roundtrip(wl->display);

    // make sure we have the required globals
    assert(wl->compositor);
    assert(wl->layer_shell);
    assert(wl->shm);
    assert(!wl_list_empty(&wl->monitors));

    // trigger the listeners that were added when binding globals
    wl_display_roundtrip(wl->display);

    struct wayland_monitor *mon;
    wl_list_for_each(mon, &wl->monitors, link) {
        // every output has one surface associated with it
        mon->surface = wl_compositor_create_surface(wl->compositor);

        mon->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            wl->layer_shell, mon->surface, mon->output,
            ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wb");
        zwlr_layer_surface_v1_set_size(mon->layer_surface, 0, height);
        zwlr_layer_surface_v1_set_exclusive_zone(mon->layer_surface, height);
        zwlr_layer_surface_v1_set_anchor(
            mon->layer_surface, (bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                        : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
                                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(mon->layer_surface, 0, 0, 0, 0);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            mon->layer_surface,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_add_listener(mon->layer_surface,
                                           &layer_surface_listener, mon);

        wl_surface_commit(mon->surface);
    }

    wl_display_roundtrip(wl->display);

    // render after a roundtrip
    schedule_frame(wl);

    log_info("wayland initialized");
    return wl;
}

void wayland_destroy(struct wayland *ctx) {
    // destroy all output ctx's
    struct wayland_monitor *output, *tmp;
    wl_list_for_each_safe(output, tmp, &ctx->monitors, link) {
        wl_list_remove(&output->link);

        wl_output_release(output->output);
        wl_surface_destroy(output->surface);
        zwlr_layer_surface_v1_destroy(output->layer_surface);
        if (output->buffer.buffer) {
            wl_buffer_destroy(output->buffer.buffer);
        }
        free(output->name);
        free(output);
    }

    // globals
    zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    wl_registry_destroy(ctx->registry);
    wl_shm_destroy(ctx->shm);
    wl_compositor_destroy(ctx->compositor);

    wl_display_flush(ctx->display);
    wl_display_disconnect(ctx->display);

    free(ctx);

    log_info("wayland destroyed");
}
