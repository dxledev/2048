/**
 * @file theme_tests.cpp
 * @brief Tests for palette colors, theme validation, palette import, theme sources,
 *        the themed icon, and JSON/session storage.
 *
 * Every file lives in a QTemporaryDir. HOME, XDG_CONFIG_HOME, and
 * GAME_2048_CONFIG_DIR point into a sandbox for the whole run, and THEMES_DIR is
 * set per test to a fixture directory, so the real configuration is never read.
 */
#include "core/color.h"
#include "core/icon.h"
#include "core/storage.h"
#include "core/theme.h"
#include "core/theme_source.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>

using namespace g2048;
namespace fs = std::filesystem;

namespace {

/** The real-world Quickshell palette the importer has to understand. */
const QByteArray realWorldQml = R"(pragma Singleton

import Quickshell
import QtQuick

Singleton {
    readonly property color background: "#e00e1e36"
    readonly property color backgroundAlt: "#0E1E36"
    readonly property color backgroundAlpha: Qt.rgba(26 / 255, 42 / 255, 70 / 255, 0.88)
    readonly property color backgroundGray: "#2a3a56"
    readonly property color foreground: "#f4b999"
    readonly property color foregroundInactive: "#fa7e75"
    readonly property color primary: "#9279aa"
    readonly property color muted: "#574f72"
    readonly property color border: "#9279aa"
    readonly property color secondary: "#c85670"
    readonly property color success: "#be6f76"
    readonly property color warning: "#f4b999"
    readonly property color danger: "#d2495b"
}
)";

/** JSON fixture written with single quotes, which keeps the literals readable (and moc-friendly). */
QByteArray js(const char *text) {
    return QByteArray(text).replace('\'', '"');
}

/** Comments, including commented-out color properties, that the importer must skip. */
const QByteArray commentedQml = R"(pragma Singleton
// readonly property color primary: "#ff0000"
import QtQuick

/* Singleton palette.
   readonly property color background: "#ffffff"
   readonly property color foreground: "#000000" */
Singleton {
    readonly property color background: "#0e1e36" // was "#ffffff"
    readonly property color foreground: "#f4b999" /* inline: "#000000" */
    // readonly property color secondary: "#00ff00"
    readonly property color primary: "#9279aa"
    /* readonly property color secondary: "#00ff00" */ readonly property color tertiary: "#c85670"
    /*
    readonly property color primary: "#00ff00"
    */
}
)";

/** A block-comment opener inside a line comment must not start a block comment. */
const QByteArray commentMarkerQml = R"(Singleton {
    // Generated from themes/*.qml
    readonly property color background: "#0e1e36"
    readonly property color foreground: "#f4b999"
    readonly property color primary: "#9279aa"
    /* end of palette */
}
)";

QString canonical(const QString &path) {
    return QFileInfo(path).canonicalFilePath();
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

bool writeJsonFile(const QString &path, const QJsonObject &data) {
    return writeFile(path, QJsonDocument(data).toJson());
}

fs::path nativePath(const QString &path) {
    return fs::path(QFile::encodeName(path).toStdString());
}

/** Create a symlink at @p link whose stored target is @p target (relative targets stay relative). */
bool makeLink(const QString &target, const QString &link) {
    std::error_code error;
    fs::create_symlink(nativePath(target), nativePath(link), error);
    return !error;
}

/** Atomically re-point @p link at @p target the way theme switchers do: new link, then rename over. */
bool relink(const QString &target, const QString &link) {
    const QString temporary = link + ".new";
    std::error_code error;
    fs::remove(nativePath(temporary), error);
    if (!makeLink(target, temporary))
        return false;
    fs::rename(nativePath(temporary), nativePath(link), error);
    return !error;
}

/** A minimal Quickshell-style singleton; each entry is "key: value" with the value written verbatim. */
QByteArray qmlSingleton(const QStringList &properties) {
    QString text = "pragma Singleton\n\nimport QtQuick\n\nSingleton {\n";
    for (const auto &property : properties)
        text += "    readonly property color " + property + "\n";
    return (text + "}\n").toUtf8();
}

/** Write THEMES/name/Colors.qml with a distinctive primary and background. */
bool addTheme(const QString &themes, const QString &name, const QString &primary,
              const QString &background = "#e00e1e36") {
    return writeFile(themes + "/" + name + "/Colors.qml",
                     qmlSingleton({"background: \"" + background + "\"", "foreground: \"#f4b999\"",
                                   "primary: \"" + primary + "\"", "secondary: \"#c85670\""}));
}

/** Human-readable list of the fields in which two themes differ. */
QString themeDiff(const Theme &actual, const Theme &expected) {
    QStringList differences;
    if (actual.name != expected.name)
        differences << "name " + actual.name + " != " + expected.name;
    if (actual.sourceKind != expected.sourceKind || actual.sourcePath != expected.sourcePath)
        differences << "source " + actual.sourceKind + ":" + actual.sourcePath + " != " + expected.sourceKind + ":"
                + expected.sourcePath;
    QStringList keys = actual.colors.keys() + expected.colors.keys();
    keys.removeDuplicates();
    for (const auto &key : keys) {
        const auto a = actual.colors.value(key), b = expected.colors.value(key);
        if (a != b)
            differences << key + " " + (a.isValid() ? a.name(QColor::HexArgb) : "missing") + " != "
                    + (b.isValid() ? b.name(QColor::HexArgb) : "missing")
                    + QString(" (spec %1/%2, rgba64 %3/%4)").arg(a.spec()).arg(b.spec())
                          .arg(quint64(a.rgba64())).arg(quint64(b.rgba64()));
    }
    for (int step = 0; step < tileSteps; ++step) {
        if (actual.tiles[step] != expected.tiles[step] || actual.tileText[step] != expected.tileText[step])
            differences << QString("tile step %1").arg(step);
    }
    return differences.isEmpty() ? QString("themes equal") : differences.join("; ");
}

/** The three colors that identify which side of a dark/light palette was picked. */
QString signature(const Theme &theme) {
    return theme.hex("background") + " " + theme.hex("text") + " " + theme.hex("accent");
}

QJsonObject withColor(QJsonObject data, const QString &key, const QJsonValue &value) {
    auto colors = data.value("colors").toObject();
    if (value.isUndefined())
        colors.remove(key);
    else
        colors.insert(key, value);
    data.insert("colors", colors);
    return data;
}

ino_t inodeOf(const QString &path) {
    struct stat info {};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? info.st_ino : 0;
}

const QByteArray darkMaterial = js("{'background':'#101418','on_background':'#e0e2e8','primary':'#9ecaff'}");
const QByteArray lightMaterial = js("{'background':'#f8f9ff','on_background':'#191c20','primary':'#33618d'}");
const QString darkSignature = "#101418 #e0e2e8 #9ecaff";
const QString lightSignature = "#f8f9ff #191c20 #33618d";

}

class ThemeTests : public QObject {
    Q_OBJECT

    QTemporaryDir sandbox;
    QString home;

    /** Point THEMES_DIR at a fresh canonical fixture directory owned by @p root. */
    QString useThemes(const QTemporaryDir &root) {
        const QString themes = canonical(root.path());
        qputenv("THEMES_DIR", QFile::encodeName(themes));
        return themes;
    }

private slots:
    void initTestCase() {
        QVERIFY(sandbox.isValid());
        home = canonical(sandbox.path()) + "/home";
        QVERIFY(QDir().mkpath(home + "/.config"));
        qputenv("HOME", QFile::encodeName(home));
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(home + "/.config"));
        qputenv("GAME_2048_CONFIG_DIR", QFile::encodeName(home + "/.config/2048"));
        qunsetenv("THEMES_DIR");
        QCOMPARE(QDir::homePath(), home);
        QCOMPARE(themesDirectory(), home + "/.config/themes");
    }

    void cleanup() {
        qunsetenv("THEMES_DIR");
    }

    // ---- color.h ---------------------------------------------------------------------------

    void parseColorAccepts_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("rgb");
        QTest::addColumn<int>("alpha");
        QTest::newRow("#RRGGBB") << "#0e1e36" << "#0e1e36" << 255;
        QTest::newRow("RRGGBB") << "0e1e36" << "#0e1e36" << 255;
        QTest::newRow("uppercase") << "#0E1E36" << "#0e1e36" << 255;
        QTest::newRow("#RGB") << "#abc" << "#aabbcc" << 255;
        QTest::newRow("RGB") << "abc" << "#aabbcc" << 255;
        QTest::newRow("#AARRGGBB alpha first") << "#e00e1e36" << "#0e1e36" << 0xe0;
        QTest::newRow("AARRGGBB") << "e00e1e36" << "#0e1e36" << 0xe0;
        QTest::newRow("transparent hex") << "#000e1e36" << "#0e1e36" << 0;
        QTest::newRow("Qt.rgba divisions") << "Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)" << "#0e1e36" << 224;
        QTest::newRow("rgba fractions") << "rgba(0.5,0.5,0.5,1)" << "#808080" << 255;
        QTest::newRow("rgba bounds") << "rgba(1, 1, 1, 0)" << "#ffffff" << 0;
        QTest::newRow("rgba integers") << "Qt.rgba(0, 1, 0, 1)" << "#00ff00" << 255;
        QTest::newRow("hex whitespace") << "  #0e1e36\t\n" << "#0e1e36" << 255;
        QTest::newRow("rgba whitespace") << "  Qt.rgba( 14/255 ,30 / 255,  54/255 , 0.88 ) " << "#0e1e36" << 224;
    }
    void parseColorAccepts() {
        QFETCH(QString, input);
        QFETCH(QString, rgb);
        QFETCH(int, alpha);
        const QColor color = parseColor(input);
        QVERIFY(color.isValid());
        QCOMPARE(QColor(color.red(), color.green(), color.blue()).name(), rgb);
        QCOMPARE(color.alpha(), alpha);
    }

    void parseColorRejects_data() {
        QTest::addColumn<QString>("input");
        for (const char *input : {"", "   ", "#", "#12345", "12345", "#1234", "#1234567", "#123456789", "red",
                                  "#ggg", "#0e1e3g", "0x0e1e36", "##0e1e36", "Qt.rgba(1,2)", "rgba(0.5,0.5,0.5)",
                                  "rgba(0,0,0,0,0)", "rgba(1.5,0,0,1)", "rgba(0,0,0,2)", "rgba(-0.1,0,0,1)",
                                  "rgba(255,0,0,1)", "rgba(1/0,0,0,1)", "Qt.rgba(14 / 0, 0, 0, 1)",
                                  "rgba(1/-2,0,0,1)", "rgba(1/2/3,0,0,1)", "rgba(x,0,0,1)", "rgba(,0,0,1)",
                                  "rgba(nan,0,0,1)", "rgba(inf,0,0,1)", "rgb(0,0,0)", "Qt.rgba(0,0,0,1",
                                  "Qt.hsla(0,0,0,1)", "rgba((1),0,0,1)", "Qt.rgba(0,0,0,1);"})
            QTest::newRow(input) << QString(input);
    }
    void parseColorRejects() {
        QFETCH(QString, input);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parseColor(input));
    }

    void formatColorRoundTrip_data() {
        QTest::addColumn<QColor>("color");
        QTest::addColumn<QString>("text");
        QTest::newRow("opaque") << QColor(0x0e, 0x1e, 0x36) << "#0e1e36";
        QTest::newRow("translucent") << QColor(0x0e, 0x1e, 0x36, 0xe0) << "#e00e1e36";
        QTest::newRow("almost opaque") << QColor(255, 255, 255, 254) << "#feffffff";
        QTest::newRow("clear") << QColor(0, 0, 0, 0) << "#00000000";
        QTest::newRow("low alpha") << QColor(1, 2, 3, 1) << "#01010203";
        QTest::newRow("from rgba()") << parseColor("Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)") << "#e00e1e36";
    }
    void formatColorRoundTrip() {
        QFETCH(QColor, color);
        QFETCH(QString, text);
        QCOMPARE(formatColor(color), text);
        const QColor parsed = parseColor(formatColor(color));
        QCOMPARE(parsed.rgba(), color.rgba());
        QCOMPARE(parsed, color);
        QCOMPARE(formatColor(parsed), text);
    }

    void opaqueKeepsRgb() {
        for (const QRgb rgb : {0x0e1e36u, 0x000000u, 0xffffffu, 0x010203u, 0xfefdfcu, 0x9279aau}) {
            for (const int alpha : {0, 1, 0x7f, 0x80, 0xe0, 0xfe, 0xff}) {
                const QColor color = QColor::fromRgba(qRgba(qRed(rgb), qGreen(rgb), qBlue(rgb), alpha));
                const QColor solid = opaque(color);
                QCOMPARE(solid.alpha(), 255);
                QCOMPARE(solid.red(), qRed(rgb));
                QCOMPARE(solid.green(), qGreen(rgb));
                QCOMPARE(solid.blue(), qBlue(rgb));
                QCOMPARE(formatColor(solid), QColor(rgb).name());
                QCOMPARE(opaque(solid), solid);
            }
        }
        const QColor wide = QColor::fromRgba64(0x1234, 0x5678, 0x9abc, 0x8000);
        QCOMPARE(opaque(wide).red(), wide.red());
        QCOMPARE(opaque(wide).green(), wide.green());
        QCOMPARE(opaque(wide).blue(), wide.blue());
        QCOMPARE(opaque(wide), QColor(wide.red(), wide.green(), wide.blue()));
    }

    void colorHelpers() {
        const QColor black(0, 0, 0), white(255, 255, 255);
        QVERIFY(qFuzzyCompare(luminance(white), 1.0));
        QVERIFY(qFuzzyIsNull(luminance(black)));
        QVERIFY(qFuzzyCompare(contrast(black, white), 21.0));
        QVERIFY(qFuzzyCompare(contrast(white, black), contrast(black, white)));
        QVERIFY(qFuzzyCompare(contrast(QColor("#9279aa"), QColor("#9279aa")), 1.0));
        QCOMPARE(mix(black, white, 0), black);
        QCOMPARE(mix(black, white, 1), white);
        QCOMPARE(mix(black, white, .5), QColor(128, 128, 128));
        QCOMPARE(mix(QColor(0, 0, 0, 10), QColor(255, 255, 255, 20), .5).alpha(), 255);
        QCOMPARE(distance(QColor("#102030"), QColor("#152a31")), 10);
        QCOMPARE(distance(QColor("#102030"), QColor("#102030")), 0);
        // Alpha never influences the RGB-based helpers.
        QCOMPARE(distance(QColor(1, 2, 3, 0), QColor(1, 2, 3)), 0);
        QVERIFY(qFuzzyCompare(luminance(QColor(255, 255, 255, 0)), 1.0));
    }

    // ---- theme.h ---------------------------------------------------------------------------

    void keysAndPresets() {
        const auto keys = colorKeys();
        QCOMPARE(keys.size(), 12);
        QCOMPARE(keys.first(), QString("background"));
        QCOMPARE(keys.size(), QSet<QString>(keys.begin(), keys.end()).size());
        const auto tiles = tileKeys();
        QCOMPARE(tiles.size(), tileSteps);
        QCOMPARE(tiles.first(), QString("tile_2"));
        QCOMPARE(tiles[1], QString("tile_4"));
        QCOMPARE(tiles[10], QString("tile_2048"));
        QCOMPARE(tiles.last(), QString("tile_super"));
        for (int step = 0; step < tileSteps - 1; ++step)
            QCOMPARE(Theme::step(tiles[step].mid(5).toInt()), step);
        QVERIFY(keepsAlpha("background"));
        for (const auto &key : keys + tiles) {
            if (key != "background")
                QVERIFY2(!keepsAlpha(key), qPrintable(key));
        }
        const auto names = presetNames();
        for (const char *name : {"forest", "midnight", "paper", "rose"})
            QVERIFY2(names.contains(name), name);
        QVERIFY(names.contains(defaultPresetName()));
        QCOMPARE(defaultPresetName(), QString("midnight"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, presetTheme("nope"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, presetTheme(""));
        for (const auto &name : names) {
            const auto theme = presetTheme(name);
            QCOMPARE(theme.name, name);
            QVERIFY(theme.sourceKind.isEmpty());
            QVERIFY(theme.sourcePath.isEmpty());
            QCOMPARE(QStringList(theme.colors.keys()), [&] { auto sorted = keys; sorted.sort(); return sorted; }());
            for (const auto &key : keys) {
                QVERIFY(theme.color(key).isValid());
                QCOMPARE(theme.color(key).alpha(), 255);
                QCOMPARE(theme.hex(key), theme.color(key).name());
            }
        }
    }

    void parseThemeAlpha() {
        auto data = presetTheme("midnight").toJson();
        data = withColor(data, "background", "#e00e1e36");
        data = withColor(data, "text", "#80f4b999");
        data = withColor(data, "accent", "Qt.rgba(146 / 255, 121 / 255, 170 / 255, 0.5)");
        data = withColor(data, "border", "rgba(0.5, 0.5, 0.5, 0)");
        data = withColor(data, "surface", "#00181b24");
        data = withColor(data, "tile_8", "#40123456");
        const auto theme = parseTheme(data);
        QCOMPARE(theme.color("background").alpha(), 0xe0);
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        QCOMPARE(theme.hex("text"), QString("#f4b999"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("border"), QString("#808080"));
        QCOMPARE(theme.hex("surface"), QString("#181b24"));
        QCOMPARE(theme.hex("tile_8"), QString("#123456"));
        QCOMPARE(theme.tiles[Theme::step(8)], QColor("#123456"));
        for (auto entry = theme.colors.constBegin(); entry != theme.colors.constEnd(); ++entry) {
            if (entry.key() != "background")
                QVERIFY2(entry.value().alpha() == 255, qPrintable(entry.key()));
        }
        for (int step = 0; step < tileSteps; ++step) {
            QCOMPARE(theme.tiles[step].alpha(), 255);
            QCOMPARE(theme.tileText[step].alpha(), 255);
        }
        const auto again = parseTheme(theme.toJson());
        QVERIFY2(again == theme, qPrintable(themeDiff(again, theme)));
    }

    void parseThemeRejects_data() {
        QTest::addColumn<QJsonObject>("data");
        const auto base = presetTheme("midnight").toJson();
        auto edit = [&](const QString &key, const QJsonValue &value) {
            auto data = base;
            if (value.isUndefined())
                data.remove(key);
            else
                data.insert(key, value);
            return data;
        };
        QTest::newRow("no version") << edit("version", QJsonValue::Undefined);
        QTest::newRow("version 2") << edit("version", 2);
        QTest::newRow("version string") << edit("version", "1");
        QTest::newRow("no name") << edit("name", QJsonValue::Undefined);
        QTest::newRow("blank name") << edit("name", "   ");
        QTest::newRow("long name") << edit("name", QString(61, 'x'));
        QTest::newRow("no colors") << edit("colors", QJsonValue::Undefined);
        QTest::newRow("colors array") << edit("colors", QJsonArray{"#000000"});
        for (const auto &key : colorKeys())
            QTest::addRow("missing %s", qPrintable(key)) << withColor(base, key, QJsonValue::Undefined);
        QTest::newRow("unknown key") << withColor(base, "foreground", "#ffffff");
        QTest::newRow("tile_1") << withColor(base, "tile_1", "#ffffff");
        QTest::newRow("tile_4096") << withColor(base, "tile_4096", "#ffffff");
        QTest::newRow("case matters") << withColor(base, "Background", "#ffffff");
        QTest::newRow("number value") << withColor(base, "text", 12);
        QTest::newRow("null value") << withColor(base, "text", QJsonValue::Null);
        QTest::newRow("named color") << withColor(base, "text", "red");
        QTest::newRow("bad hex") << withColor(base, "tile_8", "#12345");
        QTest::newRow("source string") << edit("source", "active");
        QTest::newRow("source null") << edit("source", QJsonValue::Null);
        QTest::newRow("source kind") << edit("source", QJsonObject{{"kind", "remote"}, {"path", "/themes/a"}});
        QTest::newRow("source no kind") << edit("source", QJsonObject{{"path", "/themes/a"}});
        QTest::newRow("source relative") << edit("source", QJsonObject{{"kind", "file"}, {"path", "themes/a.json"}});
        QTest::newRow("source empty path") << edit("source", QJsonObject{{"kind", "static"}, {"path", ""}});
        QTest::newRow("source no path") << edit("source", QJsonObject{{"kind", "active"}});
    }
    void parseThemeRejects() {
        QFETCH(QJsonObject, data);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parseTheme(data));
    }

    void parseThemeAcceptsLimits() {
        auto data = presetTheme("midnight").toJson();
        data.insert("name", "  " + QString(60, 'n') + "  ");
        QCOMPARE(parseTheme(data).name, QString(60, 'n'));
        data.insert("name", "x");
        QCOMPARE(parseTheme(data).name, QString("x"));
    }

    void parseThemeSource_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("active") << "active";
        QTest::newRow("static") << "static";
        QTest::newRow("file") << "file";
    }
    void parseThemeSource() {
        QFETCH(QString, kind);
        auto data = presetTheme("rose").toJson();
        data.insert("source", QJsonObject{{"kind", kind}, {"path", "/themes/rose"}});
        const auto theme = parseTheme(data);
        QCOMPARE(theme.sourceKind, kind);
        QCOMPARE(theme.sourcePath, QString("/themes/rose"));
        QCOMPARE(theme.toJson().value("source").toObject().value("kind").toString(), kind);
        const auto again = parseTheme(theme.toJson());
        QVERIFY2(again == theme, qPrintable(themeDiff(again, theme)));
        QVERIFY(!presetTheme("rose").toJson().contains("source"));
    }

    void parseThemeTileOverrides() {
        const auto base = presetTheme("midnight");
        auto data = withColor(base.toJson(), "tile_8", "#123456");
        data = withColor(data, "tile_super", "abcdef");
        const auto theme = parseTheme(data);
        QCOMPARE(theme.colors.value("tile_8"), QColor("#123456"));
        QCOMPARE(theme.tiles[Theme::step(8)], QColor("#123456"));
        QCOMPARE(theme.tiles[tileSteps - 1], QColor("#abcdef"));
        QCOMPARE(theme.tiles[Theme::step(1 << 15)], QColor("#abcdef"));
        for (int step = 0; step < tileSteps; ++step) {
            if (step != Theme::step(8) && step != tileSteps - 1)
                QCOMPARE(theme.tiles[step], base.tiles[step]);
            QVERIFY(contrast(theme.tiles[step], theme.tileText[step]) >= 3);
        }
        QCOMPARE(theme.toJson().value("colors").toObject().value("tile_8").toString(), QString("#123456"));

        auto all = base.toJson();
        const auto keys = tileKeys();
        for (int step = 0; step < tileSteps; ++step)
            all = withColor(all, keys[step], QColor(10 * step, 20 + step, 200 - step).name());
        const auto overridden = parseTheme(all);
        for (int step = 0; step < tileSteps; ++step)
            QCOMPARE(overridden.tiles[step], QColor(10 * step, 20 + step, 200 - step));
    }

    void tileStep_data() {
        QTest::addColumn<int>("value");
        QTest::addColumn<int>("step");
        QTest::newRow("2") << 2 << 0;
        QTest::newRow("4") << 4 << 1;
        QTest::newRow("8") << 8 << 2;
        QTest::newRow("16") << 16 << 3;
        QTest::newRow("1024") << 1024 << 9;
        QTest::newRow("2048") << 2048 << 10;
        QTest::newRow("4096") << 4096 << 11;
        QTest::newRow("8192") << 8192 << 11;
        QTest::newRow("1<<20") << (1 << 20) << 11;
        QTest::newRow("1<<30") << (1 << 30) << 11;
    }
    void tileStep() {
        QFETCH(int, value);
        QFETCH(int, step);
        QCOMPARE(Theme::step(value), step);
    }

    void tileTextContrast_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &name : presetNames())
            QTest::newRow(qPrintable(name)) << name;
    }
    void tileTextContrast() {
        QFETCH(QString, preset);
        const auto theme = presetTheme(preset);
        for (int step = 0; step < tileSteps; ++step) {
            const double ratio = contrast(theme.tiles[step], theme.tileText[step]);
            QVERIFY2(ratio >= 3, qPrintable(QString("step %1: %2 on %3 is %4:1").arg(step)
                .arg(theme.tileText[step].name(), theme.tiles[step].name()).arg(ratio)));
            QVERIFY(theme.tiles[step].isValid());
            QCOMPARE(theme.tiles[step].alpha(), 255);
        }
    }

    void tileTextFallbackContrast() {
        // A light theme with mid-gray text: neither the text nor the background reaches 3:1 on
        // mid-luminance faces, so readableOn() has to fall back to #111111 or #ffffff. Some face
        // always has at least 4.3:1 against one of them, so every face can get >= 3:1.
        const auto base = setColors(presetTheme("paper"), {"text=#555555"});
        QStringList failing;
        for (int gray = 0; gray < 256; ++gray) {
            const auto theme = setColors(base, {"tile_2=" + QColor(gray, gray, gray).name()});
            const double ratio = contrast(theme.tiles[0], theme.tileText[0]);
            if (ratio < 3)
                failing << QString("%1 -> %2 (%3:1)").arg(theme.tiles[0].name(), theme.tileText[0].name())
                               .arg(ratio, 0, 'f', 2);
        }
        QVERIFY2(failing.isEmpty(), qPrintable(failing.join(", ")));
    }

    void setColorsAssignments() {
        const auto base = presetTheme("midnight");
        const auto theme = setColors(base, {"accent=#ff0000", " text = 0e1e36 ", "background=#800e1e36",
                                            "tile_8=#123456", "border=Qt.rgba(1, 1, 1, 0.5)", "muted=#abc"});
        QCOMPARE(theme.name, QString("custom"));
        QCOMPARE(theme.hex("accent"), QString("#ff0000"));
        QCOMPARE(theme.hex("text"), QString("#0e1e36"));
        QCOMPARE(theme.hex("background"), QString("#800e1e36"));
        QCOMPARE(theme.hex("border"), QString("#ffffff"));
        QCOMPARE(theme.hex("muted"), QString("#aabbcc"));
        QCOMPARE(theme.tiles[Theme::step(8)], QColor("#123456"));
        for (const char *key : {"surface", "surface_alt", "accent_text", "secondary", "highlight", "board", "cell"})
            QCOMPARE(theme.color(key), base.color(key));
        QCOMPARE(base.hex("accent"), QString("#7aa2f7"));
        QCOMPARE(base.name, QString("midnight"));
        // Later assignments win.
        QCOMPARE(setColors(base, {"accent=#ff0000", "accent=#00ff00"}).hex("accent"), QString("#00ff00"));
        const auto again = parseTheme(theme.toJson());
        QVERIFY2(again == theme, qPrintable(themeDiff(again, theme)));
    }

    void setColorsClearsOverride() {
        const auto base = presetTheme("forest");
        const auto overridden = setColors(base, {"tile_8=#123456", "tile_super=#654321"});
        QVERIFY(overridden.colors.contains("tile_8"));
        const auto cleared = setColors(overridden, {"tile_8="});
        QVERIFY(!cleared.colors.contains("tile_8"));
        QVERIFY(cleared.colors.contains("tile_super"));
        QCOMPARE(cleared.tiles[Theme::step(8)], base.tiles[Theme::step(8)]);
        QCOMPARE(cleared.tiles[tileSteps - 1], QColor("#654321"));
        const auto none = setColors(cleared, {"tile_super= "});
        QCOMPARE(none.colors, base.colors);
        QCOMPARE(none.tiles, base.tiles);
        QCOMPARE(none.tileText, base.tileText);
        // Clearing an override that is not set is harmless.
        QCOMPARE(setColors(base, {"tile_2="}).colors, base.colors);
    }

    void setColorsRejects_data() {
        QTest::addColumn<QStringList>("assignments");
        QTest::newRow("nothing") << QStringList{};
        QTest::newRow("no separator") << QStringList{"accent"};
        QTest::newRow("no key") << QStringList{"=#ffffff"};
        QTest::newRow("blank key") << QStringList{"  =#ffffff"};
        QTest::newRow("colon") << QStringList{"accent:#ffffff"};
        QTest::newRow("unknown key") << QStringList{"foreground=#ffffff"};
        QTest::newRow("tile_1") << QStringList{"tile_1=#ffffff"};
        QTest::newRow("bad color") << QStringList{"accent=red"};
        QTest::newRow("short hex") << QStringList{"background=#12345"};
        QTest::newRow("clear required") << QStringList{"text="};
        QTest::newRow("one bad of many") << QStringList{"accent=#ff0000", "bogus"};
    }
    void setColorsRejects() {
        QFETCH(QStringList, assignments);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, setColors(presetTheme("midnight"), assignments));
    }

    void setColorsDropsSource() {
        auto source = presetTheme("forest");
        source.sourceKind = "static";
        source.sourcePath = "/themes/forest";
        const auto theme = setColors(source, {"accent=#ff0000"});
        QCOMPARE(theme.name, QString("custom"));
        QVERIFY(theme.sourceKind.isEmpty());
        QVERIFY(theme.sourcePath.isEmpty());
        QVERIFY(!theme.toJson().contains("source"));
        QCOMPARE(theme.color("cell"), source.color("cell"));
    }

    void presetJsonRoundTrip_data() {
        QTest::addColumn<QString>("preset");
        for (const auto &name : presetNames())
            QTest::newRow(qPrintable(name)) << name;
    }
    void presetJsonRoundTrip() {
        QFETCH(QString, preset);
        const auto theme = presetTheme(preset);
        const auto json = theme.toJson();
        QCOMPARE(json.value("version").toInt(), 1);
        QCOMPARE(json.value("name").toString(), preset);
        const auto parsed = parseTheme(json);
        QVERIFY2(parsed == theme, qPrintable(themeDiff(parsed, theme)));
        const auto reparsed = parseTheme(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
        QVERIFY2(reparsed == theme, qPrintable(themeDiff(reparsed, theme)));
        QCOMPARE(parsed.toJson(), json);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("theme.json");
        writeJson(path, json);
        const auto loaded = loadTheme(path);
        QVERIFY2(loaded == theme, qPrintable(themeDiff(loaded, theme)));
    }

    void loadThemeErrors() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadTheme(dir.filePath("missing.json")));
        QVERIFY(writeFile(dir.filePath("broken.json"), "{"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadTheme(dir.filePath("broken.json")));
        QVERIFY(writeJsonFile(dir.filePath("invalid.json"), QJsonObject{{"version", 1}, {"name", "x"}}));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, loadTheme(dir.filePath("invalid.json")));
    }

    // ---- theme_source.h: default theme and theme.json creation ------------------------------

    void defaultThemeWithoutLink() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(defaultActiveLink().isEmpty());
        const auto empty = defaultTheme();
        QVERIFY2(empty == presetTheme("midnight"), qPrintable(themeDiff(empty, presetTheme("midnight"))));
        QCOMPARE(empty.name, QString("midnight"));
        QVERIFY(empty.sourceKind.isEmpty());

        qputenv("THEMES_DIR", QFile::encodeName(themes + "/does-not-exist"));
        QVERIFY(defaultActiveLink().isEmpty());
        QCOMPARE(defaultTheme().name, QString("midnight"));

        // A dangling link is no active theme.
        qputenv("THEMES_DIR", QFile::encodeName(themes));
        QVERIFY(makeLink("gone", themes + "/current"));
        QVERIFY(defaultActiveLink().isEmpty());
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("active"));
        const auto dangling = defaultTheme();
        QCOMPARE(dangling.name, QString("midnight"));
        QVERIFY(dangling.sourceKind.isEmpty());

        // A link to a directory without a palette is unreadable, so the preset is used.
        QVERIFY(QDir().mkpath(themes + "/bare"));
        QVERIFY(relink("bare", themes + "/current"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("active"));
        const auto unreadable = defaultTheme();
        QVERIFY2(unreadable == presetTheme("midnight"), qPrintable(themeDiff(unreadable, presetTheme("midnight"))));
    }

    void defaultThemeFollowsActiveLink() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(makeLink("A", themes + "/current"));
        const auto theme = defaultTheme();
        QCOMPARE(theme.sourceKind, QString("active"));
        QCOMPARE(theme.sourcePath, themes + "/current");
        QCOMPARE(theme.name, QString("A"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        QVERIFY2(theme == followTheme("active"), qPrintable(themeDiff(theme, followTheme("active"))));
    }

    void ensureThemeCreatesDefault() {
        QTemporaryDir root, config;
        QVERIFY(root.isValid() && config.isValid());
        useThemes(root);
        const QString path = config.filePath("nested/2048/theme.json");
        const auto created = ensureTheme(path);
        QVERIFY(QFileInfo(path).isFile());
        QVERIFY2(created == presetTheme("midnight"), qPrintable(themeDiff(created, presetTheme("midnight"))));
        const auto loaded = loadTheme(path);
        QVERIFY2(loaded == created, qPrintable(themeDiff(loaded, created)));
        const auto bytes = readFile(path);
        const auto again = ensureTheme(path);
        QVERIFY2(again == created, qPrintable(themeDiff(again, created)));
        QCOMPARE(readFile(path), bytes);
    }

    void ensureThemeCreatesActive() {
        QTemporaryDir root, config;
        QVERIFY(root.isValid() && config.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(makeLink("A", themes + "/current"));
        const QString path = config.filePath("theme.json");
        const auto created = ensureTheme(path);
        QCOMPARE(created.sourceKind, QString("active"));
        QCOMPARE(created.sourcePath, themes + "/current");
        QCOMPARE(created.hex("accent"), QString("#9279aa"));
        const auto stored = readJson(path);
        QCOMPARE(stored.value("source").toObject().value("kind").toString(), QString("active"));
        QCOMPARE(stored.value("source").toObject().value("path").toString(), themes + "/current");
        const auto loaded = loadTheme(path);
        QVERIFY2(loaded == created, qPrintable(themeDiff(loaded, created)));
        QVERIFY2(refreshSource(loaded) == loaded, qPrintable(themeDiff(refreshSource(loaded), loaded)));
    }

    void ensureThemeKeepsExisting() {
        QTemporaryDir root, config;
        QVERIFY(root.isValid() && config.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(makeLink("A", themes + "/current"));
        const QString path = config.filePath("theme.json");
        const auto custom = setColors(presetTheme("rose"), {"accent=#123456"});
        writeJson(path, custom.toJson());
        const auto bytes = readFile(path);
        const auto loaded = ensureTheme(path);
        QVERIFY2(loaded == custom, qPrintable(themeDiff(loaded, custom)));
        QCOMPARE(readFile(path), bytes);
        // An invalid existing file is reported, never silently replaced.
        QVERIFY(writeFile(path, "{\"version\": 1}"));
        QVERIFY_THROWS_EXCEPTION(std::exception, ensureTheme(path));
        QCOMPARE(readFile(path), QByteArray("{\"version\": 1}"));
    }

    // ---- theme_source.h: QML palettes -------------------------------------------------------

    void qmlImportRealWorld() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath("Colors.qml");
        QVERIFY(writeFile(file, realWorldQml));
        const auto theme = importPalette(file);
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        QCOMPARE(theme.color("background").alpha(), 224);
        QCOMPARE(opaque(theme.color("background")).name(), QString("#0e1e36"));
        QCOMPARE(theme.hex("text"), QString("#f4b999"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("secondary"), QString("#c85670"));
        QCOMPARE(theme.hex("highlight"), QString("#f4b999"));
        QCOMPARE(theme.hex("muted"), QString("#fa7e75"));
        QCOMPARE(theme.hex("surface_alt"), QString("#2a3a56"));
        QCOMPARE(theme.name, QString("Colors"));
        QVERIFY(theme.sourceKind.isEmpty());
        QVERIFY(theme.sourcePath.isEmpty());
        for (const auto &key : colorKeys()) {
            QVERIFY2(theme.colors.contains(key), qPrintable(key));
            if (key != "background")
                QVERIFY2(theme.color(key).alpha() == 255, qPrintable(key));
        }
        for (int step = 0; step < tileSteps; ++step)
            QVERIFY2(contrast(theme.tiles[step], theme.tileText[step]) >= 3, qPrintable(QString::number(step)));
        QVERIFY(contrast(theme.color("accent_text"), theme.color("accent")) >= 3);
        const auto again = parseTheme(theme.toJson());
        QVERIFY2(again == theme, qPrintable(themeDiff(again, theme)));

        const auto named = importPalette(dir.path(), "Night");
        QCOMPARE(named.name, QString("Night"));
        QCOMPARE(named.colors, theme.colors);
        QCOMPARE(importPalette(dir.path()).name, QFileInfo(dir.path()).fileName());
    }

    void qmlImportQtRgbaBackground() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("hex/Colors.qml"), realWorldQml));
        auto rgba = realWorldQml;
        rgba.replace("background: \"#e00e1e36\"", "background: Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)");
        QVERIFY(rgba != realWorldQml);
        QVERIFY(writeFile(dir.filePath("rgba/Colors.qml"), rgba));
        const auto hex = importPalette(dir.filePath("hex"));
        const auto fromRgba = importPalette(dir.filePath("rgba"));
        QCOMPARE(fromRgba.hex("background"), QString("#e00e1e36"));
        QCOMPARE(fromRgba.color("background").alpha(), 224);
        QCOMPARE(fromRgba.colors, hex.colors);
        QCOMPARE(fromRgba.tiles, hex.tiles);
        // Whitespace between rgba and "(" and no spaces around the divisions are both fine.
        auto compact = realWorldQml;
        compact.replace("background: \"#e00e1e36\"", "background: Qt.rgba (14/255,30/255,54/255,0.88);");
        QVERIFY(writeFile(dir.filePath("compact/Colors.qml"), compact));
        QCOMPARE(importPalette(dir.filePath("compact")).colors, hex.colors);
    }

    void qmlImportIgnoresComments() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("Colors.qml"), commentedQml));
        const auto theme = importPalette(dir.filePath("Colors.qml"));
        QCOMPARE(theme.hex("background"), QString("#0e1e36"));
        QCOMPARE(theme.hex("text"), QString("#f4b999"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("highlight"), QString("#c85670"));
        QCOMPARE(theme.color("secondary"), mix(QColor("#9279aa"), QColor("#f4b999"), .4));
    }

    void qmlImportCommentMarkersInsideLineComment() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("Colors.qml"), commentMarkerQml));
        Theme theme;
        bool imported = false;
        try {
            theme = importPalette(dir.filePath("Colors.qml"));
            imported = true;
        } catch (const std::exception &error) {
            qInfo("import failed: %s", error.what());
        }
        QVERIFY(imported);
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
    }

    void qmlImportAliases() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QStringList common{"background: \"#0e1e36\"", "foreground: \"#f4b999\"", "primary: \"#9279aa\""};
        QVERIFY(writeFile(dir.filePath("alias/Colors.qml"), qmlSingleton(common + QStringList{"border: primary"})));
        QVERIFY(writeFile(dir.filePath("explicit/Colors.qml"),
                          qmlSingleton(common + QStringList{"border: \"#9279aa\""})));
        QVERIFY(writeFile(dir.filePath("other/Colors.qml"), qmlSingleton(common + QStringList{"border: \"#00ff00\""})));
        const auto alias = importPalette(dir.filePath("alias"));
        const auto explicitBorder = importPalette(dir.filePath("explicit"));
        QCOMPARE(alias.colors, explicitBorder.colors);
        QVERIFY(importPalette(dir.filePath("other")).color("border") != alias.color("border"));

        // Required keys may be aliases too, defined before or after their target, in short chains.
        QVERIFY(writeFile(dir.filePath("chain/Colors.qml"), qmlSingleton({
            "background: base", "base: \"#0e1e36\"", "foreground: ink;", "ink: \"#f4b999\"",
            "primary: accentColor", "accentColor: brand", "brand: \"#9279aa\"", "secondary: rose", "rose: \"#c85670\"",
        })));
        const auto chain = importPalette(dir.filePath("chain"));
        QCOMPARE(chain.hex("background"), QString("#0e1e36"));
        QCOMPARE(chain.hex("text"), QString("#f4b999"));
        QCOMPARE(chain.hex("accent"), QString("#9279aa"));
        QCOMPARE(chain.hex("secondary"), QString("#c85670"));

        // An alias to an unknown name is ignored rather than fatal.
        QVERIFY(writeFile(dir.filePath("dangling/Colors.qml"), qmlSingleton(common + QStringList{"border: nowhere"})));
        QVERIFY(writeFile(dir.filePath("plain/Colors.qml"), qmlSingleton(common)));
        QCOMPARE(importPalette(dir.filePath("dangling")).colors, importPalette(dir.filePath("plain")).colors);
    }

    void qmlImportRejects() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("empty/Colors.qml"), "Singleton {\n}\n"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, importPalette(dir.filePath("empty")));
        QVERIFY(writeFile(dir.filePath("noprimary/Colors.qml"),
                          qmlSingleton({"background: \"#0e1e36\"", "foreground: \"#f4b999\""})));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, importPalette(dir.filePath("noprimary")));
        QVERIFY(writeFile(dir.filePath("badvalue/Colors.qml"),
                          qmlSingleton({"background: \"#0e1e36\"", "foreground: \"#f4b999\"", "primary: \"#12345\""})));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, importPalette(dir.filePath("badvalue")));
        QVERIFY(writeFile(dir.filePath("range/Colors.qml"), qmlSingleton({"background: Qt.rgba(2, 0, 0, 1)",
                                                                          "foreground: \"#f4b999\"",
                                                                          "primary: \"#9279aa\""})));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, importPalette(dir.filePath("range")));
        QVERIFY_THROWS_EXCEPTION(std::exception, importPalette(dir.filePath("missing/Colors.qml")));
    }

    // ---- theme_source.h: JSON palettes ------------------------------------------------------

    void jsonImport_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");
        const QByteArray snake = js("{'background':'#101418','on_background':'#e0e2e8','primary':'#9ecaff',"
            "'on_primary':'#003258','secondary':'#bbc7db','tertiary':'#d6bee4','surface':'#101418',"
            "'surface_container_low':'#191c20','surface_container':'#1d2024','on_surface':'#e0e2e8',"
            "'on_surface_variant':'#c2c7cf','outline':'#8c9199'}");
        QByteArray camel = snake;
        for (const auto &[from, to] : std::initializer_list<std::pair<const char *, const char *>>{
                 {"on_background", "onBackground"}, {"on_primary", "onPrimary"},
                 {"surface_container_low", "surfaceContainerLow"}, {"surface_container", "surfaceContainer"},
                 {"on_surface_variant", "onSurfaceVariant"}, {"on_surface", "onSurface"}})
            camel.replace(from, to);
        QByteArray kebab = snake;
        kebab.replace('_', '-');
        QTest::newRow("material snake_case") << snake << darkSignature;
        QTest::newRow("material camelCase") << camel << darkSignature;
        QTest::newRow("material kebab-case") << kebab << darkSignature;
        QTest::newRow("on_surface text") << QByteArray(js("{'background':'#101418','on_surface':'#e0e2e8','primary':'#9ecaff'}"))
                                         << darkSignature;
        QTest::newRow("onSurface text") << QByteArray(js("{'background':'#101418','onSurface':'#e0e2e8','primary':'#9ecaff'}"))
                                        << darkSignature;
        QTest::newRow("surface background") << QByteArray(js("{'surface':'#101418','onSurface':'#e0e2e8','primary':'#9ecaff'}"))
                                            << darkSignature;
        QTest::newRow("bare hex") << QByteArray(js("{'background':'101418','foreground':'E0E2E8','primary':'9ecaff'}"))
                                  << darkSignature;
        QTest::newRow("noctalia keys") << QByteArray(js("{'mSurface':'#101418','mOnSurface':'#e0e2e8','mPrimary':'#9ecaff',"
            "'mOnPrimary':'#003258','mSecondary':'#bbc7db','mTertiary':'#d6bee4','mOutline':'#8c9199'}"))
                                       << darkSignature;
        QTest::newRow("nested colours") << QByteArray(js("{'name':'scheme','flavour':'x','mode':'dark','variant':'tonalspot',"
            "'colours':{'background':'101418','onBackground':'e0e2e8','primary':'9ecaff'}}"))
                                        << darkSignature;
        QTest::newRow("nested colors") << QByteArray(js("{'name':'x','colors':") + darkMaterial + "}") << darkSignature;
        QTest::newRow("nested palette") << QByteArray(js("{'palette':") + darkMaterial + "}") << darkSignature;
        QTest::newRow("dark/light no mode") << QByteArray(js("{'dark':") + darkMaterial + js(",'light':") + lightMaterial + "}")
                                            << darkSignature;
        QTest::newRow("dark/light mode dark") << QByteArray(js("{'mode':'dark','dark':") + darkMaterial + js(",'light':")
                                                            + lightMaterial + "}")
                                              << darkSignature;
        QTest::newRow("dark/light mode light") << QByteArray(js("{'mode':'light','dark':") + darkMaterial + js(",'light':")
                                                             + lightMaterial + "}")
                                               << lightSignature;
        QTest::newRow("light only, mode dark") << QByteArray(js("{'mode':'dark','light':") + lightMaterial + "}")
                                               << lightSignature;
        QTest::newRow("dark only, mode light") << QByteArray(js("{'mode':'light','dark':") + darkMaterial + "}")
                                               << darkSignature;
        QTest::newRow("colors/dark+light") << QByteArray(js("{'colors':{'dark':") + darkMaterial + js(",'light':")
                                                         + lightMaterial + "}}")
                                           << darkSignature;
        QTest::newRow("colours/mode light inside") << QByteArray(js("{'colours':{'mode':'light','dark':") + darkMaterial
                                                                 + js(",'light':") + lightMaterial + "}}")
                                                   << lightSignature;
        QTest::newRow("mode light outside colors") << QByteArray(js("{'mode':'light','colors':{'dark':") + darkMaterial
                                                                 + js(",'light':") + lightMaterial + "}}")
                                                   << lightSignature;
        QTest::newRow("non-color values ignored") << QByteArray(js("{'background':'#101418','foreground':'#e0e2e8',"
            "'primary':'#9ecaff','name':'Theme','size':12,'flag':true,'font':'Inter','list':['#ff0000'],"
            "'short':'#f00','nested':{'primary':'#ff0000'},'blank':'','none':null}"))
                                                  << darkSignature;
        QTest::newRow("rgba strings") << QByteArray(js("{'background':'Qt.rgba(16 / 255, 20 / 255, 24 / 255, 1)',"
            "'foreground':'rgba(0.8784313725, 0.8862745098, 0.9098039216, 1)','primary':' #9ecaff '}"))
                                      << darkSignature;
    }
    void jsonImport() {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath("scheme.json");
        QVERIFY(writeFile(file, json));
        QVERIFY(!QJsonDocument::fromJson(json).isNull());
        const auto theme = importPalette(file);
        QCOMPARE(signature(theme), expected);
        QCOMPARE(theme.name, QString("scheme"));
        for (const auto &key : colorKeys())
            QVERIFY2(theme.color(key).isValid() && theme.color(key).alpha() == 255, qPrintable(key));
        const auto again = parseTheme(theme.toJson());
        QVERIFY2(again == theme, qPrintable(themeDiff(again, theme)));
    }

    void jsonImportMaterialMapping() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("m3.json"), js("{'background':'#101418','on_background':'#e0e2e8',"
            "'primary':'#9ecaff','on_primary':'#003258','secondary':'#bbc7db','tertiary':'#d6bee4',"
            "'surface_container_low':'#191c20','on_surface_variant':'#c2c7cf','outline':'#8c9199'}")));
        const auto theme = importPalette(dir.filePath("m3.json"), "Material");
        QCOMPARE(theme.name, QString("Material"));
        QCOMPARE(theme.hex("accent_text"), QString("#003258"));
        QCOMPARE(theme.hex("secondary"), QString("#bbc7db"));
        QCOMPARE(theme.hex("highlight"), QString("#d6bee4"));
        QCOMPARE(theme.hex("surface"), QString("#191c20"));
        QCOMPARE(theme.hex("muted"), QString("#c2c7cf"));
        // camelCase and snake_case spellings are the same palette.
        QVERIFY(writeFile(dir.filePath("camel.json"), js("{'background':'#101418','onBackground':'#e0e2e8',"
            "'primary':'#9ecaff','onPrimary':'#003258','secondary':'#bbc7db','tertiary':'#d6bee4',"
            "'surfaceContainerLow':'#191c20','onSurfaceVariant':'#c2c7cf','outline':'#8c9199'}")));
        QCOMPARE(importPalette(dir.filePath("camel.json")).colors, theme.colors);
    }

    void jsonImportCssAlpha() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("css.json"), js("{'background':'#0e1e36e0','foreground':'#f4b999ff',"
            "'primary':'#9279aa80','secondary':'c8567040','tertiary':'#f4b99900'}")));
        const auto theme = importPalette(dir.filePath("css.json"));
        QCOMPARE(theme.color("background").alpha(), 0xe0);
        QCOMPARE(opaque(theme.color("background")).name(), QString("#0e1e36"));
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        QCOMPARE(theme.hex("text"), QString("#f4b999"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("secondary"), QString("#c85670"));
        QCOMPARE(theme.hex("highlight"), QString("#f4b999"));
        // The same palette written with Qt.rgba() for the translucent background agrees exactly.
        QVERIFY(writeFile(dir.filePath("qt.json"), js("{'background':'Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)',"
            "'foreground':'#f4b999','primary':'#9279aa','secondary':'#c85670','tertiary':'#f4b999'}")));
        const auto qt = importPalette(dir.filePath("qt.json"), "css");
        QCOMPARE(qt.colors, theme.colors);
    }

    void jsonImportRejects_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty object") << QByteArray("{}");
        QTest::newRow("metadata only") << QByteArray(js("{'name':'x','version':2,'mode':'dark'}"));
        QTest::newRow("empty colours") << QByteArray(js("{'colours':{}}"));
        QTest::newRow("empty dark") << QByteArray(js("{'dark':{},'light':{}}"));
        QTest::newRow("short hex only") << QByteArray(js("{'background':'#000','foreground':'#fff','primary':'#f00'}"));
        QTest::newRow("missing primary") << QByteArray(js("{'background':'#000000','foreground':'#ffffff'}"));
        QTest::newRow("missing background") << QByteArray(js("{'foreground':'#ffffff','primary':'#9279aa'}"));
        QTest::newRow("missing text") << QByteArray(js("{'background':'#000000','primary':'#9279aa'}"));
        QTest::newRow("bad rgba") << QByteArray(js("{'background':'rgba(2,0,0,1)','foreground':'#ffffff','primary':'#9279aa'}"));
        QTest::newRow("invalid json") << QByteArray("{");
        QTest::newRow("array") << QByteArray(js("['#000000']"));
    }
    void jsonImportRejects() {
        QFETCH(QByteArray, json);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("palette.json"), json));
        QVERIFY_THROWS_EXCEPTION(std::exception, importPalette(dir.filePath("palette.json")));
        QVERIFY_THROWS_EXCEPTION(std::exception, importPalette(dir.path()));
    }

    void importNativeTheme() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto native = setColors(presetTheme("paper"), {"background=#c0f6f2ea", "tile_8=#123456"});
        native.name = "Saved";
        native.sourceKind = "active";
        native.sourcePath = "/somewhere/current";
        QVERIFY(writeJsonFile(dir.filePath("export.json"), native.toJson()));
        const auto imported = importPalette(dir.filePath("export.json"), "Ignored");
        QCOMPARE(imported.name, QString("Saved"));
        QVERIFY(imported.sourceKind.isEmpty());
        QVERIFY(imported.sourcePath.isEmpty());
        QCOMPARE(imported.colors, native.colors);
        QCOMPARE(imported.tiles, native.tiles);
    }

    void directoryLookupOrder() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dir = root.filePath("lookup");
        QVERIFY(writeFile(dir + "/Colors.qml", qmlSingleton({"background: \"#0e1e36\"", "foreground: \"#f4b999\"",
                                                             "primary: \"#9279aa\""})));
        QVERIFY(writeFile(dir + "/palette.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QVERIFY(writeFile(dir + "/colors.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#0000ff'}")));
        QVERIFY(writeFile(dir + "/noctalia.json", js("{'mSurface':'#000000','mOnSurface':'#ffffff','mPrimary':'#ff00ff'}")));
        QVERIFY(writeFile(dir + "/other.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#ff0000'}")));
        const auto first = importPalette(dir);
        QCOMPARE(first.hex("accent"), QString("#9279aa"));
        QCOMPARE(first.name, QString("lookup"));
        QVERIFY(QFile::remove(dir + "/Colors.qml"));
        QCOMPARE(importPalette(dir).hex("accent"), QString("#00ff00"));
        QVERIFY(QFile::remove(dir + "/palette.json"));
        QCOMPARE(importPalette(dir).hex("accent"), QString("#0000ff"));
        QVERIFY(QFile::remove(dir + "/colors.json"));
        QCOMPARE(importPalette(dir).hex("accent"), QString("#ff00ff"));
        QVERIFY(QFile::remove(dir + "/noctalia.json"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, importPalette(dir));
        // A directory named like a palette file is not a palette.
        QVERIFY(QDir().mkpath(dir + "/Colors.qml"));
        QVERIFY(writeFile(dir + "/palette.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QCOMPARE(importPalette(dir).hex("accent"), QString("#00ff00"));
    }

    // ---- theme_source.h: following sources --------------------------------------------------

    void themesDirectoryOverride() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QCOMPARE(themesDirectory(), themes);
        qputenv("THEMES_DIR", QFile::encodeName(themes + "/sub/../"));
        QCOMPARE(themesDirectory(), themes);
        qputenv("THEMES_DIR", "~/my-themes");
        QCOMPARE(themesDirectory(), home + "/my-themes");
        qunsetenv("THEMES_DIR");
        QCOMPARE(themesDirectory(), home + "/.config/themes");
    }

    void followStaticByName() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(addTheme(themes, "B", "#33aa55", "#101010"));
        const auto theme = followTheme("static", "A");
        QCOMPARE(theme.sourceKind, QString("static"));
        QCOMPARE(theme.sourcePath, themes + "/A");
        QCOMPARE(theme.name, QString("A"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        QCOMPARE(theme.hex("background"), QString("#e00e1e36"));
        QVERIFY2(refreshSource(theme) == theme, qPrintable(themeDiff(refreshSource(theme), theme)));
        QCOMPARE(followTheme("static", "B").hex("accent"), QString("#33aa55"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("static", "missing"));
        // Without a name, "static" means the current selection's directory.
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("static"));
        QVERIFY(makeLink("B", themes + "/current"));
        const auto current = followTheme("static");
        QCOMPARE(current.sourcePath, themes + "/current");
        QCOMPARE(current.name, QString("B"));
        QCOMPARE(current.hex("accent"), QString("#33aa55"));
        // Edits inside the followed directory show up on refresh.
        QVERIFY(addTheme(themes, "A", "#123456"));
        const auto edited = refreshSource(theme);
        QCOMPARE(edited.hex("accent"), QString("#123456"));
        QCOMPARE(edited.sourcePath, theme.sourcePath);
    }

    void followStaticByPath() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        useThemes(root);
        const QString outside = canonical(elsewhere.path());
        QVERIFY(addTheme(outside, "Ocean", "#3366cc"));
        const auto theme = followTheme("static", outside + "/Ocean");
        QCOMPARE(theme.sourceKind, QString("static"));
        QCOMPARE(theme.sourcePath, outside + "/Ocean");
        QCOMPARE(theme.name, QString("Ocean"));
        QCOMPARE(theme.hex("accent"), QString("#3366cc"));
        QCOMPARE(followTheme("static", outside + "/Ocean/").sourcePath, outside + "/Ocean");
        QCOMPARE(followTheme("static", outside + "/./Ocean").sourcePath, outside + "/Ocean");
        QVERIFY2(refreshSource(theme) == theme, qPrintable(themeDiff(refreshSource(theme), theme)));
        // "~" expands to the (sandboxed) home directory.
        QVERIFY(addTheme(home + "/themes", "Tilde", "#cc6633"));
        const auto tilde = followTheme("static", "~/themes/Tilde");
        QCOMPARE(tilde.sourcePath, home + "/themes/Tilde");
        QCOMPARE(tilde.hex("accent"), QString("#cc6633"));
        QVERIFY(QDir(home + "/themes").removeRecursively());
    }

    void followLongName() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        const QString name = QString(70, 'n');
        QVERIFY(addTheme(themes, name, "#9279aa"));
        const auto theme = followTheme("static", name);
        QCOMPARE(theme.name, QString(60, 'n'));
        QCOMPARE(theme.sourcePath, themes + "/" + name);
        QVERIFY2(parseTheme(theme.toJson()) == theme, qPrintable(themeDiff(parseTheme(theme.toJson()), theme)));
        QVERIFY(writeFile(themes + "/" + QString(80, 'f') + ".json",
                          js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QCOMPARE(followTheme("file", themes + "/" + QString(80, 'f') + ".json").name, QString(60, 'f'));
    }

    void followFile() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        useThemes(root);
        const QString dir = canonical(elsewhere.path());
        QVERIFY(writeFile(dir + "/Ocean/Colors.qml", realWorldQml));
        const auto qml = followTheme("file", dir + "/Ocean/Colors.qml");
        QCOMPARE(qml.sourceKind, QString("file"));
        QCOMPARE(qml.sourcePath, dir + "/Ocean/Colors.qml");
        QCOMPARE(qml.name, QString("Colors"));
        QCOMPARE(qml.hex("background"), QString("#e00e1e36"));
        QCOMPARE(qml.hex("accent"), QString("#9279aa"));
        QVERIFY(writeFile(dir + "/mine.json", js("{'background':'#0e1e36e0','foreground':'#f4b999','primary':'#9279aa'}")));
        const auto json = followTheme("file", dir + "/mine.json");
        QCOMPARE(json.name, QString("mine"));
        QCOMPARE(json.sourcePath, dir + "/mine.json");
        QCOMPARE(json.hex("background"), QString("#e00e1e36"));
        const auto directory = followTheme("file", dir + "/Ocean");
        QCOMPARE(directory.sourcePath, dir + "/Ocean");
        QCOMPARE(directory.name, QString("Ocean"));
        QCOMPARE(directory.colors, qml.colors);
        QVERIFY2(refreshSource(json) == json, qPrintable(themeDiff(refreshSource(json), json)));
        QVERIFY(writeFile(dir + "/mine.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QCOMPARE(refreshSource(json).hex("accent"), QString("#00ff00"));
    }

    void followRejects() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("remote", themes));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("", themes));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("file"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("file", themes + "/missing.json"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("active"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("active", themes + "/missing"));
        QVERIFY(QDir().mkpath(themes + "/bare"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, followTheme("static", "bare"));
    }

    void followActiveCurrent() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(addTheme(themes, "B", "#33aa55", "#101010"));
        QVERIFY(makeLink("A", themes + "/current"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        const auto theme = followTheme("active");
        QCOMPARE(theme.sourceKind, QString("active"));
        QCOMPARE(theme.sourcePath, themes + "/current");
        QVERIFY(theme.sourcePath != canonical(theme.sourcePath));
        QCOMPARE(theme.name, QString("A"));
        QCOMPARE(theme.hex("accent"), QString("#9279aa"));
        const auto explicitLink = followTheme("active", themes + "/current");
        QVERIFY2(explicitLink == theme, qPrintable(themeDiff(explicitLink, theme)));
        // A link straight to a JSON palette file names the theme after the file's directory.
        QVERIFY(writeFile(themes + "/C/palette.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QVERIFY(makeLink(themes + "/C/palette.json", themes + "/json-link"));
        const auto jsonLink = followTheme("active", themes + "/json-link");
        QCOMPARE(jsonLink.sourcePath, themes + "/json-link");
        QCOMPARE(jsonLink.name, QString("C"));
        QCOMPARE(jsonLink.hex("accent"), QString("#00ff00"));
    }

    void followLinkToQmlFile() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "B", "#33aa55", "#101010"));
        QVERIFY(makeLink(themes + "/B/Colors.qml", themes + "/file-link"));
        Theme fileLink;
        QString error;
        try {
            fileLink = followTheme("active", themes + "/file-link");
        } catch (const std::exception &exception) {
            error = exception.what();
        }
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(fileLink.sourcePath, themes + "/file-link");
        QCOMPARE(fileLink.name, QString("B"));
        QCOMPARE(fileLink.hex("accent"), QString("#33aa55"));
    }

    void defaultActiveLinkPreference() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(addTheme(themes, "B", "#33aa55", "#101010"));
        QVERIFY(defaultActiveLink().isEmpty());
        QVERIFY(makeLink("A", themes + "/current"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        QVERIFY(makeLink(themes + "/B", themes + "/.caelestia-use"));
        QCOMPARE(defaultActiveLink(), themes + "/.caelestia-use");
        const auto theme = followTheme("active");
        QCOMPARE(theme.sourcePath, themes + "/.caelestia-use");
        QCOMPARE(theme.name, QString("B"));
        QCOMPARE(theme.hex("accent"), QString("#33aa55"));
        QCOMPARE(defaultTheme().sourcePath, themes + "/.caelestia-use");
        // A dangling preferred link never hides a valid fallback.
        QVERIFY(relink("gone", themes + "/.caelestia-use"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        QCOMPARE(followTheme("active").name, QString("A"));
        QVERIFY(QFile::remove(themes + "/.caelestia-use"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        // A real "current" directory works as well as a link.
        QVERIFY(QFile::remove(themes + "/current"));
        QVERIFY(addTheme(themes, "current", "#abcdef"));
        QCOMPARE(defaultActiveLink(), themes + "/current");
        QCOMPARE(followTheme("active").hex("accent"), QString("#abcdef"));
    }

    void staticThemeNamesFiltering() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(staticThemeNames().isEmpty());
        const QByteArray json = js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}");
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(writeFile(themes + "/B/palette.json", json));
        QVERIFY(writeFile(themes + "/C/colors.json", json));
        QVERIFY(writeFile(themes + "/D/noctalia.json", json));
        QVERIFY(addTheme(themes, ".hidden", "#9279aa"));
        QVERIFY(addTheme(themes, ".caelestia", "#9279aa"));
        QVERIFY(makeLink("A", themes + "/current"));
        QVERIFY(makeLink("B", themes + "/.caelestia-use"));
        QVERIFY(QDir().mkpath(themes + "/empty"));
        QVERIFY(writeFile(themes + "/notes/readme.txt", "no palette here"));
        QVERIFY(writeFile(themes + "/nested/inner/Colors.qml", qmlSingleton({"primary: \"#9279aa\""})));
        QVERIFY(QDir().mkpath(themes + "/dirpalette/Colors.qml"));
        QVERIFY(writeFile(themes + "/loose.json", json));
        QVERIFY(writeFile(themes + "/Colors.qml", qmlSingleton({"primary: \"#9279aa\""})));
        QCOMPARE(staticThemeNames(), (QStringList{"A", "B", "C", "D"}));
        for (const auto &name : staticThemeNames())
            QCOMPARE(followTheme("static", name).name, name);
        qputenv("THEMES_DIR", QFile::encodeName(themes + "/missing"));
        QVERIFY(staticThemeNames().isEmpty());
    }

    void refreshAfterRetarget() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(addTheme(themes, "B", "#33aa55", "#c0101010"));
        QVERIFY(makeLink("A", themes + "/current"));
        const auto followed = followTheme("active");
        QCOMPARE(followed.name, QString("A"));
        QCOMPARE(followed.hex("accent"), QString("#9279aa"));
        QVERIFY(refreshSource(followed) == followed);

        QVERIFY(relink("B", themes + "/current"));
        QCOMPARE(canonical(themes + "/current"), themes + "/B");
        QVERIFY(!QFileInfo::exists(themes + "/current.new"));
        const auto switched = refreshSource(followed);
        QCOMPARE(switched.name, QString("B"));
        QCOMPARE(switched.hex("accent"), QString("#33aa55"));
        QCOMPARE(switched.hex("background"), QString("#c0101010"));
        QCOMPARE(switched.sourceKind, QString("active"));
        QCOMPARE(switched.sourcePath, themes + "/current");
        // A theme stored after the switch still refreshes through the link, not the old target.
        const auto stored = parseTheme(switched.toJson());
        QVERIFY(relink(themes + "/A", themes + "/current"));
        const auto back = refreshSource(stored);
        QVERIFY2(back == followed, qPrintable(themeDiff(back, followed)));

        const auto plain = presetTheme("rose");
        QVERIFY(refreshSource(plain) == plain);
    }

    void followIdempotent() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        const QString themes = useThemes(root);
        const QString other = canonical(elsewhere.path());
        QVERIFY(writeFile(themes + "/Real/Colors.qml", realWorldQml));
        auto rgba = realWorldQml;
        rgba.replace("background: \"#e00e1e36\"", "background: Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)");
        QVERIFY(writeFile(themes + "/Rgba/Colors.qml", rgba));
        QVERIFY(writeFile(themes + "/Odd/Colors.qml",
                          qmlSingleton({"background: Qt.rgba(0.1234, 0.5678, 0.9012, 0.3456)",
                                        "foreground: Qt.rgba(1 / 3, 2 / 3, 1 / 7, 1)", "primary: \"#123\""})));
        QVERIFY(writeFile(themes + "/Css/palette.json", js("{'background':'#0e1e36e0','foreground':'#f4b999',"
            "'primary':'#9279aa80','secondary':'#c85670'}")));
        QVERIFY(writeFile(themes + "/Material/colors.json", js("{'colors':{'dark':{'background':'#101418',"
            "'on_background':'#e0e2e8','primary':'#9ecaff','surface_container_low':'#191c20'},'light':{}}}")));
        QVERIFY(writeFile(themes + "/Noctalia/noctalia.json", js("{'mSurface':'#0b0b12','mOnSurface':'#e6e6f0',"
            "'mPrimary':'#c7a0ff','mSecondary':'#a0c4ff','mTertiary':'#ffd6a0','mOutline':'#35354a'}")));
        QVERIFY(makeLink("Real", themes + "/current"));
        QVERIFY(writeFile(other + "/loose.json", js("{'background':'Qt.rgba(0.3, 0.3, 0.3, 0.5)',"
            "'foreground':'#eeeeee','primary':'#ff8800'}")));

        const QList<QPair<QString, QString>> sources{
            {"active", {}}, {"active", themes + "/current"}, {"static", "Real"}, {"static", "Rgba"},
            {"static", "Odd"}, {"static", "Css"}, {"static", "Material"}, {"static", "Noctalia"},
            {"static", themes + "/Odd"}, {"file", themes + "/Odd/Colors.qml"}, {"file", themes + "/Css/palette.json"},
            {"file", other + "/loose.json"}, {"file", themes + "/Material"},
        };
        for (const auto &[kind, path] : sources) {
            const QString label = kind + " " + path;
            const auto theme = followTheme(kind, path);
            const auto reparsed = parseTheme(theme.toJson());
            QVERIFY2(reparsed == theme, qPrintable(label + ": " + themeDiff(reparsed, theme)));
            const auto text = QJsonDocument(theme.toJson()).toJson();
            const auto fromText = parseTheme(QJsonDocument::fromJson(text).object());
            QVERIFY2(fromText == theme, qPrintable(label + ": " + themeDiff(fromText, theme)));
            QCOMPARE(QJsonDocument(fromText.toJson()).toJson(), text);
            const auto refreshed = refreshSource(reparsed);
            QVERIFY2(refreshed == theme, qPrintable(label + ": " + themeDiff(refreshed, theme)));
            for (int step = 0; step < tileSteps; ++step)
                QVERIFY2(contrast(theme.tiles[step], theme.tileText[step]) >= 3,
                         qPrintable(label + QString(": tile step %1").arg(step)));
        }
    }

    void sourceWatchPathsChain() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString themes = useThemes(root);
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(writeFile(themes + "/A/palette.json", js("{'background':'#000000','foreground':'#ffffff','primary':'#00ff00'}")));
        QVERIFY(addTheme(themes, "B", "#33aa55"));
        QVERIFY(makeLink("A", themes + "/current"));
        QVERIFY(makeLink(themes + "/current", themes + "/sel"));
        QCOMPARE(canonical(themes + "/sel"), themes + "/A");

        const auto paths = sourceWatchPaths(themes + "/sel");
        const QString context = paths.join(", ");
        QVERIFY2(paths.contains(themes), qPrintable(context));
        QVERIFY2(paths.contains(themes + "/A"), qPrintable(context));
        QVERIFY2(paths.contains(themes + "/A/Colors.qml"), qPrintable(context));
        QVERIFY2(paths.contains(themes + "/A/palette.json"), qPrintable(context));
        QVERIFY2(!paths.contains(themes + "/sel") && !paths.contains(themes + "/current"), qPrintable(context));
        QVERIFY2(!paths.contains(themes + "/B"), qPrintable(context));
        QVERIFY2(!paths.contains("/") && !paths.contains(QDir::rootPath()), qPrintable(context));
        QVERIFY2(!paths.contains(QDir::homePath()) && !paths.contains(canonical(QDir::homePath())), qPrintable(context));
        QVERIFY(std::is_sorted(paths.begin(), paths.end()));
        QCOMPARE(QSet<QString>(paths.begin(), paths.end()).size(), paths.size());
        for (const auto &path : paths) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
            QCOMPARE(canonical(path), path);
            QVERIFY2(!QFileInfo(path).isSymLink(), qPrintable(path));
        }

        // After relinking, the new target is watched instead of the old one.
        QVERIFY(relink("B", themes + "/current"));
        const auto relinked = sourceWatchPaths(themes + "/sel");
        QVERIFY2(relinked.contains(themes + "/B/Colors.qml"), qPrintable(relinked.join(", ")));
        QVERIFY2(!relinked.contains(themes + "/A"), qPrintable(relinked.join(", ")));
        QVERIFY2(relinked.contains(themes), qPrintable(relinked.join(", ")));

        const auto file = sourceWatchPaths(themes + "/A/Colors.qml");
        QVERIFY(file.contains(themes + "/A/Colors.qml"));
        QVERIFY(file.contains(themes + "/A"));
        QVERIFY(!file.contains(themes + "/A/palette.json"));
        QVERIFY(sourceWatchPaths({}).isEmpty());
        QVERIFY(!sourceWatchPaths("/").contains("/"));
    }

    void sourceWatchPathsStopsAtHome() {
        const QString themes = home + "/.config/themes";
        QVERIFY(addTheme(themes, "A", "#9279aa"));
        QVERIFY(makeLink("A", themes + "/current"));
        const auto cleanupThemes = qScopeGuard([&] { QDir(themes).removeRecursively(); });
        for (const auto &path : {themes + "/current", themes + "/A", themes + "/A/Colors.qml"}) {
            const auto paths = sourceWatchPaths(path);
            const QString context = path + ": " + paths.join(", ");
            QVERIFY2(paths.contains(themes + "/A/Colors.qml"), qPrintable(context));
            QVERIFY2(paths.contains(themes + "/A"), qPrintable(context));
            QVERIFY2(paths.contains(themes), qPrintable(context));
            QVERIFY2(!paths.contains(home) && !paths.contains("/"), qPrintable(context));
            for (const auto &watched : paths)
                QVERIFY2(watched.startsWith(home + "/"), qPrintable(context));
        }
    }

    // ---- icon.h ----------------------------------------------------------------------------

    void iconSvgColors() {
        const auto theme = presetTheme("midnight");
        const auto svg = iconSvg(theme);
        QVERIFY(svg.startsWith("<svg"));
        QVERIFY(svg.contains(("fill=\"" + theme.color("surface").name() + "\"").toUtf8()));
        QVERIFY(svg.contains("#181b24"));
        for (const int step : {1, 4, 7, 10})
            QVERIFY2(svg.contains(theme.tiles[step].name().toUtf8()), qPrintable(QString::number(step)));
        QXmlStreamReader xml(svg);
        int rects = 0;
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("rect"))
                ++rects;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QCOMPARE(rects, 5);
        QCOMPARE(iconSvg(theme), svg);
        QVERIFY(iconSvg(presetTheme("paper")) != svg);
        // A translucent background never leaks alpha into the icon.
        const auto translucent = setColors(theme, {"background=#800e1e36", "surface=#80223344"});
        QVERIFY(iconSvg(translucent).contains("fill=\"#223344\""));
        QCOMPARE(iconPath("/config/2048"), QString("/config/2048/icon.svg"));
    }

    void writeIconSkipsIdentical() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("nested/config/icon.svg");
        const auto midnight = presetTheme("midnight"), paper = presetTheme("paper");
        writeIcon(path, midnight);
        QCOMPARE(readFile(path), iconSvg(midnight));

        const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 400);
        fs::last_write_time(nativePath(path), old);
        const auto stamp = fs::last_write_time(nativePath(path));
        const auto inode = inodeOf(path);
        QVERIFY(inode != 0);
        writeIcon(path, midnight);
        QCOMPARE(inodeOf(path), inode);
        QVERIFY(fs::last_write_time(nativePath(path)) == stamp);
        QCOMPARE(readFile(path), iconSvg(midnight));

        writeIcon(path, paper);
        QCOMPARE(readFile(path), iconSvg(paper));
        QVERIFY(fs::last_write_time(nativePath(path)) != stamp);

        // A stale or foreign file is replaced.
        QVERIFY(writeFile(path, "<svg/>"));
        writeIcon(path, paper);
        QCOMPARE(readFile(path), iconSvg(paper));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QCOMPARE(QDir(dir.filePath("nested/config")).entryList(QDir::Files | QDir::Hidden), QStringList{"icon.svg"});
    }

    // ---- storage.h -------------------------------------------------------------------------

    void jsonRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonObject data{{"name", QString::fromUtf8("théme ☃")}, {"number", 42}, {"real", 1.5},
                               {"flag", true}, {"none", QJsonValue::Null},
                               {"list", QJsonArray{1, "two", QJsonObject{{"three", 3}}}},
                               {"nested", QJsonObject{{"deep", QJsonObject{{"x", -1}}}}}};
        const QString indented = dir.filePath("a/b/c/indented.json");
        writeJson(indented, data);
        QCOMPARE(readJson(indented), data);
        QCOMPARE(readFile(indented), QJsonDocument(data).toJson(QJsonDocument::Indented));
        const QString compact = dir.filePath("compact.json");
        writeJson(compact, data, QJsonDocument::Compact);
        QCOMPARE(readJson(compact), data);
        QCOMPARE(readFile(compact), QJsonDocument(data).toJson(QJsonDocument::Compact));
        writeJson(compact, QJsonObject{{"replaced", true}});
        QCOMPARE(readJson(compact), (QJsonObject{{"replaced", true}}));
        writeJson(compact, {});
        QCOMPARE(readJson(compact), QJsonObject());
    }

    void readJsonRejects_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("array") << QByteArray("[1, 2, 3]");
        QTest::newRow("string") << QByteArray("\"text\"");
        QTest::newRow("number") << QByteArray("42");
        QTest::newRow("null") << QByteArray("null");
        QTest::newRow("truncated") << QByteArray("{\"a\": ");
        QTest::newRow("trailing garbage") << QByteArray("{\"a\": 1} x");
        QTest::newRow("not json") << QByteArray("version = 1");
        QTest::newRow("oversized") << QByteArray("{\"pad\":\"" + QByteArray(1024 * 1024, 'x') + "\"}");
    }
    void readJsonRejects() {
        QFETCH(QByteArray, contents);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("data.json"), contents));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, readJson(dir.filePath("data.json")));
    }

    void readJsonLimits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, readJson(dir.filePath("missing.json")));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, readJson(dir.path()));
        const int limit = 1024 * 1024, overhead = int(QByteArray("{\"pad\":\"\"}").size());
        QVERIFY(writeFile(dir.filePath("limit.json"), "{\"pad\":\"" + QByteArray(limit - overhead, 'x') + "\"}"));
        QCOMPARE(QFileInfo(dir.filePath("limit.json")).size(), qint64(limit));
        QCOMPARE(readJson(dir.filePath("limit.json")).value("pad").toString().size(), limit - overhead);
        QVERIFY(writeFile(dir.filePath("over.json"), "{\"pad\":\"" + QByteArray(limit - overhead + 1, 'x') + "\"}"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, readJson(dir.filePath("over.json")));
    }

    void configDirectory() {
        QCOMPARE(defaultConfigDirectory(), home + "/.config/2048");
        qputenv("GAME_2048_CONFIG_DIR", "/elsewhere/./2048/");
        QCOMPARE(defaultConfigDirectory(), QString("/elsewhere/2048"));
        qunsetenv("GAME_2048_CONFIG_DIR");
        QCOMPARE(defaultConfigDirectory(), home + "/.config/2048");
        qputenv("GAME_2048_CONFIG_DIR", QFile::encodeName(home + "/.config/2048"));
    }

    void sessionRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("state/session.json");
        Game game(4, Mode::PowerUps, 7);
        game.setValues({2, 2, 4, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 16}, 100);
        QVERIFY(game.move(Direction::Left));
        QVERIFY(game.move(Direction::Up) || game.move(Direction::Down));
        const ModeBests best{BestScores{{3, 50}, {4, 10}, {5, 2147483647}}, BestScores{{4, 7}}};
        saveSession(path, game, best);
        const auto stored = readJson(path);
        QCOMPARE(stored.value("version").toInt(), 1);
        QCOMPARE(stored.value("best").toObject().keys(), (QStringList{"3", "4", "5"}));
        QCOMPARE(stored.value("bestClassic").toObject().keys(), (QStringList{"4"}));

        ModeBests loaded{BestScores{{5, 123}}, {}};
        const auto restored = loadSession(path, loaded);
        QCOMPARE(restored.toJson(), game.toJson());
        auto expected = best;
        expected[0][4] = game.score();
        QVERIFY(game.score() > 10);
        QCOMPARE(loaded, expected);

        // A best score above the current game's is kept; a missing entry is filled in for its mode only.
        saveSession(path, game, {BestScores{{4, 5000}}, {}});
        QCOMPARE(loadSession(path, loaded).toJson(), restored.toJson());
        QCOMPARE(loaded, (ModeBests{BestScores{{4, 5000}}, {}}));
        saveSession(path, game, {});
        loadSession(path, loaded);
        QCOMPARE(loaded, (ModeBests{BestScores{{4, game.score()}}, {}}));
        Game classic(3, Mode::Classic, 5);
        saveSession(path, classic, {BestScores{{3, 900}}, {}});
        loadSession(path, loaded);
        QCOMPARE(loaded, (ModeBests{BestScores{{3, 900}}, BestScores{{3, classic.score()}}}));
    }

    void sessionRejects_data() {
        QTest::addColumn<QJsonObject>("session");
        Game game(4, Mode::PowerUps, 3);
        const QJsonObject valid{{"version", 1}, {"game", game.toJson()}, {"best", QJsonObject{{"4", 10}}}};
        auto withBest = [&](const QJsonObject &best) {
            auto session = valid;
            session.insert("best", best);
            return session;
        };
        auto edit = [&](const QString &key, const QJsonValue &value) {
            auto session = valid;
            if (value.isUndefined())
                session.remove(key);
            else
                session.insert(key, value);
            return session;
        };
        QTest::newRow("size too small") << withBest({{"2", 10}});
        QTest::newRow("size not a number") << withBest({{"abc", 10}});
        QTest::newRow("size suffix") << withBest({{"4x", 10}});
        QTest::newRow("size empty") << withBest({{"", 10}});
        QTest::newRow("negative") << withBest({{"4", -1}});
        QTest::newRow("fraction") << withBest({{"4", 1.5}});
        QTest::newRow("string score") << withBest({{"4", "10"}});
        QTest::newRow("bool score") << withBest({{"4", true}});
        QTest::newRow("null score") << withBest({{"4", QJsonValue::Null}});
        QTest::newRow("overflow") << withBest({{"4", 2147483648.0}});
        QTest::newRow("one bad of many") << withBest({{"3", 1}, {"4", 2}, {"5", -3}});
        QTest::newRow("best array") << edit("best", QJsonArray{10});
        QTest::newRow("no best") << edit("best", QJsonValue::Undefined);
        QTest::newRow("classic best array") << edit("bestClassic", QJsonArray{10});
        QTest::newRow("bad classic best") << edit("bestClassic", QJsonObject{{"4", -1}});
        QTest::newRow("no game") << edit("game", QJsonValue::Undefined);
        QTest::newRow("version 2") << edit("version", 2);
        QTest::newRow("no version") << edit("version", QJsonValue::Undefined);
        auto badGame = game.toJson();
        badGame.insert("size", 2);
        QTest::newRow("bad game") << edit("game", badGame);
    }
    void sessionRejects() {
        QFETCH(QJsonObject, session);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("session.json");
        writeJson(path, session);
        ModeBests best{BestScores{{4, 77}}, {}};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadSession(path, best));
        QCOMPARE(best, (ModeBests{BestScores{{4, 77}}, {}}));
    }

    void sessionAcceptsLimits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("session.json");
        Game game(3, Mode::PowerUps, 1);
        writeJson(path, QJsonObject{{"version", 1}, {"game", game.toJson()},
                                    {"best", QJsonObject{{"3", 0}, {"5", 2147483647.0}, {"4", 12.0}, {"8", 99.0}}}});
        ModeBests best;
        const auto loaded = loadSession(path, best);
        QCOMPARE(loaded.size(), 3);
        // Sessions saved before Classic mode have no "bestClassic"; their scores are Standard.
        QCOMPARE(best, (ModeBests{BestScores{{3, game.score()}, {4, 12}, {5, 2147483647}}, {}}));
    }
};

QTEST_APPLESS_MAIN(ThemeTests)
#include "theme_tests.moc"
