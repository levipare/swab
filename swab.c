#include <assert.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <pixman.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <time.h>
#include <uchar.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "wlr-layer-shell-unstable-v1-protocol.h"

struct buffer {
    uint32_t width, height, size;
    void *data;
    struct wl_buffer *wlbuf;
    pixman_image_t *pix;
};

struct monitor {
    struct wl_output *output;
    char *name;
    int32_t scale;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t surface_width, surface_height;

    struct fcft_font *font;

    struct wl_list link;
};

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_list monitors;

static char input[1024];

static char *font = "monospace:size=10";
static uint32_t fg = 0xbbbbbbff;
static uint32_t bg = 0x0c0c0cff;
static bool topbar = true;
static double lineheight = 1.0;

static void noop() {}

static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    exit(EXIT_FAILURE);
}

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *wlbuf) {
    struct buffer *buf = data;
    wl_buffer_destroy(buf->wlbuf);
    pixman_image_unref(buf->pix);
    munmap(buf->data, buf->size);
    free(buf);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static struct buffer *buffer_create(uint32_t width, uint32_t height) {
    assert(width > 0);
    assert(height > 0);

    uint32_t stride = width * 4;
    uint32_t size = stride * height;

    int fd = allocate_shm_file(stride * height);
    assert(fd != -1);

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(data != MAP_FAILED);

    struct buffer *buf = malloc(sizeof(*buf));
    buf->width = width;
    buf->height = height;
    buf->size = size;
    buf->data = data;
    buf->pix = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, width, height, data, stride);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    buf->wlbuf = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buf->wlbuf, &buffer_listener, buf);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buf;
}

static inline pixman_color_t rgba_to_pixman(uint32_t c) {
    return (pixman_color_t){
        ((c >> 24) & 0xFF) * 0x101 * (c & 0xFF) / 0xFF,
        ((c >> 16) & 0xFF) * 0x101 * (c & 0xFF) / 0xFF,
        ((c >> 8) & 0xFF) * 0x101 * (c & 0xFF) / 0xFF,
        (c & 0xFF) * 0x101,
    };
}

static size_t mbsntoc32(char32_t *dst, const char *src, size_t nms, size_t len) {
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

static void draw_bar(struct monitor *mon) {
    struct buffer *buffer =
        buffer_create(mon->surface_width * mon->scale, mon->surface_height * mon->scale);

    const pixman_color_t fg_color = rgba_to_pixman(fg);
    pixman_image_t *fg_pix = pixman_image_create_solid_fill(&fg_color);
    const pixman_color_t bg_color = rgba_to_pixman(bg);

    pixman_image_fill_rectangles(PIXMAN_OP_SRC, buffer->pix, &bg_color, 1,
                                 &(pixman_rectangle16_t){0, 0, buffer->width, buffer->height});

    char *rest = input;
    char *text;
    int i = 0;
    while ((text = strsep(&rest, "^")) != NULL) {
        size_t wlen = mbsntoc32(NULL, text, strlen(text), 0);
        if (wlen == -1) {
            die("mbsntoc32():");
        }
        char32_t *text32 = malloc((wlen + 1) * sizeof(*text32));
        mbsntoc32(text32, text, strlen(text), wlen);
        text32[wlen] = 0;

        struct fcft_text_run *text_run =
            fcft_rasterize_text_run_utf32(mon->font, wlen, text32, FCFT_SUBPIXEL_NONE);

        int32_t text_width = 0;
        for (int i = 0; i < text_run->count; ++i) {
            text_width += text_run->glyphs[i]->advance.x;
        }

        int32_t x = 0;
        switch (i) {
        case 1:
            x = (buffer->width - text_width) / 2;
            break;
        case 2:
            x = buffer->width - text_width;
            break;
        }

        int32_t y = buffer->height / 2;
        y += (mon->font->ascent + mon->font->descent) / 2.0 -
             (mon->font->descent > 0 ? mon->font->descent : 0);

        for (int i = 0; i < text_run->count; ++i) {
            const struct fcft_glyph *g = text_run->glyphs[i];

            if (g->is_color_glyph) {
                pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, buffer->pix, 0, 0, 0, 0,
                                         x + g->x, y - g->y, g->width, g->height);
            } else {
                pixman_image_composite32(PIXMAN_OP_OVER, fg_pix, g->pix, buffer->pix, 0, 0, 0, 0,
                                         x + g->x, y - g->y, g->width, g->height);
            }

            x += g->advance.x;
        }
        free(text32);
        fcft_text_run_destroy(text_run);
        i++;
    }
    pixman_image_unref(fg_pix);

    wl_surface_set_buffer_scale(mon->surface, mon->scale);
    wl_surface_attach(mon->surface, buffer->wlbuf, 0, 0);
    wl_surface_damage_buffer(mon->surface, 0, 0, buffer->width, buffer->height);
    wl_surface_commit(mon->surface);
}

static void draw() {
    struct monitor *mon;
    wl_list_for_each(mon, &monitors, link) draw_bar(mon);
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct monitor *mon = data;
    mon->surface_width = w;
    mon->surface_height = h;
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    printf("%s layer surface %dx%d\n", mon->name, w, h);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    printf("layer surface closed");
    zwlr_layer_surface_v1_destroy(surface);
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void output_scale(void *data, struct wl_output *wl_output, int32_t scale) {
    struct monitor *mon = data;
    mon->scale = scale;
    printf("%s scale %d\n", mon->name, mon->scale);

    fcft_destroy(mon->font);
    char attrs[32];
    snprintf(attrs, sizeof(attrs), "dpi=%d", 96 * scale);
    mon->font = fcft_from_name(1, (const char *[]){font}, attrs);
    assert(mon->font);
    printf("%s font %s\n", mon->name, mon->font->name);

    if (!mon->surface) {
        int height = lineheight * mon->font->height / scale;

        mon->surface = wl_compositor_create_surface(compositor);
        mon->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell, mon->surface, mon->output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wb");
        zwlr_layer_surface_v1_set_size(mon->layer_surface, 0, height);
        zwlr_layer_surface_v1_set_exclusive_zone(mon->layer_surface, height);
        zwlr_layer_surface_v1_set_anchor(
            mon->layer_surface,
            (topbar ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(mon->layer_surface, 0, 0, 0, 0);
        zwlr_layer_surface_v1_add_listener(mon->layer_surface, &layer_surface_listener, mon);

        wl_surface_commit(mon->surface);
    } else {
        draw_bar(mon);
    }
}

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    struct monitor *mon = data;
    free(mon->name);
    mon->name = strdup(name);
    printf("%s name\n", mon->name);
}

static const struct wl_output_listener output_listener = {
    .geometry = noop,
    .mode = noop,
    .description = noop,
    .done = noop,
    .name = output_name,
    .scale = output_scale,
};

static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name,
                            const char *interface, uint32_t version) {
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct monitor *mon = calloc(1, sizeof(*mon));
        mon->output = wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_add_listener(mon->output, &output_listener, mon);
        wl_list_insert(&monitors, &mon->link);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        layer_shell = wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 3);
    }
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
};

void setup() {
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_NONE);

    if (!(display = wl_display_connect(NULL))) {
        die("failed to connect to wayland");
    }
    wl_list_init(&monitors);
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &wl_registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!shm || !compositor || !layer_shell) {
        die("unsupported compositor");
    }

    // roundtrip so listeners added during the registry events are handled
    // primarily output events so that we can create surfaces prior to rendering
    wl_display_roundtrip(display);

    // and again to register the surface commit
    wl_display_roundtrip(display);

    // initial render
    draw();
}

void run() {
    enum { POLL_WL, POLL_STDIN };
    struct pollfd fds[] = {
        [POLL_WL] = {.fd = wl_display_get_fd(display), .events = POLLIN},
        [POLL_STDIN] = {.fd = STDIN_FILENO, .events = POLLIN},
    };

    while (true) {
        wl_display_flush(display);

        if (poll(fds, 2, -1) < 0) {
            die("poll:");
        }

        // wayland events
        if (fds[POLL_WL].revents & POLLIN) {
            wl_display_dispatch(display);
        }
        if (fds[POLL_WL].revents & POLLHUP) {
            break;
        }

        // stdin events
        if (fds[POLL_STDIN].revents & POLLIN) {
            if (!fgets(input, sizeof(input), stdin)) {
                die("error while reading input");
            }
            input[strcspn(input, "\n")] = '\0';
            draw();
        }
        if (fds[POLL_STDIN].revents & POLLHUP) {
            fds[POLL_STDIN].fd = -1;
        }
    }
}

void cleanup() {
    struct monitor *mon, *tmp;
    wl_list_for_each_safe(mon, tmp, &monitors, link) {
        wl_list_remove(&mon->link);
        fcft_destroy(mon->font);
        wl_output_release(mon->output);
        wl_surface_destroy(mon->surface);
        zwlr_layer_surface_v1_destroy(mon->layer_surface);
        free(mon->name);
        free(mon);
    }

    zwlr_layer_shell_v1_destroy(layer_shell);
    wl_shm_destroy(shm);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    fcft_fini();
    free(font);
}

void usage(const char *prog_name) {
    // clang-format off
    printf(
    "Usage: %s [OPTION]...\n"
    "Options:\n"
    "  -b       anchor bar to bottom of display\n"
    "  -f FONT  set font description (monospace:size=10)\n"
    "  -l FONT  set line height (1.0)\n"
    "  -F HEX   set foreground color in RGBA (0xbbbbbbff)\n"
    "  -B HEX   set background color in RGBA (0x0c0c0cff)\n"
    "  -v       show version info\n"
    "  -h       show this help message\n",
    prog_name
    );
    // clang-format on
}

int main(int argc, char *argv[]) {
    setlocale(LC_CTYPE, "");

    int opt;
    while ((opt = getopt(argc, argv, "f:l:F:B:bvh")) != -1) {
        switch (opt) {
        case 'f':
            font = strdup(optarg);
            break;
        case 'l':
            lineheight = atof(optarg);
            break;
        case 'b':
            topbar = false;
            break;
        case 'F':
            fg = strtoul(optarg, NULL, 16);
            break;
        case 'B':
            bg = strtoul(optarg, NULL, 16);
            break;
        case 'v':
            puts("wayland-bar " VERSION);
            return EXIT_SUCCESS;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default: {
            die("Try '%s -h' for more information.", argv[0]);
        }
        }
    }

    setup();
    run();
    cleanup();

    return EXIT_SUCCESS;
}
