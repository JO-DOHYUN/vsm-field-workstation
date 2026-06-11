#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

namespace FilePersistence {

bool writeBytesAtomically(const QString& path, const QByteArray& bytes, QString* errorOut = nullptr);
bool writeTextAtomically(const QString& path, const QString& text, QString* errorOut = nullptr);
bool writeJsonAtomically(const QString& path, const QJsonDocument& document, QString* errorOut = nullptr);
bool copyFileAtomically(const QString& sourcePath, const QString& destinationPath, QString* errorOut = nullptr);
bool removeFileIfExists(const QString& path, QString* errorOut = nullptr);

} // namespace FilePersistence
