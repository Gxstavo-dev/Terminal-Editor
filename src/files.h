#ifndef TERMINAL_EDITOR_FILES_H
#define TERMINAL_EDITOR_FILES_H

#include "types.h"

int files_list(const char *directory, FileEntry **entries, size_t *count);
void files_free(FileEntry *entries, size_t count);

#endif
