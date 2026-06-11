#pragma once

#include <QString>

class SessionManager;

class ReplayRuntime {
public:
    struct LoadRequest {
        QString requestedPath;
        QString normalizedPath;
        QString openDirectory;
        bool exists = false;
        bool typedContainer = false;
    };

    QString openDirectory(const SessionManager& session) const;
    void restoreSession(const SessionManager& session);
    void clearSession(SessionManager& session);
    LoadRequest prepareLoadRequest(const QString& rawPath, SessionManager& session);

    static QString normalizeLocalPath(const QString& raw);
    static bool isTypedContainerPath(const QString& path);
    static QString metaPathFor(const QString& path);

private:
    QString m_lastOpenDirectory;
};
