#include "wb.h"

int main(int argc, char *argv[]) {
    struct wb_config config = {
        .height = 20,
        .bg_color = 0x0C0C0CFF,
        .fg_color = 0xBBBBBBFF,
    };

    wb_run(config);

    return 0;
}
