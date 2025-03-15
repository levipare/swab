#include <cairo/cairo.h>
#include <dbus/dbus.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "log.h"
#include "wb.h"

static char status[100];
static char activewin[64];
static int workspaces[10];
static int batpercent;

static void draw_bar(struct render_ctx *ctx) {
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

    // workspaces

    for (int i = 0; i < sizeof(workspaces) / sizeof(workspaces[0]); i++) {
        if (workspaces[i]) {
            cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0,
                                 0xBB / 255.0);
        } else {
            cairo_set_source_rgb(ctx->cr, 0x5A / 255.0, 0x5A / 255.0,
                                 0x5A / 255.0);
        }
        char workspace[3];
        sprintf(workspace, "%d", i + 1);
        pango_layout_set_text(layout, workspace, 2);

        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);
        cairo_move_to(ctx->cr, 12 + i * 24, (ctx->height - height) / 2.0);
        pango_cairo_show_layout(ctx->cr, layout);
    }

    cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);
    int width, height;
    pango_layout_set_text(layout, activewin, -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(ctx->cr, (ctx->width - width) / 2.0,
                  (ctx->height - height) / 2.0);
    pango_cairo_show_layout(ctx->cr, layout);

    // right side
    cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);
    pango_layout_set_text(layout, status, -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(ctx->cr, ctx->width - width - 12,
                  (ctx->height - height) / 2.0);
    pango_cairo_show_layout(ctx->cr, layout);

    // battery percentage
    char bat[10];
    snprintf(bat, sizeof(bat), "%d%%", batpercent);
    pango_layout_set_text(layout, bat, -1);
    cairo_rel_move_to(ctx->cr, -30, 0);
    pango_cairo_show_layout(ctx->cr, layout);
}

// #define POWER_SUPPLY_PATH "/org/freedesktop/UPower/devices/DisplayDevice"

// DBusConnection *connect_to_dbus() {
//     DBusConnection *connection;
//     DBusError error;
//     dbus_error_init(&error);

//     connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
//     if (dbus_error_is_set(&error)) {
//         fprintf(stderr, "DBus connection error: %s\n", error.message);
//         dbus_error_free(&error);
//         return NULL;
//     }
//     if (connection == NULL) {
//         fprintf(stderr, "DBus connection failed.\n");
//         return NULL;
//     }

//     return connection;
// }

// void wait_for_properties_changed(DBusConnection *connection,
//                                  struct wl_ctx *ctx) {
//     DBusMessage *message;
//     DBusMessageIter arg;
//     const char *interface_name;
//     const char *signal_name;

//     // Add a match rule to listen for PropertiesChanged signals
//     dbus_bus_add_match(
//         connection,
//         "type='signal',interface='org.freedesktop.DBus.Properties',member='"
//         "PropertiesChanged',path='" POWER_SUPPLY_PATH "'",
//         NULL);
//     dbus_connection_flush(connection);

//     while (1) {
//         // Process pending D-Bus messages without timeout
//         dbus_connection_read_write_dispatch(connection, -1);

//         // Check if a message is available
//         message = dbus_connection_pop_message(connection);

//         if (message == NULL) {
//             continue; // No message, continue the loop
//         }

//         if (dbus_message_is_signal(message,
//         "org.freedesktop.DBus.Properties",
//                                    "PropertiesChanged")) {
//             interface_name = dbus_message_get_interface(message);
//             signal_name = dbus_message_get_member(message);

//             // Parse the signal (example: print all changed properties)
//             dbus_message_iter_init(message, &arg);
//             dbus_message_iter_next(&arg); // Skip interface name
//             dbus_message_iter_recurse(
//                 &arg, &arg); // Enter the changed properties array

//             while (dbus_message_iter_get_arg_type(&arg) ==
//                    DBUS_TYPE_DICT_ENTRY) {
//                 DBusMessageIter dict_entry;

//                 // get property name
//                 dbus_message_iter_recurse(&arg, &dict_entry);
//                 char *property_name;
//                 dbus_message_iter_get_basic(&dict_entry, &property_name);

//                 // get property value
//                 dbus_message_iter_next(&dict_entry);
//                 dbus_message_iter_recurse(&dict_entry, &dict_entry);

//                 int type = dbus_message_iter_get_arg_type(&dict_entry);

//                 dbus_int64_t x;
//                 dbus_uint64_t y;
//                 double d;
//                 char *str;

//                 switch (type) {
//                 case DBUS_TYPE_INT64:
//                 case DBUS_TYPE_INT32: {
//                     dbus_message_iter_get_basic(&dict_entry, &x);
//                     break;
//                 }
//                 case DBUS_TYPE_UINT64:
//                 case DBUS_TYPE_UINT32: {
//                     dbus_message_iter_get_basic(&dict_entry, &y);
//                     break;
//                 }
//                 case DBUS_TYPE_DOUBLE: {
//                     dbus_message_iter_get_basic(&dict_entry, &d);
//                     break;
//                 }
//                 case DBUS_TYPE_STRING: {
//                     dbus_message_iter_get_basic(&dict_entry, &str);
//                     break;
//                 }
//                 }

//                 printf("%s\n", property_name);
//                 if (strcmp(property_name, "Percentage") == 0) {
//                     printf("set percentage\n");
//                     batpercent = d;

//                     render(ctx->outputs);
//                     wl_display_flush(ctx->display);
//                 }

//                 // printf("\n");

//                 dbus_message_iter_next(&arg); // Next dictionary entry
//             }

//             dbus_message_unref(message);
//             return; // Exit after receiving one signal for the example.
//                     // Remove this line for continuous monitoring.
//         }

//         dbus_message_unref(message);
//     }
// }

// static int socket_create(const char *socket_path) {
//     // create file descriptor for use with unix sockets
//     int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
//     if (socket_fd == -1) {
//         log_fatal("error creating socket");
//     }

//     // create unix socket address struct and copy in socket_path
//     struct sockaddr_un addr = {.sun_family = AF_UNIX};
//     strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

//     // connect to the socket
//     if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
//         close(socket_fd);
//         log_fatal("error connecting to socket");
//     }

//     return socket_fd;
// }

// static int socket_readline(int socket_fd, char *buf, size_t size) {
//     size_t bytes_read = 0;
//     while (1) {
//         char c;
//         ssize_t nread = recv(socket_fd, &c, 1, 0);

//         if (nread == -1) {
//             log_fatal("error while calling recv on socket");
//         }

//         if (bytes_read < size - 1) {
//             buf[bytes_read] = c;
//         }

//         if (c == '\n') {
//             buf[bytes_read] = '\0';
//             break;
//         }

//         bytes_read += nread;
//     }

//     return bytes_read;
// }

// static void *watch_hyprland(void *data) {
//     struct wl_ctx *ctx = data;
//     char *instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
//     char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");

//     if (!instance_signature || !xdg_runtime_dir) {
//         log_fatal("environment variables not set.");
//     }

//     char socket_path[256];
//     snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
//              xdg_runtime_dir, instance_signature);

//     int socket_fd = socket_create(socket_path);

//     char event[256] = {0};
//     char event_data[72] = {0};
//     size_t nread;
//     while ((nread = socket_readline(socket_fd, event, sizeof(event))) > 0) {
//         // printf("%s\n", event); // log hyprland events

//         // if its an activewindow event
//         if (strncmp(event, "activewindow>>", strlen("activewindow>>")) == 0)
//         {
//             // copy title of active window
//             char *comma_loc = strchr(event, ',');
//             strncpy(event_data, comma_loc + 1, sizeof(event_data));
//             event_data[sizeof(event_data) - 1] = '\0';

//             // add ellipses to truncate
//             event_data[sizeof(event_data) - 4] = '.';
//             event_data[sizeof(event_data) - 3] = '.';
//             event_data[sizeof(event_data) - 2] = '.';

//             strncpy(activewin, event_data, sizeof(activewin));
//             activewin[sizeof(activewin) - 1] = '\0';
//         } else if (strncmp(event, "workspacev2>>", strlen("workspacev2>>"))
//         ==
//                    0) {
//             size_t loc = strcspn(event, ">>");
//             char *comma_loc = strchr(&event[loc], ',');
//             event[loc] = '\0';

//             // subtract one since hyrpland ID's are 1 indexed
//             // and we store workspace names 0 indexed
//             int ws_id = atoi(&event[loc + 2]) - 1;
//             char *ws_name = comma_loc + 1;

//             // set status of workspaces
//             // 0: inactive
//             // 1: active
//             for (int i = 0; i < sizeof(workspaces) / sizeof(workspaces[0]);
//                  i++) {
//                 if (i == ws_id) {
//                     workspaces[i] = 1;
//                 } else {
//                     workspaces[i] = 0;
//                 }
//             }
//         }

//         render(ctx->outputs);
//         wl_display_flush(ctx->display);
//     }

//     close(socket_fd);

//     return NULL;
// }

// static void *datetime(void *data) {
//     struct wl_ctx *ctx = data;
//     while (1) {
//         time_t current_time;
//         time(&current_time);
//         struct tm *time_info = localtime(&current_time);

//         strftime(status, sizeof(status), "%a %b %-d %-I:%M:%S %p",
//         time_info);

//         //
//         render(ctx->outputs);
//         wl_display_flush(ctx->display);
//         //

//         // Sleep until the next second boundary
//         struct timespec ts;
//         clock_gettime(CLOCK_MONOTONIC, &ts);
//         size_t sleep_time = (1 * 1000000000UL - ts.tv_nsec) / 1000;
//         usleep(sleep_time);
//     }
//     return NULL;
// }

// static void *battery(void *data) {
//     struct wl_ctx *ctx = data;
//     DBusConnection *connection = connect_to_dbus();

//     while (1) {
//         wait_for_properties_changed(connection, ctx);
//     }

//     dbus_connection_unref(connection);

//     return NULL;
// }

// static void draw_bar(struct render_ctx *ctx) {
//     // Fill background with a color
//     cairo_set_source_rgb(ctx->cr, 0.8, 0.1, 0.1);
//     cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
//     cairo_fill(ctx->cr);
// }

int main(int argc, char *argv[]) {
    struct wb *bar = wb_create();
    // at this point we are free to begin rendering

    bar->wl->outputs->draw_callback = draw_bar;
    render(bar->wl->outputs);

    wb_run(bar);

    wb_destroy(bar);

    return 0;
}
