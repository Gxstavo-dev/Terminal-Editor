#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_MAX_ARGS 64

void config_defaults(AppConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->tab_width = 4;
    config->sidebar_width = 28;
    config->autoindent = true;
    config->sidebar_visible = true;
    config->theme = THEME_DARK;
}

char *config_default_path(void)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    size_t length;
    char *path;

    if (base == NULL || *base == '\0') {
        if (home == NULL || *home == '\0') {
            return NULL;
        }
        base = home;
        length = strlen(base) + sizeof("/.config/terminal-editor/config");
        path = malloc(length);
        if (path != NULL) {
            snprintf(path, length, "%s/.config/terminal-editor/config", base);
        }
        return path;
    }
    length = strlen(base) + sizeof("/terminal-editor/config");
    path = malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s/terminal-editor/config", base);
    }
    return path;
}

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static void free_argv(char **argv, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(argv[i]);
    }
    free(argv);
}

static int parse_argv(const char *text, char ***argv_out, size_t *argc_out)
{
    size_t input_length = strlen(text);
    char *token = malloc(input_length + 1);
    char **argv = NULL;
    size_t argc = 0;
    size_t token_length = 0;
    char quote = '\0';
    bool escaped = false;
    bool started = false;

    if (token == NULL) {
        return -1;
    }
    for (size_t i = 0; i <= input_length; i++) {
        char character = text[i];
        if (escaped) {
            if (character == '\0') {
                goto invalid;
            }
            token[token_length++] = character;
            escaped = false;
            started = true;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            started = true;
            continue;
        }
        if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else if (character == '\0') {
                goto invalid;
            } else {
                token[token_length++] = character;
            }
            started = true;
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            started = true;
            continue;
        }
        if (character == '\0' || isspace((unsigned char)character)) {
            if (started) {
                char *word;
                char **grown;
                if (argc >= CONFIG_MAX_ARGS) {
                    goto invalid;
                }
                token[token_length] = '\0';
                word = strdup(token);
                if (word == NULL) {
                    goto failure;
                }
                grown = realloc(argv, (argc + 1) * sizeof(*argv));
                if (grown == NULL) {
                    free(word);
                    goto failure;
                }
                argv = grown;
                argv[argc++] = word;
                token_length = 0;
                started = false;
            }
            continue;
        }
        token[token_length++] = character;
        started = true;
    }
    free(token);
    if (argc == 0) {
        free_argv(argv, argc);
        return -1;
    }
    *argv_out = argv;
    *argc_out = argc;
    return 0;

invalid:
    errno = EINVAL;
failure:
    free(token);
    free_argv(argv, argc);
    return -1;
}

static int set_lint_rule(AppConfig *config, const char *extension, const char *command)
{
    char **argv = NULL;
    size_t argc = 0;
    char *normalized;
    size_t extension_length = strlen(extension);
    size_t start = extension[0] == '.' ? 0 : 1;
    size_t normalized_length = extension_length + (start == 1 ? 1 : 0);
    size_t slot = config->rule_count;

    if (extension_length == 0 || parse_argv(command, &argv, &argc) != 0) {
        return -1;
    }
    normalized = malloc(normalized_length + 1);
    if (normalized == NULL) {
        free_argv(argv, argc);
        return -1;
    }
    if (start == 1) {
        normalized[0] = '.';
    }
    memcpy(normalized + start, extension, extension_length + 1);
    for (size_t i = 0; i < config->rule_count; i++) {
        if (strcmp(config->rules[i].extension, normalized) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == config->rule_count) {
        LintRule *rules = realloc(config->rules,
                                  (config->rule_count + 1) * sizeof(*rules));
        if (rules == NULL) {
            free(normalized);
            free_argv(argv, argc);
            return -1;
        }
        config->rules = rules;
        config->rule_count++;
    } else {
        free(config->rules[slot].extension);
        free_argv(config->rules[slot].argv, config->rules[slot].argc);
    }
    config->rules[slot].extension = normalized;
    config->rules[slot].argv = argv;
    config->rules[slot].argc = argc;
    return 0;
}

static bool parse_bool(const char *value, bool *result)
{
    if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "1") == 0) {
        *result = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "0") == 0) {
        *result = false;
        return true;
    }
    return false;
}

static bool parse_size(const char *value, size_t minimum, size_t maximum, size_t *result)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *result = (size_t)parsed;
    return true;
}

int config_load(AppConfig *config, const char *path)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (path == NULL) {
        return 0;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    while ((length = getline(&line, &capacity, file)) >= 0) {
        char *entry = trim(line);
        char *equals;
        char *key;
        char *value;
        bool valid = false;
        (void)length;

        if (*entry == '\0' || *entry == '#') {
            continue;
        }
        equals = strchr(entry, '=');
        if (equals == NULL) {
            config->warning_count++;
            continue;
        }
        *equals = '\0';
        key = trim(entry);
        value = trim(equals + 1);
        if (strcmp(key, "theme") == 0) {
            if (strcmp(value, "dark") == 0) {
                config->theme = THEME_DARK;
                valid = true;
            } else if (strcmp(value, "light") == 0) {
                config->theme = THEME_LIGHT;
                valid = true;
            }
        } else if (strcmp(key, "autoindent") == 0) {
            valid = parse_bool(value, &config->autoindent);
        } else if (strcmp(key, "sidebar_visible") == 0) {
            valid = parse_bool(value, &config->sidebar_visible);
        } else if (strcmp(key, "tab_width") == 0) {
            valid = parse_size(value, 1, 16, &config->tab_width);
        } else if (strcmp(key, "sidebar_width") == 0) {
            valid = parse_size(value, 20, 60, &config->sidebar_width);
        } else if (strncmp(key, "lint.", 5) == 0) {
            valid = set_lint_rule(config, key + 5, value) == 0;
        }
        if (!valid) {
            config->warning_count++;
        }
    }
    free(line);
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

void config_destroy(AppConfig *config)
{
    for (size_t i = 0; i < config->rule_count; i++) {
        free(config->rules[i].extension);
        free_argv(config->rules[i].argv, config->rules[i].argc);
    }
    free(config->rules);
    memset(config, 0, sizeof(*config));
}
