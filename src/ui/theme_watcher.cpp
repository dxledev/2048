/**
 * @file theme_watcher.cpp
 * @brief Debounced filesystem monitoring and live theme replacement.
 *
 * The theme file, its directory, and every real directory along the source's
 * symlink chain are watched. Watches are keyed by inode, so a path that was
 * atomically replaced or relinked is re-registered instead of silently going
 * stale. Failed reloads keep the last valid palette and retry briefly, which
 * covers palette generators that write their files in several steps.
 */
#include "theme_watcher.h"
#include "core/storage.h"
#include "core/theme_source.h"

#include <QFile>
#include <QFileInfo>
#include <sys/stat.h>

namespace g2048 {
namespace {

QPair<quint64, quint64> identity(const QString &path) {
    struct stat status {};
    if (::stat(QFile::encodeName(path).constData(), &status) != 0)
        return {0, 0};
    return {quint64(status.st_dev), quint64(status.st_ino)};
}

}

ThemeWatcher::ThemeWatcher(QString path, Theme initial, QObject *parent)
    : QObject(parent), path_(std::move(path)), theme_(std::move(initial)) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(40);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { retries_ = 0; debounce_.start(40); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { retries_ = 0; debounce_.start(40); });
    connect(&debounce_, &QTimer::timeout, this, &ThemeWatcher::reload);
    updateWatches(theme_.sourcePath);
    QTimer::singleShot(0, this, &ThemeWatcher::reload);
}

QStringList ThemeWatcher::watchedPaths() const {
    return watcher_.files() + watcher_.directories();
}

void ThemeWatcher::updateWatches(const QString &sourcePath) {
    QStringList wanted{QFileInfo(path_).absolutePath()};
    if (QFileInfo::exists(path_))
        wanted.append(path_);
    wanted.append(sourceWatchPaths(sourcePath));
    wanted.removeDuplicates();
    for (const auto &path : watchedPaths()) {
        if (!wanted.contains(path) || identities_.value(path) != identity(path)) {
            watcher_.removePath(path);
            identities_.remove(path);
        }
    }
    const auto current = watchedPaths();
    for (const auto &path : wanted) {
        if (!current.contains(path) && watcher_.addPath(path))
            identities_.insert(path, identity(path));
    }
}

void ThemeWatcher::reload() {
    QString sourcePath = theme_.sourcePath;
    try {
        auto next = loadTheme(path_);
        // Watch what theme.json asks for, even when that source cannot be read yet.
        sourcePath = next.sourcePath;
        if (!next.sourceKind.isEmpty()) {
            if (QFileInfo(next.sourcePath).canonicalFilePath() == QFileInfo(path_).canonicalFilePath())
                throw std::invalid_argument("Theme source cannot be its own output file");
            auto resolved = refreshSource(next);
            if (resolved != next)
                writeJson(path_, resolved.toJson());
            next = std::move(resolved);
        }
        retries_ = 0;
        if (next != theme_) {
            theme_ = std::move(next);
            emit changed();
        }
    } catch (const std::exception &error) {
        if (retries_ < 5) {
            ++retries_;
            debounce_.start(150);
        } else {
            emit rejected(QString::fromUtf8(error.what()));
        }
    }
    updateWatches(sourcePath);
}

}
