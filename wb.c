#include "wb.h"
#include "wl.h"

#include <assert.h>
#include <stdlib.h>

void wb_container_add(struct wb_container *con, struct wb_module *mod) {
    con->mods = realloc(con->mods, (con->count + 1) * sizeof(*con->mods));
    con->mods[con->count++] = mod;
}

void wb_container_destroy(struct wb_container *con) {
    for (int i = 0; i < con->count; ++i) {
        struct wb_module *mod = con->mods[i];
        mod->destroy(mod);
    }
}

struct wb *wb_create() {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->wl = wl_ctx_create();
    bar->output_name = bar->wl->outputs->name;

    return bar;
}

void wb_destroy(struct wb *bar) {
    wl_ctx_destroy(bar->wl);

    wb_container_destroy(&bar->left);
    wb_container_destroy(&bar->center);
    wb_container_destroy(&bar->right);

    free(bar);
}

void wb_run(struct wb *bar) {
    assert(bar->wl);
    assert(bar->output_name);

    while (wl_display_dispatch(bar->wl->display)) {
    }
}
