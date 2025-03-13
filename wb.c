#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "log.h"
#include "wl.h"

static char status[100];
static char activewin[64];
static int workspaces[10];

static void draw_bar(struct render_ctx *ctx) {
    // Fill background with a color
    cairo_set_source_rgb(ctx->cr, 0.2, 0.3, 0.35);
    cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
    cairo_fill(ctx->cr);

    // setup pango to render to cairo
    PangoLayout *layout = pango_layout_new(ctx->pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    // workspaces

    for (int i = 0; i < sizeof(workspaces) / sizeof(workspaces[0]); i++) {
        if (workspaces[i]) {
            cairo_set_source_rgb(ctx->cr, 1, 1, 1); // White text
        } else {
            cairo_set_source_rgb(ctx->cr, 0.6, 0.6, 0.6); // Gray text
        }
        char workspace[3];
        sprintf(workspace, "%d", i + 1);
        pango_layout_set_text(layout, workspace, 2);

        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);
        cairo_move_to(ctx->cr, 12 + i * 24, (ctx->height - height) / 2.0);
        pango_cairo_show_layout(ctx->cr, layout);
    }

    cairo_set_source_rgb(ctx->cr, 1, 1, 1); // White text
    int width, height;
    pango_layout_set_text(layout, activewin, -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(ctx->cr, (ctx->width - width) / 2.0,
                  (ctx->height - height) / 2.0);
    pango_cairo_show_layout(ctx->cr, layout);

    // right side
    cairo_set_source_rgb(ctx->cr, 1, 1, 1); // White text
    pango_layout_set_text(layout, status, -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(ctx->cr, ctx->width - width - 12,
                  (ctx->height - height) / 2.0);
    pango_cairo_show_layout(ctx->cr, layout);
}

static int socket_create(const char *socket_path) {
    // create file descriptor for use with unix sockets
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        log_fatal("error creating socket");
    }

    // create unix socket address struct and copy in socket_path
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // connect to the socket
    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(socket_fd);
        log_fatal("error connecting to socket");
    }

    return socket_fd;
}

static int socket_readline(int socket_fd, char *buf, size_t size) {
    size_t bytes_read = 0;
    while (1) {
        char c;
        ssize_t nread = recv(socket_fd, &c, 1, 0);

        if (nread == -1) {
            log_fatal("error while calling recv on socket");
        }

        if (bytes_read < size - 1) {
            buf[bytes_read] = c;
        }

        if (c == '\n') {
            buf[bytes_read] = '\0';
            break;
        }

        bytes_read += nread;
    }

    return bytes_read;
}

static void *watch_hyprland(void *data) {
    struct wl_ctx *ctx = data;
    char *instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (!instance_signature || !xdg_runtime_dir) {
        log_fatal("environment variables not set.");
    }

    char socket_path[256];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
             xdg_runtime_dir, instance_signature);

    int socket_fd = socket_create(socket_path);

    char event[256] = {0};
    char curtext[72] = {0};
    size_t nread;
    while ((nread = socket_readline(socket_fd, event, sizeof(event))) > 0) {
        // printf("%s\n", event); // log hyprland events

        // if its an activewindow event
        if (strncmp(event, "activewindow>>", strlen("activewindow>>")) == 0) {
            // copy title of active window
            char *comma_loc = strchr(event, ',');
            strncpy(curtext, comma_loc + 1, sizeof(curtext) - 1);

            // add ellipses to truncate
            curtext[sizeof(curtext) - 4] = '.';
            curtext[sizeof(curtext) - 3] = '.';
            curtext[sizeof(curtext) - 2] = '.';

            strcpy(activewin, curtext);
        } else if (strncmp(event, "workspacev2>>", strlen("workspacev2>>")) ==
                   0) {
            size_t loc = strcspn(event, ">>");
            char *comma_loc = strchr(&event[loc], ',');
            event[loc] = '\0';

            // subtract one since hyrpland ID's are 1 indexed
            // and we store workspace names 0 indexed
            int ws_id = atoi(&event[loc + 2]) - 1;
            char *ws_name = comma_loc + 1;

            // set status of workspaces
            // 0: inactive
            // 1: active
            for (int i = 0; i < sizeof(workspaces) / sizeof(workspaces[0]);
                 i++) {
                if (i == ws_id) {
                    workspaces[i] = 1;
                } else {
                    workspaces[i] = 0;
                }
            }
        }

        render(ctx->output);
        wl_display_flush(ctx->display);
    }

    close(socket_fd);

    return NULL;
}

static void *datetime(void *data) {
    struct wl_ctx *ctx = data;
    while (1) {
        time_t current_time;
        time(&current_time);
        struct tm *time_info = localtime(&current_time);

        strftime(status, sizeof(status), "%a %b %-d %-I:%M:%S %p", time_info);

        //
        render(ctx->output);
        wl_display_flush(ctx->display);
        //

        // Sleep until the next second boundary
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        size_t sleep_time = (1 * 1000000000UL - ts.tv_nsec) / 1000;
        usleep(sleep_time);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct wl_ctx *ctx = wl_ctx_create();
    // at this point we are free to begin rendering

    ctx->output->draw_callback = draw_bar;

    pthread_t t1, t2;
    pthread_create(&t1, NULL, datetime, ctx);
    pthread_create(&t2, NULL, watch_hyprland, ctx);

    while (wl_display_dispatch(ctx->display)) {
    }

    wl_ctx_destroy(ctx);

    return 0;
}
