#include "analysis/AnalysisWorkerRuntime.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

FrameRecord makeFrame(quint64 monoUs, quint32 canId = 0x530, quint8 bus = 0) {
    FrameRecord frame;
    frame.tExtUs = monoUs;
    frame.canId = canId;
    frame.bus = bus;
    frame.dlc = 8;
    for (int index = 0; index < 8; ++index) frame.data[index] = quint8(index);
    frame.hasCaptureSeq = true;
    frame.captureSeq = monoUs / 1000;
    return frame;
}

} // namespace

class AnalysisWorkerRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsFramesAndEmitsSnapshotFromWorkerQueue() {
        CanMonitorAnalysis::AnalysisWorkerRuntime worker(16);
        QSignalSpy snapshotSpy(&worker, &CanMonitorAnalysis::AnalysisWorkerRuntime::snapshotReady);
        QSignalSpy statusSpy(&worker, &CanMonitorAnalysis::AnalysisWorkerRuntime::statusChanged);

        CanMonitorAnalysis::AnalysisRuntime::Config config;
        config.maxStateKeys = 64;
        config.maxRowsPerSnapshot = 64;
        worker.setConfig(config);
        snapshotSpy.clear();
        statusSpy.clear();

        FrameRecordList frames;
        frames << makeFrame(1000, 0x530, 0) << makeFrame(2000, 0x531, 1);
        worker.enqueueFrames(frames);
        QTRY_VERIFY(snapshotSpy.size() >= 1);
        QTRY_VERIFY(statusSpy.size() >= 1);

        const auto snapshot = snapshotSpy.takeLast();
        QCOMPARE(snapshot.at(0).toString(), QStringLiteral("live"));
        QVERIFY(snapshot.at(3).toList().size() >= 1);
        QVERIFY(snapshot.at(4).toList().size() >= 2);

        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(0).toULongLong(), quint64(0));
        QCOMPARE(status.at(2).toULongLong(), quint64(16));
        QCOMPARE(status.at(3).toULongLong(), quint64(2));
        QCOMPARE(status.at(4).toULongLong(), quint64(2));
        QCOMPARE(status.at(5).toULongLong(), quint64(0));
    }

    void boundedQueueReportsTruthLossOnOverrun() {
        CanMonitorAnalysis::AnalysisWorkerRuntime worker(2);
        QSignalSpy statusSpy(&worker, &CanMonitorAnalysis::AnalysisWorkerRuntime::statusChanged);
        QSignalSpy errorSpy(&worker, &CanMonitorAnalysis::AnalysisWorkerRuntime::errorOccurred);

        CanMonitorAnalysis::AnalysisRuntime::Config config;
        config.maxStateKeys = 64;
        config.maxRowsPerSnapshot = 64;
        worker.setConfig(config);
        statusSpy.clear();

        FrameRecordList frames;
        frames << makeFrame(1000, 0x530, 0)
               << makeFrame(2000, 0x531, 0)
               << makeFrame(3000, 0x532, 0);
        worker.enqueueFrames(frames);
        QTRY_VERIFY(statusSpy.size() >= 1);
        QCOMPARE(errorSpy.size(), 1);

        const auto status = statusSpy.takeLast();
        QCOMPARE(status.at(2).toULongLong(), quint64(2));
        QCOMPARE(status.at(5).toULongLong(), quint64(1));
        QVERIFY(status.at(9).toULongLong() >= quint64(1));
    }
};

QTEST_MAIN(AnalysisWorkerRuntimeTest)
#include "test_analysis_worker_runtime.moc"
