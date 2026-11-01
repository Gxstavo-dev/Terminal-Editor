# V2 Syntax Highlighting and Language Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add low-memory syntax highlighting and automatic, active-buffer-only LSP diagnostics for Python and TypeScript/TSX.

**Architecture:** A local lexer classifies visible source text without starting a child process. A small LSP module owns one optional stdio server process and its JSON-RPC stream; the UI starts/stops it as the active buffer changes and polls it from the existing event loop. Linter and LSP diagnostics remain separate per-buffer layers and are combined only for display.

**Tech Stack:** C11, ncursesw, POSIX process/pipe/poll APIs, GNU Make, and the JSON-RPC/LSP subset required for initialize, document open/change/close, diagnostics, shutdown, and exit.

**Spec:** `docs/superpowers/specs/2026-11-01-v2-syntax-diagnostics-design.md`

## Global Constraints

- Extender terminal-editor con resaltado de sintaxis ligero y diagnósticos ErrorLens en vivo, manteniendo bajo el consumo de memoria.
- Añadir resaltado de sintaxis dentro del proceso del editor, sin proceso externo ni dependencia obligatoria.
- Detectar el lenguaje por extensión y seleccionar automáticamente un servidor conocido si su ejecutable está disponible en `PATH`.
- No instalar ni descargar servidores automáticamente: son dependencias externas.
- Mantener como máximo un servidor de lenguaje activo.
- Mantener los linters `lint.<extension>` actuales como ejecución al guardar.
- Si el ejecutable no está disponible, el servidor falla o el protocolo devuelve datos inválidos, mantener el editor operativo, mostrar un mensaje breve y liberar el proceso y sus recursos.
- No incluye autocompletado, navegación semántica, formato, refactorización ni instalación de servidores.

## File Structure

- `src/types.h`: language-server command configuration, per-source diagnostic layers, and the one active LSP session state.
- `src/syntax.h`, `src/syntax.c`: extension detection and allocation-free local token classification for Python and TypeScript/TSX.
- `src/lsp.h`, `src/lsp.c`: server executable selection, nonblocking process/pipe lifecycle, JSON-RPC framing, minimal JSON message handling, and LSP document/diagnostic operations.
- `src/ui.c`: syntax attributes in `draw_line`, active-buffer session synchronization, event-loop polling, ErrorLens composition, and shutdown cleanup.
- `src/config.c`, `src/config.h`: optional `language.<extension>` command override, reusing existing argv parsing and cleanup conventions.
- `Makefile`: compile and link the syntax and LSP modules with the existing POSIX/ncurses setup.
- `README.md`: document supported extensions, default server executables, optional overrides, lifecycle, and missing-server behavior.
- `docs/project/versiones.md`: record the v2 capability and memory policy after implementation.

## Review Focus

1. **Large file with a distant viewport:** syntax rendering must not allocate a token copy of the document or add a large per-character cache; inspect RSS and render cost with a long source file.
2. **UTF-8, selection, and horizontal scroll:** token attributes must follow code points while selection retains visual priority and wide characters do not shift highlighting.
3. **Server unavailable or immediately exits:** the editor remains interactive, reports a short status, closes descriptors, and reaps the child.
4. **Malformed, oversized, or fragmented LSP frames:** parsing rejects bad input safely, input limits prevent unbounded memory growth, and editing continues.
5. **Rapid buffer switches and close/exit:** at most one child remains alive, the old child is reaped, and stale LSP diagnostics do not appear on another document.

---

### Task 1: Add language detection and local syntax classification

**Files:**
- Create: `src/syntax.h`
- Create: `src/syntax.c`
- Modify: `src/types.h`
- Modify: `src/ui.c`
- Modify: `Makefile`

**Interfaces:**
- `SyntaxLanguage syntax_language_for_path(const char *path);` returns `SYNTAX_NONE`, `SYNTAX_PYTHON`, or `SYNTAX_TYPESCRIPT` for `.py`, `.ts`, `.tsx`, `.js`, and `.jsx` suffixes; matching is case-insensitive.
- `SyntaxClass syntax_classify(const char *line, size_t byte_offset, SyntaxLanguage language, SyntaxState *state);` classifies the code point at `byte_offset`; caller advances by decoded UTF-8 character length. `SyntaxState` carries only small lexer state needed between visible lines.

- [ ] Define token classes for default text, keyword, string, comment, number, and type/builtin, plus a compact lexer state for multiline strings/comments.
- [ ] Implement suffix matching on the basename (not directory names), with unknown and pathless buffers returning `SYNTAX_NONE`.
- [ ] Implement line-by-line lexical classification for Python and TypeScript-family source without heap allocation or storing token arrays; pass state sequentially while drawing visible rows.
- [ ] Integrate token color pairs in `draw_line`; selection color takes precedence, then token color, then the existing default pair. Keep tabs, Unicode width, and horizontal clipping behavior unchanged.
- [ ] Add the module to `SOURCES` in the Makefile and build with `make`; inspect a Python and TSX buffer in a terminal, including selection and horizontal scrolling.

### Task 2: Add automatic server discovery and optional command overrides

**Files:**
- Modify: `src/types.h`
- Modify: `src/config.c`
- Modify: `src/config.h`
- Create: `src/lsp.h`
- Create: `src/lsp.c`
- Modify: `Makefile`

**Interfaces:**
- `const LspServerSpec *lsp_server_for_path(const AppConfig *config, const char *path);` returns a configured override when present, otherwise the built-in Python (`pylsp`) or TypeScript/TSX (`typescript-language-server --stdio`) command if its executable is found in `PATH`; missing executables return `NULL`.
- `LspServerSpec` owns an extension and argv; config load/destroy follow the `LintRule` allocation pattern.

- [ ] Add `language.<extension>=program arguments...` parsing by reusing the existing quoted argv parser; preserve defaults when an entry is invalid and count it as a config warning.
- [ ] Implement PATH lookup without a shell; do not launch a process during discovery. Use built-in server commands only for `.py`, `.ts`, `.tsx`, `.js`, and `.jsx`; optional overrides can add other extensions.
- [ ] Document that `pylsp` and `typescript-language-server` must be installed separately and that no server is started when one is absent.
- [ ] Build with `make`; inspect default command lookup, override precedence, config ownership, and missing-executable behavior.

### Task 3: Implement bounded LSP process and JSON-RPC transport

**Files:**
- Modify: `src/lsp.h`
- Modify: `src/lsp.c`
- Modify: `src/types.h`
- Modify: `Makefile`

**Interfaces:**
- `int lsp_start(LspSession *session, const LspServerSpec *spec, const char *path, const char *language_id);` starts one child with stdin/stdout pipes, nonblocking parent descriptors, and an initialize request.
- `int lsp_send_document(LspSession *session, LspDocumentEvent event, const char *uri, const char *text, size_t text_length, int version);` emits didOpen, didChange, or didClose using Content-Length framed JSON-RPC.
- `int lsp_poll(LspSession *session, LspMessage *message);` performs nonblocking reads and returns a complete bounded message, no message, or a recoverable transport error.
- `void lsp_stop(LspSession *session);` sends shutdown/exit when initialized, closes pipes, waits briefly, then terminates and reaps an unresponsive child; it is safe on a zeroed session.

- [ ] Define session state with pid, pipe descriptors, initialization state, request id, bounded input/output buffers, document URI/version, and no per-document token cache.
- [ ] Create pipes and fork/exec argv directly; redirect child stdin/stdout to LSP pipes and stderr to `/dev/null` so protocol output stays clean. Set parent descriptors nonblocking and close every unused descriptor in both processes.
- [ ] Implement Content-Length framing with a fixed maximum inbound/outbound message size; handle partial headers, partial bodies, multiple messages per read, malformed lengths, EOF, and `EINTR` without blocking the UI.
- [ ] Implement only JSON string escaping and the small JSON token/member extraction needed for initialize responses and `textDocument/publishDiagnostics`; avoid adding a third-party dependency.
- [ ] Implement initialize/initialized, document notifications, and graceful shutdown. Ensure every failure path terminates and reaps a started child.
- [ ] Build with `make`; inspect partial-frame and process cleanup paths and confirm no server output can corrupt the terminal UI.

### Task 4: Connect LSP session lifecycle to active buffers

**Files:**
- Modify: `src/types.h`
- Modify: `src/ui.c`
- Modify: `src/lsp.h`
- Modify: `src/lsp.c`

**Interfaces:**
- `void ui_sync_language_server(App *app);` compares the active buffer identity/path and server spec with the session, closes the previous document/session as needed, and starts the selected server only for a path-backed active buffer.
- `bool lsp_take_diagnostics(LspSession *session, Diagnostic **items, size_t *count);` converts the most recent publishDiagnostics notification to editor coordinates and transfers ownership to the caller.

- [ ] Call `ui_sync_language_server` after initial UI setup and after each successful active-buffer change: opening an existing buffer, appending a buffer, closing a buffer, and returning to the editor from sidebar focus.
- [ ] On a new session, send `initialize` with the supported sync mode, wait asynchronously for its response, send `initialized`, then `didOpen` with `python` or `typescript` language id, absolute file URI, document version, and current text.
- [ ] After successful buffer edits, undo, redo, paste, or reload, send a full-text `didChange` with an incremented version; keep this update path adjacent to the existing buffer mutation handling so no edit path is missed.
- [ ] Poll the LSP descriptors during the existing 200 ms event-loop timeout without blocking key input; poll once immediately after handling keys as needed to keep diagnostics responsive.
- [ ] On switch away, send didClose, clear that buffer's LSP diagnostic layer, stop/reap its process, and then start only the newly active buffer's detected server. On close/exit, guarantee cleanup.
- [ ] Build with `make`; manually exercise active/inactive buffers with installed servers and use process inspection to confirm one-or-zero language-server children.

### Task 5: Merge LSP diagnostics into existing ErrorLens rendering

**Files:**
- Modify: `src/types.h`
- Modify: `src/ui.c`
- Modify: `src/lsp.c`
- Modify: `src/buffer.c`

**Interfaces:**
- Each `Buffer` owns separate linter and language-server diagnostic arrays and counts. `diagnostic_for_line` returns the highest-severity item on that line, preferring the LSP item on equal severity; click navigation uses the selected item's line and column.
- `void buffer_clear_language_diagnostics(Buffer *buffer);` releases only the LSP layer.

- [ ] Split current `Buffer.diagnostics` into linter and LSP ownership; update `run_lint` to replace only the linter layer and update destruction/reload paths to free both.
- [ ] Parse LSP diagnostic ranges and severities into one-based line/column records; ignore malformed locations, clamp valid positions to current document bounds, and cap diagnostic count and retained message bytes.
- [ ] Replace only the active buffer's LSP layer when a publishDiagnostics notification arrives; an empty publication clears prior LSP entries for that URI without affecting lint entries.
- [ ] Compose the ErrorLens gutter and inline label from both layers while preserving existing click-to-jump behavior and displaying one deterministic item per source line.
- [ ] Build with `make`; manually confirm an LSP warning and a linter error on the same file coexist, and that empty LSP publication clears only the LSP warning.

### Task 6: Document and review v2 behavior and memory use

**Files:**
- Modify: `README.md`
- Modify: `docs/project/versiones.md`
- Modify: `docs/superpowers/specs/2026-11-01-v2-syntax-language-diagnostics-design.md`

- [ ] Document syntax extensions, default server executable names, required manual installation, optional `language.<extension>` overrides, one-active-buffer lifecycle, and fallback when no server is found.
- [ ] Update the v2 roadmap only after the feature is integrated; retain the plugin API and unrelated language features as future work.
- [ ] Build with `make` and manually verify `.py`, `.tsx`, unknown extensions, missing servers, unsaved edits, save/reload, switch, close, and exit flows.
- [ ] Compare idle RSS with the pre-v2 binary using the same environment and measure the child server separately while a supported buffer is active; record observed values and limitations without attributing child memory to the editor process.

## Self-Review

- Spec coverage: local highlighting and unknown-extension fallback are Task 1; auto-discovery and overrides are Task 2; LSP framing/failure handling are Task 3; active-only process lifecycle and document sync are Task 4; separate/coexisting diagnostic sources and navigation are Task 5; user instructions and memory comparison are Task 6.
- Resource review: input frames, diagnostic count, and retained message bytes have explicit caps; there is no persistent full-document token copy; child cleanup covers failure and normal exit.
- Interface review: `LspServerSpec`, `LspSession`, `Diagnostic`, and `Buffer` ownership are established before consumers; session lifecycle and diagnostics APIs are introduced before UI integration.
- Scope review: no completion, install/download behavior, plugin API, or unrelated editor refactor is included.
