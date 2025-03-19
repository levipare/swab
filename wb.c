#include "log.h"
#include "wb.h"
#include "wl.h"

#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define HEX_TO_RGBA(x)                                                         \
    ((x >> 24) & 0xFF) / 255.0, ((x >> 16) & 0xFF) / 255.0,                    \
        ((x >> 8) & 0xFF) / 255.0, (x & 0xFF) / 255.0

#define BG_COLOR 0x0C0C0CFF
#define FG_COLOR 0xBBBBBBFF

static void draw_bar(void *data, struct render_ctx *ctx) {
    // Fill background with a color
    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(BG_COLOR));
    cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
    cairo_fill(ctx->cr);

    PangoLayout *layout = pango_layout_new(ctx->pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(FG_COLOR));

    // draw text
    const char *content = data;
    for (int i = 0; i < 3 && *content; ++i) {
        size_t len = strcspn(content, "\n");
        pango_layout_set_text(layout, content, len);

        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);

        double x = 0;
        if (i == 1) {
            x = (ctx->width - width) / 2.0;
        } else if (i == 2) {
            x = ctx->width - width;
        }

        cairo_move_to(ctx->cr, x, 0);
        pango_cairo_show_layout(ctx->cr, layout);

        content += len + (content[len] == '\n');
    }
}

static void *handle_stdin(void *data) {
    struct wb *bar = data;

    char buf[1024];
    while (read(STDIN_FILENO, buf, sizeof(buf))) {
        render(bar->wl->outputs, draw_bar, buf);
        wl_display_flush(bar->wl->display);
    }

    return NULL;
}

struct wb *wb_create() {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->wl = wl_ctx_create(50);
    bar->output_name = bar->wl->outputs->name;

    return bar;
}

void wb_destroy(struct wb *bar) {
    log_info("wb destroy");

    if (bar->wl) {
        wl_ctx_destroy(bar->wl);
    }

    free(bar);
}

void wb_run(struct wb *bar) {
    log_info("wb run");
    assert(bar->wl);
    assert(bar->output_name);

    // draw blank bar
    render(bar->wl->outputs, draw_bar, "test\npoop");
    wl_display_flush(bar->wl->display);

    // create thread to handle stdin
    pthread_t t;
    pthread_create(&t, NULL, handle_stdin, bar);

    // dispatch wl events
    while (wl_display_dispatch(bar->wl->display)) {
    }
}
