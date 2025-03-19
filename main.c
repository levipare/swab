#include "wb.h"

int main(int argc, char *argv[]) {
    struct wb *bar = wb_create();

    wb_run(bar);
    wb_destroy(bar);

    return 0;
}
