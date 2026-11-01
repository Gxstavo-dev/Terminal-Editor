# terminal-editor

**A low-memory, mouse-driven terminal editor inspired by Nano.**

## Build and launch

Requirements: a C compiler, GNU Make, `pkg-config`, and ncursesw development headers.

```sh
make
./terminal-editor [file-or-directory]
```

Passing a file opens it, or starts a new buffer for a new path. Passing a directory opens it in the sidebar. With no argument, the editor browses the current directory.

The editor is a terminal user interface (TUI): run it from an existing terminal. Launching it from a file manager may depend on desktop integration.

## Download and run the AppImage

The project publishes a Linux x86_64 AppImage. Download `terminal-editor-x86_64.AppImage` from the repository's Releases page, then run it from a terminal:

```sh
chmod +x terminal-editor-x86_64.AppImage
./terminal-editor-x86_64.AppImage
./terminal-editor-x86_64.AppImage path/to/file
```

The AppImage is a Linux x86_64 artifact. It is not a macOS or Windows package, and compatibility depends on the build host's architecture and glibc baseline.

### Build the AppImage locally

The packaging script downloads pinned `linuxdeploy` and `appimagetool` releases into `packaging/appimage/.cache/` when they are not already installed. You can override them with the `LINUXDEPLOY` and `APPIMAGETOOL` environment variables.

```sh
./packaging/appimage/build.sh
```

The script builds the editor with `make`, stages the binary, ncursesw, desktop metadata, icon, and license notices into an AppDir, then produces:

```text
dist/terminal-editor-x86_64.AppImage
```

Launch the result from a terminal with or without a file argument. Generated AppDir files, downloaded packaging tools, and `.AppImage` outputs are ignored by Git.

### Upload the AppImage to GitHub Releases

1. Open the repository's **Releases** page on GitHub.
2. Create a new release and choose a tag.
3. Attach `dist/terminal-editor-x86_64.AppImage` as a release asset.
4. Publish the release.

Do not commit the AppImage into the Git repository.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+C` | Copy the current selection to the system clipboard |
| `Ctrl+V` | Paste the system clipboard at the cursor, replacing a selection |
| `Ctrl+Z` | Undo the last edit |
| `Ctrl+Y` | Redo the last undone edit |
| `Ctrl+S` / `Ctrl+O` | Save the current buffer |
| `Ctrl+W` | Save changes and close the active buffer |
| `Ctrl+R` | Reload the file from disk after confirmation |
| `Ctrl+X` | Exit the editor |
| `F2` | Show or hide the sidebar |
| `Tab` | Insert spaces according to `tab_width` |
| `Enter` | Insert a new line, preserving indentation when autoindent is on |

Typing inserts text at the cursor. Backspace deletes the selection or the character before the cursor; Delete removes the selection or the character after the cursor. Mouse drags select text, including multiline and reverse drags. Each buffer keeps up to 100 edit states; starting a new edit clears its redo history.

## System clipboard

`Ctrl+C` copies a nonempty selection as UTF-8, including selected newlines. With no selection it shows `Select text to copy` and leaves the clipboard unchanged. `Ctrl+V` pastes UTF-8 text at the cursor, replaces an active selection, inserts literal newlines without autoindent, and records one undo step for a successful paste. Invalid UTF-8 or an allocation failure leaves the buffer, selection, and history unchanged.

Clipboard helpers are optional runtime dependencies. The editor detects them at runtime and runs them directly without a shell:

- Wayland: `wl-copy` and `wl-paste` from `wl-clipboard`.
- X11: `xclip` or `xsel`.

Installation examples:

```sh
# Arch Linux
sudo pacman -S wl-clipboard        # Wayland
sudo pacman -S xclip               # X11 (or xsel)

# Debian / Ubuntu
sudo apt install wl-clipboard      # Wayland
sudo apt install xclip             # X11 (or xsel)

# Fedora
sudo dnf install wl-clipboard      # Wayland
sudo dnf install xclip             # X11 (or xsel)
```

If no supported helper is installed or the operation fails, the editor shows a concise hint and does not change the buffer.

## Interface and navigation

- The left sidebar lists files and directories in the current directory, one level at a time. Click a directory to enter it; click `..` to go to its parent.
- The sidebar refreshes when files are created or removed externally. Its `×` button or `F2` hides it; `+` or `F2` shows it again.
- The footer below the editor shows the current Git branch in white, or `sin repo Git` outside a repository. A single blank row separates the footer from the editor; temporary messages share the footer.
- The terminal supplies the background color. The default dark theme uses white line numbers and turquoise accents. There are no top or bottom title bars or scrollbars.
- The mouse pointer changes to a hand over clickable sidebar entries in Kitty. Other terminal emulators may not support this feature.
- Click a file or directory in the sidebar to open it. Clicking an already open file switches to its buffer.
- Click `+` beside the sidebar heading to create a file or directory in the current directory. New files open immediately; existing paths are never overwritten.
- Right-click a file to delete it permanently after confirmation. Directories and files with unsaved local edits cannot be deleted this way.
- Use the mouse wheel over a pane to scroll that pane.

## Editing and saving

- Click in the editor to place the cursor. Drag to select text; Backspace deletes the selection or the character before the cursor.
- The text cursor uses a thin vertical bar in terminals that support DECSCUSR; its default terminal style is restored when the editor exits.
- Type to edit. `Tab` inserts spaces by default; Enter creates a new line and preserves indentation when autoindent is enabled.
- `Ctrl+Z` undoes the last edit and `Ctrl+Y` redoes it.
- `Ctrl+S` or `Ctrl+O` saves the current buffer. If it has no path, the editor asks where to save it.
- `Ctrl+W` saves changes automatically and closes the active buffer. It asks for a path if the buffer has none; if saving fails or is cancelled, the buffer stays open. If the disk version changed externally, `Ctrl+W` saves the local version over it.
- `Ctrl+X` exits and asks before discarding unsaved changes.

## External file changes

The editor checks open files and the current directory periodically while idle:

- A clean open buffer reloads automatically when the file changes on disk.
- A buffer with local edits is preserved and shows a persistent footer notice. `Ctrl+R` reloads the disk version after confirmation; `Ctrl+S` asks before overwriting the external version.
- New or removed files appear in the sidebar automatically. Creating a file with `touch new-file.md` in the current directory is supported.

## Configuration

Configuration lives at `~/.config/terminal-editor/config`. If `XDG_CONFIG_HOME` is set, it lives at `$XDG_CONFIG_HOME/terminal-editor/config`. Missing configuration is fine; defaults are used.

```ini
theme=dark
tab_width=4
autoindent=true
sidebar_visible=true
sidebar_width=28
lint.c=clang -Wall -fsyntax-only
```

Supported settings are `theme=dark|light`, `tab_width=1..16`, `autoindent=true|false`, `sidebar_visible=true|false`, `sidebar_width=20..60`, and `lint.<extension>=program arguments...`. Arguments may be quoted. The editor appends the file path as the final argument and runs the command directly without a shell.

## Linters and diagnostics

Configured linters run after a successful save and when an active buffer is reloaded from disk. Each run has a five-second timeout and a 1 MiB output limit. The command should print one diagnostic per line:

```text
path:line:column: severity: message
```

Line and column numbers start at 1; severity is `error`, `warning`, or `info`. Diagnostics appear next to their source lines with a severity marker in the gutter. Click a diagnostic or its marker to jump to that location. Plugin support is planned for v0.2.

## Fonts and icons

The terminal emulator controls the font; configure it in the terminal's preferences. In Kitty, the sidebar icons use the Nerd Font Symbols fallback. This Kitty configuration maps the folder and file symbols:

```conf
font_family Noto Sans Mono
symbol_map U+F07B,U+F15B Symbols Nerd Font Mono
```

The pointer-shape feature uses Kitty's terminal protocol; the text editor remains usable in terminals that do not support it.

## Memory

The target is approximately 6.9 MiB RSS for an idle editor with an empty buffer. An earlier user-reported measurement was **5564 KiB (about 5.43 MiB)**; the Nano measurement was **7076 KiB (about 6.91 MiB)** on the same host. These were taken before the latest UI features, so current usage may differ. Memory used by a linter is separate and may temporarily increase total usage.

To inspect RSS while the editor is open, run this in another terminal:

```sh
ps -C terminal-editor -o pid,rss,cmd
```

`RSS` is reported in KiB; divide by 1024 for MiB.

## Current limits

V1 does not include a plugin API, LSP, autocompletion, advanced language syntax highlighting, or search-and-replace. The sidebar lists one directory level and the editor loads buffers when opened. Clipboard support depends on `wl-clipboard`, `xclip`, or `xsel` and on the graphical session's clipboard service.

## License

Terminal-Editor is free software licensed under the **GNU General Public License, version 3 or later** (`GPL-3.0-or-later`). Copyright (C) 2026 Gxstavo-dev.

See [LICENSE](LICENSE) for the complete license text. The GPL permits redistribution and modification under its terms. Third-party libraries packaged in the AppImage remain under their own licenses; available notices are included under `usr/share/doc/terminal-editor/licenses/` inside the AppImage.

## Project documentation

- [V1 feature list](docs/project/caracteristicasV1.md)
- [Debug history and fixes](docs/project/debugV1.md)
- [Roadmap and project context](docs/project/versiones.md)
- [UI design source](docs/design/Untitled.figcraft)
- [Design and implementation notes](docs/superpowers/)
