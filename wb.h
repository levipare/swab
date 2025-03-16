#ifndef WB_H
#define WB_H

#include "wl.h"
#include <pthread.h>

struct wb_module {
    struct wb *bar;

    const char *name;
    pthread_t thread;
    void *private;
    void (*run)(struct wb_module *);
    char *(*content)(struct wb_module *);
    void (*destroy)(struct wb_module *);
};

struct wb {
    struct wl_ctx *wl;
    const char *output_name;

    struct wb_module **modules;
    size_t module_count;
};

struct wb *wb_create();

void wb_destroy(struct wb *bar);

void wb_run(struct wb *bar);

void wb_refresh(struct wb *bar);

void wb_add_module(struct wb *bar, struct wb_module *mod);

#endif
