#ifndef TERMINAL_EDITOR_BUFFER_H
#define TERMINAL_EDITOR_BUFFER_H

#include "types.h"

#include <wchar.h>

int buffer_init(Buffer *buffer, const char *path);
int buffer_load(Buffer *buffer, const char *path);
int buffer_save(Buffer *buffer, const char *path);
void buffer_destroy(Buffer *buffer);
int buffer_insert(Buffer *buffer, wchar_t character);
int buffer_backspace(Buffer *buffer);
int buffer_delete(Buffer *buffer);
int buffer_newline(Buffer *buffer, bool autoindent, size_t tab_width);
void buffer_set_cursor(Buffer *buffer, size_t line, size_t column, bool extend_selection);
int buffer_delete_selection(Buffer *buffer);
size_t buffer_line_codepoints(const Line *line);
size_t buffer_line_cells(const Line *line);
size_t buffer_column_to_cell(const Line *line, size_t column);
size_t buffer_cell_to_column(const Line *line, size_t cell);

#endif
