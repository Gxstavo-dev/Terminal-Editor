/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Gxstavo-dev */

#define _POSIX_C_SOURCE 200809L

#include "clipboard.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char *wl_copy_argv[] = {"wl-copy", NULL};
static char *wl_paste_argv[] = {"wl-paste", "--no-newline", NULL};
static char *xclip_copy_argv[] = {"xclip", "-selection", "clipboard", "-i", NULL};
static char *xclip_paste_argv[] = {"xclip", "-selection", "clipboard", "-o", NULL};
static char *xsel_copy_argv[] = {"xsel", "--clipboard", "--input", NULL};
static char *xsel_paste_argv[] = {"xsel", "--clipboard", "--output", NULL};

static char **wayland_first_copy[] = {
    wl_copy_argv,
    xclip_copy_argv,
    xsel_copy_argv
};

static char **wayland_first_paste[] = {
    wl_paste_argv,
    xclip_paste_argv,
    xsel_paste_argv
};

static char **x11_first_copy[] = {
    xclip_copy_argv,
    xsel_copy_argv,
    wl_copy_argv
};

static char **x11_first_paste[] = {
    xclip_paste_argv,
    xsel_paste_argv,
    wl_paste_argv
};

static char ***choose_commands(bool paste, size_t *count)
{
    const char *wayland = getenv("WAYLAND_DISPLAY");
    bool prefer_wayland = wayland != NULL && *wayland != '\0';

    *count = 3;
    if (prefer_wayland) {
        return paste ? wayland_first_paste : wayland_first_copy;
    }
    return paste ? x11_first_paste : x11_first_copy;
}

static void silence_child_streams(bool redirect_stdout)
{
    int devnull = open("/dev/null", O_RDWR);

    if (devnull < 0) {
        return;
    }
    if (redirect_stdout) {
        (void)dup2(devnull, STDOUT_FILENO);
    }
    (void)dup2(devnull, STDERR_FILENO);
    if (devnull != STDOUT_FILENO && devnull != STDERR_FILENO) {
        close(devnull);
    }
}

static int wait_for_child(pid_t pid)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int send_to_helper(char *const argv[], const char *text, size_t length)
{
    int pipefd[2];
    pid_t pid;
    struct sigaction ignore;
    struct sigaction previous;
    bool restore_sigpipe = false;
    size_t offset = 0;
    int result = -1;

    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[0]);
        silence_child_streams(true);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[0]);

    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, &ignore, &previous) == 0) {
        restore_sigpipe = true;
    }
    while (offset < length) {
        ssize_t written = write(pipefd[1], text + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        offset += (size_t)written;
    }
    close(pipefd[1]);
    if (restore_sigpipe) {
        (void)sigaction(SIGPIPE, &previous, NULL);
    }
    if (wait_for_child(pid) == 0 && offset == length) {
        result = 0;
    }
    return result;
}

static int receive_from_helper(char *const argv[], char **text, size_t *length)
{
    int pipefd[2];
    pid_t pid;
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;
    bool read_ok = true;
    int result = -1;

    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[1]);
        silence_child_streams(false);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);

    for (;;) {
        if (used == capacity) {
            size_t next = capacity == 0 ? 4096 : capacity * 2;
            char *grown;

            if (next < capacity) {
                read_ok = false;
                break;
            }
            grown = realloc(buffer, next);
            if (grown == NULL) {
                read_ok = false;
                break;
            }
            buffer = grown;
            capacity = next;
        }
        ssize_t amount = read(pipefd[0], buffer + used, capacity - used);
        if (amount < 0) {
            if (errno == EINTR) {
                continue;
            }
            read_ok = false;
            break;
        }
        if (amount == 0) {
            break;
        }
        used += (size_t)amount;
    }
    close(pipefd[0]);
    if (wait_for_child(pid) != 0) {
        read_ok = false;
    }
    if (read_ok) {
        char *complete = realloc(buffer, used + 1);
        if (complete != NULL) {
            buffer = complete;
            buffer[used] = '\0';
            *text = buffer;
            *length = used;
            result = 0;
            buffer = NULL;
        }
    }
    free(buffer);
    return result;
}

int clipboard_copy(const char *text, size_t length)
{
    size_t count;
    char ***commands;

    if (text == NULL && length > 0) {
        errno = EINVAL;
        return -1;
    }
    commands = choose_commands(false, &count);
    for (size_t i = 0; i < count; i++) {
        if (send_to_helper(commands[i], text != NULL ? text : "", length) == 0) {
            return 0;
        }
    }
    return -1;
}

int clipboard_paste(char **text, size_t *length)
{
    size_t count;
    char ***commands;

    if (text == NULL || length == NULL) {
        errno = EINVAL;
        return -1;
    }
    *text = NULL;
    *length = 0;
    commands = choose_commands(true, &count);
    for (size_t i = 0; i < count; i++) {
        if (receive_from_helper(commands[i], text, length) == 0) {
            return 0;
        }
    }
    return -1;
}
