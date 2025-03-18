#include "../wb.h"

#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

struct private {
    char status[32];
};
static void difftimespec(struct timespec *res, struct timespec *a,
                         struct timespec *b) {
    res->tv_sec = a->tv_sec - b->tv_sec - (a->tv_nsec < b->tv_nsec);
    res->tv_nsec = a->tv_nsec - b->tv_nsec + (a->tv_nsec < b->tv_nsec) * 1E9;
}

void run(struct wb_module *mod) {
    struct private *p = mod->private;
    while (1) {
        pthread_testcancel();

        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

        time_t t = time(NULL);
        strftime(p->status, sizeof(p->status), "%a %b %-d %-I:%M:%S %p",
                 localtime(&t));

        wb_refresh(mod->bar);

        // sleep until the next second boundary
        struct timespec next_sec = {.tv_sec = start.tv_sec + 1, .tv_nsec = 0};
        struct timespec sleep_time;
        difftimespec(&sleep_time, &next_sec, &start);
        nanosleep(&sleep_time, NULL);
    }
}

void destroy(struct wb_module *mod) {
    free(mod->private);
    free(mod);
}

char *content(struct wb_module *mod) {
    struct private *p = mod->private;
    return p->status;
}

// static void draw(struct wb_module *mod, struct render_ctx *ctx) {
//     struct private *p = mod->private;

//     PangoLayout *layout = pango_layout_new(ctx->pango);
//     PangoFontDescription *font_desc =
//         pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
//     pango_layout_set_font_description(layout, font_desc);
//     pango_font_description_free(font_desc);

//     pango_layout_set_text(layout, p->status, -1);

//     int width, height;
//     pango_layout_get_pixel_size(layout, &width, &height);

//     cairo_move_to(ctx->cr, ctx->width - width - 10,
//                   (ctx->height - height) / 2.0);
//     cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);

//     pango_cairo_show_layout(ctx->cr, layout);

//     g_object_unref(layout);
// }

struct wb_module *datetime_create() {
    struct wb_module *mod = calloc(1, sizeof(*mod));
    mod->name = "datetime";
    mod->private = calloc(1, sizeof(struct private));
    mod->run = run;
    mod->destroy = destroy;
    mod->content = content;

    return mod;
}
