#include "cairo.h"
#include "log.h"
#include "wb.h"
#include "wl.h"

#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <wayland-client.h>

#define HEX_TO_RGBA(x)                                                         \
    ((x >> 24) & 0xFF) / 255.0, ((x >> 16) & 0xFF) / 255.0,                    \
        ((x >> 8) & 0xFF) / 255.0, (x & 0xFF) / 255.0

#define ALIGNMENT_SEP '\x1f'

static void draw_bar(void *data, struct render_ctx *ctx) {
    struct wb *bar = data;

    // Fill background with a color
    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(bar->config.bg_color));
    cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
    cairo_fill(ctx->cr);

    PangoLayout *layout = pango_layout_new(ctx->pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    // draw text
    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(bar->config.fg_color));
    const char *content = bar->content;
    for (int i = 0; i < 3 && *content; ++i) {
        // find position of seperator char or null terminator
        char *end = strchrnul(content, ALIGNMENT_SEP);
        // len does not include seperator or null terminator
        size_t len = end - content;

        pango_layout_set_text(layout, content, len);
        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);

        // calculate alignment
        double x = 0;
        if (i == 1) {
            x = (ctx->width - width) / 2.0;
        } else if (i == 2) {
            x = ctx->width - width;
        }

        cairo_move_to(ctx->cr, x, (ctx->height - height) / 2.0);
        pango_cairo_show_layout(ctx->cr, layout);

        // +1 if we haven't reached the end of the content string
        content += len + (*end == ALIGNMENT_SEP);
    }

    // sanity check to make sure the content pointer always results
    // <= the position of the null terminator of bar->content
    assert(content <= strchr(bar->content, '\0'));
}

static void *handle_stdin(void *data) {
    struct wb *bar = data;

    struct pollfd fds[] = {
        [0] =
            {
                .fd = STDIN_FILENO,
                .events = POLLIN,
            },
        [1] =
            {
                .fd = bar->exit_pipe[0],
                .events = POLLIN,
            },
    };

    while (true) {
        int ret = poll(fds, 2, -1);

        if (ret < 0) {
            log_fatal("poll error");
        }

        if (fds[1].revents & POLLIN) {
            log_info("thread exit signal recieved");
            break;
        }

        if (fds[0].revents & POLLIN) {
            // check if fgets detects EOF -- if so then the thread exits
            if (!fgets(bar->content, sizeof(bar->content), stdin)) {
                break;
            }
            bar->content[strcspn(bar->content, "\n")] = '\0'; // replace newline

            // render bar to each output
            struct wl_output_ctx *output;
            wl_list_for_each(output, &bar->wl->outputs, link) {
                render(output, draw_bar, bar);
            }
            wl_display_flush(bar->wl->display);
        }
    }

    return NULL;
}

void wb_run(struct wb_config config) {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->config = config;
    bar->wl = wl_ctx_create(config.height);

    // initial render on all outputs
    struct wl_output_ctx *output;
    wl_list_for_each(output, &bar->wl->outputs, link) {
        render(output, draw_bar, bar);
    }
    wl_display_flush(bar->wl->display);

    // create exit pipe to signal thread when to exit
    if (pipe(bar->exit_pipe) < 0) {
        log_fatal("failed to create exit pipe");
    }

    // create thread to handle stdin
    pthread_t stdin_thread;
    pthread_create(&stdin_thread, NULL, handle_stdin, bar);

    // dispatch wl events
    while (wl_display_dispatch(bar->wl->display)) {
        if (bar->exit) {
            log_info("exiting");
            break;
        }
    }

    // signal thread to exit
    write(bar->exit_pipe[1], "x", 1);
    // wait for thread to finish
    pthread_join(stdin_thread, NULL);

    if (bar->wl) {
        wl_ctx_destroy(bar->wl);
    }

    free(bar);
}
