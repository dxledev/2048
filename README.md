# 2048

A small, fast 2048 for the Linux desktop, written in C++20 with Qt 6 Widgets. Slide the tiles, merge equal pairs, and reach 2048. Every color is themeable from the command line, and by default the game follows the theme you already use.

## Features

- **Classic rules.** Tiles slide as far as they can, equal pairs merge once per move, and every move adds a 2 (90%) or a 4. Reach 2048 to win: the board blurs behind an animated “You won!” until you choose **Keep going** for bigger tiles. Closing the dialog another way leaves the board won; a slide reopens it, and undo (Standard and Casual modes) takes you back below 2048.
- **Boards from 3×3 to 5×5**, with a best score kept for each size and mode.
- **Three modes**, chosen in the New game dialog. **Standard** adds three power-ups below the board (or beside it, in a wide window), starting with 2 Undos, 1 Swap, and no Deletes, shown as pips: **Undo** takes back a move, **Swap** exchanges two tiles, and **Delete** clears every tile of a number you pick. Making a 128, 256, or 512 tile earns one more Undo, Swap, or Delete. Uses cap at 2, and undoing a move takes back any use it earned. **Classic** is the original game, with no power-ups and no undo. **Casual** has the same three power-ups with no limit: undo, swap, and delete as often as you like, so their buttons show no pips. The game and best scores survive restarts.
- **Statistics** kept separately for Standard, Classic, and Casual, in four tabs (Overview, Tiles, Games, Style) for every board size or all of them together: games, wins and win rate, best and average score, the fastest win, win streaks, play time, how often each tile was reached, a chart of recent scores, the current game, and move directions. Play time counts only while the window is active, and an idle stretch adds at most a minute. Reset clears only the mode being shown.
- **Animations** for sliding, merging, and new tiles. A new move finishes the running animation first, so input is never dropped.
- **Keyboard and swipe** controls.
- **Resizable window.** The board scales with the window, and the buttons collapse to icons when space is tight. In a wide window the header spreads across it with the scores in the middle, and the board stays centered.
- **Native Wayland**, with a **translucent window** when the theme background has an alpha channel.
- **In-window modal dialogs** over a blurred backdrop instead of separate popup windows.
- **Painter-drawn icons.** Every glyph, and the launcher icon `icon.svg`, is drawn in the theme's colors.
- **Low memory use:** native C++ and Qt Widgets, raster rendering into shared-memory buffers, no GPU, no QML.

## Build and install

You need Qt 6.5+ base (Core, Gui, Widgets, plus Qt Test for the tests), CMake 3.21+, and a C++20 compiler. Native Wayland also needs Qt's Wayland platform plugin (`qt6-wayland` on most distributions).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/2048
```

If Qt Test is not installed, configure with `-DBUILD_TESTING=OFF`. The default build type is `MinSizeRel`.

### Per-user install

```sh
scripts/install-desktop.sh --dry-run   # print the plan, change nothing
scripts/install-desktop.sh
```

The script does the following:

- Installs `~/.local/bin/2048` atomically, along with a fallback icon in `~/.local/share/icons/hicolor`.
- Writes the desktop entry to `~/.local/bin/applications` and links it into `~/.local/share/applications`. Its `Exec` passes `--config-dir`, and its `Icon` points at the themed `icon.svg`, so the launcher icon recolors together with the game.
- Sets up a theme. A fresh configuration follows your active theme link when one exists and otherwise gets the built-in default. A palette you have already chosen is kept, unless you pass `--follow`.
- If `~/.config/apps.list` exists, adds or updates a `io.github.game2048.desktop|ICON|2048` line in it and leaves every other line untouched.

The script backs up any file whose content it changes as `FILE.2048-backup.TIMESTAMP.PID`. Running it again changes nothing.

| Option | Effect |
| --- | --- |
| `--prefix DIR` | Install under `DIR` instead of `~/.local` |
| `--config-dir DIR` | Configuration used by the desktop entry (default `~/.config/2048`) |
| `--build-dir DIR` | Where the built `2048` binary is (default `./build`) |
| `--apps-list FILE` / `--no-apps-list` | Register in another app list, creating it if needed, or skip the list |
| `--follow` / `--no-follow` | Switch to the active theme link even over a chosen palette, or leave an existing theme alone |

Alternatively, `cmake --install build --prefix ~/.local` installs the binary, `io.github.game2048.desktop`, and a scalable icon in the default colors. Keep `~/.local/bin` on your `PATH` so the entry can start `2048`.

## Controls

| Input | Action |
| --- | --- |
| Arrow keys, WASD, HJKL | Slide the tiles |
| Swipe or drag on the board | Slide the tiles |
| U, Ctrl+Z, Backspace | Undo (Standard and Casual modes) |
| X | Swap two tiles |
| R, Delete | Delete tiles by number |
| N | New game (pick a mode and board size) |
| T | Statistics |
| Tab, Shift+Tab, ←/→, 1–4 | Switch tabs in Statistics |
| Enter | Confirm the open dialog |
| Esc | Close the open dialog, or cancel a swap or delete |

The last game resumes on launch. `2048 --size 5` plays on a 5×5 board; a saved game resumes only if it has that size. `--opaque` turns translucency off, and `--config-dir DIR` uses a separate configuration.

## Theming

The game follows your active theme automatically. To override it, use the command line: every command validates the full palette and replaces `theme.json` atomically. It also redraws `icon.svg`, and open windows recolor at once. Add `--dry-run` to any command to print the resulting JSON without writing anything.

```sh
2048 theme list                         # presets, your themes, and the active link
2048 theme keys                         # every color key
2048 theme show                         # the current theme as JSON
2048 theme path                         # where theme.json lives
2048 theme preset paper                 # built in: forest, midnight, paper, rose
2048 theme set 'accent=#7aa2f7' 'tile_2048=#f7c873'
2048 theme set 'background=Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)'
2048 theme set 'tile_2048='             # remove a tile override
2048 theme use ocean                    # follow ~/.config/themes/ocean
2048 theme follow active                # follow the link to your active theme
2048 theme follow file ~/palette.json   # follow any palette file
2048 theme refresh                      # re-read the followed theme now
2048 theme detach                       # keep the colors, stop following
2048 theme import themes/ember.json     # copy a palette once
2048 theme export ~/my-2048.json        # a copy without the source binding
```

Quote color values on the command line, because an unquoted `#` starts a comment.

### Colors

| Format | Example |
| --- | --- |
| `#RRGGBB` | `#0e1e36` |
| `#AARRGGBB` (alpha first, as in Qt) | `#e00e1e36` is `#0e1e36` at alpha `0xe0` |
| `Qt.rgba(r, g, b, a)`, channels from 0 to 1 | `Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)` is `#e00e1e36` |

Only `background` keeps its alpha. On Wayland, that alpha makes the window translucent. Every other key drops alpha and keeps the exact RGB channels, so `accent=#e0ff0000` is stored as `#ff0000`. JSON palette files (but not `theme.json`) use the CSS `#RRGGBBAA` order for 8-digit values, so in a palette file the same color is written `#0e1e36e0`.

### theme.json

The theme lives in `~/.config/2048/theme.json`. That path follows `$XDG_CONFIG_HOME`, and `$GAME_2048_CONFIG_DIR` or `--config-dir` can override it. The `source` block is present only while a theme is followed:

```json
{
    "version": 1,
    "name": "ember",
    "colors": {
        "background": "#e017110e",
        "surface": "#201813",
        "surface_alt": "#2a1f19",
        "text": "#f4e9df",
        "muted": "#a8968a",
        "accent": "#f08a4b",
        "accent_text": "#1d0f06",
        "secondary": "#e2574c",
        "highlight": "#f6c65b",
        "border": "#3a2c24",
        "board": "#251c17",
        "cell": "#30251e",
        "tile_2048": "#ffd76a"
    },
    "source": { "kind": "active", "path": "/home/me/.config/themes/current" }
}
```

| Key | Used for |
| --- | --- |
| `background` | Window background; the only key that may be translucent |
| `surface`, `surface_alt` | Score cards, buttons, and dialogs; hover states |
| `text`, `muted` | Primary text; secondary text and hints |
| `accent`, `accent_text` | Primary button and score gains; text on the accent |
| `secondary`, `highlight` | High tiles; `highlight` is the 2048 tile |
| `border` | Outlines of cards, buttons, and dialogs |
| `board`, `cell` | The board and its empty cells |

Tile faces come from `cell`, `text`, `accent`, `secondary`, and `highlight`: low tiles start near `cell`, middle tiles pass through `accent` and `secondary`, and 2048 is `highlight`. Each tile gets a digit color picked for contrast. To pin a tile's color, set `tile_2`, `tile_4`, …, `tile_2048`, or `tile_super` (4096 and above). Assigning an empty value (`tile_8=`) removes the override. [themes/ember.json](themes/ember.json) is a complete example.

### Following a theme

Named themes are directories in `~/.config/themes/` (or `$THEMES_DIR`). Each one holds a `Colors.qml`, `palette.json`, `colors.json`, or another recognized JSON palette name.

- **QML color singletons:** `property color NAME: "#AARRGGBB"`, `Qt.rgba(...)`, or the name of another color property.
- **JSON palettes:** keys can be flat or nested under `colors`, `colours`, or `palette`. They can have `dark` and `light` variants, chosen by `mode`, and Material-style names (`primary`, `on_primary`, `surface_container`, `on_surface_variant`, ...).

A palette needs a background, a foreground, and a primary or accent color. The game derives everything else.

- `theme use NAME` follows one named theme.
- `theme follow active [LINK]` follows a symlink that points at your active theme. Retarget the link, for example `ln -sfn ~/.config/themes/ocean ~/.config/themes/current`, and every open window switches. Without `LINK`, the game uses the active-theme link in your themes directory, such as `~/.config/themes/current`.
- `theme follow file FILE|DIRECTORY` follows any palette: a JSON palette (`palette.json`, `colors.json`, or another `*.json` file passed explicitly) or a QML file.

Followed sources are watched for edits, atomic replacements, and relinked symlinks. Changes appear in open windows almost immediately, and the resolved colors are saved back to `theme.json`. A missing or half-written palette keeps the last valid colors. `preset`, `set`, and `import` stop following; `detach` stops following and keeps the current colors.

### Launcher icon

`icon.svg` sits beside `theme.json` and is redrawn whenever the theme changes, from the command line or in a running window. Point a desktop entry's `Icon=` at it to get a launcher icon that matches the game; `scripts/install-desktop.sh` does this for you.

## Files and environment

| Path or variable | Purpose |
| --- | --- |
| `~/.config/2048/game.json` | Current game, undo history, and best score per board size for each mode |
| `~/.config/2048/stats.json` | Standard statistics: lifetime totals, the last 30 finished games, and the current game's record |
| `~/.config/2048/stats-casual.json` | Casual statistics, in the same format |
| `~/.config/2048/stats-classic.json` | Classic statistics, in the same format |
| `~/.config/2048/theme.json` | Palette and the followed source |
| `~/.config/2048/icon.svg` | Launcher icon in the theme's colors |
| `GAME_2048_CONFIG_DIR` | Use another configuration directory (same as `--config-dir`) |
| `XDG_CONFIG_HOME` | Base for the configuration and themes directories (default `~/.config`) |
| `THEMES_DIR` | Directory of named themes (default `$XDG_CONFIG_HOME/themes`) |
| `QT_QPA_PLATFORM` | Override the platform; Wayland is chosen when `WAYLAND_DISPLAY` is set |
| `QT_WAYLAND_CLIENT_BUFFER_INTEGRATION` | Override the default shared-memory (`shm`) buffers |

All files are written atomically, so a crash or a concurrent reader never sees a half-written file.
