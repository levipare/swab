#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define DEFAULT_FONT "monospace:size=14"
#define DEFAULT_HEIGHT 20
#define DEFAULT_FG 0xFFBBBBBB
#define DEFAULT_BG 0xFF0C0C0C

void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTION]...\n", prog_name);
    // clang-format off
    printf(
    "Options:\n"
    "  -H, --height=NUM      set height to NUM pixels (default " XSTR(DEFAULT_HEIGHT) ")\n"
    "  -f, --font=STR        set font description (default " DEFAULT_FONT ")\n"
    "  -b, --bottom          anchor bar to bottom of display\n"
    "  -F, --fg=NUM          set foreground color in ARGB (default " XSTR(DEFAULT_FG) ")\n"
    "  -B  --bg=NUM          set background color in ARGB (default " XSTR(DEFAULT_BG) ")\n"
    "  -h, --help            show this help message\n"
    );
    // clang-format on
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    // default config
    struct wb_config config = {
        .font = DEFAULT_FONT,
        .height = DEFAULT_HEIGHT,
        .fg_color = DEFAULT_FG,
        .bg_color = DEFAULT_BG,
    };

    int opt;
    int option_index = 0;
    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"bottom", no_argument, 0, 'b'},
        {"font", required_argument, 0, 'f'},
        {"height", required_argument, 0, 'H'},
        {0},
    };
    while ((opt = getopt_long(argc, argv, "f:H:F:B:hb", long_options,
                              &option_index)) != -1) {
        switch (opt) {
        case 'H':
            if ((config.height = atoi(optarg)) <= 0) {
                printf("%s: -y: height must be greater than 0\n", argv[0]);
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            strncpy(config.font, optarg, sizeof(config.font));
            config.font[sizeof(config.font) - 1] = '\0';
            break;
        case 'b':
            config.bottom = true;
            break;
        case 'F':
            config.fg_color = strtoul(optarg, NULL, 0);
            break;
        case 'B':
            config.bg_color = strtoul(optarg, NULL, 0);
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default: {
            printf("Try %s --help for more information.\n", argv[0]);
            return EXIT_FAILURE;
        }
        }
    }

    wb_run(config);

    return 0;
}
