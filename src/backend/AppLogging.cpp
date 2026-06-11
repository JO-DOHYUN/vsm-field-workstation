#include "AppLogging.h"

#include "RuntimePaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(logTransport, "canmonitor.transport")
Q_LOGGING_CATEGORY(logReplay, "canmonitor.replay")
Q_LOGGING_CATEGORY(logAnalysis, "canmonitor.analysis")
Q_LOGGING_CATEGORY(logGraph, "canmonitor.graph")
Q_LOGGING_CATEGORY(logModel, "canmonitor.model")
Q_LOGGING_CATEGORY(logDeploy, "canmonitor.deploy")
Q_LOGGING_CATEGORY(logUi, "canmonitor.ui")

namespace {

QMutex g_logMutex;
QtMessageHandler g_previousHandler = nullptr;
std::unique_ptr<QFile> g_logFile;
QString g_sessionLogPath;
bool g_initialized = false;

QString levelText(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

QString formatLine(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString category = (context.category && context.category[0] != '\0')
        ? QString::fromUtf8(context.category)
        : QStringLiteral("default");
    return QStringLiteral("%1 [%2] [%3] %4")
        .arg(timestamp, levelText(type), category, message);
}

void pruneOldLogs(const QString& rootDir) {
    QDir dir(rootDir);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("session_*.log"),
                                                  QDir::Files,
                                                  QDir::Time | QDir::Reversed);
    constexpr int kMaxLogs = 10;
    for (int index = 0; index < files.size() - kMaxLogs; ++index) {
        QFile::remove(files.at(index).absoluteFilePath());
    }
}

void writeHeader(const BuildMetadata::Info& info) {
    if (!g_logFile || !g_logFile->isOpen()) return;
    QTextStream stream(g_logFile.get());
    stream << "# CAN Monitor session log\n";
    stream << "application=" << info.applicationName << '\n';
    stream << "version=" << info.version << '\n';
    stream << "build_type=" << info.buildType << '\n';
    stream << "baseline_id=" << info.baselineId << '\n';
    stream << "build_timestamp_utc=" << info.buildTimestampUtc << '\n';
    stream << "package_id=" << info.packageId << '\n';
    stream << "log_created_utc=" << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n\n";
    stream.flush();
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QString line = formatLine(type, context, message);
    {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            QTextStream stream(g_logFile.get());
            stream << line << '\n';
            stream.flush();
        }
    }

    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    } else {
        std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
        std::fflush(stderr);
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

namespace AppLogging {

void initialize(const BuildMetadata::Info& info) {
    QMutexLocker locker(&g_logMutex);
    if (g_initialized) return;

    const QString rootDir = RuntimePaths::ensureLogsRoot();
    pruneOldLogs(rootDir);

    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    g_sessionLogPath = QDir(rootDir).filePath(QStringLiteral("session_%1.log").arg(stamp));
    g_logFile = std::make_unique<QFile>(g_sessionLogPath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        writeHeader(info);
    } else {
        g_sessionLogPath.clear();
        g_logFile.reset();
    }

    g_previousHandler = qInstallMessageHandler(fileMessageHandler);
    g_initialized = true;
}

void shutdown() {
    QMutexLocker locker(&g_logMutex);
    if (!g_initialized) return;
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    if (g_logFile) {
        g_logFile->flush();
        g_logFile->close();
        g_logFile.reset();
    }
    g_initialized = false;
}

QString sessionLogFilePath() {
    QMutexLocker locker(&g_logMutex);
    return g_sessionLogPath;
}

} // namespace AppLogging
