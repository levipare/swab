#include "log.h"
#include "wb.h"
#include "wl.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <wayland-client-core.h>

static void *module_thread_run(void *data) {
    struct wb_module *mod = data;
    log_info("started module (%s)", mod->name);
    mod->run(mod);

    return NULL;
}

static void draw_bar(void *data, struct render_ctx *ctx) {
    struct wb *bar = data;

    // Fill background with a color
    cairo_set_source_rgb(ctx->cr, 0x0C / 255.0, 0x0C / 255.0, 0x0C / 255.0);
    cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
    cairo_fill(ctx->cr);

    // setup pango to render to cairo
    PangoLayout *layout = pango_layout_new(ctx->pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    // right side
    struct wb_module *datetime = bar->modules[0];
    cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);
    pango_layout_set_text(layout, datetime->content(datetime), -1);
    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(ctx->cr, ctx->width - width - 12,
                  (ctx->height - height) / 2.0);
    pango_cairo_show_layout(ctx->cr, layout);
    pango_cairo_show_layout(ctx->cr, layout);
}

void wb_refresh(struct wb *bar) {
    render(bar->wl->outputs, draw_bar, bar);
    wl_display_flush(bar->wl->display);
}

void wb_add_module(struct wb *bar, struct wb_module *mod) {
    assert(mod);
    assert(mod->run);
    assert(mod->content);
    assert(mod->destroy);
    assert(bar);

    mod->bar = bar;

    bar->modules =
        realloc(bar->modules, (bar->module_count + 1) * sizeof(*bar->modules));
    bar->modules[bar->module_count++] = mod;

    log_info("added module (%s)", mod->name);
}

struct wb *wb_create() {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->wl = wl_ctx_create();
    bar->output_name = bar->wl->outputs->name;

    return bar;
}

void wb_destroy(struct wb *bar) {
    log_info("wb destroy");

    // destroy everything
    for (int i = 0; i < bar->module_count; ++i) {
        struct wb_module *mod = bar->modules[i];
        pthread_cancel(mod->thread);
        mod->destroy(mod);
    }

    wl_ctx_destroy(bar->wl);

    free(bar);
}

void wb_run(struct wb *bar) {
    log_info("wb run");
    assert(bar->wl);
    assert(bar->output_name);

    // start all modules
    for (int i = 0; i < bar->module_count; ++i) {
        struct wb_module *mod = bar->modules[i];
        pthread_create(&mod->thread, NULL, module_thread_run, mod);
    }

    // dispatch wl events
    while (wl_display_dispatch(bar->wl->display)) {
    }
}
