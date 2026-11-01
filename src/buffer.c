#include "buffer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t next_character(const char *text, size_t remaining, mbstate_t *state,
                             wchar_t *character)
{
    size_t count = mbrtowc(character, text, remaining, state);
    if (count == (size_t)-1 || count == (size_t)-2 || count == 0) {
        memset(state, 0, sizeof(*state));
        if (character != NULL) {
            *character = (unsigned char)text[0];
        }
        return 1;
    }
    return count;
}

static size_t byte_offset(const Line *line, size_t column)
{
    size_t offset = 0;
    size_t current = 0;
    mbstate_t state = {0};

    while (offset < line->byte_length && current < column) {
        offset += next_character(line->data + offset, line->byte_length - offset,
                                 &state, NULL);
        current++;
    }
    return offset;
}

size_t buffer_line_codepoints(const Line *line)
{
    size_t offset = 0;
    size_t count = 0;
    mbstate_t state = {0};

    if (line == NULL) {
        return 0;
    }
    while (offset < line->byte_length) {
        offset += next_character(line->data + offset, line->byte_length - offset,
                                 &state, NULL);
        count++;
    }
    return count;
}

size_t buffer_column_to_cell(const Line *line, size_t column)
{
    size_t offset = 0;
    size_t cells = 0;
    size_t current = 0;
    mbstate_t state = {0};
    wchar_t character;

    while (offset < line->byte_length && current < column) {
        size_t amount = next_character(line->data + offset, line->byte_length - offset,
                                       &state, &character);
        int width = wcwidth(character);
        cells += width > 0 ? (size_t)width : 1;
        offset += amount;
        current++;
    }
    return cells;
}

size_t buffer_line_cells(const Line *line)
{
    return buffer_column_to_cell(line, buffer_line_codepoints(line));
}

size_t buffer_cell_to_column(const Line *line, size_t cell)
{
    size_t offset = 0;
    size_t cells = 0;
    size_t column = 0;
    mbstate_t state = {0};
    wchar_t character;

    while (offset < line->byte_length) {
        size_t amount = next_character(line->data + offset, line->byte_length - offset,
                                       &state, &character);
        int width = wcwidth(character);
        size_t character_width = width > 0 ? (size_t)width : 1;
        if (cells + character_width > cell) {
            break;
        }
        cells += character_width;
        offset += amount;
        column++;
    }
    return column;
}

static int set_line(Line *line, const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    free(line->data);
    line->data = copy;
    line->byte_length = length;
    return 0;
}

static int insert_line(Buffer *buffer, size_t index, const char *text, size_t length)
{
    Line line = {0};
    Line *lines;

    if (set_line(&line, text, length) != 0) {
        return -1;
    }
    lines = realloc(buffer->lines, (buffer->line_count + 1) * sizeof(*lines));
    if (lines == NULL) {
        free(line.data);
        return -1;
    }
    buffer->lines = lines;
    memmove(&buffer->lines[index + 1], &buffer->lines[index],
            (buffer->line_count - index) * sizeof(*lines));
    buffer->lines[index] = line;
    buffer->line_count++;
    return 0;
}

static void remove_line(Buffer *buffer, size_t index)
{
    free(buffer->lines[index].data);
    memmove(&buffer->lines[index], &buffer->lines[index + 1],
            (buffer->line_count - index - 1) * sizeof(*buffer->lines));
    buffer->line_count--;
}

int buffer_init(Buffer *buffer, const char *path)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->lines = calloc(1, sizeof(*buffer->lines));
    if (buffer->lines == NULL) {
        return -1;
    }
    buffer->line_count = 1;
    buffer->lines[0].data = calloc(1, 1);
    if (buffer->lines[0].data == NULL) {
        free(buffer->lines);
        memset(buffer, 0, sizeof(*buffer));
        return -1;
    }
    if (path != NULL) {
        buffer->path = strdup(path);
        if (buffer->path == NULL) {
            buffer_destroy(buffer);
            return -1;
        }
    }
    return 0;
}

static int append_loaded_line(Buffer *buffer, const char *text, size_t length)
{
    if (buffer->line_count == 0) {
        if (set_line(&buffer->lines[0], text, length) != 0) {
            return -1;
        }
        buffer->line_count = 1;
        return 0;
    }
    return insert_line(buffer, buffer->line_count, text, length);
}

int buffer_load(Buffer *buffer, const char *path)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (buffer_init(buffer, path) != 0) {
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        buffer_destroy(buffer);
        return -1;
    }

    free(buffer->lines[0].data);
    buffer->lines[0].data = NULL;
    buffer->lines[0].byte_length = 0;
    buffer->line_count = 0;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        size_t used = (size_t)length;
        bool had_newline = used > 0 && line[used - 1] == '\n';
        if (had_newline) {
            used--;
            if (used > 0 && line[used - 1] == '\r') {
                used--;
                buffer->crlf = true;
            }
        }
        if (append_loaded_line(buffer, line, used) != 0) {
            free(line);
            fclose(file);
            buffer_destroy(buffer);
            return -1;
        }
        buffer->final_newline = had_newline;
    }
    free(line);
    if (ferror(file)) {
        fclose(file);
        buffer_destroy(buffer);
        return -1;
    }
    fclose(file);
    if (buffer->line_count == 0 && insert_line(buffer, 0, "", 0) != 0) {
        buffer_destroy(buffer);
        return -1;
    }
    buffer->dirty = false;
    return 0;
}

static int delete_selection(Buffer *buffer)
{
    size_t start_line = buffer->selection_line;
    size_t start_column = buffer->selection_column;
    size_t end_line = buffer->cursor_line;
    size_t end_column = buffer->cursor_column;
    size_t start_byte;
    size_t end_byte;
    size_t remaining;
    char *merged;

    if (!buffer->selecting) {
        return 0;
    }
    if (start_line > end_line || (start_line == end_line && start_column > end_column)) {
        size_t swap = start_line;
        start_line = end_line;
        end_line = swap;
        swap = start_column;
        start_column = end_column;
        end_column = swap;
    }
    if (start_line == end_line && start_column == end_column) {
        buffer->selecting = false;
        return 0;
    }

    start_byte = byte_offset(&buffer->lines[start_line], start_column);
    end_byte = byte_offset(&buffer->lines[end_line], end_column);
    remaining = start_byte + buffer->lines[end_line].byte_length - end_byte;
    merged = malloc(remaining + 1);
    if (merged == NULL) {
        return -1;
    }
    memcpy(merged, buffer->lines[start_line].data, start_byte);
    memcpy(merged + start_byte, buffer->lines[end_line].data + end_byte,
           buffer->lines[end_line].byte_length - end_byte);
    merged[remaining] = '\0';
    free(buffer->lines[start_line].data);
    buffer->lines[start_line].data = merged;
    buffer->lines[start_line].byte_length = remaining;
    for (size_t i = start_line + 1; i <= end_line; i++) {
        remove_line(buffer, start_line + 1);
    }
    buffer->cursor_line = start_line;
    buffer->cursor_column = start_column;
    buffer->selecting = false;
    buffer->dirty = true;
    return 0;
}

int buffer_delete_selection(Buffer *buffer)
{
    return delete_selection(buffer);
}

void buffer_set_cursor(Buffer *buffer, size_t line, size_t column, bool extend_selection)
{
    if (buffer->line_count == 0) {
        return;
    }
    if (!extend_selection || !buffer->selecting) {
        buffer->selection_line = buffer->cursor_line;
        buffer->selection_column = buffer->cursor_column;
        buffer->selecting = extend_selection;
    }
    if (line >= buffer->line_count) {
        line = buffer->line_count - 1;
    }
    size_t maximum = buffer_line_codepoints(&buffer->lines[line]);
    if (column > maximum) {
        column = maximum;
    }
    buffer->cursor_line = line;
    buffer->cursor_column = column;
}

int buffer_insert(Buffer *buffer, wchar_t character)
{
    char encoded[MB_LEN_MAX];
    mbstate_t state = {0};
    size_t encoded_length;
    Line *line;
    size_t offset;
    char *text;

    if (buffer->selecting && delete_selection(buffer) != 0) {
        return -1;
    }
    encoded_length = wcrtomb(encoded, character, &state);
    if (encoded_length == (size_t)-1) {
        errno = EILSEQ;
        return -1;
    }
    line = &buffer->lines[buffer->cursor_line];
    offset = byte_offset(line, buffer->cursor_column);
    text = malloc(line->byte_length + encoded_length + 1);
    if (text == NULL) {
        return -1;
    }
    memcpy(text, line->data, offset);
    memcpy(text + offset, encoded, encoded_length);
    memcpy(text + offset + encoded_length, line->data + offset,
           line->byte_length - offset + 1);
    free(line->data);
    line->data = text;
    line->byte_length += encoded_length;
    buffer->cursor_column++;
    buffer->dirty = true;
    return 0;
}

int buffer_backspace(Buffer *buffer)
{
    Line *line;
    size_t start;
    size_t end;
    char *text;

    if (buffer->selecting) {
        return delete_selection(buffer);
    }
    line = &buffer->lines[buffer->cursor_line];
    if (buffer->cursor_column == 0) {
        if (buffer->cursor_line == 0) {
            return 0;
        }
        size_t previous = buffer->cursor_line - 1;
        size_t previous_length = buffer->lines[previous].byte_length;
        size_t previous_columns = buffer_line_codepoints(&buffer->lines[previous]);
        char *joined = realloc(buffer->lines[previous].data,
                               previous_length + line->byte_length + 1);
        if (joined == NULL) {
            return -1;
        }
        memcpy(joined + previous_length, line->data, line->byte_length + 1);
        buffer->lines[previous].data = joined;
        buffer->lines[previous].byte_length += line->byte_length;
        remove_line(buffer, buffer->cursor_line);
        buffer->cursor_line = previous;
        buffer->cursor_column = previous_columns;
        buffer->dirty = true;
        return 0;
    }
    end = byte_offset(line, buffer->cursor_column);
    start = byte_offset(line, buffer->cursor_column - 1);
    text = malloc(line->byte_length - (end - start) + 1);
    if (text == NULL) {
        return -1;
    }
    memcpy(text, line->data, start);
    memcpy(text + start, line->data + end, line->byte_length - end + 1);
    free(line->data);
    line->data = text;
    line->byte_length -= end - start;
    buffer->cursor_column--;
    buffer->dirty = true;
    return 0;
}

int buffer_delete(Buffer *buffer)
{
    Line *line;
    size_t start;
    size_t end;
    char *text;

    if (buffer->selecting) {
        return delete_selection(buffer);
    }
    line = &buffer->lines[buffer->cursor_line];
    if (buffer->cursor_column >= buffer_line_codepoints(line)) {
        if (buffer->cursor_line + 1 >= buffer->line_count) {
            if (buffer->final_newline) {
                buffer->final_newline = false;
                buffer->dirty = true;
            }
            return 0;
        }
        size_t next_length = buffer->lines[buffer->cursor_line + 1].byte_length;
        char *joined = realloc(line->data, line->byte_length + next_length + 1);
        if (joined == NULL) {
            return -1;
        }
        memcpy(joined + line->byte_length, buffer->lines[buffer->cursor_line + 1].data,
               next_length + 1);
        line->data = joined;
        line->byte_length += next_length;
        remove_line(buffer, buffer->cursor_line + 1);
        buffer->dirty = true;
        return 0;
    }
    start = byte_offset(line, buffer->cursor_column);
    end = byte_offset(line, buffer->cursor_column + 1);
    text = malloc(line->byte_length - (end - start) + 1);
    if (text == NULL) {
        return -1;
    }
    memcpy(text, line->data, start);
    memcpy(text + start, line->data + end, line->byte_length - end + 1);
    free(line->data);
    line->data = text;
    line->byte_length -= end - start;
    buffer->dirty = true;
    return 0;
}

int buffer_newline(Buffer *buffer, bool autoindent, size_t tab_width)
{
    size_t line_index;
    Line *line;
    size_t offset;
    size_t indent_bytes = 0;
    size_t indent_columns = 0;
    char *left;
    char *right;

    if (buffer->selecting && delete_selection(buffer) != 0) {
        return -1;
    }
    line_index = buffer->cursor_line;
    line = &buffer->lines[line_index];
    offset = byte_offset(line, buffer->cursor_column);
    left = strndup(line->data, offset);
    right = strdup(line->data + offset);
    if (left == NULL || right == NULL) {
        free(left);
        free(right);
        return -1;
    }
    if (autoindent) {
        while (indent_bytes < line->byte_length &&
               (line->data[indent_bytes] == ' ' || line->data[indent_bytes] == '\t')) {
            if (line->data[indent_bytes] == '\t') {
                indent_columns += tab_width == 0 ? 1 : tab_width;
            } else {
                indent_columns++;
            }
            indent_bytes++;
        }
        if (indent_bytes > 0) {
            char *indented = malloc(indent_columns + strlen(right) + 1);
            if (indented == NULL) {
                free(left);
                free(right);
                return -1;
            }
            memset(indented, ' ', indent_columns);
            strcpy(indented + indent_columns, right);
            free(right);
            right = indented;
        }
    }
    if (insert_line(buffer, buffer->cursor_line + 1, right, strlen(right)) != 0) {
        free(left);
        free(right);
        return -1;
    }
    if (set_line(&buffer->lines[line_index], left, strlen(left)) != 0) {
        remove_line(buffer, line_index + 1);
        free(left);
        free(right);
        return -1;
    }
    free(left);
    free(right);
    buffer->cursor_line++;
    buffer->cursor_column = autoindent ? indent_columns : 0;
    buffer->dirty = true;
    return 0;
}

void buffer_destroy(Buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->path);
    for (size_t i = 0; i < buffer->line_count; i++) {
        free(buffer->lines[i].data);
    }
    free(buffer->lines);
    for (size_t i = 0; i < buffer->diagnostic_count; i++) {
        free(buffer->diagnostics[i].path);
        free(buffer->diagnostics[i].message);
    }
    free(buffer->diagnostics);
    memset(buffer, 0, sizeof(*buffer));
}

int buffer_save(Buffer *buffer, const char *path)
{
    struct stat metadata;
    char *resolved = NULL;
    char *temporary;
    char *new_buffer_path = NULL;
    size_t path_length;
    int fd;
    FILE *file;
    const char *ending;
    int result = -1;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (buffer->path == NULL) {
        new_buffer_path = strdup(path);
        if (new_buffer_path == NULL) {
            return -1;
        }
    }
    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        resolved = strdup(path);
        if (resolved == NULL) {
            free(new_buffer_path);
            return -1;
        }
    }
    path_length = strlen(resolved);
    temporary = malloc(path_length + sizeof(".tmp.XXXXXX"));
    if (temporary == NULL) {
        free(resolved);
        free(new_buffer_path);
        return -1;
    }
    snprintf(temporary, path_length + sizeof(".tmp.XXXXXX"), "%s.tmp.XXXXXX", resolved);
    fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        free(resolved);
        free(new_buffer_path);
        return -1;
    }
    if (stat(resolved, &metadata) == 0) {
        (void)fchmod(fd, metadata.st_mode & 07777);
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        unlink(temporary);
        free(temporary);
        free(resolved);
        free(new_buffer_path);
        return -1;
    }
    ending = buffer->crlf ? "\r\n" : "\n";
    for (size_t i = 0; i < buffer->line_count; i++) {
        if (fwrite(buffer->lines[i].data, 1, buffer->lines[i].byte_length, file) !=
            buffer->lines[i].byte_length) {
            goto cleanup;
        }
        if (i + 1 < buffer->line_count || buffer->final_newline) {
            if (fwrite(ending, 1, strlen(ending), file) != strlen(ending)) {
                goto cleanup;
            }
        }
    }
    if (fflush(file) != 0 || fsync(fd) != 0) {
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        goto cleanup_closed;
    }
    file = NULL;
    if (rename(temporary, resolved) != 0) {
        goto cleanup_closed;
    }
    if (new_buffer_path != NULL) {
        buffer->path = new_buffer_path;
        new_buffer_path = NULL;
    }
    buffer->dirty = false;
    result = 0;
    goto cleanup_closed;

cleanup:
    fclose(file);
    file = NULL;
cleanup_closed:
    if (result != 0) {
        unlink(temporary);
    }
    free(temporary);
    free(resolved);
    free(new_buffer_path);
    return result;
}
