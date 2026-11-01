#ifndef TERMINAL_EDITOR_LINT_H
#define TERMINAL_EDITOR_LINT_H

#include "types.h"

int lint_run(const LintRule *rule, const char *path, Diagnostic **items,
             size_t *count, int *exit_status);
bool diagnostic_parse_line(const char *text, Diagnostic *item);
void diagnostics_free(Diagnostic *items, size_t count);

#endif
