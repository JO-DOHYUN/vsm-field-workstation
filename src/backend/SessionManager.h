#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

class SessionManager {
public:
    SessionManager();
    explicit SessionManager(const QString& iniFilePath);

    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& key, const QVariant& value);
    void remove(const QString& key);
    void clear();
    void sync();
    QString filePath() const;

private:
    mutable QSettings m_settings;
};
