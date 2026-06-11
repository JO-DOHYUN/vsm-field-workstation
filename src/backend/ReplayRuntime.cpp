#include "ReplayRuntime.h"

#include "RuntimePaths.h"
#include "SessionManager.h"

#include <QDir>
#include <QFileInfo>

namespace {

const QString kReplayLastOpenDirectoryKey = QStringLiteral("replay/lastOpenDirectory");

} // namespace

QString ReplayRuntime::normalizeLocalPath(const QString& raw) {
    return RuntimePaths::normalizeLocalPath(raw);
}

bool ReplayRuntime::isTypedContainerPath(const QString& path) {
    const QFileInfo info(path);
    return info.isDir()
        || info.fileName().compare(QStringLiteral("capture.stream"), Qt::CaseInsensitive) == 0
        || info.fileName().compare(QStringLiteral("capture.stream.part"), Qt::CaseInsensitive) == 0;
}

QString ReplayRuntime::metaPathFor(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        const QDir dir(info.absoluteFilePath());
        const QString finalPath = dir.filePath(QStringLiteral("session.meta.json"));
        if (QFileInfo::exists(finalPath)) return finalPath;
        return dir.filePath(QStringLiteral("session.meta.json.part"));
    }
    if (info.fileName().compare(QStringLiteral("capture.stream"), Qt::CaseInsensitive) == 0 ||
        info.fileName().compare(QStringLiteral("capture.stream.part"), Qt::CaseInsensitive) == 0) {
        const QString finalPath = info.dir().filePath(QStringLiteral("session.meta.json"));
        if (QFileInfo::exists(finalPath)) return finalPath;
        return info.dir().filePath(QStringLiteral("session.meta.json.part"));
    }
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".meta.json"));
}

QString ReplayRuntime::openDirectory(const SessionManager& session) const {
    if (!m_lastOpenDirectory.isEmpty() && QFileInfo::exists(m_lastOpenDirectory)) {
        return m_lastOpenDirectory;
    }
    const QString cached = QDir::fromNativeSeparators(session.value(kReplayLastOpenDirectoryKey).toString());
    if (!cached.isEmpty() && QFileInfo::exists(cached)) return cached;
    return RuntimePaths::ensureLogsRoot();
}

void ReplayRuntime::restoreSession(const SessionManager& session) {
    m_lastOpenDirectory = QDir::fromNativeSeparators(session.value(kReplayLastOpenDirectoryKey).toString());
}

void ReplayRuntime::clearSession(SessionManager& session) {
    m_lastOpenDirectory.clear();
    session.remove(kReplayLastOpenDirectoryKey);
    session.sync();
}

ReplayRuntime::LoadRequest ReplayRuntime::prepareLoadRequest(const QString& rawPath, SessionManager& session) {
    LoadRequest request;
    request.requestedPath = rawPath;
    request.normalizedPath = normalizeLocalPath(rawPath);
    if (request.normalizedPath.isEmpty()) return request;

    const QFileInfo replayInfo(request.normalizedPath);
    request.openDirectory = replayInfo.isDir() ? replayInfo.absoluteFilePath() : replayInfo.absolutePath();
    request.openDirectory = QDir::fromNativeSeparators(request.openDirectory);
    if (!request.openDirectory.isEmpty()) {
        m_lastOpenDirectory = request.openDirectory;
        session.setValue(kReplayLastOpenDirectoryKey, request.openDirectory);
        session.sync();
    }

    request.exists = QFileInfo::exists(request.normalizedPath);
    request.typedContainer = isTypedContainerPath(request.normalizedPath);
    return request;
}
