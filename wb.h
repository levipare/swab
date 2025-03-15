#ifndef WB_H
#define WB_H

#include "wl.h"

struct wb_module {
    void *data;
    void (*run)(struct wb_module *mod);
    void (*destroy)(struct wb_module *mod);
};

struct wb_container {
    struct wb_module **mods;
    size_t count;
};

struct wb {
    struct wl_ctx *wl;
    const char *output_name;

    struct wb_container left;
    struct wb_container center;
    struct wb_container right;
};

struct wb *wb_create();

void wb_destroy(struct wb *bar);

void wb_run(struct wb *bar);

void wb_container_add(struct wb_container *con, struct wb_module *mod);

#endif
