#!/usr/bin/env bash
# Theme command-line tests. Usage: bash tests/cli_test.sh path/to/2048
set -euo pipefail

binary="$(realpath -- "${1:?usage: cli_test.sh BINARY}")"
source_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/2048-cli.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT

# Never read or write the real configuration, themes, or home directory.
export HOME="$work/home" XDG_CONFIG_HOME="$work/home/.config" THEMES_DIR="$work/themes"
unset GAME_2048_CONFIG_DIR
mkdir -p "$HOME"
config="$work/config"
theme="$config/theme.json"
icon="$config/icon.svg"

fail() {
    printf 'cli_test: %s\n' "$*" >&2
    exit 1
}

run() {
    "$binary" --config-dir "$config" "$@"
}

# contains FILE TEXT...: every TEXT occurs in FILE.
contains() {
    local file="$1" text
    shift
    for text in "$@"; do
        grep -Fq -- "$text" "$file" || fail "$file lacks: $text"
    done
}

lacks() {
    if grep -Fq -- "$2" "$1"; then
        fail "$1 unexpectedly contains: $2"
    fi
}

snapshot() {
    (cd "$config" && find . -printf '%y %p %l\n' | LC_ALL=C sort && find . -type f -exec sha256sum {} + | LC_ALL=C sort)
}

# rejects DESCRIPTION ARGUMENTS...: the command fails with a message and changes no file.
rejects() {
    local description="$1" before
    shift
    before="$(snapshot 2>/dev/null || true)"
    if "$binary" "$@" > "$work/stdout" 2> "$work/stderr"; then
        fail "accepted $description"
    fi
    [[ -s "$work/stderr" ]] || fail "no error message for $description"
    [[ "$(snapshot 2>/dev/null || true)" == "$before" ]] || fail "$description changed files"
}

# A themes directory with two named themes and a "current" link to one of them.
mkdir -p "$THEMES_DIR/harbor" "$THEMES_DIR/dawn"
cat > "$THEMES_DIR/harbor/Colors.qml" <<'EOF'
pragma Singleton
import QtQuick

QtObject {
    // Qt order: #AARRGGBB, so this is #0e1e36 at alpha 0xe0.
    readonly property color background: "#e00e1e36"
    readonly property color foreground: "#e6edf7"
    readonly property color primary: "#5fb3f9"
    readonly property color onPrimary: "#04182b"
    readonly property color secondary: "#c792ea"
    readonly property color tertiary: "#ffcb6b"
    readonly property color outline: primary
}
EOF
cat > "$THEMES_DIR/dawn/palette.json" <<'EOF'
{
    "mode": "dark",
    "dark": {
        "background": "#0e1e36e0",
        "foreground": "#f4efe6",
        "primary": "#ff8800cc",
        "secondary": "#d36ba6",
        "tertiary": "#f2c94c"
    },
    "light": {
        "background": "#ffffff",
        "foreground": "#000000",
        "primary": "#0055aa"
    }
}
EOF
ln -s harbor "$THEMES_DIR/current"

# Help and version.
"$binary" --help > "$work/help"
contains "$work/help" 'Usage:' 'theme follow active' '#AARRGGBB'
[[ "$("$binary" --version)" == "2048 "* ]] || fail '--version output'

# Read-only commands never create the configuration directory.
run theme list > "$work/list"
for name in forest midnight paper rose harbor dawn; do
    grep -qx "  $name" "$work/list" || fail "theme list lacks $name"
done
if grep -qx '  current' "$work/list"; then
    fail 'theme list shows the current link as a theme'
fi
contains "$work/list" 'Active link:' "$THEMES_DIR/current"
run theme keys > "$work/keys"
[[ "$(wc -l < "$work/keys")" == 24 ]] || fail 'theme keys should list 12 colors and 12 tile keys'
for key in background surface surface_alt text muted accent accent_text secondary highlight border board cell \
    tile_2 tile_1024 tile_2048 tile_super; do
    grep -qx "$key" "$work/keys" || fail "theme keys lacks $key"
done
[[ "$(run theme path)" == "$theme" ]] || fail 'theme path'
[[ "$(GAME_2048_CONFIG_DIR="$work/env" "$binary" theme path)" == "$work/env/theme.json" ]] ||
    fail 'GAME_2048_CONFIG_DIR is ignored'
run theme show > "$work/show"
contains "$work/show" '"version": 1' '"background":'
run theme preset paper --dry-run > "$work/preview.json"
contains "$work/preview.json" '"name": "paper"'
test ! -e "$config" || fail 'a read-only command or --dry-run created the configuration'
test ! -e "$work/env" || fail 'theme path created the configuration'

# Presets write theme.json and a themed icon.svg.
run theme preset paper > /dev/null
test -f "$theme" && test -f "$icon" || fail 'theme preset did not write theme.json and icon.svg'
contains "$theme" '"name": "paper"' '"surface": "#fffdf8"' '"version": 1'
lacks "$theme" '"source"'
contains "$icon" '<svg' 'fill="#fffdf8"'
cmp -s "$theme" "$work/preview.json" || fail '--dry-run output differs from the written file'

# Colors: Qt.rgba() keeps alpha on the background; elsewhere alpha is dropped and RGB kept.
run theme set 'background=Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)' > /dev/null
contains "$theme" '"background": "#e00e1e36"' '"name": "custom"' '"surface": "#fffdf8"'
run theme set 'accent=#e0ff0000' 'text=#80112233' 'muted=Qt.rgba(1, 0.5, 0, 0.25)' > /dev/null
contains "$theme" '"accent": "#ff0000"' '"text": "#112233"' '"muted": "#ff8000"' '"background": "#e00e1e36"'
run theme set 'background=#0e1e36' > /dev/null
contains "$theme" '"background": "#0e1e36"'
run theme set 'background=#e00e1e36' 'tile_8=#123456' > /dev/null
contains "$theme" '"background": "#e00e1e36"' '"tile_8": "#123456"'
run theme set 'tile_8=' > /dev/null
lacks "$theme" 'tile_8'
cp "$icon" "$work/custom-icon.svg"

# Named themes from THEMES_DIR are followed statically.
run theme use harbor > /dev/null
contains "$theme" '"name": "harbor"' '"background": "#e00e1e36"' '"accent": "#5fb3f9"' \
    '"accent_text": "#04182b"' '"secondary": "#c792ea"' '"highlight": "#ffcb6b"' \
    '"kind": "static"' "\"path\": \"$THEMES_DIR/harbor\""
if cmp -s "$icon" "$work/custom-icon.svg"; then
    fail 'icon.svg was not redrawn for the new theme'
fi

# "follow active" picks the current link and survives retargeting it.
run theme follow active > /dev/null
contains "$theme" '"name": "harbor"' '"kind": "active"' "\"path\": \"$THEMES_DIR/current\"" '"background": "#e00e1e36"'
ln -sfn dawn "$THEMES_DIR/current"
run theme refresh > /dev/null
# JSON palettes use CSS #RRGGBBAA order; the stored background is Qt's #AARRGGBB.
contains "$theme" '"name": "dawn"' '"kind": "active"' '"background": "#e00e1e36"' '"accent": "#ff8800"' \
    '"text": "#f4efe6"'
run theme follow file "$THEMES_DIR/dawn/palette.json" > /dev/null
contains "$theme" '"kind": "file"' "\"path\": \"$THEMES_DIR/dawn/palette.json\""
run theme follow static harbor > /dev/null
contains "$theme" '"kind": "static"' '"name": "harbor"'

# --dry-run validates and prints but never writes.
before="$(snapshot)"
run theme preset rose --dry-run > "$work/rose.json"
contains "$work/rose.json" '"name": "rose"'
run theme set 'accent=#000000' --dry-run > /dev/null
run theme follow active --dry-run > /dev/null
run theme detach --dry-run > /dev/null
run theme refresh --dry-run > /dev/null
run theme export "$work/never.json" --dry-run > /dev/null
[[ "$(snapshot)" == "$before" ]] || fail '--dry-run changed the configuration'
test ! -e "$work/never.json" || fail 'export --dry-run wrote a file'

# Invalid input fails with a message and leaves every file alone.
rejects 'an unknown key' --config-dir "$config" theme set 'shadow=#123456'
rejects 'a bad color' --config-dir "$config" theme set 'accent=notacolor'
rejects 'an out-of-range channel' --config-dir "$config" theme set 'accent=Qt.rgba(2, 0, 0, 1)'
rejects 'an assignment without =' --config-dir "$config" theme set accent
rejects 'an empty required color' --config-dir "$config" theme set 'accent='
rejects 'an unknown preset' --config-dir "$config" theme preset neon
rejects 'a missing theme' --config-dir "$config" theme use missing
rejects 'an unknown source kind' --config-dir "$config" theme follow sideways
rejects 'following its own output' --config-dir "$config" theme follow file "$theme"
rejects 'exporting over theme.json' --config-dir "$config" theme export "$theme"
rejects 'a missing import' --config-dir "$config" theme import "$work/missing.json"
rejects 'an unknown subcommand' --config-dir "$config" theme bogus
rejects 'a missing subcommand' --config-dir "$config" theme
rejects 'an unknown option' --config-dir "$config" --bogus theme list
rejects 'a board size of 2' --size 2 --help
rejects 'a board size of 6' --size 6 --help
rejects 'a board size of 9' --size 9 --help
rejects 'a non-numeric board size' --size four --help
rejects 'a missing board size' --size
rejects '--size with a theme command' --config-dir "$config" --size 4 theme list
printf '%s\n' '{"version": 1, "name": "broken", "colors": {"cell": "#000000"}}' > "$work/broken.json"
rejects 'an incomplete theme' --config-dir "$config" theme import "$work/broken.json"

# An invalid theme.json is reported, never overwritten.
cp "$theme" "$work/valid.json"
printf '{ "version": 1, "name": "bad", "colors": { "background": "#zzzzzz" } }\n' > "$theme"
rejects 'an invalid theme.json' --config-dir "$config" theme show
rejects 'editing an invalid theme.json' --config-dir "$config" theme set 'accent=#000000'
cp "$work/valid.json" "$theme"

# detach keeps the colors and drops the source; export writes a portable copy.
run theme follow active > /dev/null
run theme detach > /dev/null
lacks "$theme" '"source"'
contains "$theme" '"name": "dawn"' '"background": "#e00e1e36"'
run theme follow active > /dev/null
run theme export "$work/export.json" > /dev/null
test -f "$work/export.json" || fail 'export wrote nothing'
lacks "$work/export.json" '"source"'
contains "$work/export.json" '"name": "dawn"' '"background": "#e00e1e36"'
contains "$theme" '"kind": "active"'
run theme preset rose > /dev/null
run theme import "$work/export.json" > /dev/null
cmp -s "$theme" "$work/export.json" || fail 'import of an exported theme is not a round trip'

# The bundled example theme imports cleanly.
example="$source_directory/themes/ember.json"
if [[ -f "$example" ]]; then
    run theme import "$example" --dry-run > "$work/ember.json"
    contains "$work/ember.json" '"name": "ember"'
fi

printf 'cli_test: all checks passed\n'
