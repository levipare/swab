#include "wl.h"

#include <assert.h>
#include <cairo.h>
#include <fcntl.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* layer surface listener */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    log_info("layer surface configured");

    struct wl_output_ctx *output = data;
    output->width = w;
    output->height = h;
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
static void output_geometry(void *data, struct wl_output *wl_output, int x,
                            int y, int physical_width, int physical_height,
                            int subpixel, const char *make, const char *model,
                            int transform) {
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                        int width, int height, int refresh) {
}

static void output_scale(void *data, struct wl_output *wl_output,
                         int32_t scale) {

    struct wl_output_ctx *output = data;
    output->scale = scale;

    // TODO: refresh bar (aka rerender on change of scale)
}

static void output_name(void *data, struct wl_output *wl_output,
                        const char *name) {

    struct wl_output_ctx *output = data;
    output->name = malloc(strlen(name) + 1);
    strcpy(output->name, name);
}

static void output_description(void *data, struct wl_output *wl_output,
                               const char *desc) {
}

static void output_done(void *data, struct wl_output *wl_output) {
    struct wl_output_ctx *output = data;
    log_info("output %s configured, scale %d", output->name, output->scale);
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
    struct wl_ctx *ctx = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        ctx->compositor =
            wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output_ctx *output = calloc(1, sizeof(struct wl_output_ctx));
        output->ctx = ctx;
        output->output =
            wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_add_listener(output->output, &output_listener, output);

        wl_list_init(&output->link);
        wl_list_insert(&ctx->outputs, &output->link);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
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

struct wl_ctx *wl_ctx_create(uint32_t height) {
    struct wl_ctx *ctx = calloc(1, sizeof(*ctx));
    wl_list_init(&ctx->outputs);

    ctx->display = wl_display_connect(NULL);

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &wl_registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    // make sure we have the required globals
    assert(ctx->compositor);
    assert(ctx->layer_shell);
    assert(ctx->shm);

    struct wl_output_ctx *output;
    wl_list_for_each(output, &ctx->outputs, link) {

        // configure each output
        output->surface = wl_compositor_create_surface(ctx->compositor);

        output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            ctx->layer_shell, output->surface, output->output,
            ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wb");

        // TODO: figure out height situation
        // configure the layer surface
        // we should have a bar struct that contains config like height
        // and then set the layer surface height based off that
        zwlr_layer_surface_v1_set_size(output->layer_surface, 0, height);
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, height);
        zwlr_layer_surface_v1_set_anchor(
            output->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(output->layer_surface, 0, 0, 0, 0);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            output->layer_surface,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                           &layer_surface_listener, output);

        wl_surface_commit(output->surface);
    }

    wl_display_roundtrip(ctx->display);

    log_info("initialized wayland");

    return ctx;
}

void wl_ctx_destroy(struct wl_ctx *ctx) {
    // destroy all output ctx's
    struct wl_output_ctx *output, *tmp;
    wl_list_for_each_safe(output, tmp, &ctx->outputs, link) {
        wl_list_remove(&output->link);

        wl_output_release(output->output);
        wl_surface_destroy(output->surface);
        zwlr_layer_surface_v1_destroy(output->layer_surface);
        if (output->buffer.buffer) {
            wl_buffer_destroy(output->buffer.buffer);
            cairo_surface_destroy(output->cairo_surface);
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
}

void render(struct wl_output_ctx *output,
            void (*draw)(void *, struct render_ctx *), void *data) {
    uint32_t width = output->width * output->scale;
    uint32_t height = output->height * output->scale;

    // check if buffer is out of date of output configuration
    if (width != output->buffer.width || height != output->buffer.height) {
        // destroy out of date buffer
        pool_buffer_destroy(&output->buffer);

        // create new one
        pool_buffer_create(&output->buffer, output->ctx->shm, width, height);

        output->cairo_surface = cairo_image_surface_create_for_data(
            output->buffer.data, CAIRO_FORMAT_ARGB32,
            output->width * output->scale, output->height * output->scale,
            output->scale * output->width * 4);
    }

    assert(output->output);
    assert(output->buffer.buffer);
    assert(draw);

    cairo_t *cr = cairo_create(output->cairo_surface);
    cairo_scale(cr, output->scale, output->scale);
    PangoContext *pango = pango_cairo_create_context(cr);

    struct render_ctx rctx = {.cr = cr,
                              .pango = pango,
                              .width = output->width,
                              .height = output->height};

    // call the callback associated with an output
    draw(data, &rctx);

    cairo_destroy(cr);
    g_object_unref(pango);

    wl_surface_set_buffer_scale(output->surface, output->scale);
    wl_surface_attach(output->surface, output->buffer.buffer, 0, 0);
    wl_surface_damage(output->surface, 0, 0, output->width, output->height);
    wl_surface_commit(output->surface);
}
