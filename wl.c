#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "shm.h"
#include "wl.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* wl_buffer listener */
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

/* layer surface listener */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    struct wl_ctx *ctx = data;
    ctx->width = w * ctx->scale;
    ctx->height = h * ctx->scale;
    // TODO: resize shm_pool maybe?
    // the bar should probobly never resize but ehh

    // printf("%dx%d\n", ctx->width, ctx->height);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
    zwlr_layer_surface_v1_destroy(surface);
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* wl_output listener */
static void output_geometry(void *data, struct wl_output *wl_output, int x,
                            int y, int physical_width, int physical_height,
                            int subpixel, const char *make, const char *model,
                            int transform) {
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                        int width, int height, int refresh) {
}

static void output_done(void *data, struct wl_output *wl_output) {
}

static void output_scale(void *data, struct wl_output *wl_output,
                         int32_t scale) {
    struct wl_ctx *ctx = data;
    ctx->scale = scale;
}

static const struct wl_output_listener wl_output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

/* registry listener */
static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    // printf("%s %d\n", interface, version);
    struct wl_ctx *ctx = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        log_info("found compositor");
        ctx->compositor =
            wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_output") == 0) {
        log_info("found output");
        ctx->output =
            wl_registry_bind(wl_registry, name, &wl_output_interface, 2);
        wl_output_add_listener(ctx->output, &wl_output_listener, ctx);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        log_info("found layer shell");
        ctx->layer_shell =
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

struct wl_ctx *wl_ctx_create() {
    struct wl_ctx *ctx = malloc(sizeof(*ctx));

    ctx->display = wl_display_connect(NULL);
    assert(ctx->display);
    log_info("connected to display");

    ctx->registry = wl_display_get_registry(ctx->display);
    assert(ctx->registry);
    wl_registry_add_listener(ctx->registry, &wl_registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    // create wayland objects
    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    assert(ctx->surface);
    log_info("created surface");

    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        "wb");
    assert(ctx->layer_surface);
    log_info("created layer surface");

    // TODO: figure out height situation
    // configure the layer surface
    // we should a a bar struct that contains config like height
    // and then set the layer surface height based off that
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, 20);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, 20);
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(ctx->layer_surface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface,
                                       &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_roundtrip(ctx->display);

    // TODO: make this seperate from wl initialization
    // create buffer after getting surface size
    int stride = ctx->width * 4;
    size_t size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    assert(fd != -1);

    ctx->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(ctx->shm_data != MAP_FAILED);

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    ctx->buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height,
                                            stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    ctx->cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ctx->shm_data, CAIRO_FORMAT_ARGB32, ctx->width,
        ctx->height, ctx->width * 4);

    return ctx;
}

void wl_ctx_destroy(struct wl_ctx *ctx) {
    munmap(ctx->shm_data, ctx->width * ctx->height * 4);
    wl_buffer_destroy(ctx->buffer);

    // TODO: make this seperate from wayland destruction
    cairo_surface_destroy(ctx->cairo_surface);
}

void render(struct wl_ctx *ctx, draw_callback_t draw) {
    cairo_t *cr = cairo_create(ctx->cairo_surface);

    draw(ctx, cr);

    cairo_destroy(cr);

    wl_surface_set_buffer_scale(ctx->surface, ctx->scale);
    wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
    wl_surface_damage(ctx->surface, 0, 0, ctx->width, ctx->height);
    wl_surface_commit(ctx->surface);
}
