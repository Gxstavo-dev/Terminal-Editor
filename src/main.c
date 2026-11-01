#include "types.h"
#include "ui.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    App app = {0};
    char cwd[4096];

    if (argc > 2) {
        fprintf(stderr, "usage: %s [file]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return EXIT_FAILURE;
    }
    app.current_directory = cwd;
    app.initial_path = argc == 2 ? argv[1] : NULL;
    app.running = true;
    app.editor_focused = true;

    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "warning: locale unavailable; continuing with default locale\n");
    }
    return ui_run(&app);
}
