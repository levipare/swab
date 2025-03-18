#include "wb.h"

int main(int argc, char *argv[]) {
    struct wb *bar = wb_create();

    WB_ADD_MODULE(bar, hyprland);
    WB_ADD_MODULE(bar, battery);
    WB_ADD_MODULE(bar, datetime);

    wb_run(bar);
    wb_destroy(bar);

    return 0;
}
