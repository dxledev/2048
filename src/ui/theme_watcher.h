#pragma once

#include "core/theme.h"
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace g2048 {

/** @brief Debounced watcher that reloads theme.json and its bound source at runtime. */
class ThemeWatcher : public QObject {
    Q_OBJECT
public:
    /** @brief Watch @p path and begin with an already validated theme. */
    ThemeWatcher(QString path, Theme initial, QObject *parent = nullptr);
    /** @brief Return the last accepted theme. */
    const Theme &theme() const { return theme_; }
    /** @brief Return the output theme file path. */
    const QString &path() const { return path_; }
    /** @brief Return every path currently watched, for diagnostics and tests. */
    QStringList watchedPaths() const;

public slots:
    /** @brief Re-read theme.json and its source now. */
    void reload();

signals:
    /** @brief Emitted after a reload produced a different valid theme. */
    void changed();
    /** @brief Emitted when an update is invalid; the last valid theme stays active. */
    void rejected(const QString &message);

private:
    QString path_;
    Theme theme_;
    QFileSystemWatcher watcher_;
    QHash<QString, QPair<quint64, quint64>> identities_;
    QTimer debounce_;
    int retries_ = 0;
    /** @brief Synchronize watches with the configured source, re-adding replaced inodes. */
    void updateWatches(const QString &sourcePath);
};

}
