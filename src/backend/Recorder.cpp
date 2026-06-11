#include "Recorder.h"
#include "AppLogging.h"
#include "FilePersistence.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

Recorder::Recorder(QObject* parent)
    : QObject(parent),
      m_drainTimer(this) {
    m_drainTimer.setInterval(220);
    connect(&m_drainTimer, &QTimer::timeout, this, [this]() {
        flushBuffer(false);
    });
}

bool Recorder::start(const QString& binPath, const QString& metaJson, const QString& rulesSnapshotPath, const QString& rulesSourcePath, QString* errorOut) {
    stop();
    m_lastError.clear();
    QDir().mkpath(QFileInfo(binPath).absolutePath());
    m_bin.setFileName(binPath);
    if (!m_bin.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = m_bin.errorString();
        if (errorOut) *errorOut = m_lastError;
        return false;
    }

    m_writeBuffer.clear();
    m_writeBuffer.reserve(262144);
    m_bytesWritten = 0;
    m_frameCount = 0;
    m_framesSinceFlush = 0;
    m_flushTimer.restart();
    m_fileFlushTimer.restart();
    m_drainTimer.start();

    QJsonObject meta;
    meta["created_local"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    meta["record_size"] = 20;
    meta["format"] = "board-can-record-v1";
    meta["note"] = "raw 20-byte records from board";
    QString metaError;
    if (!FilePersistence::writeJsonAtomically(metaJson, QJsonDocument(meta), &metaError)) {
        qCWarning(logDeploy).noquote() << "Recorder metadata write failed:" << metaError;
        m_lastError = metaError;
        m_bin.close();
        QString cleanupError;
        FilePersistence::removeFileIfExists(binPath, &cleanupError);
        if (!cleanupError.isEmpty()) {
            qCWarning(logDeploy).noquote() << "Recorder bin cleanup after metadata failure failed:" << cleanupError;
        }
        if (errorOut) *errorOut = m_lastError;
        return false;
    }

    m_pendingRulesSnapshotPath = rulesSnapshotPath;
    m_pendingRulesSourcePath = rulesSourcePath;
    QString cleanupError;
    if (!FilePersistence::removeFileIfExists(m_pendingRulesSnapshotPath, &cleanupError) && !cleanupError.isEmpty()) {
        qCWarning(logDeploy).noquote() << "Recorder stale model snapshot cleanup failed:" << cleanupError;
    }
    return true;
}

void Recorder::flushBuffer(bool force) {
    if (!m_bin.isOpen()) {
        m_writeBuffer.clear();
        m_framesSinceFlush = 0;
        return;
    }
    if (m_writeBuffer.isEmpty()) return;
    if (!force && m_writeBuffer.size() < 262144 && m_framesSinceFlush < 8192 && m_flushTimer.isValid() && m_flushTimer.elapsed() < 700) return;
    const qint64 written = m_bin.write(m_writeBuffer);
    if (written > 0) m_bytesWritten += quint64(written);
    m_writeBuffer.clear();
    m_framesSinceFlush = 0;
    const bool needFsFlush = force || !m_fileFlushTimer.isValid() || m_fileFlushTimer.elapsed() >= 3000;
    if (needFsFlush) {
        m_bin.flush();
        m_fileFlushTimer.restart();
    }
    m_flushTimer.restart();
}

void Recorder::stop() {
    m_drainTimer.stop();
    flushBuffer(true);
    if (m_bin.isOpen()) m_bin.close();

    if (!m_pendingRulesSourcePath.isEmpty() && !m_pendingRulesSnapshotPath.isEmpty()) {
        QString snapshotError;
        if (!FilePersistence::copyFileAtomically(m_pendingRulesSourcePath, m_pendingRulesSnapshotPath, &snapshotError)) {
            m_lastError = snapshotError;
            qCWarning(logDeploy).noquote() << "Recorder rules snapshot copy failed:" << snapshotError;
        }
    }
    m_pendingRulesSnapshotPath.clear();
    m_pendingRulesSourcePath.clear();
}

void Recorder::append20(const QByteArray& raw20) {
    if (!(m_bin.isOpen() && raw20.size() == 20)) return;
    m_writeBuffer += raw20;
    ++m_frameCount;
    ++m_framesSinceFlush;
}

void Recorder::flushPending(bool force) {
    flushBuffer(force);
}
