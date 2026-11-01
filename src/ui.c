#include "ui.h"

#include "buffer.h"
#include "files.h"
#include "lint.h"

#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define GUTTER_WIDTH 6
#define MIN_EDITOR_WIDTH 30
#define MESSAGE_SECONDS 2

enum {
    PAIR_DEFAULT = 1,
    PAIR_HEADING,
    PAIR_SELECTED,
    PAIR_GUTTER,
    PAIR_ERROR,
    PAIR_WARNING,
    PAIR_INFO,
    PAIR_SELECTION,
    PAIR_BORDER,
    PAIR_SIDEBAR_DIM
};

#define BORDER_COLOR 255

static short saved_border_color[3];
static bool restore_border_color;
static uint64_t sidebar_render_key;
static bool sidebar_render_key_valid;
static bool kitty_sync_updates;
static char current_pointer_shape[16];

typedef struct {
    int rows;
    int columns;
    int sidebar_width;
    int editor_x;
    int editor_width;
    bool sidebar_visible;
} Layout;

static void set_message(App *app, const char *message)
{
    struct timespec now;
    snprintf(app->message, sizeof(app->message), "%s", message);
    clock_gettime(CLOCK_MONOTONIC, &now);
    app->message_until.tv_sec = now.tv_sec + MESSAGE_SECONDS;
    app->message_until.tv_nsec = now.tv_nsec;
}

static bool message_visible(const App *app)
{
    struct timespec now;
    if (app->message[0] == '\0' || clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return false;
    }
    return now.tv_sec < app->message_until.tv_sec ||
           (now.tv_sec == app->message_until.tv_sec &&
            now.tv_nsec < app->message_until.tv_nsec);
}

static bool active_external_change_pending(const App *app)
{
    return app->active_buffer < app->buffer_count &&
           app->buffers[app->active_buffer].external_change_pending;
}

static bool status_visible(const App *app)
{
    return message_visible(app) || active_external_change_pending(app);
}

static void init_colors(const AppConfig *config)
{
    if (!has_colors()) {
        return;
    }
    start_color();
    use_default_colors();
    if (config->theme == THEME_LIGHT) {
        init_pair(PAIR_DEFAULT, COLOR_BLACK, -1);
        init_pair(PAIR_HEADING, COLOR_BLUE, -1);
        init_pair(PAIR_SELECTED, COLOR_WHITE, -1);
        init_pair(PAIR_GUTTER, COLOR_BLACK, -1);
        init_pair(PAIR_ERROR, COLOR_RED, -1);
        init_pair(PAIR_WARNING, COLOR_MAGENTA, -1);
        init_pair(PAIR_INFO, COLOR_BLUE, -1);
        init_pair(PAIR_SELECTION, COLOR_BLACK, COLOR_YELLOW);
    } else {
        init_pair(PAIR_DEFAULT, COLOR_WHITE, -1);
        init_pair(PAIR_HEADING, COLOR_CYAN, -1);
        init_pair(PAIR_SELECTED, COLOR_WHITE, -1);
        init_pair(PAIR_GUTTER, COLOR_WHITE, -1);
        init_pair(PAIR_ERROR, COLOR_RED, -1);
        init_pair(PAIR_WARNING, COLOR_YELLOW, -1);
        init_pair(PAIR_INFO, COLOR_CYAN, -1);
        init_pair(PAIR_SELECTION, COLOR_BLACK, COLOR_CYAN);
    }
    if (COLORS > BORDER_COLOR) {
        init_pair(PAIR_BORDER, BORDER_COLOR, -1);
        if (can_change_color() &&
            color_content(BORDER_COLOR, &saved_border_color[0],
                          &saved_border_color[1], &saved_border_color[2]) == OK &&
            init_color(BORDER_COLOR, 192, 192, 192) == OK) {
            restore_border_color = true;
        }
    } else {
        init_pair(PAIR_BORDER, config->theme == THEME_LIGHT ? COLOR_BLACK : COLOR_WHITE,
                  -1);
    }
    if (COLORS >= 256) {
        init_pair(PAIR_SIDEBAR_DIM, 244, -1);
    } else if (COLORS >= 16) {
        init_pair(PAIR_SIDEBAR_DIM, 8, -1);
    } else if (config->theme == THEME_LIGHT) {
        init_pair(PAIR_SIDEBAR_DIM, COLOR_BLACK, -1);
    } else {
        init_pair(PAIR_SIDEBAR_DIM, COLOR_WHITE, -1);
    }
    bkgdset(COLOR_PAIR(PAIR_DEFAULT));
}

static Layout get_layout(const App *app)
{
    Layout layout = {0};
    getmaxyx(stdscr, layout.rows, layout.columns);
    layout.sidebar_width = (int)app->config.sidebar_width;
    layout.sidebar_visible = app->config.sidebar_visible && layout.rows >= 8 &&
                             layout.columns >= layout.sidebar_width + MIN_EDITOR_WIDTH + GUTTER_WIDTH;
    if (layout.sidebar_visible) {
        layout.editor_x = layout.sidebar_width + 1;
    } else {
        layout.editor_x = 3;
    }
    layout.editor_width = layout.columns - layout.editor_x;
    return layout;
}

static int draw_utf8(int row, int column, const char *text, int max_cells, int pair,
                     bool bold)
{
    size_t offset = 0;
    int cells = 0;
    mbstate_t state = {0};

    if (row < 0 || column < 0 || max_cells <= 0) {
        return 0;
    }
    while (text[offset] != '\0' && cells < max_cells) {
        wchar_t character;
        size_t amount = mbrtowc(&character, text + offset, strlen(text + offset), &state);
        int width;
        wchar_t one[2];

        if (amount == (size_t)-1 || amount == (size_t)-2 || amount == 0) {
            memset(&state, 0, sizeof(state));
            character = L'�';
            amount = 1;
        }
        width = wcwidth(character);
        if (width < 1) {
            width = 1;
        }
        if (cells + width > max_cells) {
            break;
        }
        one[0] = character;
        one[1] = L'\0';
        attrset(COLOR_PAIR(pair) | (bold ? A_BOLD : A_NORMAL));
        mvaddnwstr(row, column + cells, one, 1);
        cells += width;
        offset += amount;
    }
    attrset(A_NORMAL);
    return cells;
}

static bool selection_contains(const Buffer *buffer, size_t line, size_t column)
{
    size_t start_line = buffer->selection_line;
    size_t start_column = buffer->selection_column;
    size_t end_line = buffer->cursor_line;
    size_t end_column = buffer->cursor_column;

    if (!buffer->selecting) {
        return false;
    }
    if (start_line > end_line || (start_line == end_line && start_column > end_column)) {
        size_t swap = start_line;
        start_line = end_line;
        end_line = swap;
        swap = start_column;
        start_column = end_column;
        end_column = swap;
    }
    if (line < start_line || line > end_line) {
        return false;
    }
    if (start_line == end_line) {
        return line == start_line && column >= start_column && column < end_column;
    }
    if (line == start_line) {
        return column >= start_column;
    }
    if (line == end_line) {
        return column < end_column;
    }
    return true;
}

static Diagnostic *diagnostic_for_line(Buffer *buffer, size_t line)
{
    for (size_t i = 0; i < buffer->diagnostic_count; i++) {
        if (buffer->diagnostics[i].line == line + 1) {
            return &buffer->diagnostics[i];
        }
    }
    return NULL;
}

static int severity_pair(Severity severity)
{
    switch (severity) {
    case DIAG_ERROR:
        return PAIR_ERROR;
    case DIAG_WARNING:
        return PAIR_WARNING;
    case DIAG_INFO:
    default:
        return PAIR_INFO;
    }
}

static bool buffer_path_is(const Buffer *buffer, const char *path)
{
    char *left;
    char *right;
    bool same;
    if (buffer->path == NULL || path == NULL) {
        return false;
    }
    left = realpath(buffer->path, NULL);
    right = realpath(path, NULL);
    same = left != NULL && right != NULL ? strcmp(left, right) == 0
                                          : strcmp(buffer->path, path) == 0;
    free(left);
    free(right);
    return same;
}

static char *join_path(const char *directory, const char *name)
{
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s/%s", directory, name);
    }
    return path;
}

static char *find_git_directory(const char *start)
{
    char *directory = strdup(start);
    if (directory == NULL) {
        return NULL;
    }
    for (;;) {
        char *marker = join_path(directory, ".git");
        struct stat metadata;
        if (marker == NULL) {
            break;
        }
        if (stat(marker, &metadata) == 0) {
            if (S_ISDIR(metadata.st_mode)) {
                free(directory);
                return marker;
            }
            FILE *file = fopen(marker, "r");
            char *line = NULL;
            size_t capacity = 0;
            ssize_t length = file == NULL ? -1 : getline(&line, &capacity, file);
            if (file != NULL) {
                fclose(file);
            }
            free(marker);
            if (length > 8 && strncmp(line, "gitdir: ", 8) == 0) {
                line[strcspn(line + 8, "\r\n") + 8] = '\0';
                char *git_directory = line[8] == '/'
                                          ? strdup(line + 8)
                                          : join_path(directory, line + 8);
                free(line);
                free(directory);
                return git_directory;
            }
            free(line);
            break;
        }
        free(marker);
        if (strcmp(directory, "/") == 0) {
            break;
        }
        char *slash = strrchr(directory, '/');
        if (slash == NULL) {
            break;
        }
        if (slash == directory) {
            directory[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
    free(directory);
    return NULL;
}

static bool refresh_git_branch(App *app)
{
    char branch[sizeof(app->git_branch)] = "sin repo Git";
    char *git_directory = find_git_directory(app->current_directory);
    if (git_directory != NULL) {
        char *head_path = join_path(git_directory, "HEAD");
        FILE *head = head_path == NULL ? NULL : fopen(head_path, "r");
        char *line = NULL;
        size_t capacity = 0;
        ssize_t length = head == NULL ? -1 : getline(&line, &capacity, head);
        if (head != NULL) {
            fclose(head);
        }
        if (length > 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "ref: refs/heads/", 16) == 0) {
                snprintf(branch, sizeof(branch), "%s", line + 16);
            } else {
                snprintf(branch, sizeof(branch), "detached %.7s", line);
            }
        }
        free(line);
        free(head_path);
    }
    free(git_directory);
    if (strcmp(app->git_branch, branch) == 0) {
        return false;
    }
    snprintf(app->git_branch, sizeof(app->git_branch), "%s", branch);
    return true;
}

static void draw_sidebar(App *app, const Layout *layout)
{
    int file_start = 4;
    int file_end = layout->rows;
    int width = layout->sidebar_width;

    if (!layout->sidebar_visible || width < 8) {
        return;
    }
    attrset(COLOR_PAIR(PAIR_DEFAULT));
    for (int row = 0; row < layout->rows; row++) {
        mvhline(row, 0, ' ', width);
    }
    draw_utf8(0, width - 2, "×", 1, PAIR_DEFAULT, true);
    draw_utf8(2, 2, "Files", width - 5, PAIR_DEFAULT, true);
    draw_utf8(2, width - 3, "+", 1, PAIR_DEFAULT, true);
    for (int row = file_start; row < file_end; row += 2) {
        size_t index = app->file_scroll + (size_t)((row - file_start) / 2);
        if (index >= app->file_count) {
            break;
        }
        FileEntry *entry = &app->files[index];
        bool selected = index == app->selected_file;
        int pair = selected ? PAIR_SELECTED : PAIR_SIDEBAR_DIM;
        attrset(COLOR_PAIR(PAIR_DEFAULT));
        mvhline(row, 0, ' ', width);
        draw_utf8(row, 2, entry->is_directory ? "\uf07b" : "\uf15b", 1, pair,
                  selected);
        draw_utf8(row, 5, entry->name, width - 7, pair, selected);
    }
    attrset(COLOR_PAIR(PAIR_BORDER));
    mvvline(0, width, ACS_VLINE, layout->rows);
    attrset(A_NORMAL);
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t sidebar_key(const App *app, const Layout *layout)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_bytes(hash, &layout->rows, sizeof(layout->rows));
    hash = hash_bytes(hash, &layout->sidebar_width, sizeof(layout->sidebar_width));
    hash = hash_bytes(hash, &app->file_count, sizeof(app->file_count));
    hash = hash_bytes(hash, &app->file_scroll, sizeof(app->file_scroll));
    hash = hash_bytes(hash, &app->selected_file, sizeof(app->selected_file));
    hash = hash_bytes(hash, app->current_directory, strlen(app->current_directory));
    hash = hash_bytes(hash, "\0", 1);
    for (size_t i = 0; i < app->file_count; i++) {
        size_t name_length = strlen(app->files[i].name);
        hash = hash_bytes(hash, &name_length, sizeof(name_length));
        hash = hash_bytes(hash, app->files[i].name, name_length);
        hash = hash_bytes(hash, &app->files[i].is_directory,
                          sizeof(app->files[i].is_directory));
    }
    return hash;
}

static int draw_line(Buffer *buffer, size_t line_index, int row, int source_x,
                     int max_cells, size_t left_cell, int *end_x)
{
    Line *line = &buffer->lines[line_index];
    size_t offset = 0;
    size_t codepoint = 0;
    size_t cell = 0;
    mbstate_t state = {0};
    int drawn = 0;

    while (offset < line->byte_length) {
        wchar_t character;
        size_t amount = mbrtowc(&character, line->data + offset,
                                line->byte_length - offset, &state);
        int width;
        wchar_t one[2];
        if (amount == (size_t)-1 || amount == (size_t)-2 || amount == 0) {
            memset(&state, 0, sizeof(state));
            character = L'�';
            amount = 1;
        }
        width = wcwidth(character);
        if (width < 1) {
            width = 1;
        }
        if (cell + (size_t)width > left_cell && cell < left_cell + (size_t)max_cells) {
            size_t screen_cell = cell >= left_cell ? cell - left_cell : 0;
            one[0] = character;
            one[1] = L'\0';
            attrset(COLOR_PAIR(selection_contains(buffer, line_index, codepoint)
                                   ? PAIR_SELECTION : PAIR_DEFAULT));
            mvaddnwstr(row, source_x + (int)screen_cell, one, 1);
            drawn = (int)screen_cell + width;
        }
        cell += (size_t)width;
        offset += amount;
        codepoint++;
        if (cell >= left_cell + (size_t)max_cells) {
            break;
        }
    }
    *end_x = source_x + (drawn > max_cells ? max_cells : drawn);
    attrset(A_NORMAL);
    return drawn;
}

static int diagnostic_x(Buffer *buffer, size_t line_index, int source_x, int width,
                        size_t left_cell)
{
    int end_x;
    Line *line = &buffer->lines[line_index];
    size_t line_cells = buffer_line_cells(line);
    int visible_cells = (int)(line_cells > left_cell ? line_cells - left_cell : 0);
    if (visible_cells > width) {
        visible_cells = width;
    }
    end_x = source_x + visible_cells;
    return end_x + 2;
}

static void keep_cursor_visible(App *app, const Layout *layout, int reserved_rows)
{
    Buffer *buffer;
    int visible_rows = layout->rows - reserved_rows;
    int source_x = layout->editor_x + GUTTER_WIDTH;
    int visible_columns = layout->columns - source_x;
    size_t cursor_cell;

    if (app->buffer_count == 0 || visible_rows <= 0 || visible_columns <= 0) {
        return;
    }
    buffer = &app->buffers[app->active_buffer];
    if (buffer->cursor_line < buffer->top_line) {
        buffer->top_line = buffer->cursor_line;
    } else if (buffer->cursor_line >= buffer->top_line + (size_t)visible_rows) {
        buffer->top_line = buffer->cursor_line - (size_t)visible_rows + 1;
    }
    cursor_cell = buffer_column_to_cell(&buffer->lines[buffer->cursor_line],
                                        buffer->cursor_column);
    if (cursor_cell < buffer->left_cell) {
        buffer->left_cell = cursor_cell;
    } else if (cursor_cell >= buffer->left_cell + (size_t)visible_columns) {
        buffer->left_cell = cursor_cell - (size_t)visible_columns + 1;
    }
}

static void draw_editor(App *app, const Layout *layout)
{
    int source_x = layout->editor_x + GUTTER_WIDTH;
    int visible_rows = layout->rows - 2;
    int visible_columns = layout->columns - source_x;

    if (visible_rows <= 0 || visible_columns <= 0) {
        return;
    }
    for (int row = 0; row < visible_rows; row++) {
        mvhline(row, layout->editor_x, ' ', layout->columns - layout->editor_x);
    }
    if (app->buffer_count == 0) {
        draw_utf8(2, source_x, "Select a file in the sidebar or run terminal-editor FILE",
                  visible_columns, PAIR_INFO, false);
        return;
    }
    Buffer *buffer = &app->buffers[app->active_buffer];
    keep_cursor_visible(app, layout, 2);
    for (int row = 0; row < visible_rows; row++) {
        size_t line_index = buffer->top_line + (size_t)row;
        if (line_index >= buffer->line_count) {
            continue;
        }
        char number[32];
        Diagnostic *diagnostic = diagnostic_for_line(buffer, line_index);
        int gutter_pair = diagnostic == NULL ? PAIR_GUTTER : severity_pair(diagnostic->severity);
        snprintf(number, sizeof(number), "%c%4zu ", diagnostic == NULL ? ' ' : '!',
                 line_index + 1);
        attrset(COLOR_PAIR(gutter_pair));
        mvaddnstr(row, layout->editor_x, number, GUTTER_WIDTH);
        int end_x = source_x;
        draw_line(buffer, line_index, row, source_x, visible_columns,
                  buffer->left_cell, &end_x);
        if (diagnostic != NULL) {
            int x = diagnostic_x(buffer, line_index, source_x, visible_columns,
                                 buffer->left_cell);
            if (x < layout->columns - 5) {
                char label[1024];
                const char *severity = diagnostic->severity == DIAG_ERROR ? "ERR" :
                                       diagnostic->severity == DIAG_WARNING ? "WARN" : "INFO";
                snprintf(label, sizeof(label), "│ %s %s", severity,
                         diagnostic->message == NULL ? "" : diagnostic->message);
                draw_utf8(row, x, label, layout->columns - x,
                          severity_pair(diagnostic->severity), false);
            }
        }
    }
    size_t cursor_cell = buffer_column_to_cell(&buffer->lines[buffer->cursor_line],
                                               buffer->cursor_column);
    int cursor_row = (int)(buffer->cursor_line - buffer->top_line);
    int cursor_column = source_x + (int)cursor_cell - (int)buffer->left_cell;
    if (cursor_row >= 0 && cursor_row < visible_rows && cursor_column >= source_x &&
        cursor_column < layout->columns) {
        move(cursor_row, cursor_column);
    }
}

static int text_cell_width(const char *text)
{
    size_t offset = 0;
    int cells = 0;
    mbstate_t state = {0};
    while (text[offset] != '\0') {
        wchar_t character;
        size_t amount = mbrtowc(&character, text + offset, strlen(text + offset), &state);
        if (amount == (size_t)-1 || amount == (size_t)-2 || amount == 0) {
            memset(&state, 0, sizeof(state));
            amount = 1;
            character = L'�';
        }
        int width = wcwidth(character);
        cells += width > 0 ? width : 1;
        offset += amount;
    }
    return cells;
}

static void draw_footerbar(App *app, const Layout *layout, bool show_message,
                           const char *message)
{
    int row = layout->rows - 1;
    int start = layout->editor_x;
    int width = layout->columns - start;
    char branch_label[sizeof(app->git_branch) + 8];
    int branch_width;
    int message_start;
    int saved_y;
    int saved_x;

    if (row < 0 || width <= 0) {
        return;
    }
    getyx(stdscr, saved_y, saved_x);
    snprintf(branch_label, sizeof(branch_label), "Git: %s", app->git_branch);
    branch_width = text_cell_width(branch_label);
    if (branch_width > width - 2) {
        branch_width = width - 2;
    }
    attrset(COLOR_PAIR(PAIR_DEFAULT));
    mvhline(row, start, ' ', width);
    mvhline(row - 1, start, ' ', width);
    draw_utf8(row, start + 1, branch_label, branch_width, PAIR_SELECTED, false);
    message_start = start + branch_width + 3;
    if (show_message && message_start < layout->columns) {
        draw_utf8(row, message_start, message, layout->columns - message_start,
                  PAIR_SELECTED, false);
    }
    move(saved_y, saved_x);
}

static void draw_screen(App *app)
{
    Layout layout = get_layout(app);
    bool external_change = active_external_change_pending(app);
    bool show_message = status_visible(app);
    const char *message = external_change
                              ? "File changed on disk: Ctrl+R reloads, Ctrl+S overwrites"
                              : app->message;

    if (layout.sidebar_visible) {
        uint64_t key = sidebar_key(app, &layout);
        if (!sidebar_render_key_valid || key != sidebar_render_key) {
            draw_sidebar(app, &layout);
            sidebar_render_key = key;
            sidebar_render_key_valid = true;
        }
    } else {
        sidebar_render_key_valid = false;
        for (int row = 0; row < layout.rows; row++) {
            mvhline(row, 0, ' ', layout.editor_x);
        }
        draw_utf8(0, 0, "+", 1, PAIR_DEFAULT, true);
    }
    draw_editor(app, &layout);
    draw_footerbar(app, &layout, show_message, message);
    attrset(A_NORMAL);
    if (kitty_sync_updates) {
        fflush(stdout);
        putp("\033[?2026h");
        fflush(stdout);
    }
    refresh();
    if (kitty_sync_updates) {
        putp("\033[?2026l");
        fflush(stdout);
    }
}

static int refresh_files(App *app)
{
    FileEntry *entries = NULL;
    size_t count = 0;
    struct stat directory_metadata;
    bool have_directory_metadata = stat(app->current_directory, &directory_metadata) == 0;
    char *selected_path = app->selected_file < app->file_count
                              ? strdup(app->files[app->selected_file].path)
                              : NULL;

    if (files_list(app->current_directory, &entries, &count) != 0) {
        free(selected_path);
        return -1;
    }
    files_free(app->files, app->file_count);
    app->files = entries;
    app->file_count = count;
    if (selected_path != NULL) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].path, selected_path) == 0) {
                app->selected_file = i;
                found = true;
                break;
            }
        }
        if (!found) {
            app->selected_file = count == 0 ? 0 : count - 1;
        }
    } else if (app->selected_file >= count) {
        app->selected_file = count == 0 ? 0 : count - 1;
    }
    free(selected_path);
    if (have_directory_metadata) {
        app->directory_metadata = directory_metadata;
        app->directory_metadata_valid = true;
    }
    return 0;
}

static bool directory_metadata_changed(const App *app)
{
    struct stat current;
    if (stat(app->current_directory, &current) != 0) {
        return false;
    }
    if (!app->directory_metadata_valid) {
        return true;
    }
    return current.st_dev != app->directory_metadata.st_dev ||
           current.st_ino != app->directory_metadata.st_ino ||
           current.st_mtim.tv_sec != app->directory_metadata.st_mtim.tv_sec ||
           current.st_mtim.tv_nsec != app->directory_metadata.st_mtim.tv_nsec ||
           current.st_ctim.tv_sec != app->directory_metadata.st_ctim.tv_sec ||
           current.st_ctim.tv_nsec != app->directory_metadata.st_ctim.tv_nsec;
}

static int append_buffer(App *app, const char *path)
{
    Buffer buffer = {0};
    Buffer *buffers;

    if (buffer_load(&buffer, path) != 0) {
        return -1;
    }
    buffers = realloc(app->buffers, (app->buffer_count + 1) * sizeof(*buffers));
    if (buffers == NULL) {
        buffer_destroy(&buffer);
        return -1;
    }
    app->buffers = buffers;
    app->buffers[app->buffer_count] = buffer;
    app->active_buffer = app->buffer_count;
    app->buffer_count++;
    app->editor_focused = true;
    return 0;
}

static int open_file(App *app, const char *path)
{
    struct stat status;
    if (stat(path, &status) == 0 && S_ISDIR(status.st_mode)) {
        char *directory = realpath(path, NULL);
        if (directory == NULL) {
            return -1;
        }
        free(app->current_directory);
        app->current_directory = directory;
        app->file_scroll = 0;
        return refresh_files(app);
    }
    for (size_t i = 0; i < app->buffer_count; i++) {
        if (buffer_path_is(&app->buffers[i], path)) {
            app->active_buffer = i;
            app->editor_focused = true;
            return 0;
        }
    }
    return append_buffer(app, path);
}

static Buffer *active_buffer(App *app)
{
    return app->buffer_count == 0 ? NULL : &app->buffers[app->active_buffer];
}

static LintRule *lint_rule_for(AppConfig *config, const char *path)
{
    const char *name = strrchr(path, '/');
    const char *extension;
    name = name == NULL ? path : name + 1;
    extension = strrchr(name, '.');
    if (extension == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < config->rule_count; i++) {
        if (strcmp(config->rules[i].extension, extension) == 0) {
            return &config->rules[i];
        }
    }
    return NULL;
}

static bool same_path(const char *first, const char *second)
{
    char *left = realpath(first, NULL);
    char *right = realpath(second, NULL);
    bool same = left != NULL && right != NULL ? strcmp(left, right) == 0
                                              : strcmp(first, second) == 0;
    free(left);
    free(right);
    return same;
}

static void run_lint(App *app, Buffer *buffer)
{
    LintRule *rule = buffer->path == NULL ? NULL : lint_rule_for(&app->config,
                                                                buffer->path);
    Diagnostic *items = NULL;
    size_t count = 0;
    int exit_status = 0;

    if (rule == NULL) {
        diagnostics_free(buffer->diagnostics, buffer->diagnostic_count);
        buffer->diagnostics = NULL;
        buffer->diagnostic_count = 0;
        return;
    }
    if (lint_run(rule, buffer->path, &items, &count, &exit_status) != 0) {
        char message[256];
        snprintf(message, sizeof(message), "Lint failed: %s", strerror(errno));
        set_message(app, message);
        return;
    }
    for (size_t i = 0; i < count;) {
        if (same_path(items[i].path, buffer->path)) {
            i++;
            continue;
        }
        free(items[i].path);
        free(items[i].message);
        memmove(&items[i], &items[i + 1], (count - i - 1) * sizeof(*items));
        count--;
    }
    diagnostics_free(buffer->diagnostics, buffer->diagnostic_count);
    buffer->diagnostics = items;
    buffer->diagnostic_count = count;
    for (size_t i = 0; i < count; i++) {
        if (buffer->diagnostics[i].line > buffer->line_count) {
            buffer->diagnostics[i].line = buffer->line_count;
        }
        size_t maximum = buffer_line_codepoints(
            &buffer->lines[buffer->diagnostics[i].line - 1]) + 1;
        if (buffer->diagnostics[i].column > maximum) {
            buffer->diagnostics[i].column = maximum;
        }
    }
    if (count > 0) {
        char message[128];
        snprintf(message, sizeof(message), "Lint: %zu diagnostic(s)", count);
        set_message(app, message);
    } else if (exit_status != 0) {
        char message[128];
        snprintf(message, sizeof(message), "Lint exited with status %d", exit_status);
        set_message(app, message);
    } else {
        set_message(app, "No lint issues");
    }
}

static int reload_buffer_from_disk(Buffer *buffer)
{
    Buffer loaded = {0};
    size_t old_line = buffer->cursor_line;
    size_t old_column = buffer->cursor_column;
    size_t old_top = buffer->top_line;
    size_t old_left = buffer->left_cell;

    if (buffer->path == NULL || buffer_load(&loaded, buffer->path) != 0) {
        return -1;
    }
    loaded.cursor_line = old_line < loaded.line_count ? old_line : loaded.line_count - 1;
    size_t max_column = buffer_line_codepoints(&loaded.lines[loaded.cursor_line]);
    loaded.cursor_column = old_column < max_column ? old_column : max_column;
    loaded.top_line = old_top < loaded.line_count ? old_top : loaded.line_count - 1;
    loaded.left_cell = old_left;
    buffer_destroy(buffer);
    *buffer = loaded;
    return 0;
}

static bool poll_external_changes(App *app)
{
    bool changed_any = refresh_git_branch(app);

    if (directory_metadata_changed(app)) {
        if (refresh_files(app) == 0) {
            changed_any = true;
        } else {
            set_message(app, "Could not refresh directory");
        }
    }

    for (size_t i = 0; i < app->buffer_count; i++) {
        Buffer *buffer = &app->buffers[i];
        if (!buffer_check_external_change(buffer)) {
            continue;
        }
        changed_any = true;
        if (buffer->dirty) {
            buffer->external_change_pending = true;
            continue;
        }
        if (reload_buffer_from_disk(buffer) == 0) {
            if (i == app->active_buffer) {
                run_lint(app, buffer);
                set_message(app, "Reloaded external changes");
            }
        } else {
            buffer->disk_metadata_valid = false;
            if (i == app->active_buffer) {
                set_message(app, "Could not reload changed file");
            }
        }
    }
    return changed_any;
}

static int prompt_path(const char *label, char *output, size_t output_size)
{
    wchar_t input[4096] = {0};
    size_t length = 0;
    int rows;
    int columns;
    wint_t key;
    int result;

    sidebar_render_key_valid = false;
    curs_set(1);
    getmaxyx(stdscr, rows, columns);
    for (;;) {
        move(rows - 1, 0);
        clrtoeol();
        draw_utf8(rows - 1, 0, label, columns, PAIR_INFO, true);
        int label_width = (int)strlen(label);
        mvaddnwstr(rows - 1, label_width, input, (int)length);
        move(rows - 1, label_width + (int)length);
        refresh();
        result = get_wch(&key);
        if (result == ERR) {
            continue;
        }
        if (result == KEY_CODE_YES) {
            if (key == KEY_BACKSPACE && length > 0) {
                length--;
                input[length] = L'\0';
            }
            continue;
        }
        if (key == 27) {
            return 0;
        }
        if (key == L'\n' || key == L'\r' || key == KEY_ENTER) {
            break;
        }
        if ((key == 127 || key == 8) && length > 0) {
            length--;
            input[length] = L'\0';
        } else if (iswprint(key) && length + 1 < sizeof(input) / sizeof(input[0])) {
            input[length++] = (wchar_t)key;
            input[length] = L'\0';
        }
    }
    size_t converted = wcstombs(output, input, output_size - 1);
    if (converted == (size_t)-1) {
        errno = EILSEQ;
        return -1;
    }
    output[converted] = '\0';
    return output[0] == '\0' ? 0 : 1;
}

static void create_sidebar_entry(App *app)
{
    wint_t key;
    char name[4096];
    char *path;
    int kind = 0;
    int rows;
    int columns;
    int result;
    bool opened = false;

    sidebar_render_key_valid = false;
    getmaxyx(stdscr, rows, columns);
    for (;;) {
        move(rows - 1, 0);
        clrtoeol();
        draw_utf8(rows - 1, 0, "Create (f)ile or (d)irectory (Esc cancels):",
                  columns, PAIR_INFO, true);
        refresh();
        result = get_wch(&key);
        if (result == ERR) continue;
        if (key == 27) return;
        if (result != KEY_CODE_YES && (key == L'f' || key == L'F')) { kind = 1; break; }
        if (result != KEY_CODE_YES && (key == L'd' || key == L'D')) { kind = 2; break; }
    }

    result = prompt_path(kind == 1 ? "New file name: " : "New directory name: ",
                         name, sizeof(name));
    if (result <= 0) {
        set_message(app, "Creation cancelled");
        return;
    }
    if (strchr(name, '/') != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        set_message(app, "Enter a name in the current directory");
        return;
    }
    path = join_path(app->current_directory, name);
    if (path == NULL) {
        set_message(app, "Not enough memory to create entry");
        return;
    }

    if (kind == 1) {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            char message[256];
            snprintf(message, sizeof(message), "Create failed: %s", strerror(errno));
            set_message(app, message);
            free(path);
            return;
        }
        close(fd);
    } else if (mkdir(path, 0777) != 0) {
        char message[256];
        snprintf(message, sizeof(message), "Create failed: %s", strerror(errno));
        set_message(app, message);
        free(path);
        return;
    }

    if (refresh_files(app) != 0) {
        set_message(app, "Created entry; could not refresh sidebar");
        free(path);
        return;
    }
    for (size_t i = 0; i < app->file_count; i++) {
        if (strcmp(app->files[i].path, path) == 0) {
            app->selected_file = i;
            int visible = rows > 4 ? (rows - 4 + 1) / 2 : 1;
            app->file_scroll = i >= (size_t)visible ? i - (size_t)visible + 1 : 0;
            break;
        }
    }
    if (kind == 1) {
        if (open_file(app, path) == 0) {
            app->editor_focused = true;
            opened = true;
        }
        else set_message(app, "File created; could not open it");
    }
    if (kind == 2 || opened) {
        set_message(app, kind == 1 ? "File created" : "Directory created");
    }
    free(path);
}

static bool confirm_action(const char *prompt)
{
    wint_t answer;
    int rows;
    int columns;
    int result;

    sidebar_render_key_valid = false;
    getmaxyx(stdscr, rows, columns);
    move(rows - 1, 0);
    clrtoeol();
    draw_utf8(rows - 1, 0, prompt, columns, PAIR_WARNING, true);
    refresh();
    do {
        result = get_wch(&answer);
    } while (result == ERR);
    return answer == L'y' || answer == L'Y';
}

static bool save_active_buffer(App *app, bool confirm_external)
{
    Buffer *buffer = active_buffer(app);
    char selected_path[4096];
    const char *path;
    int prompt_result;

    if (buffer != NULL && buffer->path != NULL &&
        buffer_check_external_change(buffer)) {
        if (buffer->dirty) {
            buffer->external_change_pending = true;
        } else if (reload_buffer_from_disk(buffer) != 0) {
            buffer->disk_metadata_valid = false;
            set_message(app, "Could not reload changed file");
            return false;
        }
    }
    if (buffer != NULL && buffer->external_change_pending &&
        confirm_external &&
        !confirm_action("File changed externally. Overwrite it? [y/N]")) {
        return false;
    }

    if (buffer == NULL) {
        prompt_result = prompt_path("Save new file: ", selected_path,
                                    sizeof(selected_path));
        if (prompt_result <= 0) {
            set_message(app, "Save cancelled");
            return false;
        }
        if (append_buffer(app, selected_path) != 0) {
            set_message(app, "Could not create buffer");
            return false;
        }
        buffer = active_buffer(app);
    }
    if (buffer->path == NULL) {
        prompt_result = prompt_path("Save as: ", selected_path,
                                    sizeof(selected_path));
        if (prompt_result <= 0) {
            set_message(app, "Save cancelled");
            return false;
        }
        path = selected_path;
    } else {
        path = buffer->path;
    }
    if (buffer_save(buffer, path) != 0) {
        char message[256];
        snprintf(message, sizeof(message), "Save failed: %s", strerror(errno));
        set_message(app, message);
        return false;
    }
    buffer->external_change_pending = false;
    refresh_files(app);
    set_message(app, "Saved");
    run_lint(app, buffer);
    return true;
}

static void close_active_buffer(App *app)
{
    size_t closing = app->active_buffer;
    if (app->buffer_count == 0 || closing >= app->buffer_count) {
        set_message(app, "No open file");
        return;
    }
    if (app->buffers[closing].dirty && !save_active_buffer(app, false)) {
        return;
    }
    buffer_destroy(&app->buffers[closing]);
    memmove(&app->buffers[closing], &app->buffers[closing + 1],
            (app->buffer_count - closing - 1) * sizeof(*app->buffers));
    app->buffer_count--;
    if (app->buffer_count == 0) {
        free(app->buffers);
        app->buffers = NULL;
        app->active_buffer = 0;
    } else if (closing >= app->buffer_count) {
        app->active_buffer = app->buffer_count - 1;
    } else {
        app->active_buffer = closing;
    }
    app->selected_buffer = app->active_buffer;
    app->editor_focused = true;
    set_message(app, "Closed file");
}

static bool any_dirty(const App *app)
{
    for (size_t i = 0; i < app->buffer_count; i++) {
        if (app->buffers[i].dirty) {
            return true;
        }
    }
    return false;
}

static bool confirm_exit(App *app)
{
    wint_t answer;
    int rows;
    int columns;
    if (!any_dirty(app)) {
        return true;
    }
    sidebar_render_key_valid = false;
    getmaxyx(stdscr, rows, columns);
    move(rows - 1, 0);
    clrtoeol();
    draw_utf8(rows - 1, 0, "Unsaved changes. Exit? [y/N]", columns,
              PAIR_WARNING, true);
    refresh();
    if (get_wch(&answer) == ERR) {
        return false;
    }
    return answer == L'y' || answer == L'Y';
}

static void enter_directory(App *app, const char *path)
{
    char *directory = realpath(path, NULL);
    if (directory == NULL) {
        set_message(app, "Could not open directory");
        return;
    }
    free(app->current_directory);
    app->current_directory = directory;
    app->file_scroll = 0;
    if (refresh_files(app) != 0) {
        set_message(app, "Could not read directory");
    }
}

static void sidebar_click(App *app, const Layout *layout, int y)
{
    if (y >= 4 && y < layout->rows && (y - 4) % 2 == 0) {
        size_t index = app->file_scroll + (size_t)((y - 4) / 2);
        if (index < app->file_count) {
            FileEntry *entry = &app->files[index];
            app->selected_file = index;
            if (entry->is_directory) {
                enter_directory(app, entry->path);
            } else if (open_file(app, entry->path) != 0) {
                set_message(app, "Could not open file");
            }
        }
    }
}

static void delete_sidebar_file(App *app, const Layout *layout, int y)
{
    size_t index;
    FileEntry *entry;
    char prompt[256];

    if (y < 4 || y >= layout->rows || (y - 4) % 2 != 0) {
        return;
    }
    index = app->file_scroll + (size_t)((y - 4) / 2);
    if (index >= app->file_count) {
        return;
    }
    entry = &app->files[index];
    if (entry->is_directory) {
        set_message(app, "Directories cannot be deleted from the sidebar");
        return;
    }
    for (size_t i = 0; i < app->buffer_count; i++) {
        if (buffer_path_is(&app->buffers[i], entry->path) && app->buffers[i].dirty) {
            set_message(app, "Cannot delete a file with unsaved changes");
            return;
        }
    }
    snprintf(prompt, sizeof(prompt), "Delete %.180s permanently? [y/N]", entry->name);
    if (!confirm_action(prompt)) {
        return;
    }
    if (unlink(entry->path) != 0) {
        char message[256];
        snprintf(message, sizeof(message), "Delete failed: %s", strerror(errno));
        set_message(app, message);
        return;
    }
    if (refresh_files(app) != 0) {
        set_message(app, "File deleted; could not refresh directory");
        return;
    }
    set_message(app, "File deleted");
}

static void place_mouse_cursor(App *app, const Layout *layout, int y, int x,
                               bool extend_selection)
{
    Buffer *buffer = active_buffer(app);
    int source_x = layout->editor_x + GUTTER_WIDTH;
    if (buffer == NULL || y < 0 || y >= layout->rows) {
        return;
    }
    size_t line_index = buffer->top_line + (size_t)y;
    if (line_index >= buffer->line_count) {
        line_index = buffer->line_count - 1;
    }
    Diagnostic *diagnostic = diagnostic_for_line(buffer, line_index);
    if (diagnostic != NULL &&
        (x < source_x || x >= diagnostic_x(buffer, line_index, source_x,
                                           layout->columns - source_x,
                                           buffer->left_cell))) {
        buffer_set_cursor(buffer, diagnostic->line > 0 ? diagnostic->line - 1 : 0,
                          diagnostic->column > 0 ? diagnostic->column - 1 : 0, false);
        return;
    }
    size_t cell = x > source_x ? buffer->left_cell + (size_t)(x - source_x)
                               : buffer->left_cell;
    size_t column = buffer_cell_to_column(&buffer->lines[line_index], cell);
    buffer_set_cursor(buffer, line_index, column, extend_selection);
}

static void set_mouse_pointer_shape(const char *shape)
{
    if (!kitty_sync_updates || strcmp(current_pointer_shape, shape) == 0) {
        return;
    }
    fprintf(stdout, "\033]22;%s\033\\", shape);
    fflush(stdout);
    snprintf(current_pointer_shape, sizeof(current_pointer_shape), "%s", shape);
}

static void update_mouse_pointer(const App *app, const Layout *layout,
                                 const MEVENT *event)
{
    bool clickable = false;
    bool sidebar_hover = layout->sidebar_visible && event->x >= 0 &&
                         event->x < layout->sidebar_width;

    if (sidebar_hover) {
        clickable = (event->y == 0 && event->x >= layout->sidebar_width - 3) ||
                    (event->y == 2 && event->x >= layout->sidebar_width - 4 &&
                     event->x < layout->sidebar_width - 2) ||
                    (event->y >= 4 && event->y < layout->rows &&
                     (event->y - 4) % 2 == 0 &&
                     app->file_scroll + (size_t)((event->y - 4) / 2) < app->file_count);
    } else if (!layout->sidebar_visible && event->x == 0 && event->y == 0) {
        clickable = true;
    }
    set_mouse_pointer_shape(clickable ? "pointer" : sidebar_hover ? "default" : "text");
}

static void handle_mouse(App *app, const Layout *layout, MEVENT *event)
{
    update_mouse_pointer(app, layout, event);
    if (!layout->sidebar_visible && event->x < layout->editor_x) {
        if (event->y == 0 && (event->bstate & BUTTON1_CLICKED)) {
            app->config.sidebar_visible = true;
        }
        return;
    }
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        int delta = event->bstate & BUTTON4_PRESSED ? -3 : 3;
        if (layout->sidebar_visible && event->x < layout->sidebar_width) {
            if (event->y >= 4) {
                long next = (long)app->file_scroll + delta;
                app->file_scroll = next < 0 ? 0 : (size_t)next;
            }
        } else if (active_buffer(app) != NULL) {
            Buffer *buffer = active_buffer(app);
            long next = (long)buffer->top_line + delta;
            buffer->top_line = next < 0 ? 0 : (size_t)next;
        }
        return;
    }
    if (layout->sidebar_visible && event->x < layout->sidebar_width) {
        app->editor_focused = false;
        if (event->y == 0 && event->x >= layout->sidebar_width - 3 &&
            (event->bstate & BUTTON1_CLICKED)) {
            app->config.sidebar_visible = false;
        } else if (event->y == 2 && event->x >= layout->sidebar_width - 4 &&
                   event->x < layout->sidebar_width - 2 &&
                   (event->bstate & BUTTON1_CLICKED)) {
            create_sidebar_entry(app);
        } else if (event->bstate & BUTTON3_CLICKED) {
            delete_sidebar_file(app, layout, event->y);
        } else if (event->bstate & BUTTON1_CLICKED) {
            sidebar_click(app, layout, event->y);
        }
        return;
    }
    app->editor_focused = true;
    Buffer *buffer = active_buffer(app);
    if ((event->bstate & REPORT_MOUSE_POSITION) && buffer != NULL && buffer->selecting) {
        place_mouse_cursor(app, layout, event->y, event->x, true);
    } else if (event->bstate & BUTTON1_PRESSED) {
        place_mouse_cursor(app, layout, event->y, event->x, false);
        buffer = active_buffer(app);
        if (buffer != NULL) {
            buffer->selection_line = buffer->cursor_line;
            buffer->selection_column = buffer->cursor_column;
            buffer->selecting = true;
        }
    } else if (event->bstate & BUTTON1_CLICKED) {
        place_mouse_cursor(app, layout, event->y, event->x, false);
    }
}

static void handle_key(App *app, wint_t input, int key_result)
{
    Buffer *buffer = active_buffer(app);
    if (key_result == KEY_CODE_YES) {
        if (input == KEY_F(2)) {
            app->config.sidebar_visible = !app->config.sidebar_visible;
        } else if (input == KEY_ENTER && app->editor_focused && buffer != NULL) {
            if (buffer_history_record(buffer) != 0) return;
            buffer_newline(buffer, app->config.autoindent, app->config.tab_width);
        } else if (input == KEY_BACKSPACE && app->editor_focused && buffer != NULL) {
            if (buffer_history_record(buffer) != 0) return;
            buffer_backspace(buffer);
        } else if (input == KEY_DC && app->editor_focused && buffer != NULL) {
            if (buffer_history_record(buffer) != 0) return;
            buffer_delete(buffer);
        }
        return;
    }
    if (input == 24 || input == 3) {
        if (confirm_exit(app)) {
            app->running = false;
        }
        return;
    }
    if (input == 23) {
        close_active_buffer(app);
        return;
    }
    if (input == 26 || input == 25) {
        buffer = active_buffer(app);
        if (buffer == NULL || !app->editor_focused) return;
        int result = input == 26 ? buffer_undo(buffer) : buffer_redo(buffer);
        set_message(app, result < 0 ? "Not enough memory for undo history"
                                    : result > 0 ? (input == 26 ? "Undo" : "Redo")
                                                 : (input == 26 ? "Nothing to undo" : "Nothing to redo"));
        return;
    }
    if (input == 18) {
        buffer = active_buffer(app);
        if (buffer == NULL || buffer->path == NULL) {
            set_message(app, "No file to reload");
            return;
        }
        if (buffer_check_external_change(buffer)) {
            buffer->external_change_pending = true;
        }
        if (!buffer->external_change_pending) {
            set_message(app, "No external changes");
            return;
        }
        if (buffer->dirty &&
            !confirm_action("Reload file and discard local edits? [y/N]")) {
            return;
        }
        if (reload_buffer_from_disk(buffer) != 0) {
            buffer->disk_metadata_valid = false;
            set_message(app, "Could not reload changed file");
            return;
        }
        run_lint(app, buffer);
        set_message(app, "Reloaded external changes");
        return;
    }
    if (input == 15 || input == 19) {
        save_active_buffer(app, true);
        return;
    }
    if (!app->editor_focused) {
        return;
    }
    buffer = active_buffer(app);
    if (buffer == NULL) {
        return;
    }
    if (input == L'\n' || input == L'\r' || input == KEY_ENTER) {
        if (buffer_history_record(buffer) != 0) return;
        buffer_newline(buffer, app->config.autoindent, app->config.tab_width);
    } else if (input == L'\t') {
        if (buffer_history_record(buffer) != 0) return;
        for (size_t i = 0; i < app->config.tab_width; i++) {
            buffer_insert(buffer, L' ');
        }
    } else if (input == 127 || input == 8) {
        if (buffer_history_record(buffer) != 0) return;
        buffer_backspace(buffer);
    } else if (iswprint(input)) {
        if (buffer_history_record(buffer) != 0) return;
        buffer_insert(buffer, (wchar_t)input);
    }
}

int ui_run(App *app)
{
    wint_t input;
    int result;
    bool redraw = true;
    bool message_was_visible = false;

    if (initscr() == NULL) {
        return 1;
    }
    raw();
    noecho();
    keypad(stdscr, TRUE);
    idlok(stdscr, FALSE);
    idcok(stdscr, FALSE);
    scrollok(stdscr, FALSE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    wtimeout(stdscr, 200);
    curs_set(1);
    /* DECSCUSR 6 selects a steady vertical bar cursor in Kitty and compatible terminals. */
    fputs("\033[6 q", stdout);
    fflush(stdout);
    init_colors(&app->config);
    const char *term = getenv("TERM");
    kitty_sync_updates = term != NULL && strcmp(term, "xterm-kitty") == 0;
    refresh_git_branch(app);
    if (refresh_files(app) != 0) {
        set_message(app, "Could not refresh directory");
    }

    while (app->running) {
        Layout layout = get_layout(app);
        if (redraw) {
            draw_screen(app);
            message_was_visible = status_visible(app);
            redraw = false;
        }
        result = get_wch(&input);
        if (result == ERR) {
            bool external_changed = poll_external_changes(app);
            if (external_changed || status_visible(app) != message_was_visible) {
                redraw = true;
            }
            continue;
        }
        if (result == KEY_CODE_YES && input == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) == OK) {
                handle_mouse(app, &layout, &event);
            }
        } else if (result == KEY_CODE_YES && input == KEY_RESIZE) {
            clearok(stdscr, TRUE);
        } else {
            handle_key(app, input, result);
        }
        redraw = true;
    }
    if (restore_border_color) {
        init_color(BORDER_COLOR, saved_border_color[0], saved_border_color[1],
                   saved_border_color[2]);
    }
    if (kitty_sync_updates) {
        fprintf(stdout, "\033]22;\033\\");
        fflush(stdout);
    }
    endwin();
    fputs("\033[0 q", stdout);
    fflush(stdout);
    return 0;
}
