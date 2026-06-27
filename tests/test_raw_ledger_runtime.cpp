#include "RawFrameTableModel.h"
#include "transport/RawLedgerRuntime.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

namespace {
void appendU16(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 value) {
    for (int byte = 0; byte < 4; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

void appendU64(QByteArray& out, quint64 value) {
    for (int byte = 0; byte < 8; ++byte) out.append(char((value >> (byte * 8)) & 0xFF));
}

FrameRecord makeFrame(quint64 us, quint32 id, quint8 bus, quint8 dlc, quint8 seed) {
    FrameRecord frame;
    frame.tExtUs = us;
    frame.canId = id;
    frame.bus = bus;
    frame.dlc = dlc;
    frame.ext = false;
    frame.rtr = false;
    frame.seq = seed;
    for (int i = 0; i < 8; ++i) frame.data[i] = quint8(seed + i);
    return frame;
}

TypedRecord makeSegmentRecord() {
    QByteArray payload;
    appendU64(payload, 3);
    appendU64(payload, 900);
    appendU16(payload, 2);
    payload.append(char(kTypedCanRxSegmentEntrySize));
    payload.append(char(0x01));
    appendU32(payload, 0);
    appendU32(payload, 0);
    appendU32(payload, 0);

    auto appendEntry = [&payload](quint64 captureSeq, quint64 us, quint8 bus, quint32 id, quint8 seed) {
        appendU64(payload, captureSeq);
        appendU64(payload, us);
        appendU32(payload, id);
        payload.append(char(8));
        payload.append(char(bus));
        for (int i = 0; i < 8; ++i) payload.append(char(seed + i));
    };
    appendEntry(900, 2000, 0, 0x620, 0x50);
    appendEntry(901, 2100, 1, 0x720, 0x4B);

    TypedRecord record;
    record.header.recordType = static_cast<quint8>(TypedRecordType::CanRxSegment);
    record.header.seq = 77;
    record.header.payloadLength = quint16(payload.size());
    record.payload = payload;
    return record;
}

FrameRecordList makeSegmentFrames() {
    FrameRecord first = makeFrame(2000, 0x620, 0, 8, 0x50);
    first.hasCaptureSeq = true;
    first.captureSeq = 900;
    first.seq = 77;

    FrameRecord second = makeFrame(2100, 0x720, 1, 8, 0x4B);
    second.hasCaptureSeq = true;
    second.captureSeq = 901;
    second.seq = 77;
    return FrameRecordList{first, second};
}
}

class RawLedgerRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void appendPreservesOrderAndPayload() {
        CanMonitorTransport::RawLedgerRuntime ledger;
        QVERIFY(ledger.reset(QStringLiteral("unit")));

        FrameRecordList frames;
        frames << makeFrame(1000, 0x120, 0, 8, 0x10)
               << makeFrame(1100, 0x121, 1, 3, 0x20)
               << makeFrame(1200, 0x120, 0, 1, 0x30);

        const auto result = ledger.appendFrames(frames);
        QVERIFY(result.ok);
        QCOMPARE(result.appended, 3);
        QCOMPARE(ledger.rowCount(), quint64(3));
        QVERIFY(ledger.segmentBytes() > 0);

        const auto row0 = ledger.readRow(0);
        const auto row1 = ledger.readRow(1);
        const auto row2 = ledger.readRow(2);
        QVERIFY(row0.has_value());
        QVERIFY(row1.has_value());
        QVERIFY(row2.has_value());
        QCOMPARE(row0->ledgerSeq, quint64(0));
        QCOMPARE(row1->ledgerSeq, quint64(1));
        QCOMPARE(row2->ledgerSeq, quint64(2));
        QCOMPARE(row1->canId, quint32(0x121));
        QCOMPARE(row1->bus, quint8(1));
        QCOMPARE(row1->dlc, quint8(3));
        QCOMPARE(row1->data[0], quint8(0x20));
        QCOMPARE(row1->data[2], quint8(0x22));
        const auto diag = ledger.diagnostics();
        QCOMPARE(diag.committedRows, quint64(3));
        QCOMPARE(diag.cacheRows, 3);
        QVERIFY(diag.cacheHits >= 3);
        QCOMPARE(diag.fileReadFailCount, quint64(0));
    }

    void tableModelFiltersWithoutDroppingTruth() {
        RawFrameTableModel model;
        QSignalSpy rowsSpy(&model, &RawFrameTableModel::rowsAppended);

        FrameRecordList frames;
        frames << makeFrame(1000, 0x120, 0, 8, 0x10)
               << makeFrame(1100, 0x121, 1, 8, 0x20)
               << makeFrame(1200, 0x122, 1, 2, 0x30);
        model.appendFrames(frames);

        QCOMPARE(model.totalRows(), quint64(3));
        QCOMPARE(model.count(), 3);
        QCOMPARE(rowsSpy.count(), 1);
        QCOMPARE(model.data(model.index(1, 0), RawFrameTableModel::IdRole).toUInt(), quint32(0x121));

        model.setBusFilter(1);
        QCOMPARE(model.totalRows(), quint64(3));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.data(model.index(0, 0), RawFrameTableModel::BusRole).toInt(), 1);

        model.setIdFilter(QStringLiteral("0x122"));
        QCOMPARE(model.totalRows(), quint64(3));
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0, 0), RawFrameTableModel::DlcRole).toInt(), 2);
        QCOMPARE(model.droppedDisplayRows(), quint64(0));
    }

    void appendTypedSegmentPreservesRowsAndCaptureSeq() {
        CanMonitorTransport::RawLedgerRuntime ledger;
        QVERIFY(ledger.reset(QStringLiteral("segment")));
        const auto result = ledger.appendFrames(makeSegmentFrames());
        QVERIFY(result.ok);
        QCOMPARE(result.appended, 2);
        QCOMPARE(ledger.rowCount(), quint64(2));

        const auto row0 = ledger.readRow(0);
        const auto row1 = ledger.readRow(1);
        QVERIFY(row0.has_value());
        QVERIFY(row1.has_value());
        QVERIFY(row0->hasCaptureSeq);
        QCOMPARE(row0->captureSeq, quint64(900));
        QCOMPARE(row0->canId, quint32(0x620));
        QCOMPARE(row0->bus, quint8(0));
        QCOMPARE(row1->captureSeq, quint64(901));
        QCOMPARE(row1->canId, quint32(0x720));
        QCOMPARE(row1->bus, quint8(1));
    }

    void malformedSegmentDoesNotCreateBlankRows() {
        CanMonitorTransport::RawLedgerRuntime ledger;
        QVERIFY(ledger.reset(QStringLiteral("malformed")));
        const auto result = ledger.appendFrames(FrameRecordList{});

        QVERIFY(result.ok);
        QCOMPARE(ledger.rowCount(), quint64(0));

        RawFrameTableModel model;
        model.appendFrames(FrameRecordList{});
        QCOMPARE(model.totalRows(), quint64(0));
        QCOMPARE(model.count(), 0);
    }

    void latestRowsStayReadableFromCommittedCache() {
        CanMonitorTransport::RawLedgerRuntime ledger;
        QVERIFY(ledger.reset(QStringLiteral("cache")));

        FrameRecordList frames;
        constexpr int frameCount = 8300;
        frames.reserve(frameCount);
        for (int index = 0; index < frameCount; ++index) {
            frames << makeFrame(1000 + quint64(index), 0x300 + quint32(index % 64), quint8(index % 2), 8, quint8(index));
        }

        const auto result = ledger.appendFrames(frames);
        QVERIFY(result.ok);
        QCOMPARE(result.appended, frameCount);
        QCOMPARE(ledger.committedRowCount(), quint64(frameCount));

        const auto beforeRead = ledger.diagnostics();
        QVERIFY(beforeRead.cacheRows <= 8192);
        QCOMPARE(beforeRead.readFailCount, quint64(0));
        QCOMPARE(beforeRead.fileReadFailCount, quint64(0));

        const auto latest = ledger.readRow(frameCount - 1);
        QVERIFY(latest.has_value());
        QCOMPARE(latest->ledgerSeq, quint64(frameCount - 1));
        QCOMPARE(latest->canId, quint32(0x300 + ((frameCount - 1) % 64)));

        const auto old = ledger.readRow(0);
        QVERIFY(old.has_value());
        QCOMPARE(old->ledgerSeq, quint64(0));

        const auto missing = ledger.readRow(frameCount);
        QVERIFY(!missing.has_value());

        const auto afterRead = ledger.diagnostics();
        QVERIFY(afterRead.cacheHits > beforeRead.cacheHits);
        QVERIFY(afterRead.cacheMisses > beforeRead.cacheMisses);
        QCOMPARE(afterRead.readFailCount, quint64(1));
        QCOMPARE(afterRead.fileReadFailCount, quint64(0));
    }

    void tableModelMarksUnreadableRowsInsteadOfBlankDlcZero() {
        QCOMPARE(RawFrameTableModel::unreadableRowValue(RawFrameTableModel::ValidRole, 7).toBool(), false);
        QCOMPARE(RawFrameTableModel::unreadableRowValue(RawFrameTableModel::IdTextRole, 7).toString(), QStringLiteral("LEDGER ERR"));
        QCOMPARE(RawFrameTableModel::unreadableRowValue(RawFrameTableModel::DlcRole, 7).toInt(), -1);
        QCOMPARE(RawFrameTableModel::unreadableRowValue(RawFrameTableModel::BusRole, 7).toInt(), -1);
        QCOMPARE(RawFrameTableModel::unreadableRowValue(RawFrameTableModel::SourceRole, 7).toString(), QStringLiteral("ledger-error"));
    }
};

QTEST_MAIN(RawLedgerRuntimeTest)
#include "test_raw_ledger_runtime.moc"
