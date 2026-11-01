/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Gxstavo-dev */

#ifndef TERMINAL_EDITOR_CLIPBOARD_H
#define TERMINAL_EDITOR_CLIPBOARD_H

#include <stddef.h>

int clipboard_copy(const char *text, size_t length);
int clipboard_paste(char **text, size_t *length);

#endif
