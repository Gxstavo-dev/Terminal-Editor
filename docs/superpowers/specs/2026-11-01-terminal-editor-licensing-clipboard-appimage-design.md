# Design: GPL, system clipboard, and AppImage distribution

**Status:** awaiting user review
**Date:** 2026-11-01

## Goal

Prepare the public `Gxstavo-dev/Terminal-Editor` repository for community use and Linux downloads. License the project under GNU GPL version 3 or later, add clipboard copy/paste to the terminal editor, provide a repeatable x86_64 AppImage build, and document how users build, operate, download, and launch the application.

The GPL choice allows redistribution and modification under its terms. It does not prevent others from sharing the AppImage. The public source repository remains viewable and forkable on GitHub.

## License

- Add the complete GNU GPL-3.0-or-later text as `LICENSE`.
- Identify the copyright holder as `Gxstavo-dev`, using 2026 as the initial copyright year.
- Mark project-owned source as GPL-3.0-or-later in the README and provide the license link.
- Keep third-party libraries under their own licenses. If packaging bundles libraries into the AppImage, include their required license notices with the release/package.

## Clipboard behavior

- `Ctrl+C` copies the current selection to the operating system clipboard.
- `Ctrl+V` inserts clipboard text at the cursor, replacing the current selection when one exists.
- Copy with no active selection reports a short message and changes no clipboard data.
- `Ctrl+X` remains the editor exit shortcut; `Ctrl+C` is no longer an exit shortcut.
- Preserve UTF-8 text and newlines when transferring selections and paste data.
- Use external clipboard helpers without invoking a shell: prefer `wl-copy`/`wl-paste` on Wayland, then `xclip` or `xsel` on X11. Detect available tools at runtime. If no supported clipboard helper is installed or the operation fails, leave the buffer unchanged and show a concise status message.
- Clipboard helpers remain optional runtime dependencies, documented for Wayland and X11.

## AppImage packaging

- Add a reproducible packaging script and AppDir assets under `packaging/appimage/`.
- Build the C editor with the existing Makefile, stage the binary and runtime dependencies into an AppDir, and create a type 2 x86_64 `.AppImage` with the AppImage tooling.
- Include an `AppRun`, application metadata, and a desktop entry marked as a terminal application. The editor remains a TUI and requires an attached terminal; documented launching uses a terminal command and optionally a file path argument.
- Keep generated AppDir files and `.AppImage` artifacts out of Git. The repository contains the recipe, not the built binary.
- Document the artifact's Linux/x86_64 scope, executable permission (`chmod +x`), launch command, and manual upload to a GitHub Release. The user will upload the artifact after push; this task will not publish a release.
- Build dependencies (`linuxdeploy`/AppImage output tooling) are packaging-time tools and are not installed as project runtime dependencies.

## README and project documentation

- Add a complete usage section covering opening a path or directory, mouse navigation, editing, save/close/exit shortcuts, clipboard shortcuts, file/folder creation, deletion, and configuration.
- Add local AppImage build instructions, download/run instructions, and the GitHub Release upload steps.
- Describe the GPL choice accurately: others may redistribute and modify under GPL terms.
- Keep current V1 feature and debug documentation in sync with the new clipboard and distribution flow.

## Acceptance criteria

1. The repository contains a complete GPL-3.0-or-later `LICENSE` and correct attribution to `Gxstavo-dev`.
2. A selected multiline UTF-8 region can be copied and pasted; paste replaces a selection and updates the buffer's dirty state.
3. `Ctrl+X` still exits, while `Ctrl+C` copies only when text is selected and does not exit.
4. Missing clipboard tools produce a clear message without corrupting or replacing the buffer.
5. A packaging script creates an executable x86_64 AppImage from the current source and stages its runtime dependencies and metadata.
6. README instructions accurately explain terminal launch and how to upload the resulting file to a GitHub Release.
7. `make` and the package build complete; manual checks cover clipboard behavior and launching the AppImage with and without a file argument.

## Risks and limits

- A public GitHub repository exposes the source and permits GitHub users to view and fork it. GPL explicitly permits redistribution when its conditions are met.
- AppImages are Linux artifacts, not macOS or Windows packages. Compatibility depends on the build's architecture and glibc baseline.
- TUI apps need a terminal. Launching the AppImage from a file manager may depend on desktop integration; launching from an existing terminal is the reliable supported path.
- Clipboard support depends on `wl-clipboard`, `xclip`, or `xsel` and the graphical session's clipboard service.
- This specification records the selected license and intended behavior; it is not a legal opinion about any particular dispute or jurisdiction.
