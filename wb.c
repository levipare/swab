#include "wb.h"

#include <assert.h>
#include <fcft/fcft.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wchar.h>

#include "log.h"
#include "pixman.h"
#include "wl.h"

#define ALIGNMENT_SEP '\x1f'

pixman_color_t argb_to_pixman(uint32_t argb) {
    uint16_t a = (argb >> 24) & 0xFF;
    uint16_t r = (argb >> 16) & 0xFF;
    uint16_t g = (argb >> 8) & 0xFF;
    uint16_t b = (argb >> 0) & 0xFF;
    // premultiplies
    pixman_color_t color = {
        (r << 8) * a / 0xFF,
        (g << 8) * a / 0xFF,
        (b << 8) * a / 0xFF,
        a << 8,
    };

    return color;
}

static void draw_bar(void *data, struct render_ctx *ctx) {
    struct wb *bar = data;

    // Fill background with a color
    pixman_color_t bg = argb_to_pixman(bar->config.bg_color);
    pixman_image_fill_rectangles(
        PIXMAN_OP_SRC, ctx->pix, &bg, 1,
        &(pixman_rectangle16_t){0, 0, ctx->width, ctx->height});

    // draw text
    const char *content = bar->content;
    for (int i = 0; i < 3 && *content; ++i) {
        // find position of seperator or null terminator
        const char *end = &content[strcspn(content, "\x1f")];
        // len does not include seperator or null terminator
        size_t len = end - content;

        // convert input string to wc string
        wchar_t str[len];
        int n = mbstowcs(str, content, len);
        if (n == -1) {
            log_fatal("failed to convert multi-byte string to wchar_t string");
        }
        struct fcft_text_run *text_run = fcft_rasterize_text_run_utf32(
            bar->font, n, (uint32_t *)str, FCFT_SUBPIXEL_DEFAULT);
        assert(text_run);

        // calculate width of the run
        int run_width = 0;
        for (int i = 0; i < text_run->count; ++i) {
            const struct fcft_glyph *g = text_run->glyphs[i];
            run_width += g->advance.x;
        }
        // calculate alignment (left, center, right)
        int x = 0;
        if (i == 1) {
            x = (ctx->width - run_width) / 2;
        } else if (i == 2) {
            x = ctx->width - run_width;
        }
        // center text vertically
        int y = (ctx->height - bar->font->height) / 2;

        uint32_t fg_color = bar->config.fg_color;
        pixman_color_t fg = {
            (fg_color >> 8) & 0xFF00,
            (fg_color) & 0xFF00,
            (fg_color << 8) & 0xFF00,
            (fg_color >> 16) & 0xFF00,
        };
        pixman_image_t *clr_pix = pixman_image_create_solid_fill(&fg);
        // render each glyph
        for (int i = 0; i < text_run->count; ++i) {
            const struct fcft_glyph *g = text_run->glyphs[i];
            pixman_image_composite32(
                PIXMAN_OP_OVER, clr_pix, g->pix, ctx->pix, 0, 0, 0, 0, x + g->x,
                y + bar->font->ascent - g->y, g->width, g->height);
            x += g->advance.x;
            y += g->advance.y;
        }
        pixman_image_unref(clr_pix);
        fcft_text_run_destroy(text_run);

        // +1 if we haven't reached the end of the content string
        content += len + (*end == ALIGNMENT_SEP);
    }

    // sanity check to ensure no buffer overflow
    assert(content <= strchr(bar->content, '\0'));
}

void wb_run(struct wb_config config) {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->config = config;
    bar->wl = wl_ctx_create(config.bottom, config.height, draw_bar, bar);

    // load font
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_NONE);
    bar->font = fcft_from_name(1, (const char *[]){"monospace:size=24"}, NULL);
    assert(bar->font);
    log_info("using font: %s", bar->font->name);

    // main loop
    enum { POLL_STDIN, POLL_WL };
    struct pollfd fds[] = {
        [POLL_WL] = {.fd = bar->wl->fd, .events = POLLIN | POLLOUT},
        [POLL_STDIN] = {.fd = STDIN_FILENO, .events = POLLIN},
    };
    while (true) {
        int ret = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (ret < 0) {
            log_fatal("poll error");
        }

        // wayland events
        if (fds[POLL_WL].revents & POLLIN) {
            wl_display_dispatch(bar->wl->display);
        }

        if (fds[POLL_WL].revents & POLLOUT) {
            wl_display_flush(bar->wl->display);
        }

        if (fds[POLL_WL].revents & POLLHUP) {
            break;
        }

        // stdin events
        if (fds[POLL_STDIN].revents & POLLIN) {
            char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            assert(n > 0);
            buf[n] = '\0';

            // it is possible that multiple events were recieved
            // we only want the most recent one
            char *last_nl = strrchr(buf, '\n');
            if (!last_nl) {
                log_error("input received does not contain newline");
                continue;
            }

            *last_nl = '\0';
            char *second_last_nl = strrchr(buf, '\n');
            strcpy(bar->content, second_last_nl ? second_last_nl + 1 : buf);

            schedule_frame(bar->wl);
        }

        if (fds[POLL_STDIN].revents & POLLHUP) {
            fds[POLL_STDIN].fd = -1; // disable polling of stdin
        }
    }

    // cleanup
    if (bar->wl) {
        wl_ctx_destroy(bar->wl);
    }

    fcft_destroy(bar->font);
    free(bar);
}
