#include "StorageRuntime.h"

#include "FilePersistence.h"
#include "RuntimePaths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace {

void writeU16Le(char* out, quint16 value) {
    out[0] = char(value & 0xFF);
    out[1] = char((value >> 8) & 0xFF);
}

void writeU64Le(char* out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) {
        out[byte] = char((value >> (byte * 8)) & 0xFF);
    }
}

QString normalizeTargetDirectory(const QString& requestedDirectory) {
    const QString normalized = RuntimePaths::normalizeLocalPath(requestedDirectory);
    const QString directory = normalized.trimmed().isEmpty()
        ? StorageRuntime::defaultLogDirectory()
        : QDir::fromNativeSeparators(normalized);
    QDir().mkpath(directory);
    return directory;
}

QString sanitizeLogStem(QString requestedName, const QString& fallbackStem) {
    QString stem = requestedName.trimmed();
    if (stem.endsWith(QStringLiteral(".typed"), Qt::CaseInsensitive)) stem.chop(6);
    if (stem.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) stem.chop(4);
    if (stem.isEmpty()) stem = fallbackStem;

    QString cleaned;
    cleaned.reserve(stem.size());
    bool previousUnderscore = false;
    for (const QChar ch : stem) {
        const bool invalid = ch.isSpace()
            || QStringLiteral("<>:\"/\\|?*").contains(ch)
            || ch.category() == QChar::Other_Control;
        const QChar next = invalid ? QLatin1Char('_') : ch;
        if (next == QLatin1Char('_')) {
            if (previousUnderscore) continue;
            previousUnderscore = true;
        } else {
            previousUnderscore = false;
        }
        cleaned.append(next);
    }
    cleaned = cleaned.trimmed();
    while (cleaned.startsWith(QLatin1Char('.')) || cleaned.startsWith(QLatin1Char('_'))) cleaned.remove(0, 1);
    while (cleaned.endsWith(QLatin1Char('.')) || cleaned.endsWith(QLatin1Char('_'))) cleaned.chop(1);
    return cleaned.isEmpty() ? fallbackStem : cleaned;
}

QString uniquePath(const QString& requestedPath, const QString& suffix) {
    if (!QFileInfo::exists(requestedPath)) return requestedPath;
    const QString base = requestedPath.left(requestedPath.size() - suffix.size());
    for (int i = 2; i < 1000; ++i) {
        const QString candidate = QStringLiteral("%1_%2%3").arg(base).arg(i).arg(suffix);
        if (!QFileInfo::exists(candidate)) return candidate;
    }
    return QStringLiteral("%1_%2%3").arg(base).arg(QDateTime::currentMSecsSinceEpoch()).arg(suffix);
}

} // namespace

QString StorageRuntime::defaultLogDirectory() {
    return RuntimePaths::ensureLogsRoot();
}

QString StorageRuntime::defaultSnapshotDirectory() {
    return RuntimePaths::ensureSnapshotsRoot();
}

QString StorageRuntime::defaultTempLogDirectory() {
    const QString path = QDir(defaultLogDirectory()).filePath(QStringLiteral("CAN_Monitor_Reboot_temp"));
    QDir().mkpath(path);
    return path;
}

StorageRuntime::LogSessionPaths StorageRuntime::makeTypedCapturePaths(const QString& stamp) {
    return makeTypedCapturePaths(stamp, defaultLogDirectory(), QString());
}

StorageRuntime::LogSessionPaths StorageRuntime::makeTypedCapturePaths(const QString& stamp, const QString& targetDirectory, const QString& targetName) {
    const QString directory = normalizeTargetDirectory(targetDirectory);
    const QString stem = sanitizeLogStem(targetName, QStringLiteral("typed_capture_%1").arg(stamp));
    const QString suffix = QStringLiteral(".typed");
    LogSessionPaths paths;
    paths.typedSession = true;
    paths.recordPath = uniquePath(QDir(directory).filePath(stem + suffix), suffix);
    paths.suggestedSavePath = paths.recordPath;
    return paths;
}

StorageRuntime::LogSessionPaths StorageRuntime::makeLegacyLogPaths(const QString& stamp, bool includeModelSnapshot) {
    return makeLegacyLogPaths(stamp, includeModelSnapshot, defaultLogDirectory(), QString());
}

StorageRuntime::LogSessionPaths StorageRuntime::makeLegacyLogPaths(const QString& stamp, bool includeModelSnapshot, const QString& targetDirectory, const QString& targetName) {
    const QString tempDir = defaultTempLogDirectory();
    const QString directory = normalizeTargetDirectory(targetDirectory);
    const QString stem = sanitizeLogStem(targetName, QStringLiteral("can_log_%1").arg(stamp));
    const QString suffix = QStringLiteral(".bin");
    LogSessionPaths paths;
    paths.typedSession = false;
    paths.recordPath = QDir(tempDir).filePath(QStringLiteral("capture_%1.bin").arg(stamp));
    paths.metaPath = QDir(tempDir).filePath(QStringLiteral("capture_%1.meta.json").arg(stamp));
    paths.modelPath = includeModelSnapshot
        ? QDir(tempDir).filePath(QStringLiteral("capture_%1.model.json").arg(stamp))
        : QString();
    paths.suggestedSavePath = uniquePath(QDir(directory).filePath(stem + suffix), suffix);
    return paths;
}

StorageRuntime::PendingSavePaths StorageRuntime::makePendingSavePaths(const QString& requestedPath) {
    PendingSavePaths paths;
    const QString normalized = RuntimePaths::normalizeLocalPath(requestedPath);
    if (normalized.isEmpty()) return paths;
    paths.valid = true;
    paths.finalBin = normalized.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
        ? normalized
        : (normalized + QStringLiteral(".bin"));
    const QString base = paths.finalBin.left(paths.finalBin.size() - 4);
    paths.finalMeta = base + QStringLiteral(".meta.json");
    paths.finalModel = base + QStringLiteral(".model.json");
    return paths;
}

StorageRuntime::Paths StorageRuntime::makePaths(const QString& sessionDir) {
    QDir dir(sessionDir);
    const QString root = dir.absolutePath();
    Paths paths;
    paths.sessionDir = root;
    paths.streamPart = dir.filePath(QStringLiteral("capture.stream.part"));
    paths.streamFinal = dir.filePath(QStringLiteral("capture.stream"));
    paths.indexPart = dir.filePath(QStringLiteral("capture.index.part"));
    paths.indexFinal = dir.filePath(QStringLiteral("capture.index"));
    paths.metaPart = dir.filePath(QStringLiteral("session.meta.json.part"));
    paths.metaFinal = dir.filePath(QStringLiteral("session.meta.json"));
    paths.eventsPart = dir.filePath(QStringLiteral("events.jsonl.part"));
    paths.eventsFinal = dir.filePath(QStringLiteral("events.jsonl"));
    return paths;
}

void StorageRuntime::setError(QString* errorOut, const QString& message) {
    if (errorOut) *errorOut = message;
}

bool StorageRuntime::startTypedSession(const QString& sessionDir, const QJsonObject& metadata, QString* errorOut) {
    discard();
    m_paths = makePaths(sessionDir);
    QDir dir(m_paths.sessionDir);
    if (!dir.mkpath(QStringLiteral("."))) {
        setError(errorOut, QStringLiteral("Failed to create typed session directory: %1").arg(m_paths.sessionDir));
        return false;
    }

    m_stream.setFileName(m_paths.streamPart);
    if (!m_stream.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorOut, m_stream.errorString());
        return false;
    }

    m_index.setFileName(m_paths.indexPart);
    if (!m_index.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorOut, m_index.errorString());
        closeFiles();
        FilePersistence::removeFileIfExists(m_paths.streamPart);
        return false;
    }

    m_events.setFileName(m_paths.eventsPart);
    if (!m_events.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(errorOut, m_events.errorString());
        closeFiles();
        FilePersistence::removeFileIfExists(m_paths.streamPart);
        FilePersistence::removeFileIfExists(m_paths.indexPart);
        return false;
    }

    QJsonObject meta = metadata;
    meta.insert(QStringLiteral("format"), QStringLiteral("typed-evidence-stream-v1"));
    meta.insert(QStringLiteral("transport_version"), int(kTypedTransportVersion));
    meta.insert(QStringLiteral("created_local"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    meta.insert(QStringLiteral("stream_file"), QStringLiteral("capture.stream"));
    meta.insert(QStringLiteral("index_file"), QStringLiteral("capture.index"));
    meta.insert(QStringLiteral("events_file"), QStringLiteral("events.jsonl"));

    QString metaError;
    if (!FilePersistence::writeJsonAtomically(m_paths.metaPart, QJsonDocument(meta), &metaError)) {
        setError(errorOut, metaError);
        discard();
        return false;
    }

    m_active = true;
    m_recordCount = 0;
    m_bytesWritten = 0;
    return true;
}

bool StorageRuntime::appendTypedRecord(const TypedRecord& record, QString* errorOut) {
    if (!m_active || !m_stream.isOpen() || !m_index.isOpen()) {
        setError(errorOut, QStringLiteral("Typed storage session is not active."));
        return false;
    }
    if (record.frameBytes.size() < kTypedTransportFrameOverhead) {
        setError(errorOut, QStringLiteral("Typed record has no complete frame bytes."));
        return false;
    }

    const quint64 offset = quint64(m_stream.pos());
    const qint64 written = m_stream.write(record.frameBytes);
    if (written != record.frameBytes.size()) {
        setError(errorOut, m_stream.errorString().isEmpty() ? QStringLiteral("Failed to write typed stream bytes.") : m_stream.errorString());
        return false;
    }

    char indexEntry[24] = {};
    writeU64Le(indexEntry + 0, offset);
    writeU64Le(indexEntry + 8, typedRecordMonoUs(record));
    indexEntry[16] = char(record.header.recordType);
    indexEntry[17] = char(record.header.flags);
    writeU16Le(indexEntry + 18, record.header.seq);
    writeU16Le(indexEntry + 20, record.header.payloadLength);
    writeU16Le(indexEntry + 22, 0);

    const qint64 indexWritten = m_index.write(indexEntry, qint64(sizeof(indexEntry)));
    if (indexWritten != qint64(sizeof(indexEntry))) {
        setError(errorOut, m_index.errorString().isEmpty() ? QStringLiteral("Failed to write typed index entry.") : m_index.errorString());
        return false;
    }

    ++m_recordCount;
    m_bytesWritten += quint64(written);
    return true;
}

bool StorageRuntime::appendEventJsonLine(const QJsonObject& event, QString* errorOut) {
    if (!m_active || !m_events.isOpen()) {
        setError(errorOut, QStringLiteral("Typed storage session is not active."));
        return false;
    }
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    const qint64 written = m_events.write(line);
    if (written != line.size()) {
        setError(errorOut, m_events.errorString().isEmpty() ? QStringLiteral("Failed to write typed event line.") : m_events.errorString());
        return false;
    }
    return true;
}

bool StorageRuntime::replacePartFile(const QString& partPath, const QString& finalPath, QString* errorOut) {
    if (!QFileInfo::exists(partPath)) {
        setError(errorOut, QStringLiteral("Missing typed session part file: %1").arg(partPath));
        return false;
    }
    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath)) {
        setError(errorOut, QStringLiteral("Failed to replace existing typed session file: %1").arg(finalPath));
        return false;
    }
    if (!QFile::rename(partPath, finalPath)) {
        setError(errorOut, QStringLiteral("Failed to finalize typed session file: %1").arg(finalPath));
        return false;
    }
    return true;
}

bool StorageRuntime::finalizeTypedSession(QString* errorOut) {
    if (!m_active) return true;

    m_stream.flush();
    m_index.flush();
    m_events.flush();
    closeFiles();

    if (!replacePartFile(m_paths.streamPart, m_paths.streamFinal, errorOut)) return false;
    if (!replacePartFile(m_paths.indexPart, m_paths.indexFinal, errorOut)) return false;
    if (!replacePartFile(m_paths.metaPart, m_paths.metaFinal, errorOut)) return false;
    if (!replacePartFile(m_paths.eventsPart, m_paths.eventsFinal, errorOut)) return false;
    m_active = false;
    return true;
}

void StorageRuntime::closeFiles() {
    if (m_stream.isOpen()) m_stream.close();
    if (m_index.isOpen()) m_index.close();
    if (m_events.isOpen()) m_events.close();
}

void StorageRuntime::discard() {
    closeFiles();
    if (!m_paths.streamPart.isEmpty()) FilePersistence::removeFileIfExists(m_paths.streamPart);
    if (!m_paths.indexPart.isEmpty()) FilePersistence::removeFileIfExists(m_paths.indexPart);
    if (!m_paths.metaPart.isEmpty()) FilePersistence::removeFileIfExists(m_paths.metaPart);
    if (!m_paths.eventsPart.isEmpty()) FilePersistence::removeFileIfExists(m_paths.eventsPart);
    m_active = false;
    m_recordCount = 0;
    m_bytesWritten = 0;
}
