#!/usr/bin/env bash
# Per-user installer tests. Usage: bash tests/install_test.sh BUILD_DIR scripts/install-desktop.sh
set -euo pipefail

build_directory="$(realpath -- "${1:?usage: install_test.sh BUILD_DIR INSTALLER}")"
installer="$(realpath -- "${2:?usage: install_test.sh BUILD_DIR INSTALLER}")"
source_directory="$(cd -- "$(dirname -- "$installer")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/2048-install.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT

# Everything the installer may touch lives under $root; logs go elsewhere.
root="$work/root"
log="$work/log"
mkdir -p "$root" "$log"
export HOME="$root/home" XDG_CONFIG_HOME="$root/home/.config" THEMES_DIR="$root/themes"
unset GAME_2048_CONFIG_DIR
mkdir -p "$XDG_CONFIG_HOME"

desktop_id=io.github.game2048
prefix="$root/prefix"
config="$root/config"
apps="$root/apps.list"
executable="$prefix/bin/2048"
desktop="$prefix/bin/applications/$desktop_id.desktop"
registered="$prefix/share/applications/$desktop_id.desktop"
fallback="$prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg"
entry="$desktop_id.desktop|$fallback|2048"
header='icon-override = true'
unrelated='example.desktop|/usr/share/icons/example.svg|Example App|extra'

fail() {
    printf 'install_test: %s\n' "$*" >&2
    exit 1
}

install() {
    bash "$installer" --build-dir "$build_directory" "$@"
}

snapshot() {
    (cd "$root" && find . -printf '%y %p %l\n' | LC_ALL=C sort && find . -type f -exec sha256sum {} + | LC_ALL=C sort)
}

contains() {
    local file="$1" text
    shift
    for text in "$@"; do
        grep -Fq -- "$text" "$file" || fail "$file lacks: $text"
    done
}

has_line() {
    grep -Fqx -- "$2" "$1" || fail "$1 lacks the line: $2"
}

# refuses DESCRIPTION ARGUMENTS...: the installer fails and changes nothing.
refuses() {
    local description="$1" before
    shift
    before="$(snapshot)"
    if install "$@" > "$log/stdout" 2> "$log/stderr"; then
        fail "installer accepted $description"
    fi
    [[ -s "$log/stderr" ]] || fail "no error message for $description"
    [[ "$(snapshot)" == "$before" ]] || fail "installer changed files when refusing $description"
}

theme_dir() {
    mkdir -p "$THEMES_DIR/$1"
    printf '%s\n' 'QtObject {' \
        "    readonly property color background: \"$2\"" \
        "    readonly property color foreground: \"#e6edf7\"" \
        "    readonly property color primary: \"$3\"" \
        '}' > "$THEMES_DIR/$1/Colors.qml"
}

theme_dir harbor '#e00e1e36' '#5fb3f9'
theme_dir dusk '#201a2b' '#d19af7'
ln -s harbor "$THEMES_DIR/current"
printf '%s\n' "$header" "$unrelated" > "$apps"
cp "$apps" "$log/apps.original"
arguments=(--prefix "$prefix" --config-dir "$config" --apps-list "$apps")

# A dry run describes the plan and writes nothing.
before="$(snapshot)"
install "${arguments[@]}" --dry-run > "$log/dry-run"
contains "$log/dry-run" 'Dry run' "$executable" "$desktop" "$registered" "$fallback" \
    "\"$executable\" --config-dir \"$config\"" "$fallback" "$apps" "$THEMES_DIR/current"
[[ "$(snapshot)" == "$before" ]] || fail '--dry-run changed files'
test ! -e "$prefix" && test ! -e "$config" || fail '--dry-run created the prefix or configuration'

# First installation.
install "${arguments[@]}" > "$log/first"
contains "$log/first" "$executable" "$apps"
test -x "$executable" && test ! -L "$executable" || fail 'executable not installed'
cmp -s "$executable" "$build_directory/2048" || fail 'installed executable differs from the build'
[[ "$("$executable" --version)" == "2048 "* ]] || fail 'installed executable does not run'
cmp -s "$fallback" "$source_directory/assets/$desktop_id.svg" || fail 'fallback icon not installed'
test -f "$desktop" && test ! -L "$desktop" || fail 'desktop entry not written'
has_line "$desktop" "Exec=\"$executable\" --config-dir \"$config\""
has_line "$desktop" "Icon=$desktop_id"
has_line "$desktop" 'Name=2048'
has_line "$desktop" "StartupWMClass=$desktop_id"
test -L "$registered" || fail 'desktop entry is not registered with a symlink'
[[ "$(readlink -- "$registered")" == "$desktop" ]] || fail 'registration points elsewhere'
if command -v desktop-file-validate > /dev/null 2>&1; then
    desktop-file-validate "$registered" || fail 'invalid desktop entry'
fi
[[ "$(sed -n 1p "$apps")" == "$header" ]] || fail 'app list header changed'
[[ "$(sed -n 2p "$apps")" == "$unrelated" ]] || fail 'unrelated app list line changed'
[[ "$(grep -Fxc -- "$entry" "$apps")" == 1 && "$(wc -l < "$apps")" == 3 ]] || fail 'app list entry missing'
contains "$config/theme.json" '"name": "harbor"' '"kind": "active"' "\"path\": \"$THEMES_DIR/current\"" \
    '"background": "#e00e1e36"'
[[ -z "$(find "$XDG_CONFIG_HOME" -mindepth 1)" && "$(find "$HOME" -mindepth 1)" == "$XDG_CONFIG_HOME" ]] ||
    fail 'the installer touched the home directory'

# Reinstalling is idempotent: nothing changes and nothing is backed up.
first="$(snapshot)"
install "${arguments[@]}" > "$log/second"
[[ "$(snapshot)" == "$first" ]] || fail 'a second installation changed files'
[[ "$(grep -Fc -- "$desktop_id.desktop" "$apps")" == 1 ]] || fail 'duplicate app list entry'
[[ "$(sed -n 1p "$apps")" == "$header" && "$(sed -n 2p "$apps")" == "$unrelated" ]] ||
    fail 'second installation changed unrelated app list lines'
install "${arguments[@]}" --dry-run > /dev/null
[[ "$(snapshot)" == "$first" ]] || fail '--dry-run after installation changed files'

# A retargeted active link is picked up; the replaced theme.json is backed up.
ln -sfn dusk "$THEMES_DIR/current"
install "${arguments[@]}" > /dev/null
contains "$config/theme.json" '"name": "dusk"' '"kind": "active"' '"background": "#201a2b"'
backups=("$config"/theme.json.2048-backup.*)
[[ ${#backups[@]} == 1 && -f "${backups[0]}" ]] || fail 'replaced theme.json was not backed up'
contains "${backups[0]}" '"name": "harbor"'
ln -sfn harbor "$THEMES_DIR/current"

# An existing entry gets the new icon and label, keeps extra fields, and loses duplicates.
other="$root/other.list"
printf '%s\n' "$header" "$desktop_id.desktop|/old/icon.svg|Old Name|keep-me" "$unrelated" \
    "$desktop_id.desktop|/dup.svg|Duplicate" > "$other"
cp "$other" "$log/other.original"
install --prefix "$prefix" --config-dir "$config" --apps-list "$other" --no-follow > /dev/null
printf '%s\n' "$header" "$entry|keep-me" "$unrelated" > "$log/other.expected"
cmp -s "$other" "$log/other.expected" || fail "app list entry not rewritten: $(cat "$other")"
cmp -s "$other".2048-backup.* "$log/other.original" || fail 'rewritten app list was not backed up'
printf '%s' 'last.desktop|/last.svg|Last' > "$other"
install --prefix "$prefix" --config-dir "$config" --apps-list "$other" --no-follow > /dev/null
printf '%s\n' 'last.desktop|/last.svg|Last' "$entry" > "$log/other.expected"
cmp -s "$other" "$log/other.expected" || fail 'entry not appended after a final line without newline'

# --no-apps-list leaves the list alone; the default list is used only when it exists.
printf '%s\n' "$header" > "$other"
install --prefix "$prefix" --config-dir "$config" --apps-list "$other" --no-apps-list > /dev/null
has_line "$other" "$header"
[[ "$(wc -l < "$other")" == 1 ]] || fail '--no-apps-list modified the app list'
install --prefix "$prefix" --config-dir "$config" > /dev/null
test ! -e "$XDG_CONFIG_HOME/apps.list" || fail 'a missing default app list was created'
printf '%s\n' "$header" > "$XDG_CONFIG_HOME/apps.list"
install --prefix "$prefix" --config-dir "$config" > /dev/null
has_line "$XDG_CONFIG_HOME/apps.list" "$entry"
rm -f -- "$XDG_CONFIG_HOME"/apps.list*

# A palette the user chose is kept unless --follow is given.
custom="$root/custom"
"$executable" --config-dir "$custom" theme preset paper > /dev/null
install --prefix "$prefix" --config-dir "$custom" --no-apps-list > /dev/null
contains "$custom/theme.json" '"name": "paper"'
if grep -Fq '"source"' "$custom/theme.json"; then
    fail 'installer replaced a chosen palette'
fi
install --prefix "$prefix" --config-dir "$custom" --no-apps-list --follow > /dev/null
contains "$custom/theme.json" '"name": "harbor"' '"kind": "active"'

# --no-follow on a fresh configuration still creates theme.json.
fresh="$root/fresh"
install --prefix "$prefix" --config-dir "$fresh" --no-apps-list --no-follow > /dev/null
test -f "$fresh/theme.json" || fail '--no-follow left no theme'

# Without an active theme link a fresh configuration gets the built-in default.
mv "$THEMES_DIR" "$root/themes.away"
mkdir -p "$THEMES_DIR"
plain="$root/plain"
install --prefix "$prefix" --config-dir "$plain" --no-apps-list > /dev/null
contains "$plain/theme.json" '"name": "midnight"'
if grep -Fq '"source"' "$plain/theme.json"; then
    fail 'default theme follows a source'
fi
refuses '--follow without an active theme link' "${arguments[@]}" --follow
rmdir "$THEMES_DIR"
mv "$root/themes.away" "$THEMES_DIR"

# A plain file left by `cmake --install` is backed up and replaced by the managed link.
other_prefix="$root/other-prefix"
mkdir -p "$other_prefix/share/applications"
cp "$source_directory/assets/$desktop_id.desktop" "$other_prefix/share/applications/"
install --prefix "$other_prefix" --config-dir "$config" --no-apps-list > /dev/null
test -L "$other_prefix/share/applications/$desktop_id.desktop" || fail 'plain registration not replaced'
cmp -s "$other_prefix/share/applications/$desktop_id.desktop".2048-backup.* \
    "$source_directory/assets/$desktop_id.desktop" || fail 'plain registration not backed up'

# Unsafe or broken setups are refused before anything is written.
ln -s "$apps" "$root/linked.list"
refuses 'a symlinked app list' --prefix "$prefix" --config-dir "$config" --apps-list "$root/linked.list"
refuses 'a missing build' --build-dir "$root/no-build" "${arguments[@]}"
refuses 'a quote in the prefix' --prefix "$root/a\"b" --config-dir "$config" --no-apps-list
refuses 'a pipe in the configuration path' --prefix "$prefix" --config-dir "$root/a|b" --no-apps-list
refuses 'an unknown option' "${arguments[@]}" --bogus
refuses 'a missing option value' --prefix
mv "$executable" "$root/executable.away"
ln -s "$root/executable.away" "$executable"
refuses 'a symlinked executable' "${arguments[@]}"
rm -f -- "$executable"
mv "$root/executable.away" "$executable"
ln -sfn "$root/elsewhere.desktop" "$registered"
refuses 'a foreign registration link' "${arguments[@]}"
ln -sfn "$desktop" "$registered"
cp "$config/theme.json" "$log/theme.valid"
printf '%s\n' '{"version": 1}' > "$config/theme.json"
refuses 'an invalid theme.json' "${arguments[@]}"
cp "$log/theme.valid" "$config/theme.json"

printf 'install_test: all checks passed\n'
