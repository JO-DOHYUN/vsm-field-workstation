#include "SessionManager.h"

SessionManager::SessionManager() : m_settings() {}

SessionManager::SessionManager(const QString& iniFilePath)
    : m_settings(iniFilePath, QSettings::IniFormat) {}

QVariant SessionManager::value(const QString& key, const QVariant& defaultValue) const {
    return m_settings.value(key, defaultValue);
}

void SessionManager::setValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
}

void SessionManager::remove(const QString& key) {
    m_settings.remove(key);
}

void SessionManager::clear() {
    m_settings.clear();
}

void SessionManager::sync() {
    m_settings.sync();
}

QString SessionManager::filePath() const {
    return m_settings.fileName();
}
