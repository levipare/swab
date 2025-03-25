#include "wb.h"

#include <getopt.h>

void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTION]...\n", prog_name);
    printf("Options:\n");
    printf("  -y, --height=NUM      set height to NUM pixels (default 20)\n"
           "  -f, --fg=NUM          set foreground color in argb format\n"
           "  -b  --bg=NUM          set background color in argb format\n"
           "  -h, --help            show this help message\n");
}

int main(int argc, char *argv[]) {
    struct wb_config config = {
        .height = 20,
        .bg_color = 0x0C0C0CFF,
        .fg_color = 0xBBBBBBFF,
    };

    int opt;
    int option_index = 0;
    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"height", required_argument, 0, 'y'},
        {"fg", required_argument, 0, 'f'},
        {"bg", required_argument, 0, 'b'},
        {0},
    };
    while ((opt = getopt_long(argc, argv, "y:f:b:h", long_options,
                              &option_index)) != -1) {
        switch (opt) {
        case 'y':
            if ((config.height = atoi(optarg)) <= 0) {
                printf("%s: -y: height must be greater than 0\n", argv[0]);
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            config.fg_color = strtoul(optarg, NULL, 0);
            break;
        case 'b':
            config.bg_color = strtoul(optarg, NULL, 0);
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default: {
            printf("Try %s --help for more information\n", argv[0]);
            return EXIT_FAILURE;
        }
        }
    }

    wb_run(config);

    return 0;
}
