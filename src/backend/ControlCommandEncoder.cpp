#include "ControlCommandEncoder.h"

#include "CanTypes.h"
#include "TypedRecords.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace CanMonitorControl {
namespace {

void wrU16Le(QByteArray& out, quint16 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void wrU32Le(QByteArray& out, quint32 value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

void setI16Le(quint8* data, int offset, int value) {
    const qint16 clamped = qint16(std::clamp(value, int(std::numeric_limits<qint16>::min()), int(std::numeric_limits<qint16>::max())));
    data[offset] = quint8(quint16(clamped) & 0xFF);
    data[offset + 1] = quint8((quint16(clamped) >> 8) & 0xFF);
}

int steeringRaw01Deg(double steeringDeg) {
    return int(std::lround(std::clamp(steeringDeg, -90.0, 90.0) * 10.0));
}

int steeringRaw001Deg(double steeringDeg) {
    return int(std::lround(std::clamp(steeringDeg, -90.0, 90.0) * 100.0));
}

} // namespace

ControlCanFrame ControlCommandEncoder::makeAdcuManualFrame(int signedCommand,
                                                           double steeringDeg,
                                                           quint8 drivingMode,
                                                           quint8 aliveCounter,
                                                           quint8 bus) {
    Q_UNUSED(signedCommand);
    Q_UNUSED(steeringDeg);
    ControlCanFrame frame;
    frame.bus = bus;
    frame.canId = 0x503;
    frame.dlc = 8;

    std::memset(frame.data, 0, sizeof(frame.data));
    frame.data[0] = quint8(2U << 6); // source: ADCU autonomous
    frame.data[5] = quint8((drivingMode & 0x07U) << 5);
    frame.data[7] = quint8((aliveCounter & 0x0FU) << 4);
    return frame;
}

ControlCanFrame ControlCommandEncoder::makeVcuAutoFrame(quint32 canId,
                                                        int signedCommand,
                                                        double steeringDeg,
                                                        quint8 motorMode,
                                                        quint8 bus) {
    ControlCanFrame frame;
    frame.bus = bus;
    frame.canId = canId;
    frame.dlc = 8;

    std::memset(frame.data, 0, sizeof(frame.data));
    frame.data[0] = quint8((motorMode & 0x03U) << 6);
    setI16Le(frame.data, 1, std::clamp(signedCommand, -10000, 10000));
    setI16Le(frame.data, 3, steeringRaw01Deg(steeringDeg));
    return frame;
}

QVector<ControlCanFrame> ControlCommandEncoder::makeControlBurst(int signedCommand,
                                                                 int rpm,
                                                                 double steeringDeg,
                                                                 quint8 motorMode,
                                                                 quint8 drivingMode,
                                                                 quint8 aliveCounter,
                                                                 quint8 bus) {
    QVector<ControlCanFrame> frames;
    frames.reserve(5);
    frames.push_back(makeAdcuManualFrame(signedCommand, steeringDeg, drivingMode, aliveCounter, bus));
    frames.push_back(makeVcuAutoFrame(0x510, signedCommand, steeringDeg, motorMode, bus));
    frames.push_back(makeVcuAutoFrame(0x512, signedCommand, steeringDeg, motorMode, bus));
    frames.push_back(makeVcuAutoFrame(0x511, signedCommand, steeringDeg, motorMode, bus));
    frames.push_back(makeVcuAutoFrame(0x513, signedCommand, steeringDeg, motorMode, bus));
    return frames;
}

QByteArray ControlCommandEncoder::buildHostCanTxRequest(quint32 commandId, const ControlCanFrame& frame) {
    QByteArray payload;
    payload.reserve(19);
    wrU32Le(payload, commandId);
    payload.append(char(frame.bus));
    payload.append(char((frame.extended ? 0x01 : 0x00) | (frame.rtr ? 0x02 : 0x00)));
    wrU32Le(payload, frame.canId);
    payload.append(char(std::min<quint8>(frame.dlc, 8)));
    for (int i = 0; i < 8; ++i) payload.append(char(frame.data[i]));

    QByteArray headerAndPayload;
    headerAndPayload.reserve(kTypedTransportHeaderSize + payload.size());
    headerAndPayload.append(char(kTypedTransportVersion));
    headerAndPayload.append(char(kHostCommandRecordTypeCanTxRequest));
    headerAndPayload.append(char(0));
    wrU16Le(headerAndPayload, quint16(commandId & 0xFFFFU));
    wrU16Le(headerAndPayload, quint16(payload.size()));
    headerAndPayload.append(payload);

    QByteArray out;
    out.reserve(kTypedTransportFrameOverhead + payload.size());
    out.append(char(kTypedTransportSof0));
    out.append(char(kTypedTransportSof1));
    out.append(headerAndPayload);
    wrU16Le(out, crc16Ccitt(reinterpret_cast<const quint8*>(headerAndPayload.constData()), headerAndPayload.size()));
    return out;
}

QByteArray ControlCommandEncoder::buildHostHeartbeat(quint32 commandId, quint32 hostMonoMs) {
    QByteArray payload;
    payload.reserve(kTypedHostHeartbeatPayloadSize);
    wrU32Le(payload, commandId);
    wrU32Le(payload, hostMonoMs);
    wrU16Le(payload, 0);
    wrU16Le(payload, 0);

    QByteArray headerAndPayload;
    headerAndPayload.reserve(kTypedTransportHeaderSize + payload.size());
    headerAndPayload.append(char(kTypedTransportVersion));
    headerAndPayload.append(char(kHostCommandRecordTypeHeartbeat));
    headerAndPayload.append(char(0));
    wrU16Le(headerAndPayload, quint16(commandId & 0xFFFFU));
    wrU16Le(headerAndPayload, quint16(payload.size()));
    headerAndPayload.append(payload);

    QByteArray out;
    out.reserve(kTypedTransportFrameOverhead + payload.size());
    out.append(char(kTypedTransportSof0));
    out.append(char(kTypedTransportSof1));
    out.append(headerAndPayload);
    wrU16Le(out, crc16Ccitt(reinterpret_cast<const quint8*>(headerAndPayload.constData()), headerAndPayload.size()));
    return out;
}

QByteArray ControlCommandEncoder::buildHostControlSession(quint32 commandId,
                                                          quint8 action,
                                                          quint8 requestedBus,
                                                          quint16 leaseMs) {
    QByteArray payload;
    payload.reserve(kTypedHostControlSessionPayloadSize);
    wrU32Le(payload, commandId);
    payload.append(char(action));
    payload.append(char(requestedBus));
    wrU16Le(payload, 0);
    wrU16Le(payload, leaseMs);
    wrU16Le(payload, 0);
    wrU32Le(payload, 0);
    wrU32Le(payload, 0);
    wrU32Le(payload, 0);

    QByteArray headerAndPayload;
    headerAndPayload.reserve(kTypedTransportHeaderSize + payload.size());
    headerAndPayload.append(char(kTypedTransportVersion));
    headerAndPayload.append(char(kHostCommandRecordTypeControlSession));
    headerAndPayload.append(char(0));
    wrU16Le(headerAndPayload, quint16(commandId & 0xFFFFU));
    wrU16Le(headerAndPayload, quint16(payload.size()));
    headerAndPayload.append(payload);

    QByteArray out;
    out.reserve(kTypedTransportFrameOverhead + payload.size());
    out.append(char(kTypedTransportSof0));
    out.append(char(kTypedTransportSof1));
    out.append(headerAndPayload);
    wrU16Le(out, crc16Ccitt(reinterpret_cast<const quint8*>(headerAndPayload.constData()), headerAndPayload.size()));
    return out;
}

QString ControlCommandEncoder::frameSummary(const ControlCanFrame& frame) {
    return QStringLiteral("BUS %1 %2 DLC %3 %4")
        .arg(frame.bus)
        .arg(idText(frame.canId))
        .arg(frame.dlc)
        .arg(hexBytes(frame.data, frame.dlc));
}

quint16 ControlCommandEncoder::crc16Ccitt(const quint8* data, qsizetype len) {
    quint16 crc = 0xFFFF;
    for (qsizetype index = 0; index < len; ++index) {
        crc ^= quint16(data[index]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
        }
    }
    return crc;
}

} // namespace CanMonitorControl
