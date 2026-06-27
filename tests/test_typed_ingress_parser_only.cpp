#include "TypedTransportParser.h"
#include "transport/TypedIngressRuntime.h"

#include <QtTest>

namespace {
void appendU16(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

QByteArray makeTypedFrame(TypedRecordType type, quint16 seq, const QByteArray& payload) {
    QByteArray frame;
    frame.append(char(kTypedTransportSof0));
    frame.append(char(kTypedTransportSof1));
    frame.append(char(kTypedTransportVersion));
    frame.append(char(static_cast<quint8>(type)));
    frame.append(char(0));
    appendU16(frame, seq);
    appendU16(frame, quint16(payload.size()));
    frame.append(payload);
    const auto* crcStart = reinterpret_cast<const quint8*>(frame.constData() + 2);
    appendU16(frame, TypedTransportParser::crc16Ccitt(crcStart, frame.size() - 2));
    return frame;
}
}

class TypedIngressParserOnlyTest : public QObject {
    Q_OBJECT

private slots:
    void parsesBatchesWithoutStorageOwnership() {
        CanMonitorTransport::TypedIngressRuntime runtime;
        QByteArray capability(kTypedCapabilityPayloadSize, char(0));
        const QByteArray frame = makeTypedFrame(TypedRecordType::Capability, 1, capability);

        QVector<TypedRecord> records;
        auto partial = runtime.ingestEach(frame.left(8), 10, false, [&records](TypedRecord&& record) {
            records.push_back(std::move(record));
        });
        QCOMPARE(records.size(), 0);

        auto result = runtime.ingestEach(frame.mid(8), 11, false, [&records](TypedRecord&& record) {
            records.push_back(std::move(record));
        });
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().header.recordType, static_cast<quint8>(TypedRecordType::Capability));
        QCOMPARE(result.capabilityFirstSeen, true);
        QCOMPARE(result.capabilityElapsedMs, qint64(11));
        QCOMPARE(result.status.frames, quint64(1));

        const QJsonObject diagnostics = runtime.makeCaptureDiagnostics();
        QVERIFY(diagnostics.value(QStringLiteral("parser")).isObject());
        QVERIFY(!diagnostics.contains(QStringLiteral("storage")));
    }
};

QTEST_APPLESS_MAIN(TypedIngressParserOnlyTest)

#include "test_typed_ingress_parser_only.moc"
