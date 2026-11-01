#include "ui.h"

#include "buffer.h"
#include "files.h"

#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    PAIR_SELECTION
};

typedef struct {
    int rows;
    int columns;
    int sidebar_width;
    int editor_x;
    int editor_width;
    int file_split;
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

static void init_colors(void)
{
    if (!has_colors()) {
        return;
    }
    start_color();
    use_default_colors();
    init_pair(PAIR_DEFAULT, COLOR_WHITE, -1);
    init_pair(PAIR_HEADING, COLOR_CYAN, -1);
    init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_GUTTER, COLOR_WHITE, -1);
    init_pair(PAIR_ERROR, COLOR_RED, -1);
    init_pair(PAIR_WARNING, COLOR_YELLOW, -1);
    init_pair(PAIR_INFO, COLOR_CYAN, -1);
    init_pair(PAIR_SELECTION, COLOR_BLACK, COLOR_CYAN);
}

static Layout get_layout(const App *app)
{
    Layout layout = {0};
    getmaxyx(stdscr, layout.rows, layout.columns);
    layout.file_split = layout.rows / 2;
    layout.sidebar_width = (int)app->config.sidebar_width;
    layout.sidebar_visible = app->config.sidebar_visible &&
                             layout.columns >= layout.sidebar_width + MIN_EDITOR_WIDTH + GUTTER_WIDTH;
    if (layout.sidebar_visible) {
        layout.editor_x = layout.sidebar_width + 1;
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

static void draw_sidebar(App *app, const Layout *layout)
{
    int file_end = layout->file_split;
    int buffer_header = layout->file_split;
    int width = layout->sidebar_width;

    if (!layout->sidebar_visible || width < 8) {
        return;
    }
    draw_utf8(0, 1, "FILES", width - 2, PAIR_HEADING, true);
    for (int row = 1; row < file_end; row++) {
        size_t index = app->file_scroll + (size_t)(row - 1);
        if (index >= app->file_count) {
            break;
        }
        FileEntry *entry = &app->files[index];
        char label[4096];
        int pair = index == app->selected_file ? PAIR_SELECTED : PAIR_DEFAULT;
        snprintf(label, sizeof(label), "%s %s", entry->is_directory ? "/" : " ",
                 entry->name);
        attrset(COLOR_PAIR(pair));
        mvhline(row, 0, ' ', width);
        draw_utf8(row, 1, label, width - 2, pair, false);
    }
    if (buffer_header < layout->rows) {
        draw_utf8(buffer_header, 1, "OPEN", width - 2, PAIR_HEADING, true);
    }
    for (int row = buffer_header + 1; row < layout->rows; row++) {
        size_t index = app->buffer_scroll + (size_t)(row - buffer_header - 1);
        if (index >= app->buffer_count) {
            break;
        }
        Buffer *buffer = &app->buffers[index];
        const char *name = buffer->path == NULL ? "[new file]" : strrchr(buffer->path, '/')
                                                               ? strrchr(buffer->path, '/') + 1
                                                               : buffer->path;
        char label[4096];
        int pair = index == app->active_buffer ? PAIR_SELECTED : PAIR_DEFAULT;
        snprintf(label, sizeof(label), "%s%s", buffer->dirty ? "* " : "  ", name);
        attrset(COLOR_PAIR(pair));
        mvhline(row, 0, ' ', width);
        draw_utf8(row, 1, label, width - 2, pair, false);
    }
    attrset(A_NORMAL);
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

static void keep_cursor_visible(App *app, const Layout *layout, int message_row)
{
    Buffer *buffer;
    int visible_rows = message_row ? layout->rows - 1 : layout->rows;
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

static void draw_editor(App *app, const Layout *layout, bool message_row)
{
    int source_x = layout->editor_x + GUTTER_WIDTH;
    int visible_rows = message_row ? layout->rows - 1 : layout->rows;
    int visible_columns = layout->columns - source_x;

    if (visible_rows <= 0 || visible_columns <= 0) {
        return;
    }
    if (app->buffer_count == 0) {
        draw_utf8(2, source_x, "Select a file in the sidebar or run terminal-editor FILE",
                  visible_columns, PAIR_INFO, false);
        return;
    }
    Buffer *buffer = &app->buffers[app->active_buffer];
    keep_cursor_visible(app, layout, message_row);
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

static void draw_screen(App *app)
{
    Layout layout = get_layout(app);
    bool show_message = message_visible(app);

    erase();
    if (layout.sidebar_visible) {
        draw_sidebar(app, &layout);
    }
    draw_editor(app, &layout, show_message);
    if (show_message && layout.rows > 0) {
        attrset(COLOR_PAIR(PAIR_INFO) | A_BOLD);
        mvhline(layout.rows - 1, 0, ' ', layout.columns);
        draw_utf8(layout.rows - 1, 0, app->message, layout.columns,
                  PAIR_INFO, true);
    }
    attrset(A_NORMAL);
    refresh();
}

static int refresh_files(App *app)
{
    FileEntry *entries = NULL;
    size_t count = 0;
    if (files_list(app->current_directory, &entries, &count) != 0) {
        return -1;
    }
    files_free(app->files, app->file_count);
    app->files = entries;
    app->file_count = count;
    if (app->selected_file >= count) {
        app->selected_file = count == 0 ? 0 : count - 1;
    }
    return 0;
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

static int prompt_path(const char *label, char *output, size_t output_size)
{
    wchar_t input[4096] = {0};
    size_t length = 0;
    int rows;
    int columns;
    wint_t key;
    int result;

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
        if (key == L'\n' || key == KEY_ENTER) {
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

static void save_active_buffer(App *app)
{
    Buffer *buffer = active_buffer(app);
    char selected_path[4096];
    const char *path;
    int prompt_result;

    if (buffer == NULL) {
        prompt_result = prompt_path("Save new file: ", selected_path,
                                    sizeof(selected_path));
        if (prompt_result <= 0) {
            set_message(app, "Save cancelled");
            return;
        }
        if (append_buffer(app, selected_path) != 0) {
            set_message(app, "Could not create buffer");
            return;
        }
        buffer = active_buffer(app);
    }
    if (buffer->path == NULL) {
        prompt_result = prompt_path("Save as: ", selected_path,
                                    sizeof(selected_path));
        if (prompt_result <= 0) {
            set_message(app, "Save cancelled");
            return;
        }
        path = selected_path;
    } else {
        path = buffer->path;
    }
    if (buffer_save(buffer, path) != 0) {
        char message[256];
        snprintf(message, sizeof(message), "Save failed: %s", strerror(errno));
        set_message(app, message);
        return;
    }
    refresh_files(app);
    set_message(app, "Saved");
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
    int split = layout->file_split;
    if (y > 0 && y < split) {
        size_t index = app->file_scroll + (size_t)(y - 1);
        if (index < app->file_count) {
            FileEntry *entry = &app->files[index];
            app->selected_file = index;
            if (entry->is_directory) {
                enter_directory(app, entry->path);
            } else if (open_file(app, entry->path) != 0) {
                set_message(app, "Could not open file");
            }
        }
    } else if (y > split && y < layout->rows) {
        size_t index = app->buffer_scroll + (size_t)(y - split - 1);
        if (index < app->buffer_count) {
            app->active_buffer = index;
            app->selected_buffer = index;
            app->editor_focused = true;
        }
    }
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
                          diagnostic->column, false);
        return;
    }
    size_t cell = x > source_x ? buffer->left_cell + (size_t)(x - source_x)
                               : buffer->left_cell;
    size_t column = buffer_cell_to_column(&buffer->lines[line_index], cell);
    buffer_set_cursor(buffer, line_index, column, extend_selection);
}

static void handle_mouse(App *app, const Layout *layout, MEVENT *event)
{
    if (!layout->sidebar_visible && event->x < layout->editor_x) {
        return;
    }
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        int delta = event->bstate & BUTTON4_PRESSED ? -3 : 3;
        if (layout->sidebar_visible && event->x < layout->sidebar_width) {
            if (event->y < layout->file_split) {
                long next = (long)app->file_scroll + delta;
                app->file_scroll = next < 0 ? 0 : (size_t)next;
            } else {
                long next = (long)app->buffer_scroll + delta;
                app->buffer_scroll = next < 0 ? 0 : (size_t)next;
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
        if (event->bstate & BUTTON1_CLICKED) {
            sidebar_click(app, layout, event->y);
        }
        return;
    }
    app->editor_focused = true;
    if (event->bstate & BUTTON1_PRESSED) {
        place_mouse_cursor(app, layout, event->y, event->x, false);
        Buffer *buffer = active_buffer(app);
        if (buffer != NULL) {
            buffer->selection_line = buffer->cursor_line;
            buffer->selection_column = buffer->cursor_column;
            buffer->selecting = true;
        }
    } else if (event->bstate & BUTTON1_CLICKED) {
        place_mouse_cursor(app, layout, event->y, event->x, false);
    } else if ((event->bstate & REPORT_MOUSE_POSITION) && active_buffer(app) != NULL) {
        place_mouse_cursor(app, layout, event->y, event->x, true);
    }
}

static void handle_key(App *app, wint_t input, int key_result)
{
    Buffer *buffer = active_buffer(app);
    if (key_result == KEY_CODE_YES) {
        if (input == KEY_F(2)) {
            app->config.sidebar_visible = !app->config.sidebar_visible;
        } else if (input == KEY_ENTER && app->editor_focused && buffer != NULL) {
            buffer_newline(buffer, app->config.autoindent, app->config.tab_width);
        } else if (input == KEY_BACKSPACE && app->editor_focused && buffer != NULL) {
            buffer_backspace(buffer);
        } else if (input == KEY_DC && app->editor_focused && buffer != NULL) {
            buffer_delete(buffer);
        }
        return;
    }
    if (input == 24) {
        if (confirm_exit(app)) {
            app->running = false;
        }
        return;
    }
    if (input == 15) {
        save_active_buffer(app);
        return;
    }
    if (!app->editor_focused) {
        return;
    }
    buffer = active_buffer(app);
    if (buffer == NULL) {
        return;
    }
    if (input == L'\n' || input == KEY_ENTER) {
        buffer_newline(buffer, app->config.autoindent, app->config.tab_width);
    } else if (input == L'\t') {
        for (size_t i = 0; i < app->config.tab_width; i++) {
            buffer_insert(buffer, L' ');
        }
    } else if (input == 127 || input == 8) {
        buffer_backspace(buffer);
    } else if (iswprint(input)) {
        buffer_insert(buffer, (wchar_t)input);
    }
}

int ui_run(App *app)
{
    wint_t input;
    int result;

    if (initscr() == NULL) {
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    wtimeout(stdscr, 200);
    curs_set(1);
    init_colors();

    while (app->running) {
        Layout layout = get_layout(app);
        draw_screen(app);
        result = get_wch(&input);
        if (result == ERR) {
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
    }
    endwin();
    return 0;
}
