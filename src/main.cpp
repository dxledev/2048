/**
 * @file main.cpp
 * @brief Application entry point for the game window and the theme command line.
 *
 * Theme commands run in a QCoreApplication without connecting to a display.
 * The game window prefers native Wayland, uses shared-memory buffers because
 * the interface is drawn entirely by the raster engine, and requests an alpha
 * channel so a translucent theme background reaches the compositor.
 */
#include "cli.h"
#include "core/storage.h"
#include "core/theme_source.h"
#include "ui/window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication::setApplicationName("2048");
    QCoreApplication::setApplicationVersion("1.0.0");

    try {
        QStringList arguments;
        for (int index = 1; index < argc; ++index)
            arguments.append(QString::fromLocal8Bit(argv[index]));
        const auto options = g2048::parseOptions(arguments);
        if (options.help || options.version || !options.arguments.isEmpty()) {
            QCoreApplication app(argc, argv);
            if (options.help) {
                g2048::printHelp();
                return 0;
            }
            if (options.version) {
                QTextStream(stdout) << "2048 1.0.0\n";
                return 0;
            }
            return g2048::runThemeCommand(options);
        }
        if (options.dryRun)
            throw std::invalid_argument("--dry-run applies to theme commands");
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "wayland");
        // The raster-only UI needs no GPU driver or EGL buffers.
        if (qEnvironmentVariableIsEmpty("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"))
            qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
        QApplication app(argc, argv);
        app.setStyle("Fusion");
        app.setDesktopFileName("io.github.game2048");
        auto theme = g2048::ensureTheme(options.configDirectory + "/theme.json");
        g2048::Window window(options.configDirectory, theme, options.size, !options.opaque);
        window.show();
        return app.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << "2048: " << error.what() << '\n';
        return 1;
    }
}
