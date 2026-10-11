#!/usr/bin/env bash
# Per-user installation of 2048: the executable, a desktop entry whose icon is
# redrawn in the game's theme colors, an optional launcher-list entry, and an
# initial theme. Every file is replaced atomically, files whose content would
# change are backed up first, and --dry-run reports the plan without writing.
set -euo pipefail

readonly app_name=2048
readonly desktop_id=io.github.game2048
readonly app_label=2048

project_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
config_home="${XDG_CONFIG_HOME:-}"
[[ "$config_home" == /* ]] || config_home="${HOME:?HOME is not set}/.config"
build_directory="$project_directory/build"
install_prefix="${HOME:?HOME is not set}/.local"
config_directory="$config_home/2048"
default_apps_list="$config_home/apps.list"
apps_list="$default_apps_list"
use_apps_list=1
follow_mode=auto
dry_run=0

stamp="$(date +%Y%m%dT%H%M%S)"
backups=()
temporaries=()
trap 'rm -f -- ${temporaries[@]+"${temporaries[@]}"}' EXIT

usage() {
    cat <<'EOF'
Usage: install-desktop.sh [OPTIONS]

Install 2048 for the current user: the executable, a desktop entry and icon,
an entry in a launcher app list, and a theme.

Options:
  --dry-run          print what would be done without changing anything
  --build-dir DIR    directory holding the built 2048 binary (default: ./build)
  --prefix DIR       installation prefix (default: ~/.local)
  --config-dir DIR   configuration used by the launcher entry
                     (default: ${XDG_CONFIG_HOME:-~/.config}/2048)
  --apps-list FILE   app list to register in, created if needed
                     (default: ${XDG_CONFIG_HOME:-~/.config}/apps.list, if it exists)
  --no-apps-list     leave the app list alone
  --follow           follow the active theme link even when a palette is already set
  --no-follow        leave an existing theme alone (a new configuration gets the
                     game's default, which follows the active theme link if any)
  -h, --help         show this help
EOF
}

fail() {
    printf 'install-desktop: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'install-desktop: warning: %s\n' "$*" >&2
}

# Make a path absolute without resolving symlinks, expanding a leading ~.
absolute_path() {
    local path="$1"
    [[ "$path" == "~" || "$path" == "~/"* ]] && path="$HOME${path:1}"
    realpath -ms -- "$path"
}

# Paths that end up in the desktop entry or the app list must not contain characters
# that need escaping there: quotes, field codes, variable expansions, or the list delimiter.
require_plain_path() {
    [[ "$2" != *[$'\n'\"\`\$%\\\|]* ]] ||
        fail "$1 must not contain newlines or any of: \" \` \$ % \\ |"
}

parse_options() {
    while (( $# )); do
        case "$1" in
            --dry-run) dry_run=1; shift ;;
            --follow) follow_mode=always; shift ;;
            --no-follow) follow_mode=never; shift ;;
            --no-apps-list) use_apps_list=0; shift ;;
            -h|--help) usage; exit 0 ;;
            --build-dir|--prefix|--config-dir|--apps-list)
                (( $# >= 2 )) && [[ -n "$2" ]] || fail "$1 requires a value"
                [[ "$2" != *$'\n'* ]] || fail "$1 must not contain newlines"
                case "$1" in
                    --build-dir) build_directory="$(absolute_path "$2")" ;;
                    --prefix) install_prefix="$(absolute_path "$2")" ;;
                    --config-dir) config_directory="$(absolute_path "$2")" ;;
                    --apps-list) apps_list="$(absolute_path "$2")"; use_apps_list=1 ;;
                esac
                shift 2 ;;
            *) fail "Unknown option: $1 (see --help)" ;;
        esac
    done
    build_directory="$(absolute_path "$build_directory")"
    install_prefix="$(absolute_path "$install_prefix")"
    config_directory="$(absolute_path "$config_directory")"
    # The default list is only updated when a launcher already keeps one there.
    if (( use_apps_list )) && [[ "$apps_list" == "$default_apps_list" && ! -e "$apps_list" ]]; then
        use_apps_list=0
        apps_list_note="skipped, $apps_list does not exist (pass --apps-list to create it)"
    fi
}

set_paths() {
    build_binary="$build_directory/$app_name"
    executable="$install_prefix/bin/$app_name"
    desktop_file="$install_prefix/bin/applications/$desktop_id.desktop"
    registered="$install_prefix/share/applications/$desktop_id.desktop"
    fallback_icon="$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg"
    theme_file="$config_directory/theme.json"
    themed_icon="$config_directory/icon.svg"
    apps_entry="$desktop_id.desktop|$themed_icon|$app_label"
}

# Refuse to write through a symlink or over a directory.
require_plain_target() {
    [[ ! -L "$1" ]] || fail "Refusing to replace a symlink: $1"
    [[ ! -d "$1" ]] || fail "Expected a file, found a directory: $1"
}

theme_command() {
    "$build_binary" --config-dir "$config_directory" theme "$@"
}

# Read one top-level string field from the CLI's indented JSON output.
json_field() {
    sed -n "s/^    \"$1\": \"\\(.*\\)\",\\{0,1\\}\$/\\1/p" <<< "$2" | head -n 1
}

source_field() {
    sed -n "s/^        \"$1\": \"\\(.*\\)\",\\{0,1\\}\$/\\1/p" <<< "$2" | head -n 1
}

validate() {
    [[ -f "$build_binary" && -x "$build_binary" ]] ||
        fail "No built binary at $build_binary; run: cmake --build build"
    [[ "$install_prefix" != / ]] || fail 'Refusing to install into /'
    [[ "$config_directory" != / && "$config_directory" != "$HOME" ]] ||
        fail "Invalid configuration directory: $config_directory"
    require_plain_path --prefix "$install_prefix"
    require_plain_path --config-dir "$config_directory"
    [[ ! -e "$config_directory" || -d "$config_directory" ]] ||
        fail "Configuration path is not a directory: $config_directory"
    for target in "$executable" "$desktop_file" "$fallback_icon" "$theme_file" "$themed_icon"; do
        require_plain_target "$target"
    done
    if [[ -L "$registered" ]]; then
        [[ "$(readlink -- "$registered")" == "$desktop_file" ]] ||
            fail "Existing desktop registration points elsewhere: $registered -> $(readlink -- "$registered")"
    else
        [[ ! -d "$registered" ]] || fail "Expected a file, found a directory: $registered"
    fi
    if (( use_apps_list )); then
        [[ ! -L "$apps_list" ]] || fail "Refusing to modify a symlinked app list: $apps_list"
        [[ ! -e "$apps_list" || -f "$apps_list" ]] || fail "App list is not a regular file: $apps_list"
    fi
    version="$("$build_binary" --version)" || fail "$build_binary does not run"

    current_json=""
    if [[ -e "$theme_file" ]]; then
        current_json="$(theme_command show)" ||
            fail "Existing theme is invalid: $theme_file (fix it or move it away first)"
    fi
    plan_theme
}

# Decide which theme command to run, and preview its result with --dry-run.
plan_theme() {
    local active_json="" active_error="" has_source=0
    [[ -n "$current_json" && -n "$(source_field kind "$current_json")" ]] && has_source=1
    if [[ "$follow_mode" != never ]]; then
        if active_json="$(theme_command follow active --dry-run 2>/dev/null)"; then
            :
        else
            active_json=""
            active_error="$(theme_command follow active --dry-run 2>&1 >/dev/null || true)"
        fi
    fi
    theme_args=()
    theme_preview=""
    if [[ "$follow_mode" == always ]]; then
        [[ -n "$active_json" ]] || fail "Cannot follow the active theme: ${active_error#2048: }"
        theme_args=(follow active)
        theme_preview="$active_json"
        theme_note="follows the active theme $(source_field path "$active_json") ($(json_field name "$active_json"))"
    elif [[ -z "$current_json" ]]; then
        if [[ -n "$active_json" ]]; then
            theme_args=(follow active)
            theme_preview="$active_json"
            theme_note="follows the active theme $(source_field path "$active_json") ($(json_field name "$active_json"))"
        else
            theme_args=(refresh)
            theme_preview="$(theme_command refresh --dry-run)"
            theme_note="default theme ($(json_field name "$theme_preview"))"
            [[ "$follow_mode" == never ]] || theme_note+="; no active theme link found"
        fi
    elif (( has_source )) && theme_preview="$(theme_command refresh --dry-run 2>/dev/null)"; then
        # Keep the user's own source binding and pick up its latest colors.
        theme_args=(refresh)
        theme_note="keeps following $(source_field path "$current_json") ($(json_field name "$theme_preview"))"
    else
        theme_preview=""
        if (( has_source )); then
            theme_note="keeps $(json_field name "$current_json"); its source cannot be read right now"
        else
            theme_note="keeps the current palette ($(json_field name "$current_json"))"
        fi
        if [[ ! -f "$themed_icon" ]] && (( ! has_source )); then
            theme_args=(refresh)
            theme_preview="$current_json"
        fi
    fi
}

backup_file() {
    local file="$1" backup
    [[ -f "$file" ]] || return 0
    backup="$file.$app_name-backup.$stamp.$$"
    [[ ! -e "$backup" && ! -L "$backup" ]] || fail "Backup already exists: $backup"
    cp -p -- "$file" "$backup"
    backups+=("$backup")
}

new_temporary() {
    local directory="$1" temporary
    mkdir -p -- "$directory"
    temporary="$(mktemp "$directory/.$app_name-install.XXXXXX")"
    temporaries+=("$temporary")
    printf '%s\n' "$temporary"
}

# Atomically move a prepared temporary file over its destination. Identical files are
# left untouched; changed ones are backed up first unless the second argument is 0.
commit_file() {
    local temporary="$1" destination="$2" keep_backup="${3:-1}"
    if [[ -f "$destination" ]] && cmp -s -- "$temporary" "$destination"; then
        rm -f -- "$temporary"
        committed=0
        return 0
    fi
    (( keep_backup )) && backup_file "$destination"
    mv -fT -- "$temporary" "$destination"
    committed=1
}

install_executable() {
    local temporary
    temporary="$(new_temporary "${executable%/*}")"
    install -m755 -- "$build_binary" "$temporary"
    commit_file "$temporary" "$executable" 0
}

install_fallback_icon() {
    local temporary
    temporary="$(new_temporary "${fallback_icon%/*}")"
    install -m644 -- "$project_directory/assets/$desktop_id.svg" "$temporary"
    commit_file "$temporary" "$fallback_icon"
}

desktop_entry() {
    awk -v executable="$executable" -v config="$config_directory" -v icon="$themed_icon" '
        /^Exec=/ { print "Exec=\"" executable "\" --config-dir \"" config "\""; next }
        /^Icon=/ { print "Icon=" icon; next }
        { print }' "$project_directory/assets/$desktop_id.desktop"
}

install_desktop_entry() {
    local temporary
    temporary="$(new_temporary "${desktop_file%/*}")"
    desktop_entry > "$temporary"
    chmod 644 -- "$temporary"
    commit_file "$temporary" "$desktop_file"
    mkdir -p -- "${registered%/*}"
    if [[ ! -L "$registered" ]]; then
        # A plain file here usually comes from an earlier `cmake --install`.
        backup_file "$registered"
        ln -sfT -- "$desktop_file" "$registered"
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q -- "${registered%/*}" || warn 'update-desktop-database failed'
    fi
}

# Rewrite the app list: keep every unrelated line as is, point the first entry for this
# app at the themed icon and label (extra fields survive), drop duplicates, or append one.
updated_apps_list() {
    if [[ -f "$apps_list" ]]; then
        awk -v id="$desktop_id.desktop" -v icon="$themed_icon" -v label="$app_label" '
            BEGIN { FS = OFS = "|" }
            $1 == id { if (!found) { $2 = icon; $3 = label; print; found = 1 } next }
            { print }
            END { if (!found) print id, icon, label }' "$apps_list"
    else
        printf '%s\n' "$apps_entry"
    fi
}

plan_apps_list() {
    if (( ! use_apps_list )); then
        apps_list_note="${apps_list_note:-disabled}"
    elif [[ ! -f "$apps_list" ]]; then
        apps_list_note="$apps_list (new file)"
    elif cmp -s -- "$apps_list" <(updated_apps_list); then
        apps_list_note="$apps_list (already registered)"
    elif awk -F '|' -v id="$desktop_id.desktop" '$1 == id { found = 1 } END { exit !found }' "$apps_list"; then
        apps_list_note="$apps_list (entry updated)"
    else
        apps_list_note="$apps_list (entry added)"
    fi
}

register_app() {
    (( use_apps_list )) || return 0
    local temporary
    temporary="$(new_temporary "${apps_list%/*}")"
    updated_apps_list > "$temporary"
    if [[ -f "$apps_list" ]]; then
        chmod --reference="$apps_list" -- "$temporary"
    else
        chmod 644 -- "$temporary"
    fi
    commit_file "$temporary" "$apps_list"
}

configure_theme() {
    if (( ${#theme_args[@]} )); then
        local current=""
        [[ -f "$theme_file" ]] && current="$(< "$theme_file")"
        if [[ -n "$current" && "$current" == "$theme_preview" && -f "$themed_icon" ]]; then
            theme_note+="; unchanged"
        else
            [[ -n "$current" && "$current" != "$theme_preview" ]] && backup_file "$theme_file"
            "$executable" --config-dir "$config_directory" theme "${theme_args[@]}" >/dev/null
        fi
    fi
    # The launcher entry points at icon.svg; the game redraws it in theme colors on start.
    if [[ ! -f "$themed_icon" ]]; then
        local temporary
        temporary="$(new_temporary "$config_directory")"
        install -m644 -- "$project_directory/assets/$desktop_id.svg" "$temporary"
        mv -fT -- "$temporary" "$themed_icon"
    fi
}

print_plan() {
    printf 'Dry run: nothing will be changed.\n'
    printf '  %-15s %s\n' \
        'Binary' "$build_binary ($version)" \
        'Executable' "$executable" \
        'Fallback icon' "$fallback_icon" \
        'Desktop entry' "$desktop_file" \
        'Registration' "$registered -> $desktop_file" \
        'Exec' "\"$executable\" --config-dir \"$config_directory\"" \
        'Icon' "$themed_icon" \
        'App list' "$apps_list_note" \
        'Theme' "$theme_note"
}

print_summary() {
    printf 'Installed %s.\n' "$version"
    printf '  %-15s %s\n' \
        'Executable' "$executable" \
        'Desktop entry' "$registered" \
        'Icon' "$themed_icon (redrawn with the theme)" \
        'App list' "$apps_list_note" \
        'Theme' "$theme_note" \
        'Configuration' "$config_directory"
    local backup
    for backup in ${backups[@]+"${backups[@]}"}; do
        printf '  %-15s %s\n' 'Backup' "$backup"
    done
    [[ ":$PATH:" == *":$install_prefix/bin:"* ]] ||
        printf 'Add %s to PATH to run %s from a terminal.\n' "$install_prefix/bin" "$app_name"
}

main() {
    parse_options "$@"
    set_paths
    validate
    plan_apps_list
    if (( dry_run )); then
        print_plan
        return 0
    fi
    install_executable
    install_fallback_icon
    install_desktop_entry
    register_app
    configure_theme
    print_summary
}

main "$@"
