# terminal-editor

Low-memory terminal editor prototype inspired by Nano.

## Build

Requirements: a C compiler, GNU Make, `pkg-config`, and ncursesw development headers.

```sh
make
./terminal-editor [file-or-directory]
```

With a file path, the editor opens that file (or starts a buffer for a new path). With a directory path, it opens that directory in the sidebar. With no argument, it browses the current directory.

The terminal emulator must report mouse events for mouse navigation and text selection. The sidebar lists one directory level and open buffers; click `..` to move to the parent directory.

## Editing

- Click in the editor to place the cursor; drag to select text.
- Click a file or directory in the sidebar to open it. Click a buffer to switch to it.
- Use the mouse wheel over a pane to scroll that pane.
- `F2` hides or shows the sidebar.
- Type to edit; `Tab` inserts four spaces by default.
- `Ctrl+O` saves; `Ctrl+X` exits and asks before discarding unsaved changes.
- A save prompt accepts a path when the buffer has no filename.

The editor has no persistent title bar, footer, or scrollbar. Messages appear briefly on the last terminal row when needed. The default dark theme uses white line numbers and turquoise accents.

## Configuration

Configuration lives at `~/.config/terminal-editor/config`. If `XDG_CONFIG_HOME` is set, the file is `$XDG_CONFIG_HOME/terminal-editor/config`. Missing configuration is fine; defaults are used.

```ini
theme=dark
tab_width=4
autoindent=true
sidebar_visible=true
sidebar_width=28
lint.c=clang -Wall -fsyntax-only
```

Available settings are `theme=dark|light`, `tab_width=1..16`, `autoindent=true|false`, `sidebar_visible=true|false`, `sidebar_width=20..60`, and `lint.<extension>=program arguments...`. Arguments may be quoted; the file path is appended as the final argument. Commands run directly without a shell.

## Linters and diagnostics

Configured linters run after a successful save, with a five-second timeout and a 1 MiB output cap. Their output should use one diagnostic per line:

```text
path:line:column: severity: message
```

Line and column numbers start at 1; severity is `error`, `warning`, or `info`. Diagnostics are shown next to their source line. Click a diagnostic label or its gutter marker to jump to the reported location. Plugin support is planned for v0.2.

## Fonts

The terminal emulator controls the font. Change the font in the terminal's preferences; this editor cannot set it portably. Font control sequences differ between terminal emulators.

## Memory target

The target is approximately 6.9 MiB RSS for the idle editor with an empty buffer, compared with Nano on the same host. The initial measurement in the development environment was 5,044 kB RSS. A linter is a separate child process and may temporarily use additional memory.
