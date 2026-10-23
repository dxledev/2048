/**
 * @file cli.cpp
 * @brief Command-line parsing and theme management subcommands.
 *
 * Every theme mutation is validated in full before theme.json is replaced
 * atomically; a running window notices the new file and repaints at once.
 * Read-only commands and --dry-run never touch the user's files.
 */
#include "cli.h"
#include "core/game.h"
#include "core/storage.h"
#include "core/theme.h"
#include "core/theme_source.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <stdexcept>

namespace g2048 {
namespace {

void requireCount(const QStringList &arguments, int count) {
    if (arguments.size() != count)
        throw std::invalid_argument("Invalid arguments. Run 2048 --help for usage.");
}

QString homeRelative(const QString &path) {
    const auto home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

}

Options parseOptions(QStringList arguments) {
    Options result;
    for (int index = 0; index < arguments.size(); ++index) {
        QString argument = arguments[index];
        // Accept --option=value as well as --option value.
        if (argument.startsWith("--") && argument.contains('=')) {
            const auto separator = argument.indexOf('=');
            arguments.insert(index + 1, argument.mid(separator + 1));
            argument = argument.left(separator);
        }
        if (argument == "--config-dir" || argument == "--size") {
            if (index + 1 >= arguments.size() || arguments[index + 1].isEmpty() || arguments[index + 1].startsWith("--"))
                throw std::invalid_argument((argument + " requires a value").toStdString());
            const auto value = arguments[++index];
            if (argument == "--config-dir") {
                result.configDirectory = QDir(value).absolutePath();
            } else {
                bool valid = false;
                result.size = value.toInt(&valid);
                if (!valid || result.size < Game::minimumSize || result.size > Game::maximumSize)
                    throw std::invalid_argument("--size requires a board size from 3 to 5");
            }
        } else if (argument == "--opaque") {
            result.opaque = true;
        } else if (argument == "--dry-run") {
            result.dryRun = true;
        } else if (argument == "--help" || argument == "-h") {
            result.help = true;
        } else if (argument == "--version") {
            result.version = true;
        } else if (argument.startsWith("--")) {
            throw std::invalid_argument(("Unknown option: " + argument).toStdString());
        } else {
            result.arguments.append(argument);
        }
    }
    if (result.configDirectory.isEmpty())
        result.configDirectory = defaultConfigDirectory();
    return result;
}

void printHelp() {
    QTextStream(stdout) << R"(2048 — slide, merge, and reach the 2048 tile

Usage:
  2048 [--size 3-5] [--opaque] [--config-dir DIRECTORY]
  2048 theme list | keys | path | show
  2048 theme preset NAME                       built-in palette (see theme list)
  2048 theme set KEY=COLOR [KEY=COLOR ...]     edit individual colors
  2048 theme use NAME|DIRECTORY|FILE           follow a named theme (see theme list)
  2048 theme follow active [LINK]              follow whichever theme a link points to
  2048 theme follow static [NAME|DIRECTORY]    follow a named theme directory
  2048 theme follow file FILE|DIRECTORY        follow any palette file
  2048 theme refresh                           re-read the followed theme now
  2048 theme detach                            keep the current colors, stop following
  2048 theme import FILE                       copy a palette once
  2048 theme export FILE

Colors: #RRGGBB, #AARRGGBB (Qt order, alpha first), or Qt.rgba(r, g, b, a) with
channels from 0 to 1, e.g. 'background=Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)'.
Only the background keeps its alpha, which makes the window translucent where the
platform supports it; elsewhere alpha is dropped and the RGB channels are kept.
Tile overrides (tile_2 ... tile_2048, tile_super) are optional; 'tile_8=' clears one.

Palette files: QML color singletons (Colors.qml) and JSON palettes, including
Material-style keys (primary, on_surface, ...) and light/dark variants.
Followed files are watched; the window recolors as soon as they change.

Every change is written atomically to theme.json and live-reloaded by open windows.
All commands accept --config-dir DIRECTORY (or GAME_2048_CONFIG_DIR) and --dry-run,
which prints the resulting JSON without writing anything.

By default the game follows your active theme link in ~/.config/themes (or THEMES_DIR)
and falls back to the midnight preset when there is none.

Keys: arrows, WASD, or HJKL move · swipe · U / Ctrl+Z undo · N new game · Esc close
)";
}

int runThemeCommand(const Options &options) {
    if (options.size)
        throw std::invalid_argument("--size applies to the game, not theme commands");
    auto arguments = options.arguments;
    if (arguments.isEmpty() || arguments.takeFirst() != "theme" || arguments.isEmpty())
        throw std::invalid_argument("Expected a theme subcommand. Run 2048 --help.");
    const QString command = arguments.takeFirst();
    const QString path = options.configDirectory + "/theme.json";
    QTextStream output(stdout);
    if (command == "list") {
        requireCount(arguments, 0);
        output << "Presets:\n";
        for (const auto &name : presetNames())
            output << "  " << name << '\n';
        const auto names = staticThemeNames();
        if (!names.isEmpty()) {
            output << "Themes in " << homeRelative(themesDirectory()) << ":\n";
            for (const auto &name : names)
                output << "  " << name << '\n';
        }
        if (const auto link = defaultActiveLink(); !link.isEmpty())
            output << "Active link:\n  " << homeRelative(link) << " -> "
                   << homeRelative(QFileInfo(link).canonicalFilePath()) << '\n';
        return 0;
    }
    if (command == "keys") {
        requireCount(arguments, 0);
        output << colorKeys().join('\n') << '\n' << tileKeys().join('\n') << '\n';
        return 0;
    }
    if (command == "path") {
        requireCount(arguments, 0);
        output << path << '\n';
        return 0;
    }
    const auto current = [&] {
        return QFileInfo::exists(path) ? loadTheme(path) : defaultTheme();
    };
    if (command == "show") {
        requireCount(arguments, 0);
        output << QJsonDocument(current().toJson()).toJson();
        return 0;
    }
    Theme next;
    QString destination = path;
    if (command == "preset") {
        requireCount(arguments, 1);
        next = presetTheme(arguments[0]);
    } else if (command == "set") {
        next = setColors(current(), arguments);
    } else if (command == "use" || command == "follow") {
        if (command == "use")
            arguments.prepend("static");
        if (arguments.isEmpty() || arguments.size() > 2)
            throw std::invalid_argument("Expected theme follow active|static|file [PATH]");
        next = followTheme(arguments[0], arguments.value(1));
        if (QFileInfo(next.sourcePath).canonicalFilePath() == QFileInfo(path).canonicalFilePath()
            || QFileInfo(next.sourcePath).absoluteFilePath() == QFileInfo(path).absoluteFilePath())
            throw std::invalid_argument("A theme cannot follow its own output file");
    } else if (command == "refresh") {
        requireCount(arguments, 0);
        next = refreshSource(current());
    } else if (command == "detach") {
        requireCount(arguments, 0);
        next = current();
        next.sourceKind.clear();
        next.sourcePath.clear();
    } else if (command == "import") {
        requireCount(arguments, 1);
        next = importPalette(QFileInfo(arguments[0]).absoluteFilePath());
    } else if (command == "export") {
        requireCount(arguments, 1);
        next = current();
        next.sourceKind.clear();
        next.sourcePath.clear();
        destination = QFileInfo(arguments[0]).absoluteFilePath();
        if (destination == QFileInfo(path).absoluteFilePath()
            || (QFileInfo::exists(destination) && QFileInfo(destination).canonicalFilePath() == QFileInfo(path).canonicalFilePath()))
            throw std::invalid_argument("Export to a different file than theme.json");
    } else {
        throw std::invalid_argument(("Unknown theme command: " + command).toStdString());
    }
    if (options.dryRun) {
        output << QJsonDocument(next.toJson()).toJson();
    } else {
        writeJson(destination, next.toJson());
        output << destination << '\n';
    }
    return 0;
}

}
