#ifndef TERMINAL_EDITOR_TYPES_H
#define TERMINAL_EDITOR_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <time.h>

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT
} Theme;

typedef enum {
    DIAG_INFO = 0,
    DIAG_WARNING,
    DIAG_ERROR
} Severity;

typedef struct {
    char *data;
    size_t byte_length;
} Line;

typedef struct {
    Line *lines;
    size_t line_count;
    size_t cursor_line;
    size_t cursor_column;
    size_t selection_line;
    size_t selection_column;
    bool selecting;
    bool dirty;
    bool final_newline;
    bool crlf;
} BufferState;

typedef struct {
    char *path;
    size_t line;
    size_t column;
    Severity severity;
    char *message;
} Diagnostic;

typedef struct {
    char *extension;
    char **argv;
    size_t argc;
} LintRule;

typedef struct {
    size_t tab_width;
    size_t sidebar_width;
    bool autoindent;
    bool sidebar_visible;
    Theme theme;
    LintRule *rules;
    size_t rule_count;
    size_t warning_count;
} AppConfig;

typedef struct {
    char *name;
    char *path;
    bool is_directory;
} FileEntry;

typedef struct {
    char *path;
    Line *lines;
    size_t line_count;
    size_t cursor_line;
    size_t cursor_column;
    size_t top_line;
    size_t left_cell;
    bool dirty;
    bool final_newline;
    bool crlf;
    bool selecting;
    size_t selection_line;
    size_t selection_column;
    Diagnostic *diagnostics;
    size_t diagnostic_count;
    struct stat disk_metadata;
    bool disk_metadata_valid;
    bool disk_file_exists;
    bool external_change_pending;
    BufferState undo[100];
    size_t undo_count;
    BufferState redo[100];
    size_t redo_count;
} Buffer;

typedef struct {
    Buffer *buffers;
    size_t buffer_count;
    size_t active_buffer;
    size_t selected_file;
    size_t selected_buffer;
    size_t file_scroll;
    size_t buffer_scroll;
    char *current_directory;
    char git_branch[256];
    struct stat directory_metadata;
    bool directory_metadata_valid;
    FileEntry *files;
    size_t file_count;
    AppConfig config;
    bool running;
    bool editor_focused;
    char message[256];
    struct timespec message_until;
} App;

#endif
