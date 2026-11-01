/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Gxstavo-dev */

#ifndef TERMINAL_EDITOR_CONFIG_H
#define TERMINAL_EDITOR_CONFIG_H

#include "types.h"

void config_defaults(AppConfig *config);
char *config_default_path(void);
int config_load(AppConfig *config, const char *path);
void config_destroy(AppConfig *config);

#endif
