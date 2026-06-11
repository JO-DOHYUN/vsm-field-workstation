#include "FilePersistence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {

bool ensureParentDirectory(const QString& path, QString* errorOut) {
    const QFileInfo info(path);
    const QString parent = info.absolutePath();
    if (parent.trimmed().isEmpty()) return true;
    if (QDir().mkpath(parent)) return true;
    if (errorOut) *errorOut = QStringLiteral("parent directory create failed: %1").arg(parent);
    return false;
}

} // namespace

namespace FilePersistence {

bool writeBytesAtomically(const QString& path, const QByteArray& bytes, QString* errorOut) {
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("target path is empty");
        return false;
    }
    if (!ensureParentDirectory(normalized, errorOut)) return false;

    QSaveFile file(normalized);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = QStringLiteral("open failed for %1: %2").arg(normalized, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (errorOut) *errorOut = QStringLiteral("write failed for %1: %2").arg(normalized, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorOut) *errorOut = QStringLiteral("commit failed for %1: %2").arg(normalized, file.errorString());
        return false;
    }
    return true;
}

bool writeTextAtomically(const QString& path, const QString& text, QString* errorOut) {
    return writeBytesAtomically(path, text.toUtf8(), errorOut);
}

bool writeJsonAtomically(const QString& path, const QJsonDocument& document, QString* errorOut) {
    return writeBytesAtomically(path, document.toJson(QJsonDocument::Indented), errorOut);
}

bool copyFileAtomically(const QString& sourcePath, const QString& destinationPath, QString* errorOut) {
    const QString source = QDir::fromNativeSeparators(sourcePath.trimmed());
    const QString destination = QDir::fromNativeSeparators(destinationPath.trimmed());
    if (source.isEmpty()) return true;
    if (destination.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("destination path is empty");
        return false;
    }
    if (!ensureParentDirectory(destination, errorOut)) return false;

    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("source open failed for %1: %2").arg(source, input.errorString());
        return false;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = QStringLiteral("destination open failed for %1: %2").arg(destination, output.errorString());
        return false;
    }

    QByteArray chunk;
    chunk.resize(1024 * 1024);
    while (true) {
        const qint64 readBytes = input.read(chunk.data(), chunk.size());
        if (readBytes < 0) {
            if (errorOut) *errorOut = QStringLiteral("source read failed for %1: %2").arg(source, input.errorString());
            output.cancelWriting();
            return false;
        }
        if (readBytes == 0) break;
        if (output.write(chunk.constData(), readBytes) != readBytes) {
            if (errorOut) *errorOut = QStringLiteral("destination write failed for %1: %2").arg(destination, output.errorString());
            output.cancelWriting();
            return false;
        }
    }

    if (!output.commit()) {
        if (errorOut) *errorOut = QStringLiteral("destination commit failed for %1: %2").arg(destination, output.errorString());
        return false;
    }
    return true;
}

bool removeFileIfExists(const QString& path, QString* errorOut) {
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty()) return true;
    if (!QFileInfo::exists(normalized)) return true;
    if (QFile::remove(normalized)) return true;
    if (errorOut) *errorOut = QStringLiteral("remove failed for %1").arg(normalized);
    return false;
}

} // namespace FilePersistence
