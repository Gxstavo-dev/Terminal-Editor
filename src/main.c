#include "types.h"
#include "buffer.h"
#include "config.h"
#include "files.h"
#include "ui.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    App app = {0};
    struct stat status;
    int result;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [file]\n", argv[0]);
        return EXIT_FAILURE;
    }
    app.current_directory = getcwd(NULL, 0);
    if (app.current_directory == NULL) {
        perror("getcwd");
        return EXIT_FAILURE;
    }
    config_defaults(&app.config);
    char *config_path = config_default_path();
    if (config_load(&app.config, config_path) != 0) {
        app.config.warning_count++;
    }
    free(config_path);
    if (argc == 2 && stat(argv[1], &status) == 0 && S_ISDIR(status.st_mode)) {
        char *directory = realpath(argv[1], NULL);
        if (directory == NULL) {
            perror(argv[1]);
            config_destroy(&app.config);
            free(app.current_directory);
            return EXIT_FAILURE;
        }
        free(app.current_directory);
        app.current_directory = directory;
    } else if (argc == 2) {
        app.buffers = calloc(1, sizeof(*app.buffers));
        if (app.buffers == NULL || buffer_load(&app.buffers[0], argv[1]) != 0) {
            perror(argv[1]);
            free(app.buffers);
            config_destroy(&app.config);
            free(app.current_directory);
            return EXIT_FAILURE;
        }
        app.buffer_count = 1;
    }
    if (files_list(app.current_directory, &app.files, &app.file_count) != 0) {
        perror(app.current_directory);
        for (size_t i = 0; i < app.buffer_count; i++) {
            buffer_destroy(&app.buffers[i]);
        }
        free(app.buffers);
        config_destroy(&app.config);
        free(app.current_directory);
        return EXIT_FAILURE;
    }
    app.running = true;
    app.editor_focused = true;
    if (app.config.warning_count > 0) {
        struct timespec now;
        snprintf(app.message, sizeof(app.message), "Config has %zu invalid setting(s)",
                 app.config.warning_count);
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
            app.message_until.tv_sec = now.tv_sec + 2;
            app.message_until.tv_nsec = now.tv_nsec;
        }
    }

    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "warning: locale unavailable; continuing with default locale\n");
    }
    result = ui_run(&app);
    for (size_t i = 0; i < app.buffer_count; i++) {
        buffer_destroy(&app.buffers[i]);
    }
    free(app.buffers);
    files_free(app.files, app.file_count);
    config_destroy(&app.config);
    free(app.current_directory);
    return result;
}
