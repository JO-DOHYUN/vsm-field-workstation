#include "RuntimePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString normalizePath(QString path) {
    path = QDir::fromNativeSeparators(path);
    if (path.isEmpty()) {
        path = QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    }
    return path;
}

QString ensurePath(const QString& path) {
    const QString normalized = normalizePath(path);
    QDir dir;
    dir.mkpath(normalized);
    return normalized;
}

bool looksLikeSourceRoot(const QDir& dir) {
    return QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))) &&
        QFileInfo::exists(dir.filePath(QStringLiteral("qml"))) &&
        QFileInfo::exists(dir.filePath(QStringLiteral("src")));
}

QString findProjectRootFrom(QString path) {
    path = normalizePath(path);
    QDir dir(path);
    if (QFileInfo(path).isFile()) {
        dir = QFileInfo(path).absoluteDir();
    }

    while (dir.exists()) {
        if (looksLikeSourceRoot(dir)) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return {};
}

QString fallbackRuntimeRoot() {
    const QString appDir = normalizePath(QCoreApplication::applicationDirPath());
    if (!appDir.isEmpty()) return QDir(appDir).filePath(QStringLiteral("replay_data"));
    return QDir(normalizePath(QDir::currentPath())).filePath(QStringLiteral("replay_data"));
}

} // namespace

namespace RuntimePaths {

QString normalizeLocalPath(const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};

    const QUrl url(trimmed);
    if (url.isValid() && url.isLocalFile()) {
        return QDir::fromNativeSeparators(url.toLocalFile());
    }

    if (trimmed.startsWith(QStringLiteral("file:/"), Qt::CaseInsensitive)) {
        const QUrl userUrl = QUrl::fromUserInput(trimmed);
        if (userUrl.isValid() && userUrl.isLocalFile()) {
            return QDir::fromNativeSeparators(userUrl.toLocalFile());
        }
    }

    return QDir::fromNativeSeparators(trimmed);
}

QString appDataRoot() {
    return normalizePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString configRoot() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (path.isEmpty()) {
        path = QDir(appDataRoot()).filePath(QStringLiteral("config"));
    }
    return normalizePath(path);
}

QString projectRoot() {
    const QString envRootRaw = qEnvironmentVariable("CAN_MONITOR_WORKSPACE_ROOT");
    if (!envRootRaw.isEmpty()) return normalizePath(envRootRaw);

    const QString fromCurrent = findProjectRootFrom(QDir::currentPath());
    if (!fromCurrent.isEmpty()) return fromCurrent;

    const QString fromAppDir = findProjectRootFrom(QCoreApplication::applicationDirPath());
    if (!fromAppDir.isEmpty()) return fromAppDir;

    return QDir(fallbackRuntimeRoot()).absolutePath();
}

QString projectDataRoot() {
    const QString root = projectRoot();
    if (looksLikeSourceRoot(QDir(root))) {
        return QDir(root).filePath(QStringLiteral("replay_data"));
    }
    return root.endsWith(QStringLiteral("replay_data"))
        ? root
        : QDir(root).filePath(QStringLiteral("replay_data"));
}

QString logsRoot() {
    return QDir(projectDataRoot()).filePath(QStringLiteral("logs"));
}

QString snapshotsRoot() {
    return QDir(projectDataRoot()).filePath(QStringLiteral("snapshots"));
}

QString ensureAppDataRoot() {
    return ensurePath(appDataRoot());
}

QString ensureConfigRoot() {
    return ensurePath(configRoot());
}

QString ensureProjectDataRoot() {
    return ensurePath(projectDataRoot());
}

QString ensureLogsRoot() {
    return ensurePath(logsRoot());
}

QString ensureSnapshotsRoot() {
    return ensurePath(snapshotsRoot());
}

} // namespace RuntimePaths
