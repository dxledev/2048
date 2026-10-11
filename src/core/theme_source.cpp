/**
 * @file theme_source.cpp
 * @brief Import and resolve palettes from external theme files and directories.
 *
 * Adapters convert QML color singletons and JSON palettes to the canonical
 * palette. QML hex values follow Qt's #AARRGGBB order; JSON palettes use the
 * CSS #RRGGBBAA order. Either way, alpha survives only on the background key,
 * and dropping it never shifts the red, green, and blue channels.
 */
#include "theme_source.h"
#include "color.h"
#include "storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <stdexcept>

namespace g2048 {
namespace {

using Palette = QMap<QString, QColor>;

const QStringList paletteFiles{"Colors.qml", "palette.json", "colors.json", "noctalia.json"};

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

/** Lowercase and strip punctuation so on_background, onBackground, and on-background match. */
QString normalized(const QString &key) {
    QString result;
    for (const QChar character : key) {
        if (character.isLetterOrNumber())
            result.append(character.toLower());
    }
    return result;
}

/** Remove // and block comments in one pass, leaving string literals untouched. */
QString withoutComments(const QString &source) {
    QString result;
    result.reserve(source.size());
    QChar quote;
    for (qsizetype index = 0; index < source.size(); ++index) {
        const QChar character = source[index];
        const QChar next = index + 1 < source.size() ? source[index + 1] : QChar();
        if (!quote.isNull()) {
            result.append(character);
            if (character == '\\' && !next.isNull())
                result.append(source[++index]);
            else if (character == quote)
                quote = QChar();
        } else if (character == '"' || character == '\'') {
            quote = character;
            result.append(character);
        } else if (character == '/' && next == '/') {
            while (index < source.size() && source[index] != '\n')
                ++index;
            result.append('\n');
        } else if (character == '/' && next == '*') {
            const auto end = source.indexOf("*/", index + 2);
            index = end < 0 ? source.size() : end + 1;
            result.append(' ');
        } else {
            result.append(character);
        }
    }
    return result;
}

Palette qmlPalette(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        fail("Cannot read palette: " + path);
    const QString contents = withoutComments(QString::fromUtf8(file.readAll()));
    static const QRegularExpression pattern(
        R"(property\s+color\s+(\w+)\s*:\s*(?:["']([^"'\n]*)["']|(Qt\.rgba\s*\([^)]*\))|([A-Za-z_]\w*)\s*(?:;|$)))",
        QRegularExpression::MultilineOption);
    Palette result;
    QMap<QString, QString> references;
    auto matches = pattern.globalMatch(contents);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto key = normalized(match.captured(1));
        if (!match.captured(4).isEmpty()) {
            references.insert(key, normalized(match.captured(4)));
            continue;
        }
        const auto value = match.captured(2).isEmpty() ? match.captured(3).remove(' ') : match.captured(2);
        try {
            result.insert(key, parseColor(value));
        } catch (const std::invalid_argument &) {
            // Named colors such as "transparent" are skipped; required keys are checked later.
        }
    }
    // Resolve "property color border: primary" style aliases, allowing short chains.
    for (int pass = 0; pass < 4 && !references.isEmpty(); ++pass) {
        for (auto reference = references.begin(); reference != references.end();) {
            if (result.contains(reference.value())) {
                result.insert(reference.key(), result.value(reference.value()));
                reference = references.erase(reference);
            } else {
                ++reference;
            }
        }
    }
    return result;
}

QColor cssColor(QString text) {
    static const QRegularExpression rgba("^#?([0-9a-fA-F]{6})([0-9a-fA-F]{2})$");
    if (const auto match = rgba.match(text); match.hasMatch()) {
        QColor color = parseColor(match.captured(1));
        color.setAlpha(match.captured(2).toInt(nullptr, 16));
        return color;
    }
    return parseColor(text);
}

Palette jsonPalette(QJsonObject data) {
    const QString mode = data.value("mode").toString();
    for (const auto *nested : {"colours", "colors", "palette"}) {
        if (data.value(nested).isObject()) {
            data = data.value(nested).toObject();
            break;
        }
    }
    if (data.value("dark").isObject() || data.value("light").isObject()) {
        const auto chosen = data.value("mode").toString(mode.isEmpty() ? "dark" : mode);
        data = data.value(data.value(chosen).isObject() ? chosen : (chosen == "dark" ? "light" : "dark")).toObject();
    }
    static const QRegularExpression hex("^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
    Palette result;
    for (auto value = data.constBegin(); value != data.constEnd(); ++value) {
        const QString text = value.value().toString().trimmed();
        if (hex.match(text).hasMatch())
            result.insert(normalized(value.key()), cssColor(text));
        else if (text.startsWith("Qt.rgba") || text.startsWith("rgba("))
            result.insert(normalized(value.key()), parseColor(text));
    }
    return result;
}

std::optional<QColor> find(const Palette &palette, const QStringList &keys) {
    for (const auto &key : keys) {
        if (palette.contains(key))
            return palette.value(key);
    }
    return std::nullopt;
}

QColor pick(const Palette &palette, const QStringList &keys) {
    if (const auto color = find(palette, keys))
        return *color;
    fail("Palette is missing " + keys.join(" / "));
}

Theme mapPalette(const Palette &palette, const QString &name) {
    if (palette.isEmpty())
        fail("No colors found in palette");
    const auto background = pick(palette, {"background", "base", "m3background", "msurface", "surface"});
    const auto base = opaque(background);
    const auto text = opaque(pick(palette, {"foreground", "onbackground", "text", "monsurface", "onsurface"}));
    const auto accent = opaque(pick(palette, {"primary", "accent", "mprimary", "m3primary"}));
    const auto outline = opaque(find(palette, {"outline", "border", "moutline"}).value_or(accent));
    auto surface = opaque(find(palette, {"surfacecontainerlow", "backgroundalt", "mantle"}).value_or(base));
    if (distance(surface, base) < 6)
        surface = mix(base, text, .045);
    auto alternate = opaque(find(palette, {"surfacecontainer", "backgroundgray", "surface0", "msurfacevariant"}).value_or(surface));
    if (distance(alternate, surface) < 6)
        alternate = mix(surface, text, .07);
    auto muted = opaque(find(palette, {"foregroundinactive", "onsurfacevariant", "monsurfacevariant", "subtext0", "muted"})
                            .value_or(mix(base, text, .65)));
    if (contrast(muted, surface) < 3)
        muted = mix(surface, text, .65);
    const auto secondary = opaque(find(palette, {"secondary", "msecondary", "m3secondary", "mauve", "pink"})
                                      .value_or(mix(accent, text, .4)));
    const auto highlight = opaque(find(palette, {"tertiary", "mtertiary", "m3tertiary", "warning", "yellow", "peach"})
                                      .value_or(mix(secondary, text, .35)));
    auto onAccent = opaque(find(palette, {"onprimary", "monprimary", "m3onprimary"})
                               .value_or(luminance(accent) > .4 ? QColor("#151515") : QColor("#ffffff")));
    if (contrast(onAccent, accent) < 3)
        onAccent = luminance(accent) > .4 ? QColor("#151515") : QColor("#ffffff");
    const auto board = mix(surface, outline, .16);
    const Theme theme{name, {
        {"background", background}, {"surface", surface}, {"surface_alt", alternate}, {"text", text},
        {"muted", muted}, {"accent", accent}, {"accent_text", onAccent}, {"secondary", secondary},
        {"highlight", highlight}, {"border", mix(surface, outline, .28)}, {"board", board},
        {"cell", mix(board, text, .05)},
    }, {}, {}};
    // Round-trip through JSON so the cached tiles and 8-bit channels match a reload exactly.
    return parseTheme(theme.toJson());
}

QString configHome() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString expandedPath(QString path) {
    if (path == "~" || path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/** Return the palette file for a source path: the file itself or the first known file in a directory. */
QString paletteFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir())
        return path;
    for (const auto &file : paletteFiles) {
        if (QFileInfo(path + "/" + file).isFile())
            return path + "/" + file;
    }
    fail("No palette file (Colors.qml or a JSON palette) in " + path);
}

QString sourcePath(const QString &kind, const QString &requested) {
    if (kind == "active") {
        const auto link = requested.isEmpty() ? defaultActiveLink() : expandedPath(requested);
        if (link.isEmpty())
            fail("No active theme link found in " + themesDirectory() + "; pass one explicitly");
        return link;
    }
    if (kind == "static") {
        const QString name = requested.isEmpty() ? "current" : requested;
        if (!name.contains('/') && name != "." && name != ".." && !name.startsWith('~'))
            return themesDirectory() + "/" + name;
        return expandedPath(name);
    }
    if (requested.isEmpty())
        fail("theme follow file needs a FILE or DIRECTORY");
    return expandedPath(requested);
}

void collectWatchPaths(const QString &path, QSet<QString> &paths, int depth) {
    if (depth > 8)
        return;
    const QSet<QString> skipped{"/", QDir::rootPath(), QFileInfo("/home").canonicalFilePath(),
                                QFileInfo(QDir::homePath()).canonicalFilePath()};
    for (QString current = QDir::cleanPath(path);;) {
        const QFileInfo info(current);
        if (info.exists()) {
            const auto canonical = info.canonicalFilePath();
            if (!canonical.isEmpty() && !skipped.contains(canonical))
                paths.insert(canonical);
        }
        // Watch the directory that holds each link in a chain, so relinking is noticed.
        if (info.isSymLink())
            collectWatchPaths(info.symLinkTarget(), paths, depth + 1);
        const auto parent = QFileInfo(current).absolutePath();
        if (parent == current || skipped.contains(QFileInfo(parent).canonicalFilePath()) || parent == QDir::homePath())
            break;
        current = parent;
    }
}

}

QString themesDirectory() {
    const auto override = qEnvironmentVariable("THEMES_DIR");
    return override.isEmpty() ? configHome() + "/themes" : expandedPath(override);
}

QString defaultActiveLink() {
    for (const auto *name : {".caelestia-use", "current"}) {
        // exists() follows links, so a dangling link never hides a valid fallback.
        const auto path = themesDirectory() + "/" + name;
        if (QFileInfo(path).exists())
            return path;
    }
    return {};
}

QStringList staticThemeNames() {
    QStringList names;
    const QDir directory(themesDirectory());
    for (const auto &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name.startsWith('.') || name == "current")
            continue;
        for (const auto &file : paletteFiles) {
            if (QFileInfo(directory.filePath(name + "/" + file)).isFile()) {
                names.append(name);
                break;
            }
        }
    }
    return names;
}

Theme importPalette(const QString &path, const QString &name) {
    const auto file = paletteFile(path);
    const QString paletteName = (name.isEmpty() ? QFileInfo(path).completeBaseName() : name).left(60);
    // A link may carry any name, so the format comes from the file it points at.
    const auto target = QFileInfo(file).canonicalFilePath();
    if ((target.isEmpty() ? file : target).endsWith(".qml", Qt::CaseInsensitive))
        return mapPalette(qmlPalette(file), paletteName);
    const auto data = readJson(file);
    if (data.value("version").toInt() == 1 && data.value("colors").toObject().contains("cell")) {
        auto theme = parseTheme(data);
        theme.sourceKind.clear();
        theme.sourcePath.clear();
        return theme;
    }
    return mapPalette(jsonPalette(data), paletteName);
}

Theme followTheme(const QString &kind, const QString &path) {
    if (!QStringList{"active", "static", "file"}.contains(kind))
        fail("Unknown source: " + kind + ". Use active, static, or file");
    const auto resolved = sourcePath(kind, path);
    if (!QFileInfo::exists(resolved))
        fail("Theme source does not exist: " + resolved);
    QString name = QFileInfo(QFileInfo(resolved).canonicalFilePath()).fileName();
    if (QFileInfo(QFileInfo(resolved).canonicalFilePath()).isFile())
        name = QFileInfo(QFileInfo(resolved).canonicalFilePath()).dir().dirName();
    if (kind == "file")
        name = QFileInfo(resolved).completeBaseName();
    auto theme = importPalette(resolved, name.isEmpty() ? kind : name.left(60));
    theme.sourceKind = kind;
    theme.sourcePath = resolved;
    return theme;
}

Theme defaultTheme() {
    if (!defaultActiveLink().isEmpty()) {
        try {
            return followTheme("active");
        } catch (const std::exception &) {
            // An unreadable link falls back to the preset; theme refresh can retry later.
        }
    }
    return presetTheme(defaultPresetName());
}

Theme ensureTheme(const QString &path) {
    if (QFileInfo::exists(path))
        return loadTheme(path);
    const auto theme = defaultTheme();
    writeJson(path, theme.toJson());
    return theme;
}

Theme refreshSource(const Theme &theme) {
    if (theme.sourceKind.isEmpty())
        return theme;
    return followTheme(theme.sourceKind, theme.sourcePath);
}

QStringList sourceWatchPaths(const QString &path) {
    if (path.isEmpty())
        return {};
    QSet<QString> paths;
    collectWatchPaths(path, paths, 0);
    if (QFileInfo(path).isDir()) {
        for (const auto &file : paletteFiles)
            collectWatchPaths(path + "/" + file, paths, 0);
    }
    QStringList result(paths.begin(), paths.end());
    result.sort();
    return result;
}

}
