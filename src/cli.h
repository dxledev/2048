#pragma once

#include <QStringList>

namespace g2048 {

/** @brief Parsed command-line settings shared by startup and theme commands. */
struct Options {
    QString configDirectory; ///< Absolute directory used for persistent application data.
    QStringList arguments; ///< Remaining positional arguments, including a theme command.
    int size = 0; ///< Board size for a fresh game; zero resumes the saved game.
    bool opaque = false; ///< Paint the background opaque even when the platform supports alpha.
    bool dryRun = false; ///< Validate and print a theme change without writing it.
    bool help = false; ///< Whether help output was requested.
    bool version = false; ///< Whether version output was requested.
};

/** @brief Parse application and theme-command options, throwing on invalid input. */
Options parseOptions(QStringList arguments);
/** @brief Print the supported command-line syntax to standard output. */
void printHelp();
/** @brief Execute the parsed theme subcommand and return its process exit code. */
int runThemeCommand(const Options &options);

}
