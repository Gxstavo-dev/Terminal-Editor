# GPL, Clipboard, and AppImage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** License Terminal-Editor under GPL-3.0-or-later, add Linux system clipboard shortcuts, and produce a downloadable x86_64 AppImage with clear usage instructions.

**Architecture:** Keep the line-based buffer as the owner of selection extraction and text insertion. Add a small clipboard module that invokes optional Wayland/X11 helper programs directly, without a shell. Add an AppImage packaging recipe that stages the existing binary, runtime libraries, terminal launcher, desktop metadata, and notices into an AppDir before generating a type 2 AppImage.

**Tech Stack:** C11, ncursesw, POSIX process and pipe APIs, GNU Make, Wayland/X11 clipboard helper CLIs, linuxdeploy, appimagetool.

**Spec:** `docs/superpowers/specs/2026-11-01-terminal-editor-licensing-clipboard-appimage-design.md`

## Global Constraints

- The project license is GNU GPL-3.0-or-later; attribution is `Gxstavo-dev`, year 2026.
- `Ctrl+C` copies selected text; `Ctrl+V` pastes at the cursor and replaces an active selection; `Ctrl+X` remains exit.
- Copy without a selection changes no clipboard data and reports a short status message.
- Clipboard helper programs are optional runtime dependencies and are launched without a shell.
- Clipboard text must be UTF-8; helper failure or invalid input must not partially alter the buffer.
- The AppImage target is Linux x86_64 and requires a terminal to run this TUI reliably.
- Build and packaging tools are not runtime dependencies; generated AppDir and AppImage files are ignored by Git.
- Do not publish a GitHub Release; push the project and AppImage build recipe, then give the user the manual asset-upload steps.

## File Structure

- `LICENSE`: complete GPL-3.0-or-later license text.
- `src/buffer.h`, `src/buffer.c`: expose selected UTF-8 text and transactional text insertion/replacement for paste.
- `src/clipboard.h`, `src/clipboard.c`: select available Wayland/X11 helper commands, transfer bytes with pipes, and report failures.
- `src/ui.c`: map `Ctrl+C`, `Ctrl+V`, and existing `Ctrl+X` to clipboard and exit behavior.
- `src/main.c`, `src/ui.h`, `src/types.h`, `src/config.c`, `src/config.h`, `src/files.c`, `src/files.h`, `src/lint.c`, `src/lint.h`: add SPDX identifiers for project-owned C source/header files; no behavior changes in these modules.
- `Makefile`: compile and link the clipboard module.
- `packaging/appimage/build.sh`: build and stage the AppDir, collect dynamic libraries and license notices, and create `dist/terminal-editor-x86_64.AppImage`.
- `packaging/appimage/AppRun`, `packaging/appimage/terminal-editor.desktop`, `packaging/appimage/terminal-editor.svg`: AppImage entry point, TUI desktop metadata, and icon.
- `.gitignore`: exclude generated AppDir, downloaded packaging tools/cache, and `dist/*.AppImage`.
- `README.md`, `docs/project/caracteristicasV1.md`, `docs/project/versiones.md`: describe GPL status, full usage, clipboard dependencies, package creation, and download/launch instructions.
- `docs/project/debugV1.md`: record any clipboard or packaging issue encountered and its confirmed fix.

## Review Focus

1. **No active selection:** `Ctrl+C` must not exit, replace clipboard data, or change the buffer; it should show the selection-required message.
2. **Reverse and multi-line selection:** copy the same selected UTF-8 bytes whether the drag started at the beginning or end; include exactly the selected line separators.
3. **Clipboard helper missing/failing:** show an actionable message and leave text, selection, and dirty state intact.
4. **Invalid UTF-8 or embedded NUL on paste:** reject the clipboard payload before modifying the buffer.
5. **AppImage started without a TTY, unsupported architecture, or missing AppImage tools:** fail with a clear packaging/runtime message; do not claim the output was built.

---

### Task 1: Add GPL license and source notices

**Files:**
- Create: `LICENSE`
- Modify: all project-owned `src/*.c` and `src/*.h`
- Modify: `README.md`

**Interfaces:**
- Consumes: the approved GPL-3.0-or-later decision and copyright holder `Gxstavo-dev`.
- Produces: a complete GPL license file, SPDX headers, and a README license section.

- [ ] Copy the complete, unmodified GNU GPL version 3 text into `LICENSE`, with the standard final option allowing GPL version 3 or any later version.
- [ ] Add `/* SPDX-License-Identifier: GPL-3.0-or-later */` and `/* Copyright (C) 2026 Gxstavo-dev */` headers to each project-owned C source and header file.
- [ ] Add a README section linking to `LICENSE` and stating that redistribution/modification is allowed under GPL terms.
- [ ] Review that no third-party code is accidentally marked as project-owned and run `git diff --check`.

### Task 2: Add buffer selection export and atomic paste insertion

**Files:**
- Modify: `src/buffer.h`
- Modify: `src/buffer.c`

**Interfaces:**
- Produces: `int buffer_copy_selection(const Buffer *buffer, char **text, size_t *length);` returns `1` for a nonempty selection, `0` when none exists, and `-1` on allocation failure.
- Produces: `int buffer_insert_text(Buffer *buffer, const char *text, size_t length);` validates and inserts/replaces text as one buffer operation, recording exactly one undo state only after all allocations succeed; returns `0` on success and `-1` without changing buffer or undo/redo history on failure.
- Consumes: existing `Buffer.selecting`, cursor coordinates, `buffer_delete_selection()`, `buffer_insert()`, `buffer_newline()`, and undo history.

- [ ] Implement selection normalization for forward and reverse drags, then copy UTF-8 byte spans from the first endpoint through the last, inserting `\n` between selected lines.
- [ ] Implement UTF-8 validation before any buffer mutation; reject embedded NUL bytes and invalid sequences.
- [ ] Build the resulting line array off-buffer; only after all allocations succeed, record the old state once and swap in the new lines so allocation or parsing errors preserve the original lines, cursor, selection, dirty flag, final-newline state, and undo/redo history.
- [ ] Treat pasted newlines literally without autoindent; normalize CRLF separators in clipboard input to the buffer's line model.
- [ ] Preserve the existing selection when copying. On paste, replace the active selection and position the cursor after inserted text.
- [ ] Confirm by inspecting forward/reverse and multi-line selections, Unicode text, empty selection, and injected invalid paste data through the UI helper path.

### Task 3: Implement Linux system clipboard helpers and shortcuts

**Files:**
- Create: `src/clipboard.h`
- Create: `src/clipboard.c`
- Modify: `src/ui.c`
- Modify: `Makefile`
- Modify: `README.md`

**Interfaces:**
- Produces: `int clipboard_copy(const char *text, size_t length);` sends bytes to a supported helper and returns `0` only after successful transfer.
- Produces: `int clipboard_paste(char **text, size_t *length);` allocates the complete UTF-8 payload and returns it only after the helper exits successfully.
- Consumes: `buffer_copy_selection()` and `buffer_insert_text()` from Task 2.

- [ ] Add helper selection using `WAYLAND_DISPLAY` with `wl-copy`/`wl-paste --no-newline`, and X11 with `xclip -selection clipboard -i/-o` or `xsel --clipboard --input/--output`.
- [ ] Use `fork`, pipes, `execvp`, and `waitpid`; never construct a shell command. Close unused pipe ends, drain child output, handle `EINTR` and child exit status, and redirect helper diagnostics away from the ncurses screen.
- [ ] Add `src/clipboard.c` to `SOURCES` in the Makefile.
- [ ] Handle raw input `3` as copy and `22` as paste before the existing exit logic; keep raw input `24` (`Ctrl+X`) as the exit command.
- [ ] On copy without a selection, show `Select text to copy`; on missing helper or failed command, show a concise Wayland/X11 helper hint.
- [ ] Rely on `buffer_insert_text()` to record one undo state for a successful paste; the UI must not add a second undo snapshot.
- [ ] Build with `make` and manually check copy, paste, replace-selection paste, undo-paste, no-selection copy, and helper-not-installed behavior in Kitty or another compatible Linux terminal.

### Task 4: Create the AppImage AppDir recipe and artifact

**Files:**
- Create: `packaging/appimage/build.sh`
- Create: `packaging/appimage/AppRun`
- Create: `packaging/appimage/terminal-editor.desktop`
- Create: `packaging/appimage/terminal-editor.svg`
- Modify: `.gitignore`

**Interfaces:**
- Produces: executable `dist/terminal-editor-x86_64.AppImage`.
- Consumes: `make`, linuxdeploy, its AppImage output plugin/appimagetool, and the current x86_64 Linux host.

- [ ] Write `AppRun` to resolve `APPDIR` and `exec "$APPDIR/usr/bin/terminal-editor" "$@"` without dropping arguments.
- [ ] Add a desktop entry with `Type=Application`, `Name=Terminal-Editor`, `Exec=terminal-editor %F`, `Icon=terminal-editor`, `Terminal=true`, and `Categories=Development;TextEditor;`.
- [ ] Add a compact scalable SVG icon and install it at the desktop-entry icon path in the AppDir.
- [ ] Write a fail-fast packaging script that checks architecture and required tools, invokes `make`, clears only its generated AppDir, stages the binary and desktop assets, invokes linuxdeploy to collect runtime dependencies, copies available third-party license notices, and runs appimagetool to create the output.
- [ ] Ensure every command is quoted, temporary files are scoped to the generated package directory, and missing tools return a message naming the required package/tool.
- [ ] Add generated `AppDir`, downloaded packaging tools/cache, and `dist/*.AppImage` to `.gitignore`.
- [ ] Run the build script on x86_64 Linux, inspect the AppDir dependency list, and confirm the output exists and has its executable bit set.
- [ ] Launch the resulting AppImage from an existing terminal with no arguments and with a sample file path; confirm it reaches the editor and passes the path through.

### Task 5: Document normal use, clipboard setup, and GitHub Release upload

**Files:**
- Modify: `README.md`
- Modify: `docs/project/caracteristicasV1.md`
- Modify: `docs/project/versiones.md`
- Modify: `docs/project/debugV1.md` if the build reveals a fixable issue

**Interfaces:**
- Consumes: final shortcut behavior, supported clipboard helper names, packaging script path, and artifact filename from Tasks 3 and 4.
- Produces: instructions that a new Linux user can follow without developer context.

- [ ] Add a concise keyboard shortcut table for copy, paste, undo, redo, save, close buffer, and exit; explain the mouse actions and sidebar create/delete controls.
- [ ] Add clipboard helper installation examples for common Wayland and X11 distributions and explain that the editor detects installed helpers.
- [ ] Add local AppImage build instructions, including required AppImage tools, exact command, output path, executable permission, and launch command with an optional filename.
- [ ] Explain manual GitHub Release asset upload: open the repository's Releases page, create a release/tag, attach `dist/terminal-editor-x86_64.AppImage`, and publish. Do not commit the binary into Git.
- [ ] State GPL-3.0-or-later accurately and explain that users may redistribute under its terms.
- [ ] Update V1 feature and roadmap docs, remove obsolete shortcuts/limitations, and verify all README links point to existing paths.

### Task 6: Verify, commit, and push the completed work

**Files:**
- Review: all files changed in Tasks 1–5

**Interfaces:**
- Consumes: approved source, docs, and AppImage build output.
- Produces: one implementation commit on the requested branch and a successful push to `origin/main`; leave the ignored AppImage available locally for the user to upload as a Release asset.

- [ ] Run `make` and the AppImage build script; report exact command output for failures rather than claiming success.
- [ ] Run `git diff --check`, review `git status`, and confirm generated AppImage/cache files are ignored.
- [ ] Manually verify clipboard copy/paste, `Ctrl+X`, and AppImage launch with a file argument using the checklist in the spec.
- [ ] Stage source, license, package recipe, and docs; exclude `dist/*.AppImage` and downloaded build tools.
- [ ] Commit with `feat: add clipboard support and AppImage packaging`.
- [ ] Push the authorized branch to `origin`; confirm the pushed commit hash and leave the local `.AppImage` path in the final response with the manual GitHub upload steps.
