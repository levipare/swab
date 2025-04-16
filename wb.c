#include "wb.h"

#include <assert.h>
#include <fcft/fcft.h>
#include <fontconfig/fontconfig.h>
#include <pixman.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "wayland.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define ALIGNMENT_SEP '\x1f'

static pixman_color_t argb_to_pixman(uint32_t argb) {
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

size_t mbsntoc32(char32_t *dst, const char *src, size_t nms, size_t len) {
    mbstate_t ps = {0};

    char32_t *out = dst;
    const char *in = src;

    size_t consumed = 0;
    size_t chars = 0;
    size_t rc;

    while ((out == NULL || chars < len) && consumed < nms &&
           (rc = mbrtoc32(out, in, nms - consumed, &ps)) != 0) {
        switch (rc) {
        case 0:
            goto done;

        case (size_t)-1:
        case (size_t)-2:
        case (size_t)-3:
            goto err;
        }

        in += rc;
        consumed += rc;
        chars++;

        if (out != NULL)
            out++;
    }

done:
    return chars;

err:
    return (size_t)-1;
}

enum align { ALIGN_START, ALIGN_CENTER, ALIGN_END };

static void draw_text(struct fcft_font *font, pixman_image_t *pix,
                      pixman_color_t *color, const char *cstr, int32_t x,
                      int32_t y, enum align horiz, enum align vert) {
    size_t len = strlen(cstr);
    if (len == 0) {
        return;
    }

    char32_t str32[len];
    size_t n = mbsntoc32(str32, cstr, len + 1, len);
    if (n == (size_t)-1) {
        log_fatal("failed to convert multi-byte string to wchar_t string");
    }
    struct fcft_text_run *text_run =
        fcft_rasterize_text_run_utf32(font, n, str32, FCFT_SUBPIXEL_NONE);
    assert(text_run);

    // calculate width of the run
    int run_width = 0;
    for (int i = 0; i < text_run->count; ++i) {
        const struct fcft_glyph *g = text_run->glyphs[i];
        run_width += g->advance.x;
    }

    switch (horiz) {
    case ALIGN_START:
        break;
    case ALIGN_CENTER:
        x -= run_width / 2;
        break;
    case ALIGN_END:
        x -= run_width;
        break;
    }

    switch (vert) {
    case ALIGN_START:
        y += font->ascent;
        break;
    case ALIGN_CENTER:
        y += (font->ascent + font->descent) / 2.0 -
             (font->descent > 0 ? font->descent : 0);
        break;
    case ALIGN_END:
        break;
    }

    pixman_image_t *clr_pix = pixman_image_create_solid_fill(color);

    // render each glyph
    for (int i = 0; i < text_run->count; ++i) {
        const struct fcft_glyph *g = text_run->glyphs[i];
        if (g->is_color_glyph) {
            pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, pix, 0, 0, 0,
                                     0, x + g->x, y - g->y, g->width,
                                     g->height);
        } else {
            pixman_image_composite32(PIXMAN_OP_OVER, clr_pix, g->pix, pix, 0, 0,
                                     0, 0, x + g->x, y - g->y, g->width,
                                     g->height);
        }
        x += g->advance.x;
    }
    pixman_image_unref(clr_pix);
    fcft_text_run_destroy(text_run);
}

static void draw_bar(void *data, struct render_ctx *ctx) {
    struct wb *bar = data;

    // Fill background with a color
    pixman_color_t bg = argb_to_pixman(bar->config.bg_color);
    pixman_image_fill_rectangles(
        PIXMAN_OP_SRC, ctx->pix, &bg, 1,
        &(pixman_rectangle16_t){0, 0, ctx->width, ctx->height});

    // draw text
    pixman_color_t fg = argb_to_pixman(bar->config.fg_color);
    const char *status = bar->status;
    for (int i = 0; i < 3 && *status; ++i) {
        // find position of seperator or null terminator
        const char *end = &status[strcspn(status, "\x1f")];
        // len does not include seperator or null terminator
        size_t len = end - status;
        char comp[len + 1];
        memcpy(comp, status, len);
        comp[len] = '\0';

        enum align horiz = ALIGN_START;
        int32_t x = 0;
        switch (i) {
        case 1:
            horiz = ALIGN_CENTER;
            x = ctx->width / 2;
            break;
        case 2:
            horiz = ALIGN_END;
            x = ctx->width;
            break;
        }

        draw_text(bar->font, ctx->pix, &fg, comp, x, ctx->height / 2, horiz,
                  ALIGN_CENTER);

        // +1 if we haven't reached the end of the status string
        status += len + (*end == ALIGNMENT_SEP);
    }

    // sanity check to ensure no buffer overflow
    assert(status <= strchr(bar->status, '\0'));
}

// Expects a pointer to a heap allocated string and will reallocate the given
// string in order to append the formatted string
static void strappf(char **cstr_ptr, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int attr_len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    assert(attr_len >= 0);

    // allocate space for attribute
    *cstr_ptr = realloc(*cstr_ptr, strlen(*cstr_ptr) + attr_len + 1);

    // format
    char attr[attr_len + 1];
    va_start(args, fmt);
    vsnprintf(attr, sizeof(attr), fmt, args);
    va_end(args);

    // append
    strcat(*cstr_ptr, attr);
}

// takes a fontconfig font pattern and scales the size and pixelsize
// attributes according to the scale paramter
// * returns a heap allocated string
static char *scale_font_pattern(const char *pattern, int32_t scale) {
    FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
    double pt_size = -1.0;
    FcResult have_pt_size = FcPatternGetDouble(pat, FC_SIZE, 0, &pt_size);

    double px_size = -1.0;
    FcResult have_px_size = FcPatternGetDouble(pat, FC_PIXEL_SIZE, 0, &px_size);

    FcPatternRemove(pat, FC_SIZE, 0);
    FcPatternRemove(pat, FC_PIXEL_SIZE, 0);

    char *stripped = (char *)FcNameUnparse(pat);
    if (have_pt_size == FcResultMatch) {
        strappf(&stripped, ":size=%.2f", pt_size * scale);
    }
    if (have_px_size == FcResultMatch) {
        strappf(&stripped, ":pixelsize=%.2f", px_size * scale);
    }

    FcPatternDestroy(pat);

    return stripped;
}

static void event_loop(struct wb *bar) {
    enum { POLL_WL, POLL_STDIN };
    struct pollfd fds[] = {
        [POLL_WL] = {.fd = bar->wl->fd, .events = POLLIN},
        [POLL_STDIN] = {.fd = STDIN_FILENO, .events = POLLIN},
    };
    while (true) {
        int ret;
        do {
            ret = wl_display_dispatch_pending(bar->wl->display);
            wl_display_flush(bar->wl->display);
        } while (ret == -1);

        ret = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (ret < 0) {
            log_fatal("poll failed");
        }

        // wayland events
        if (fds[POLL_WL].revents & POLLIN) {
            wl_display_dispatch(bar->wl->display);
        }
        if (fds[POLL_WL].revents & POLLHUP) {
            break; // if wayland disconnects then exit event loop
        }

        // stdin events
        if (fds[POLL_STDIN].revents & POLLIN) {
            if (!fgets(bar->status, sizeof(bar->status), stdin)) {
                log_error("error while reading in status");
                continue;
            }
            bar->status[strcspn(bar->status, "\n")] = '\0';

            struct wayland_monitor *mon;
            wl_list_for_each(mon, &bar->wl->monitors, link) {
                render(mon, draw_bar, bar);
            }
        }
        if (fds[POLL_STDIN].revents & POLLHUP) {
            break;
        }
    }
}

static void on_scale(void *data, struct wayland_monitor *mon, int32_t scale) {
    struct wb *bar = data;

    // destroy previous font
    fcft_destroy(bar->font);

    // load new font
    const char *fonts[] = {scale_font_pattern(bar->config.font, scale)};
    bar->font = fcft_from_name(sizeof(fonts) / sizeof(fonts[0]), fonts, NULL);
    free((char *)fonts[0]);
    if (!bar->font) {
        log_fatal("failed to load font '%s'", bar->config.font);
    }
    log_info("loaded font: %s", bar->font->name);

    // render with new font
    render(mon, draw_bar, bar);
}

void wb_run(struct wb_config config) {
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_NONE);

    struct wb *bar = calloc(1, sizeof(*bar));
    bar->config = config;
    struct wayland_layer_surface_config ls_config = {
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .height = config.height,
        .zone = config.height,
        .anchor = (config.bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                 : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT};
    bar->wl = wayland_create(ls_config, on_scale, bar);

    // the main event loop which handles input and wayland events
    event_loop(bar);

    // cleanup
    wayland_destroy(bar->wl);
    fcft_destroy(bar->font);
    fcft_fini();
    free(bar);
}
