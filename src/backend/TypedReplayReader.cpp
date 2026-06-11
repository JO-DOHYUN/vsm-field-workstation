#include "TypedReplayReader.h"

#include "TypedTransportParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>

namespace {

QString byteOffsetText(quint64 offset) {
    return QStringLiteral("offset %1").arg(offset);
}

QString firstExistingPath(const QDir& dir, const QStringList& names) {
    for (const QString& name : names) {
        const QString path = dir.filePath(name);
        if (QFileInfo::exists(path)) return QDir::fromNativeSeparators(path);
    }
    return {};
}

struct EventFileInfo {
    quint64 count = 0;
    QString first;
    QString last;
};

struct IndexFileInfo {
    bool aligned = true;
    quint64 byteCount = 0;
    quint64 entryCount = 0;
    quint64 remainderBytes = 0;
    quint64 mismatchCount = 0;
    quint64 firstOffset = 0;
    quint64 lastOffset = 0;
    quint64 firstMonoUs = 0;
    quint64 lastMonoUs = 0;
};

EventFileInfo readEventFileInfo(const QString& path) {
    EventFileInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return info;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine().trimmed());
        if (line.isEmpty()) continue;
        if (info.first.isEmpty()) info.first = line.left(160);
        info.last = line.left(160);
        ++info.count;
    }
    return info;
}

void readMetaInfo(const QString& path, TypedReplayReader::Summary& summary) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    summary.metaFormat = root.value(QStringLiteral("format")).toString();
    summary.metaCreatedLocal = root.value(QStringLiteral("created_local")).toString();
    summary.metaStreamFile = root.value(QStringLiteral("stream_file")).toString();
}

IndexFileInfo readIndexFileInfo(const QString& path, const QVector<TypedReplayReader::RecordEntry>& records) {
    IndexFileInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return info;
    const QByteArray bytes = file.readAll();
    info.byteCount = quint64(bytes.size());
    info.remainderBytes = quint64(bytes.size() % 24);
    info.aligned = info.remainderBytes == 0;
    info.entryCount = quint64(bytes.size() / 24);

    const quint64 compareCount = std::min<quint64>(info.entryCount, quint64(records.size()));
    if (info.entryCount != quint64(records.size())) {
        info.mismatchCount += info.entryCount > quint64(records.size())
            ? info.entryCount - quint64(records.size())
            : quint64(records.size()) - info.entryCount;
    }
    for (quint64 index = 0; index < compareCount; ++index) {
        const auto* p = reinterpret_cast<const quint8*>(bytes.constData() + qsizetype(index * 24));
        const quint64 offset = typedReadU64Le(p);
        const quint64 monoUs = typedReadU64Le(p + 8);
        const quint8 recordType = p[16];
        const quint8 flags = p[17];
        const quint16 seq = typedReadU16Le(p + 18);
        const quint16 payloadLength = typedReadU16Le(p + 20);
        if (index == 0) {
            info.firstOffset = offset;
            info.firstMonoUs = monoUs;
        }
        info.lastOffset = offset;
        info.lastMonoUs = monoUs;

        const auto& record = records.at(qsizetype(index));
        if (offset != record.offset ||
            monoUs != record.monoUs ||
            recordType != record.record.header.recordType ||
            flags != record.record.header.flags ||
            seq != record.record.header.seq ||
            payloadLength != record.record.header.payloadLength) {
            ++info.mismatchCount;
        }
    }
    return info;
}

void finishSummaryDiagnostics(TypedReplayReader::Summary& summary, int faultCount) {
    summary.durationUs = summary.hasRecords && summary.lastMonoUs >= summary.firstMonoUs
        ? summary.lastMonoUs - summary.firstMonoUs
        : 0;
    summary.partialCapture = summary.streamPart || summary.metaPart || summary.indexPart || summary.eventsPart || summary.trailingBytes > 0;
    if (summary.partialCapture) {
        summary.captureState = QStringLiteral("PARTIAL");
    } else if (summary.crcFailures > 0 ||
               summary.lengthFailures > 0 ||
               summary.seqGaps > 0 ||
               summary.trailingBytes > 0 ||
               !summary.indexSizeAligned ||
               summary.indexMismatchCount > 0) {
        summary.captureState = QStringLiteral("DEGRADED");
    } else {
        summary.captureState = QStringLiteral("FINALIZED");
    }

    QStringList parts;
    parts << summary.captureState;
    parts << QStringLiteral("%1 record(s)").arg(summary.recordCount);
    parts << QStringLiteral("%1 byte(s)").arg(summary.bytesRead);
    parts << QStringLiteral("duration %1ms").arg(summary.durationUs / 1000.0, 0, 'f', 1);
    if (summary.indexPresent) parts << QStringLiteral("index %1").arg(summary.indexEntryCount);
    else parts << QStringLiteral("index missing");
    if (summary.indexMismatchCount > 0) parts << QStringLiteral("index mismatch %1").arg(summary.indexMismatchCount);
    if (summary.metaPresent) parts << QStringLiteral("meta ok");
    else parts << QStringLiteral("meta missing");
    if (summary.eventsPresent) parts << QStringLiteral("events %1").arg(summary.eventLineCount);
    else parts << QStringLiteral("events missing");
    if (faultCount > 0) parts << QStringLiteral("faults %1").arg(faultCount);
    summary.diagnosticSummary = parts.join(QStringLiteral(" | "));
}

} // namespace

void TypedReplayReader::reset() {
    m_records.clear();
    m_faults.clear();
    m_summary = {};
}

void TypedReplayReader::setError(QString* errorOut, const QString& message) {
    if (errorOut) *errorOut = message;
}

qsizetype TypedReplayReader::findSof(const QByteArray& bytes, qsizetype from) {
    for (qsizetype index = qMax<qsizetype>(0, from); index + 1 < bytes.size(); ++index) {
        const auto b0 = quint8(bytes.at(index));
        const auto b1 = quint8(bytes.at(index + 1));
        if (b0 == kTypedTransportSof0 && b1 == kTypedTransportSof1) return index;
    }
    return -1;
}

void TypedReplayReader::addFault(quint64 offset, const QString& code, const QString& message) {
    Fault fault;
    fault.offset = offset;
    fault.code = code;
    fault.message = message;
    m_faults.append(fault);
}

bool TypedReplayReader::loadPath(const QString& path, QString* errorOut) {
    const QFileInfo info(path);
    QString streamPath;
    QString sessionDir;
    bool sessionContainer = false;
    bool streamFinal = false;
    bool streamPart = false;

    if (info.isDir()) {
        sessionContainer = true;
        QDir dir(info.absoluteFilePath());
        sessionDir = QDir::fromNativeSeparators(dir.absolutePath());
        const QString finalPath = dir.filePath(QStringLiteral("capture.stream"));
        const QString partPath = dir.filePath(QStringLiteral("capture.stream.part"));
        if (QFileInfo::exists(finalPath)) {
            streamPath = QDir::fromNativeSeparators(finalPath);
            streamFinal = true;
        } else if (QFileInfo::exists(partPath)) {
            streamPath = QDir::fromNativeSeparators(partPath);
            streamPart = true;
        }
    } else if (info.isFile()) {
        const QString fileName = info.fileName();
        if (fileName.compare(QStringLiteral("capture.stream"), Qt::CaseInsensitive) == 0) {
            streamPath = QDir::fromNativeSeparators(info.absoluteFilePath());
            sessionDir = QDir::fromNativeSeparators(info.dir().absolutePath());
            streamFinal = true;
        } else if (fileName.compare(QStringLiteral("capture.stream.part"), Qt::CaseInsensitive) == 0) {
            streamPath = QDir::fromNativeSeparators(info.absoluteFilePath());
            sessionDir = QDir::fromNativeSeparators(info.dir().absolutePath());
            streamPart = true;
        } else {
            streamPath = QDir::fromNativeSeparators(info.absoluteFilePath());
            sessionDir = QDir::fromNativeSeparators(info.dir().absolutePath());
        }
    }

    if (streamPath.isEmpty()) {
        setError(errorOut, QStringLiteral("Typed replay path has no capture.stream or capture.stream.part: %1").arg(path));
        reset();
        return false;
    }

    const bool ok = loadFile(streamPath, errorOut);
    if (!ok) return false;

    QDir dir(sessionDir);
    const QString metaPath = firstExistingPath(dir, {QStringLiteral("session.meta.json"), QStringLiteral("session.meta.json.part")});
    const QString indexPath = firstExistingPath(dir, streamPart
        ? QStringList{QStringLiteral("capture.index.part"), QStringLiteral("capture.index")}
        : QStringList{QStringLiteral("capture.index"), QStringLiteral("capture.index.part")});
    const QString eventsPath = firstExistingPath(dir, streamPart
        ? QStringList{QStringLiteral("events.jsonl.part"), QStringLiteral("events.jsonl")}
        : QStringList{QStringLiteral("events.jsonl"), QStringLiteral("events.jsonl.part")});

    m_summary.inputPath = QDir::fromNativeSeparators(info.absoluteFilePath());
    m_summary.sessionDir = sessionDir;
    m_summary.streamPath = streamPath;
    m_summary.metaPath = metaPath;
    m_summary.indexPath = indexPath;
    m_summary.eventsPath = eventsPath;
    m_summary.sessionContainer = sessionContainer;
    m_summary.streamFinal = streamFinal;
    m_summary.streamPart = streamPart;
    m_summary.metaPresent = !metaPath.isEmpty();
    m_summary.indexPresent = !indexPath.isEmpty();
    m_summary.eventsPresent = !eventsPath.isEmpty();
    m_summary.metaPart = metaPath.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive);
    m_summary.indexPart = indexPath.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive);
    m_summary.eventsPart = eventsPath.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive);
    if (m_summary.metaPresent) {
        readMetaInfo(metaPath, m_summary);
    }
    if (m_summary.indexPresent) {
        const IndexFileInfo indexInfo = readIndexFileInfo(indexPath, m_records);
        m_summary.indexSizeAligned = indexInfo.aligned;
        m_summary.indexByteCount = indexInfo.byteCount;
        m_summary.indexEntryCount = indexInfo.entryCount;
        m_summary.indexRemainderBytes = indexInfo.remainderBytes;
        m_summary.indexMismatchCount = indexInfo.mismatchCount;
        m_summary.indexFirstOffset = indexInfo.firstOffset;
        m_summary.indexLastOffset = indexInfo.lastOffset;
        m_summary.indexFirstMonoUs = indexInfo.firstMonoUs;
        m_summary.indexLastMonoUs = indexInfo.lastMonoUs;
    }
    if (m_summary.eventsPresent) {
        const EventFileInfo eventInfo = readEventFileInfo(eventsPath);
        m_summary.eventLineCount = eventInfo.count;
        m_summary.firstEventText = eventInfo.first;
        m_summary.lastEventText = eventInfo.last;
    }
    finishSummaryDiagnostics(m_summary, m_faults.size());
    return true;
}

bool TypedReplayReader::loadFile(const QString& path, QString* errorOut) {
    reset();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorOut, QStringLiteral("Failed to open typed replay stream: %1").arg(file.errorString()));
        return false;
    }

    const QByteArray bytes = file.readAll();
    m_summary.bytesRead = quint64(bytes.size());
    const QFileInfo streamInfo(path);
    m_summary.inputPath = QDir::fromNativeSeparators(streamInfo.absoluteFilePath());
    m_summary.streamPath = QDir::fromNativeSeparators(streamInfo.absoluteFilePath());
    m_summary.sessionDir = QDir::fromNativeSeparators(streamInfo.dir().absolutePath());
    m_summary.streamFinal = streamInfo.fileName().compare(QStringLiteral("capture.stream"), Qt::CaseInsensitive) == 0;
    m_summary.streamPart = streamInfo.fileName().compare(QStringLiteral("capture.stream.part"), Qt::CaseInsensitive) == 0;

    qsizetype pos = 0;
    bool haveLastSeq = false;
    quint16 lastSeq = 0;

    while (pos < bytes.size()) {
        const qsizetype sof = findSof(bytes, pos);
        if (sof < 0) {
            const qsizetype dropped = bytes.size() - pos;
            if (dropped > 0) {
                m_summary.bytesDropped += quint64(dropped);
                addFault(quint64(pos), QStringLiteral("garbage_tail"),
                         QStringLiteral("Dropped %1 byte(s) after last valid typed frame.").arg(dropped));
            }
            break;
        }

        if (sof > pos) {
            const qsizetype dropped = sof - pos;
            m_summary.bytesDropped += quint64(dropped);
            addFault(quint64(pos), QStringLiteral("garbage"),
                     QStringLiteral("Dropped %1 byte(s) before typed SOF.").arg(dropped));
            pos = sof;
        }

        const qsizetype remaining = bytes.size() - pos;
        if (remaining < kTypedTransportFrameOverhead) {
            m_summary.trailingBytes = quint64(remaining);
            addFault(quint64(pos), QStringLiteral("incomplete_frame"),
                     QStringLiteral("Incomplete typed frame header at %1.").arg(byteOffsetText(quint64(pos))));
            break;
        }

        const auto* p = reinterpret_cast<const quint8*>(bytes.constData() + pos);
        const quint16 payloadLength = typedReadU16Le(p + 7);
        if (payloadLength > kTypedTransportMaxPayloadLength) {
            ++m_summary.lengthFailures;
            addFault(quint64(pos), QStringLiteral("length"),
                     QStringLiteral("Typed payload length %1 exceeds maximum %2 at %3.")
                         .arg(payloadLength)
                         .arg(kTypedTransportMaxPayloadLength)
                         .arg(byteOffsetText(quint64(pos))));
            ++pos;
            ++m_summary.bytesDropped;
            continue;
        }

        const qsizetype frameLength = kTypedTransportFrameOverhead + qsizetype(payloadLength);
        if (remaining < frameLength) {
            m_summary.trailingBytes = quint64(remaining);
            addFault(quint64(pos), QStringLiteral("incomplete_frame"),
                     QStringLiteral("Incomplete typed frame body at %1; expected %2 byte(s), have %3.")
                         .arg(byteOffsetText(quint64(pos)))
                         .arg(frameLength)
                         .arg(remaining));
            break;
        }

        const quint16 expectedCrc = typedReadU16Le(p + 9 + payloadLength);
        const quint16 actualCrc = TypedTransportParser::crc16Ccitt(p + 2, kTypedTransportHeaderSize + payloadLength);
        if (expectedCrc != actualCrc) {
            ++m_summary.crcFailures;
            addFault(quint64(pos), QStringLiteral("crc"),
                     QStringLiteral("Typed frame CRC mismatch at %1.").arg(byteOffsetText(quint64(pos))));
            ++pos;
            ++m_summary.bytesDropped;
            continue;
        }

        TypedRecord record;
        record.header.version = p[2];
        record.header.recordType = p[3];
        record.header.flags = p[4];
        record.header.seq = typedReadU16Le(p + 5);
        record.header.payloadLength = payloadLength;
        record.payload = bytes.mid(pos + 9, payloadLength);
        record.frameBytes = bytes.mid(pos, frameLength);

        if (record.header.version != kTypedTransportVersion) {
            ++m_summary.versionWarnings;
            addFault(quint64(pos), QStringLiteral("version"),
                     QStringLiteral("Typed frame version %1 differs from expected %2 at %3.")
                         .arg(record.header.version)
                         .arg(kTypedTransportVersion)
                         .arg(byteOffsetText(quint64(pos))));
        }

        if (!haveLastSeq) {
            haveLastSeq = true;
            m_summary.firstSeq = record.header.seq;
        } else {
            const quint16 expectedSeq = quint16(lastSeq + 1);
            if (record.header.seq != expectedSeq) {
                ++m_summary.seqGaps;
                addFault(quint64(pos), QStringLiteral("seq_gap"),
                         QStringLiteral("Typed sequence gap at %1: expected %2, got %3.")
                             .arg(byteOffsetText(quint64(pos)))
                             .arg(expectedSeq)
                             .arg(record.header.seq));
            }
        }
        lastSeq = record.header.seq;
        m_summary.lastSeq = record.header.seq;

        const quint64 monoUs = typedRecordMonoUs(record);
        if (!m_summary.hasRecords) {
            m_summary.hasRecords = true;
            m_summary.firstMonoUs = monoUs;
        }
        m_summary.lastMonoUs = monoUs;
        m_summary.typeCounts[record.header.recordType] += 1;

        RecordEntry entry;
        entry.offset = quint64(pos);
        entry.monoUs = monoUs;
        entry.record = record;
        m_records.append(entry);

        pos += frameLength;
    }

    m_summary.recordCount = quint64(m_records.size());
    if (m_records.isEmpty()) {
        setError(errorOut, QStringLiteral("No valid typed replay records found in %1.").arg(path));
        finishSummaryDiagnostics(m_summary, m_faults.size());
        return false;
    }

    finishSummaryDiagnostics(m_summary, m_faults.size());
    return true;
}
