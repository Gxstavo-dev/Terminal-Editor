# Terminal Editor Prototype Implementation Plan

> **For agentic workers:** Use the `executing-plans` skill to implement this plan task by task.

**Goal:** Build v0.1 of a low-memory, mouse-driven terminal editor with a file sidebar, user configuration, external lint commands, and inline diagnostics.

**Architecture:** C11 with ncursesw. A line-based buffer and app state feed a single ncurses event loop; focused modules handle file operations, sidebar listing, user configuration, and external lint diagnostics. Linter commands run as child processes and communicate through a documented line format.

**Tech Stack:** C11, ncursesw, POSIX file/process APIs, GNU Make.

**Spec:** `docs/superpowers/specs/2026-10-31-terminal-editor-design.md`

## Global Constraints

- Use C with ncursesw.
- The editor starts with or without a path and edits, creates, and saves files.
- The sidebar lists the current directory and open buffers; `F2` toggles it.
- Mouse clicks place the cursor and navigate the sidebar; drag selects; the wheel scrolls the pane under the pointer.
- Keep persistent title, footer, and scrollbar hidden; show white line numbers.
- Read configuration from `~/.config/terminal-editor/config`.
- Run configured external linters on save; accept `path:line:column: severity: message`, with severity `error`, `warning`, or `info`.
- Target about 6.9 MiB RSS for the idle editor with an empty buffer, measured on the same host as Nano; report lint-child usage separately.
- Do not implement the plugin API in v0.1.

## File Structure

- `Makefile`: compile and link the editor with ncursesw; provide `all` and `clean` targets.
- `src/types.h`: buffer, file-entry, app-config, linter-rule, and diagnostic data types shared across modules.
- `src/buffer.h`, `src/buffer.c`: text editing, cursor state, file load/save, dirty state.
- `src/config.h`, `src/config.c`: defaults and parser for the user config.
- `src/lint.h`, `src/lint.c`: child process launch and diagnostic parsing.
- `src/ui.h`, `src/ui.c`: ncurses layout, rendering, mouse input, sidebar, and event loop.
- `src/main.c`: locale setup, command-line arguments, app lifecycle, cleanup.
- `README.md`: build/run/config/linter instructions, including terminal-font guidance.
- `versiones.md`: durable roadmap and decision log, updated as v0.1 ships.

## Review Focus

1. **Small terminal or resize:** sidebar must collapse or hide without out-of-bounds drawing; the editor remains usable after `KEY_RESIZE`.
2. **UTF-8 and mouse coordinates:** cursor placement and drag selection must map terminal cells to text positions; wide glyphs must not shift the cursor incorrectly.
3. **Paths with spaces and unsaved buffers:** opening, saving, switching buffers, and exit confirmation must preserve the intended file and changes.
4. **Lint child failure or malformed output:** exit status, missing executable, long lines, unknown severity, and invalid line/column must not crash the editor or corrupt the buffer.
5. **Large file / high diagnostic count:** memory growth must be attributable to buffers or diagnostics, and rendering must stay bounded to visible rows.

---

### Task 1: Create the buildable C/ncursesw skeleton

**Files:**
- Create: `Makefile`
- Create: `src/main.c`
- Create: `src/types.h`
- Create: `src/ui.h`
- Create: `src/ui.c`

**Interfaces:**
- `int ui_run(void);` initializes ncursesw, draws a temporary editor canvas, waits for `Ctrl+X`, restores terminal state, and returns an exit code.
- `int main(int argc, char **argv);` initializes the locale and calls `ui_run` after validating the number of paths.

- [ ] Add `all` and `clean` Makefile targets; compile with `-std=c11 -Wall -Wextra -Wpedantic`, obtain ncursesw flags from `pkg-config`, and link the ncursesw library.
- [ ] Add shared types: `Line { char *data; size_t byte_length; }`; `Diagnostic { char *path; size_t line; size_t column; Severity severity; char *message; }`; `LintRule { char *extension; char **argv; size_t argc; }`; `AppConfig { size_t tab_width; size_t sidebar_width; bool autoindent; bool sidebar_visible; Theme theme; LintRule *rules; size_t rule_count; }`; `FileEntry { char *name; char *path; bool is_directory; }`; `Buffer { char *path; Line *lines; size_t line_count; size_t cursor_line; size_t cursor_column; size_t top_line; bool dirty; Diagnostic *diagnostics; size_t diagnostic_count; }`; and `App { Buffer *buffers; size_t buffer_count; size_t active_buffer; AppConfig config; char *current_directory; }`. `cursor_column` and diagnostic columns are zero-based Unicode code-point offsets; line bytes remain UTF-8.
- [ ] Implement `ui_run` as a minimal `initscr` / draw / `get_wch` / `endwin` lifecycle. Enable mouse reporting and make `Ctrl+X` exit cleanly.
- [ ] Build with `make`; confirm the binary links to ncursesw and that exiting restores the terminal.

### Task 2: Implement buffers and file operations

**Files:**
- Create: `src/buffer.h`
- Create: `src/buffer.c`
- Modify: `src/types.h`
- Modify: `src/main.c`

**Interfaces:**
- `int buffer_load(Buffer *buffer, const char *path);` loads a file or creates one empty line when the path does not exist.
- `int buffer_save(Buffer *buffer, const char *path);` writes all lines, updates `buffer->path`, and clears `dirty` only after a successful write.
- `void buffer_destroy(Buffer *buffer);` frees the path and every line.
- `int buffer_insert(Buffer *buffer, wchar_t character);`, `int buffer_backspace(Buffer *buffer);`, `int buffer_delete(Buffer *buffer);`, and `int buffer_newline(Buffer *buffer, bool autoindent, size_t tab_width);` mutate the buffer and mark it dirty.

- [ ] Implement file loading with `getline`, split into UTF-8 line records, preserve an empty final line correctly, and report read errors without leaving a partially initialized buffer.
- [ ] Implement cursor insertion, deletion, backspace, newline, and four-space tab insertion with autoindent.
- [ ] Implement safe save through a sibling temporary file followed by `rename`; keep the dirty flag set if writing or renaming fails.
- [ ] Wire command-line paths into initial buffers; treat a nonexistent path as a new unsaved-on-disk buffer.
- [ ] Build with `make` and inspect that buffer functions compile with warnings enabled.

### Task 3: Add the sidebar, rendering, and mouse interaction

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.c`
- Create: `src/files.h`
- Create: `src/files.c`
- Modify: `src/main.c`

**Interfaces:**
- `int files_list(const char *directory, FileEntry **entries, size_t *count);` returns one directory level, excluding `.` and `..`.
- `void files_free(FileEntry *entries, size_t count);` frees the listing.
- `int ui_run(App *app);` owns the ncurses event loop and calls buffer, file, and config operations.

- [ ] Render a left sidebar with directory entries and open buffers, and a right editor pane with white line numbers and the dark/turquoise palette.
- [ ] Omit persistent title, footer, and scrollbar rows. Reserve only a temporary message row when an operation reports success or failure.
- [ ] Handle `KEY_MOUSE`: click text to place cursor, drag to select, click sidebar entries to open files/change buffers/enter directories, click diagnostics to jump, and wheel to scroll the pane under the pointer.
- [ ] Handle `F2` sidebar visibility, `Ctrl+O` save, `Ctrl+X` exit, and unsaved-change confirmation. Clicking a pane gives it focus; keyboard remains available for text and shortcuts.
- [ ] Recompute pane geometry after `KEY_RESIZE`; hide the sidebar when terminal width is too small for both panes.
- [ ] Build with `make`; inspect mouse and resize branches for bounds checks before proceeding.

### Task 4: Add user configuration

**Files:**
- Create: `src/config.h`
- Create: `src/config.c`
- Modify: `src/types.h`
- Modify: `src/main.c`
- Create: `README.md`

**Interfaces:**
- `void config_defaults(AppConfig *config);` initializes theme, tab width 4, autoindent, sidebar width, and an empty lint-rule list.
- `int config_load(AppConfig *config, const char *path);` applies valid settings from `~/.config/terminal-editor/config`; missing config is not an error.
- `void config_destroy(AppConfig *config);` frees all strings and rules.

- [ ] Parse documented `key=value` settings for theme, tab width, autoindent, and sidebar width; ignore blank/comment lines and reject malformed values with a transient warning.
- [ ] Parse `lint.<extension>=<program> <arg>...` into an argv array without invoking a shell; support whitespace-separated tokens with double quotes and backslash escapes; append the current file path as the final argument.
- [ ] Load settings at startup and apply them before the first render.
- [ ] Document config syntax and note that font selection belongs to the terminal emulator, with a general terminal-settings guide.
- [ ] Build with `make`; inspect malformed and missing config paths for safe fallback to defaults.

### Task 5: Run linters and display inline diagnostics

**Files:**
- Create: `src/lint.h`
- Create: `src/lint.c`
- Modify: `src/types.h`
- Modify: `src/ui.c`
- Modify: `src/config.c`
- Modify: `README.md`

**Interfaces:**
- `int lint_run(const LintRule *rule, const char *path, Diagnostic **items, size_t *count);` launches the configured argv with `fork`/`execvp`, captures output, and returns parsed diagnostics.
- `void diagnostics_free(Diagnostic *items, size_t count);` frees each message and the array.
- `bool diagnostic_parse_line(const char *text, Diagnostic *item);` accepts `path:line:column: severity: message`, with one-based line/column and severity `error`, `warning`, or `info`.

- [ ] Launch the linter without a shell, capture both stdout and stderr, wait for its exit, and preserve the editor buffer regardless of child failure.
- [ ] Parse valid diagnostics from the right so paths and messages may contain spaces or colons; ignore malformed lines, clamp out-of-range locations when navigating, and show a transient warning if the program cannot start.
- [ ] Run the matching rule after a successful save and replace that buffer's prior diagnostics with the new results.
- [ ] Draw severity markers in the line-number margin and short diagnostic text beside visible source lines; clip text to available width and keep the underlying source unchanged.
- [ ] Map diagnostic clicks to the stored source location and scroll the editor to that line.
- [ ] Build with `make`; inspect process cleanup paths for pipe closure and child reaping.

### Task 6: Document v0.1 and record the memory baseline

**Files:**
- Modify: `README.md`
- Modify: `versiones.md`
- Modify: `docs/superpowers/specs/2026-10-31-terminal-editor-design.md`

- [ ] Document build prerequisites, startup with a file or current directory, mouse actions, keyboard shortcuts, config, diagnostic format, and limitations.
- [ ] Update `versiones.md` to mark v0.1 implemented only after the user-facing flows are complete.
- [ ] Measure idle RSS with an empty buffer and compare against Nano on the same host; record editor RSS and linter-child RSS separately.
- [ ] Leave plugin API work in v0.2 and record any remaining Unicode, terminal, or linter limitations in the roadmap.
