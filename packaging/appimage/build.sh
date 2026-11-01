#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
CACHE_DIR="$SCRIPT_DIR/.cache"
APP_DIR="$ROOT_DIR/dist/appdir"
APPDIR="$APP_DIR/Terminal-Editor.AppDir"
OUTPUT="$ROOT_DIR/dist/terminal-editor-x86_64.AppImage"
LINUXDEPLOY_VERSION="1-alpha-20251107-1"
APPIMAGETOOL_VERSION="1.9.1"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
    printf '%s\n' "$(command -v "$1")"
}

fetch_tool() {
    local env_name="$1"
    local command_name="$2"
    local url="$3"
    local destination="$4"
    local override="${!env_name:-}"

    if [ -n "$override" ]; then
        [ -x "$override" ] || fail "$env_name does not point to an executable: $override"
        printf '%s\n' "$override"
        return
    fi
    if command -v "$command_name" >/dev/null 2>&1; then
        command -v "$command_name"
        return
    fi
    mkdir -p "$CACHE_DIR"
    if [ ! -x "$destination" ]; then
        require_tool curl >/dev/null
        printf 'Downloading %s ...\n' "$command_name" >&2
        curl -fL --retry 3 --output "$destination.tmp" "$url" ||
            fail "could not download $command_name from $url"
        chmod +x "$destination.tmp"
        mv "$destination.tmp" "$destination"
    fi
    printf '%s\n' "$destination"
}

[ "$(uname -s)" = "Linux" ] || fail "this AppImage recipe only builds on Linux"
[ "$(uname -m)" = "x86_64" ] || fail "this AppImage recipe targets x86_64; detected $(uname -m)"
require_tool make >/dev/null
require_tool pkg-config >/dev/null
require_tool cc >/dev/null

LINUXDEPLOY=$(fetch_tool LINUXDEPLOY linuxdeploy "$LINUXDEPLOY_URL" \
    "$CACHE_DIR/linuxdeploy-$LINUXDEPLOY_VERSION-x86_64.AppImage")
APPIMAGETOOL=$(fetch_tool APPIMAGETOOL appimagetool "$APPIMAGETOOL_URL" \
    "$CACHE_DIR/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage")

make -C "$ROOT_DIR"

rm -rf "$APP_DIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$APPDIR/usr/share/doc/terminal-editor/licenses"
mkdir -p "$ROOT_DIR/dist"

install -m 755 "$ROOT_DIR/terminal-editor" "$APPDIR/usr/bin/terminal-editor"
install -m 755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
install -m 644 "$SCRIPT_DIR/terminal-editor.desktop" \
    "$APPDIR/usr/share/applications/terminal-editor.desktop"
install -m 644 "$SCRIPT_DIR/terminal-editor.desktop" "$APPDIR/terminal-editor.desktop"
install -m 644 "$SCRIPT_DIR/terminal-editor.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/terminal-editor.svg"
install -m 644 "$SCRIPT_DIR/terminal-editor.svg" "$APPDIR/terminal-editor.svg"
install -m 644 "$ROOT_DIR/LICENSE" "$APPDIR/usr/share/doc/terminal-editor/LICENSE"
install -m 644 "$SCRIPT_DIR/NOTICE" "$APPDIR/usr/share/doc/terminal-editor/NOTICE"

copy_notice() {
    local source="$1"
    local destination="$2"

    if [ -f "$source" ]; then
        install -m 644 "$source" "$destination"
        return 0
    fi
    return 1
}

copy_notice /usr/share/licenses/ncurses/COPYING \
    "$APPDIR/usr/share/doc/terminal-editor/licenses/ncurses-COPYING" ||
    copy_notice /usr/share/doc/ncurses-base/copyright \
    "$APPDIR/usr/share/doc/terminal-editor/licenses/ncurses-COPYING" ||
    copy_notice /usr/share/doc/libncursesw6/copyright \
    "$APPDIR/usr/share/doc/terminal-editor/licenses/ncurses-COPYING" ||
    printf 'warning: ncurses license notice not found on this host\n' >&2

# The strip binary bundled with linuxdeploy cannot read newer .relr.dyn sections
# produced by current binutils, so leave the binaries unstripped.
export NO_STRIP=1
export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/terminal-editor" \
    --desktop-file "$APPDIR/usr/share/applications/terminal-editor.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/terminal-editor.svg"

rm -f "$OUTPUT"
"$APPIMAGETOOL" --no-appstream "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"

printf 'Created %s\n' "$OUTPUT"
