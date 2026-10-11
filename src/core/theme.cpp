/**
 * @file theme.cpp
 * @brief Theme presets, palette validation, tile color derivation, and JSON format.
 *
 * Only the window background may be translucent; every other key is stored
 * opaque so stacked surfaces never compound their transparency. Tile faces are
 * derived once per theme from the cell, accent, secondary, and highlight colors
 * and cached together with a readable digit color for each step.
 */
#include "theme.h"
#include "color.h"
#include "storage.h"

#include <QFileInfo>
#include <algorithm>
#include <stdexcept>

namespace g2048 {
namespace {

const QMap<QString, QStringList> palettes{
    {"midnight", {"#11131a", "#181b24", "#20242f", "#e9ecf4", "#8b93a7", "#7aa2f7", "#0f1420", "#bb9af7", "#f7c873", "#2a2f3d", "#1d212c", "#272c39"}},
    {"paper", {"#f6f2ea", "#fffdf8", "#efe9de", "#3b342c", "#8a7f72", "#e0915a", "#ffffff", "#d9614c", "#e8c24a", "#e3dccf", "#d8cfc1", "#e8e1d5"}},
    {"forest", {"#101819", "#182324", "#1e2d2e", "#edf3ed", "#91a6a2", "#8fd1a8", "#0f1f17", "#e0b973", "#f2e48a", "#2b3a3b", "#1b2829", "#253536"}},
    {"rose", {"#1d1720", "#261e2a", "#2f2534", "#f7eaf1", "#b8a0b2", "#efa7c4", "#2a1320", "#c49bea", "#f4cf8e", "#3b2f40", "#2a2130", "#362b3c"}},
};

[[noreturn]] void invalid(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

QColor readableOn(const QColor &face, const Theme &theme) {
    const auto light = theme.color("text"), dark = opaque(theme.color("background"));
    auto best = contrast(face, light) >= contrast(face, dark) ? light : dark;
    if (contrast(face, best) < 3) {
        const QColor black("#111111"), white("#ffffff");
        best = contrast(face, black) >= contrast(face, white) ? black : white;
    }
    return best;
}

void cacheTiles(Theme &theme) {
    const auto cell = theme.color("cell"), text = theme.color("text"), accent = theme.color("accent");
    const auto secondary = theme.color("secondary"), highlight = theme.color("highlight");
    std::array<QColor, tileSteps> derived{
        mix(cell, text, .16), mix(cell, text, .28), mix(cell, accent, .42), mix(cell, accent, .64),
        mix(cell, accent, .84), accent, mix(accent, secondary, .34), mix(accent, secondary, .67),
        secondary, mix(secondary, highlight, .5), highlight, mix(highlight, text, .45),
    };
    // Palettes whose highlight equals their text color would otherwise repeat the 2048 face.
    auto &beyond = derived[tileSteps - 1];
    if (distance(beyond, highlight) < 28)
        beyond = mix(highlight, opaque(theme.color("background")), .38);
    const auto overrides = tileKeys();
    for (int step = 0; step < tileSteps; ++step) {
        theme.tiles[step] = theme.colors.value(overrides[step], derived[step]);
        theme.tileText[step] = readableOn(theme.tiles[step], theme);
    }
}

}

QString Theme::hex(const QString &key) const {
    return formatColor(color(key));
}

int Theme::step(int value) {
    int step = 0;
    while (value > 2 && step < tileSteps - 1) {
        value >>= 1;
        ++step;
    }
    return step;
}

QStringList colorKeys() {
    return {"background", "surface", "surface_alt", "text", "muted", "accent", "accent_text",
            "secondary", "highlight", "border", "board", "cell"};
}

QStringList tileKeys() {
    QStringList keys;
    for (int value = 2; value <= 2048; value *= 2)
        keys.append("tile_" + QString::number(value));
    keys.append("tile_super");
    return keys;
}

bool keepsAlpha(const QString &key) {
    return key == "background";
}

QStringList presetNames() {
    return palettes.keys();
}

QString defaultPresetName() {
    return "midnight";
}

Theme presetTheme(const QString &name) {
    if (!palettes.contains(name))
        invalid("Unknown preset: " + name + ". Available: " + presetNames().join(", "));
    Theme theme{name, {}, {}, {}};
    const auto keys = colorKeys();
    const auto values = palettes.value(name);
    for (int index = 0; index < keys.size(); ++index)
        theme.colors.insert(keys[index], QColor(values[index]));
    cacheTiles(theme);
    return theme;
}

QJsonObject Theme::toJson() const {
    QJsonObject result;
    for (auto entry = colors.constBegin(); entry != colors.constEnd(); ++entry)
        result.insert(entry.key(), formatColor(entry.value()));
    QJsonObject data{{"version", 1}, {"name", name}, {"colors", result}};
    if (!sourceKind.isEmpty())
        data.insert("source", QJsonObject{{"kind", sourceKind}, {"path", sourcePath}});
    return data;
}

Theme parseTheme(const QJsonObject &data) {
    if (data.value("version").toInt(-1) != 1)
        invalid("Theme version must be 1");
    const QString name = data.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 60)
        invalid("Theme name must contain 1–60 characters");
    if (!data.value("colors").isObject())
        invalid("Theme must contain a colors object");
    const auto colors = data.value("colors").toObject();
    const auto keys = colorKeys(), optional = tileKeys();
    Theme theme{name, {}, {}, {}};
    for (auto entry = colors.constBegin(); entry != colors.constEnd(); ++entry) {
        const bool required = keys.contains(entry.key());
        if (!required && !optional.contains(entry.key()))
            invalid("Unknown color: " + entry.key() + ". Run 2048 theme keys");
        if (!entry.value().isString())
            invalid(entry.key() + ": expected a color string");
        QColor color;
        try {
            color = parseColor(entry.value().toString());
        } catch (const std::invalid_argument &error) {
            invalid(entry.key() + ": " + error.what());
        }
        theme.colors.insert(entry.key(), keepsAlpha(entry.key()) ? color : opaque(color));
    }
    for (const auto &key : keys) {
        if (!theme.colors.contains(key))
            invalid("Theme is missing color: " + key);
    }
    cacheTiles(theme);
    if (data.contains("source")) {
        if (!data.value("source").isObject())
            invalid("Theme source must be an object");
        const auto source = data.value("source").toObject();
        theme.sourceKind = source.value("kind").toString();
        theme.sourcePath = source.value("path").toString();
        if (!QStringList{"active", "static", "file"}.contains(theme.sourceKind)
            || theme.sourcePath.isEmpty() || !QFileInfo(theme.sourcePath).isAbsolute())
            invalid("Theme source needs kind active, static, or file and an absolute path");
    }
    return theme;
}

Theme loadTheme(const QString &path) {
    return parseTheme(readJson(path));
}

Theme setColors(Theme theme, const QStringList &assignments) {
    if (assignments.isEmpty())
        invalid("Provide at least one KEY=COLOR assignment");
    auto data = theme.toJson();
    auto colors = data.value("colors").toObject();
    const auto keys = colorKeys(), optional = tileKeys();
    for (const auto &assignment : assignments) {
        const auto separator = assignment.indexOf('=');
        const auto key = assignment.left(separator).trimmed();
        if (separator < 1 || (!keys.contains(key) && !optional.contains(key)))
            invalid("Expected KEY=COLOR. Run 2048 theme keys for the list of keys");
        const auto value = assignment.mid(separator + 1).trimmed();
        if (value.isEmpty() && optional.contains(key))
            colors.remove(key);
        else
            colors.insert(key, value);
    }
    data.insert("name", "custom");
    data.remove("source");
    data.insert("colors", colors);
    return parseTheme(data);
}

}
