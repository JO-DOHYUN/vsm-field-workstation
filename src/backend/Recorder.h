#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QElapsedTimer>
#include <QTimer>

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr);

    bool start(const QString& binPath, const QString& metaJson, const QString& rulesSnapshotPath, const QString& rulesSourcePath, QString* errorOut = nullptr);
    void stop();
    bool isActive() const { return m_bin.isOpen(); }
    QString currentPath() const { return m_bin.fileName(); }
    QString lastError() const { return m_lastError; }
    quint64 bytesWritten() const { return m_bytesWritten; }
    quint64 frameCount() const { return m_frameCount; }
    void append20(const QByteArray& raw20);
    void flushPending(bool force = false);

private:
    void flushBuffer(bool force = false);

    QFile m_bin;
    QByteArray m_writeBuffer;
    QElapsedTimer m_flushTimer;
    QElapsedTimer m_fileFlushTimer;
    QTimer m_drainTimer;
    quint64 m_bytesWritten = 0;
    quint64 m_frameCount = 0;
    int m_framesSinceFlush = 0;
    QString m_pendingRulesSnapshotPath;
    QString m_pendingRulesSourcePath;
    QString m_lastError;
};
