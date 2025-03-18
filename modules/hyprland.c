#include "../log.h"
#include "../wb.h"

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct private {
    int socket2;
    int workspaces[10];
    char activewin[72];
};

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

/*
 * expects a null terminator string
 */
static void set_activewin(struct private *p, const char *cstr) {
    strncpy(p->activewin, cstr, sizeof(p->activewin));
    p->activewin[sizeof(p->activewin) - 1] = '\0';
    p->activewin[sizeof(p->activewin) - 2] = '.';
    p->activewin[sizeof(p->activewin) - 3] = '.';
    p->activewin[sizeof(p->activewin) - 4] = '.';
}

static void run(struct wb_module *mod) {
    struct private *p = mod->private;

    char event[1024];
    FILE *fp = fdopen(p->socket2, "r");
    while (fgets(event, sizeof(event), fp)) {
        event[strcspn(event, "\n")] = '\0';
        // log_debug("%s", event); // log hyprland events

        // if its an activewindow event
        if (strncmp(event, "activewindow>>", strlen("activewindow>>")) == 0) {
            // copy title of active window
            char *title = strchr(event, ',') + 1;
            set_activewin(p, title);

            wb_refresh(mod->bar);
        } else if (strncmp(event, "workspacev2>>", strlen("workspacev2>>")) ==
                   0) {
            char *ws_id_str = strstr(event, ">>") + 2;

            // subtract one since hyrpland ID's are 1 indexed
            // and we store as 0 indexed
            int ws_id = atoi(ws_id_str) - 1;

            // set status of workspaces
            // 0: inactive
            // 1: active
            for (int i = 0;
                 i < sizeof(p->workspaces) / sizeof(p->workspaces[0]); i++) {
                p->workspaces[i] = i == ws_id;
            }
            wb_refresh(mod->bar);
        }
    }
}

static void destroy(struct wb_module *mod) {
    struct private *p = mod->private;
    close(p->socket2);

    free(mod->private);
    free(mod);
}

// static void draw(struct wb_module *mod, struct render_ctx *ctx) {
//     struct private *p = mod->private;

//     PangoLayout *layout = pango_layout_new(ctx->pango);
//     PangoFontDescription *font_desc =
//         pango_font_description_from_string("JetBrainsMono Nerd Font 14px");
//     pango_layout_set_font_description(layout, font_desc);
//     pango_font_description_free(font_desc);

//     int width, height;
//     // draw workspaces
//     for (int i = 0; i < sizeof(p->workspaces) / sizeof(p->workspaces[0]);
//     ++i) {
//         char buf[3];
//         snprintf(buf, sizeof(buf), "%d", i + 1);
//         pango_layout_set_text(layout, buf, -1);
//         pango_layout_get_pixel_size(layout, &width, &height);
//         cairo_move_to(ctx->cr, 10 + i * 24, (ctx->height - height) / 2.0);
//         if (p->workspaces[i]) {
//             cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0,
//                                  0xBB / 255.0);
//         } else {
//             cairo_set_source_rgb(ctx->cr, 0x55 / 255.0, 0x55 / 255.0,
//                                  0x55 / 255.0);
//         }
//         pango_cairo_show_layout(ctx->cr, layout);
//     }

//     // draw active window in the center
//     pango_layout_set_text(layout, p->activewin, -1);

//     pango_layout_get_pixel_size(layout, &width, &height);

//     cairo_move_to(ctx->cr, (ctx->width - width) / 2.0,
//                   (ctx->height - height) / 2.0);
//     cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);

//     pango_cairo_show_layout(ctx->cr, layout);

//     g_object_unref(layout);
// }

static char *content(struct wb_module *mod) {
    struct private *p = mod->private;
    return p->activewin;
}

struct wb_module *hyprland_create() {
    struct wb_module *mod = calloc(1, sizeof(*mod));
    mod->name = "hyprland";
    mod->private = calloc(1, sizeof(struct private));
    mod->run = run;
    mod->destroy = destroy;
    mod->content = content;

    // setup hyprland sockets
    struct private *p = mod->private;
    const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!instance_signature || !xdg_runtime_dir) {
        log_fatal("environment variables not set");
    }

    // Request socket
    // used for a initial requests
    char socket_path[108];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket.sock",
             xdg_runtime_dir, instance_signature);

    // get active window
    const char cmd[] = "activewindow";
    const char res[4096];
    int request_socket = socket_create(socket_path);

    // send command
    if (write(request_socket, cmd, sizeof(cmd)) == -1) {
        close(request_socket);
        log_fatal("write error");
    }
    // read response
    if (read(request_socket, (void *)res, sizeof(res)) == -1) {
        close(request_socket);
        log_fatal("read error");
    }
    close(request_socket);

    // likely will not have an active window on startup
    // so we must check if response was not Invalid
    if (strcmp(res, "Invalid") != 0) {
        char *win_title = strchr(res, '>') + 2;
        // replaces the colon that appears after window title with null
        *(strchr(win_title, '\n') - 1) = '\0';
        set_activewin(p, win_title);
    }

    // get active workspace
    request_socket = socket_create(socket_path);
    const char cmd2[] = "activeworkspace";
    const char res2[4096];
    if (write(request_socket, cmd2, sizeof(cmd2)) == -1) {
        close(request_socket);
        log_fatal("write error");
    }
    if (read(request_socket, (void *)res2, sizeof(res2)) == -1) {
        close(request_socket);
        log_fatal("read error");
    }
    close(request_socket);

    if (strcmp(res2, "Invalid") != 0) {
        char *ws_id_str = strstr(res2, "ID ") + 3;
        *strchr(ws_id_str + 3, ' ') = '\0';
        int ws_id = atoi(ws_id_str + 3);
        p->workspaces[ws_id - 1] = 1;
    }

    // create socket to watch hyprland events
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
             xdg_runtime_dir, instance_signature);
    p->socket2 = socket_create(socket_path);

    return mod;
}
