/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Gxstavo-dev */

#define _POSIX_C_SOURCE 200809L

#include "lint.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LINT_TIMEOUT_MS 5000
#define LINT_OUTPUT_LIMIT (1024 * 1024)
#define DIAGNOSTIC_LIMIT 4096

static regex_t diagnostic_regex;
static bool regex_initialized;

static int ensure_regex(void)
{
    if (regex_initialized) {
        return 0;
    }
    if (regcomp(&diagnostic_regex,
                "^(.+):([0-9]+):([0-9]+): (error|warning|info): (.*)$",
                REG_EXTENDED) != 0) {
        errno = EINVAL;
        return -1;
    }
    regex_initialized = true;
    return 0;
}

void diagnostics_free(Diagnostic *items, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(items[i].path);
        free(items[i].message);
    }
    free(items);
}

static bool parse_number(const char *text, size_t *number)
{
    char *end;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > SIZE_MAX) {
        return false;
    }
    *number = (size_t)parsed;
    return true;
}

bool diagnostic_parse_line(const char *text, Diagnostic *item)
{
    regmatch_t match[6];
    char *parts[6] = {0};
    size_t lengths[6];
    bool valid = false;

    if (ensure_regex() != 0 || regexec(&diagnostic_regex, text, 6, match, 0) != 0) {
        return false;
    }
    for (size_t i = 1; i < 6; i++) {
        lengths[i] = (size_t)(match[i].rm_eo - match[i].rm_so);
        parts[i] = strndup(text + match[i].rm_so, lengths[i]);
        if (parts[i] == NULL) {
            goto cleanup;
        }
    }
    if (!parse_number(parts[2], &item->line) ||
        !parse_number(parts[3], &item->column)) {
        goto cleanup;
    }
    if (strcmp(parts[4], "error") == 0) {
        item->severity = DIAG_ERROR;
    } else if (strcmp(parts[4], "warning") == 0) {
        item->severity = DIAG_WARNING;
    } else {
        item->severity = DIAG_INFO;
    }
    item->path = parts[1];
    item->message = parts[5];
    parts[1] = NULL;
    parts[5] = NULL;
    valid = true;

cleanup:
    for (size_t i = 1; i < 6; i++) {
        free(parts[i]);
    }
    return valid;
}

static int append_diagnostic(Diagnostic **items, size_t *count, Diagnostic *item)
{
    if (*count >= DIAGNOSTIC_LIMIT) {
        errno = EOVERFLOW;
        return -1;
    }
    Diagnostic *grown = realloc(*items, (*count + 1) * sizeof(**items));
    if (grown == NULL) {
        return -1;
    }
    *items = grown;
    (*items)[(*count)++] = *item;
    memset(item, 0, sizeof(*item));
    return 0;
}

static int remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    long seconds = deadline->tv_sec - now.tv_sec;
    long nanoseconds = deadline->tv_nsec - now.tv_nsec;
    long milliseconds = seconds * 1000 + nanoseconds / 1000000;
    if (milliseconds <= 0) {
        return 0;
    }
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static int run_child(const LintRule *rule, const char *path, char **output,
                     size_t *output_length, int *exit_status)
{
    int pipefd[2];
    pid_t child;
    char **argv;
    size_t used = 0;
    size_t capacity = 4096;
    char *buffer = malloc(capacity);
    struct timespec deadline;
    bool timed_out = false;
    int failure = 0;
    int status = 0;

    if (buffer == NULL) {
        return -1;
    }
    if (pipe(pipefd) != 0) {
        free(buffer);
        return -1;
    }
    argv = calloc(rule->argc + 2, sizeof(*argv));
    if (argv == NULL) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(buffer);
        return -1;
    }
    for (size_t i = 0; i < rule->argc; i++) {
        argv[i] = rule->argv[i];
    }
    argv[rule->argc] = (char *)path;
    child = fork();
    if (child < 0) {
        free(argv);
        close(pipefd[0]);
        close(pipefd[1]);
        free(buffer);
        return -1;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_RDONLY);
        close(pipefd[0]);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(pipefd[1]);
        execvp(argv[0], argv);
        dprintf(STDERR_FILENO, "lint command could not run: %s\n", argv[0]);
        _exit(127);
    }
    free(argv);
    close(pipefd[1]);
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        kill(child, SIGKILL);
        close(pipefd[0]);
        waitpid(child, &status, 0);
        free(buffer);
        return -1;
    }
    deadline.tv_sec += LINT_TIMEOUT_MS / 1000;
    deadline.tv_nsec += (LINT_TIMEOUT_MS % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;) {
        struct pollfd descriptor = {.fd = pipefd[0], .events = POLLIN | POLLHUP};
        int timeout = remaining_ms(&deadline);
        int ready;
        if (timeout <= 0) {
            timed_out = true;
            break;
        }
        ready = poll(&descriptor, 1, timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure = errno;
            break;
        }
        if (ready == 0) {
            timed_out = true;
            break;
        }
        if (descriptor.revents & (POLLIN | POLLHUP)) {
            char chunk[4096];
            ssize_t amount = read(pipefd[0], chunk, sizeof(chunk));
            if (amount == 0) {
                break;
            }
            if (amount < 0) {
                if (errno == EINTR) {
                    continue;
                }
                failure = errno;
                break;
            }
            if ((size_t)amount > LINT_OUTPUT_LIMIT - used) {
                failure = EOVERFLOW;
                break;
            }
            if (used + (size_t)amount + 1 > capacity) {
                size_t next_capacity = capacity * 2;
                while (next_capacity < used + (size_t)amount + 1) {
                    next_capacity *= 2;
                }
                if (next_capacity > LINT_OUTPUT_LIMIT + 1) {
                    next_capacity = LINT_OUTPUT_LIMIT + 1;
                }
                char *grown = realloc(buffer, next_capacity);
                if (grown == NULL) {
                    kill(child, SIGKILL);
                    close(pipefd[0]);
                    waitpid(child, &status, 0);
                    free(buffer);
                    return -1;
                }
                buffer = grown;
                capacity = next_capacity;
            }
            memcpy(buffer + used, chunk, (size_t)amount);
            used += (size_t)amount;
        }
    }
    close(pipefd[0]);
    if (timed_out || failure != 0) {
        kill(child, SIGKILL);
    }
    for (;;) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            break;
        }
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure = errno;
            break;
        }
        int timeout = remaining_ms(&deadline);
        if (timeout <= 0) {
            timed_out = true;
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
    if (timed_out || failure != 0) {
        errno = failure != 0 ? failure : ETIMEDOUT;
        free(buffer);
        return -1;
    }
    if (!WIFEXITED(status)) {
        errno = ECHILD;
        free(buffer);
        return -1;
    }
    *exit_status = WEXITSTATUS(status);
    buffer[used] = '\0';
    *output = buffer;
    *output_length = used;
    return 0;
}

int lint_run(const LintRule *rule, const char *path, Diagnostic **items,
             size_t *count, int *exit_status)
{
    char *output = NULL;
    size_t output_length = 0;
    size_t start = 0;

    *items = NULL;
    *count = 0;
    *exit_status = -1;
    if (rule == NULL || rule->argc == 0 || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (run_child(rule, path, &output, &output_length, exit_status) != 0) {
        return -1;
    }
    while (start < output_length) {
        size_t end = start;
        Diagnostic item = {0};
        while (end < output_length && output[end] != '\n') {
            end++;
        }
        if (end > start && output[end - 1] == '\r') {
            output[end - 1] = '\0';
        } else if (end < output_length) {
            output[end] = '\0';
        }
        if (diagnostic_parse_line(output + start, &item)) {
            if (append_diagnostic(items, count, &item) != 0) {
                free(item.path);
                free(item.message);
                diagnostics_free(*items, *count);
                *items = NULL;
                *count = 0;
                free(output);
                return -1;
            }
        }
        start = end + 1;
    }
    free(output);
    return 0;
}
