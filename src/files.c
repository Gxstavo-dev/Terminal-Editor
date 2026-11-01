#include "files.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int entry_compare(const void *left, const void *right)
{
    const FileEntry *a = left;
    const FileEntry *b = right;
    if (strcmp(a->name, "..") == 0) {
        return strcmp(b->name, "..") == 0 ? 0 : -1;
    }
    if (strcmp(b->name, "..") == 0) {
        return 1;
    }
    if (a->is_directory != b->is_directory) {
        return a->is_directory ? -1 : 1;
    }
    return strcasecmp(a->name, b->name);
}

static char *join_path(const char *directory, const char *name)
{
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s/%s", directory, name);
    }
    return path;
}

static int append_entry(FileEntry **entries, size_t *count, const char *directory,
                        const char *name, bool is_directory)
{
    FileEntry entry = {0};
    FileEntry *grown;

    entry.name = strdup(name);
    entry.path = join_path(directory, name);
    entry.is_directory = is_directory;
    if (entry.name == NULL || entry.path == NULL) {
        free(entry.name);
        free(entry.path);
        return -1;
    }
    grown = realloc(*entries, (*count + 1) * sizeof(**entries));
    if (grown == NULL) {
        free(entry.name);
        free(entry.path);
        return -1;
    }
    *entries = grown;
    (*entries)[(*count)++] = entry;
    return 0;
}

int files_list(const char *directory, FileEntry **entries, size_t *count)
{
    DIR *dir;
    struct dirent *item;
    FileEntry *result = NULL;
    size_t result_count = 0;

    *entries = NULL;
    *count = 0;
    dir = opendir(directory);
    if (dir == NULL) {
        return -1;
    }
    if (append_entry(&result, &result_count, directory, "..", true) != 0) {
        closedir(dir);
        return -1;
    }
    errno = 0;
    while ((item = readdir(dir)) != NULL) {
        struct stat status;
        char *path;
        bool is_directory;

        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        path = join_path(directory, item->d_name);
        if (path == NULL) {
            files_free(result, result_count);
            closedir(dir);
            return -1;
        }
        is_directory = stat(path, &status) == 0 && S_ISDIR(status.st_mode);
        free(path);
        if (append_entry(&result, &result_count, directory, item->d_name,
                         is_directory) != 0) {
            files_free(result, result_count);
            closedir(dir);
            return -1;
        }
        errno = 0;
    }
    if (errno != 0) {
        files_free(result, result_count);
        closedir(dir);
        return -1;
    }
    closedir(dir);
    qsort(result, result_count, sizeof(*result), entry_compare);
    *entries = result;
    *count = result_count;
    return 0;
}

void files_free(FileEntry *entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
        free(entries[i].path);
    }
    free(entries);
}
