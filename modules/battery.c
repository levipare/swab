#include "../log.h"
#include "../wb.h"

#include <assert.h>
#include <dbus/dbus.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define POWER_SUPPLY_PATH "/org/freedesktop/UPower/devices/DisplayDevice"

struct private {
    DBusConnection *conn;
    double percentage;
};

static DBusConnection *connect_to_dbus() {
    DBusConnection *connection;
    DBusError error;
    dbus_error_init(&error);

    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error)) {
        log_error("DBus connection error: %s", error.message);
        dbus_error_free(&error);
        return NULL;
    }
    if (connection == NULL) {
        log_error("DBus connection failed.");
        return NULL;
    }

    return connection;
}

static double get_current_percentage(DBusConnection *conn) {
    // get current percentage
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *message = dbus_message_new_method_call(
        "org.freedesktop.UPower",
        "/org/freedesktop/UPower/devices/DisplayDevice",
        "org.freedesktop.DBus.Properties", "Get");
    const char *interface_name = "org.freedesktop.UPower.Device";
    const char *property_name = "Percentage";
    dbus_message_append_args(message, DBUS_TYPE_STRING, &interface_name,
                             DBUS_TYPE_STRING, &property_name,
                             DBUS_TYPE_INVALID);

    DBusMessage *reply =
        dbus_connection_send_with_reply_and_block(conn, message, -1, &error);
    dbus_message_unref(message);
    if (dbus_error_is_set(&error)) {
        log_error("reply error: %s", error.message);
        dbus_error_free(&error);
        dbus_connection_unref(conn);
    }

    // extract Percentage (double) from message
    double d;
    DBusMessageIter iter;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &iter); // enter into variant
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_DOUBLE) {
        dbus_message_iter_get_basic(&iter, &d);
        return d;
    }

    return 0;
}

static void run(struct wb_module *mod) {
    struct private *p = mod->private;

    dbus_bus_add_match(
        p->conn,
        "type='signal',interface='org.freedesktop.DBus.Properties',member='"
        "PropertiesChanged',path='" POWER_SUPPLY_PATH "'",
        NULL);
    dbus_connection_flush(p->conn);
    while (1) {
        // process pending D-Bus messages without timeout
        dbus_connection_read_write_dispatch(p->conn, -1);

        DBusMessage *message = dbus_connection_pop_message(p->conn);

        if (message == NULL) {
            continue; // No message, continue the loop
        }

        if (dbus_message_is_signal(message, "org.freedesktop.DBus.Properties",
                                   "PropertiesChanged")) {
            DBusMessageIter arg;
            dbus_message_iter_init(message, &arg);
            dbus_message_iter_next(&arg); // skip interface name
            dbus_message_iter_recurse(
                &arg, &arg); // eneter the changed properties array

            while (dbus_message_iter_get_arg_type(&arg) ==
                   DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter dict_entry;

                // get property name
                dbus_message_iter_recurse(&arg, &dict_entry);
                char *property_name;
                dbus_message_iter_get_basic(&dict_entry, &property_name);

                // get property value
                dbus_message_iter_next(&dict_entry);
                dbus_message_iter_recurse(&dict_entry, &dict_entry);

                int type = dbus_message_iter_get_arg_type(&dict_entry);

                // the possible types
                dbus_int64_t x;
                dbus_uint64_t y;
                double d;
                char *str;

                switch (type) {
                case DBUS_TYPE_INT64:
                case DBUS_TYPE_INT32:
                    dbus_message_iter_get_basic(&dict_entry, &x);
                    break;
                case DBUS_TYPE_UINT64:
                case DBUS_TYPE_UINT32:
                    dbus_message_iter_get_basic(&dict_entry, &y);
                    break;
                case DBUS_TYPE_DOUBLE:
                    dbus_message_iter_get_basic(&dict_entry, &d);
                    break;
                case DBUS_TYPE_STRING:
                    dbus_message_iter_get_basic(&dict_entry, &str);
                    break;
                }

                // log_debug("%s", property_name);
                // if the signal contains a percentage change
                if (strcmp(property_name, "Percentage") == 0) {
                    p->percentage = d;

                    wb_refresh(mod->bar);
                }

                dbus_message_iter_next(&arg); // Next dictionary entry
            }
        }

        dbus_message_unref(message);
    }
}

static void destroy(struct wb_module *mod) {
    struct private *p = mod->private;

    dbus_connection_unref(p->conn);
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

//     char percentage_str[24];
//     snprintf(percentage_str, sizeof(percentage_str), "BAT %d%%",
//              (int)p->percentage);
//     pango_layout_set_text(layout, percentage_str, -1);

//     int width, height;
//     pango_layout_get_pixel_size(layout, &width, &height);

//     cairo_move_to(ctx->cr, ctx->width - width - 210,
//                   (ctx->height - height) / 2.0);
//     cairo_set_source_rgb(ctx->cr, 0xBB / 255.0, 0xBB / 255.0, 0xBB / 255.0);

//     pango_cairo_show_layout(ctx->cr, layout);

//     g_object_unref(layout);
// }

static char *content(struct wb_module *mod) {
    struct private *p = mod->private;
    char *batperc = malloc(5);
    snprintf(batperc, 5, "%d%%", (int)p->percentage);
    return batperc;
}

WB_MODULE_CREATE(battery) {
    struct wb_module *mod = calloc(1, sizeof(*mod));
    mod->name = "battery";
    mod->run = run;
    mod->destroy = destroy;
    mod->content = content;

    struct private *p = mod->private = calloc(1, sizeof(struct private));

    p->conn = connect_to_dbus();
    p->percentage = get_current_percentage(p->conn);

    return mod;
}
