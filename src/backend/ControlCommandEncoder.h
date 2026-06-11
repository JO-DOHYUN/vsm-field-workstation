#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace CanMonitorControl {

inline constexpr quint8 kHostCommandRecordTypeCanTxRequest = 10;
inline constexpr quint8 kHostCommandRecordTypeHeartbeat = 11;
inline constexpr quint8 kHostCommandRecordTypeControlSession = 12;
inline constexpr quint8 kDefaultControlBus = 1;
inline constexpr quint8 kControlSessionAnyBus = 0xFF;
inline constexpr quint8 kControlSessionDisarm = 0;
inline constexpr quint8 kControlSessionArm = 1;
inline constexpr quint8 kControlSessionRenewLease = 2;

struct ControlCanFrame {
    quint8 bus = kDefaultControlBus;
    quint32 canId = 0;
    bool extended = false;
    bool rtr = false;
    quint8 dlc = 8;
    quint8 data[8] = {0};
};

class ControlCommandEncoder {
public:
    static ControlCanFrame makeAdcuManualFrame(int signedCommand,
                                               double steeringDeg,
                                               quint8 drivingMode,
                                               quint8 aliveCounter,
                                               quint8 bus = kDefaultControlBus);
    static ControlCanFrame makeVcuAutoFrame(quint32 canId,
                                            int signedCommand,
                                            double steeringDeg,
                                            quint8 motorMode,
                                            quint8 bus = kDefaultControlBus);
    static QVector<ControlCanFrame> makeControlBurst(int signedCommand,
                                                     int rpm,
                                                     double steeringDeg,
                                                     quint8 motorMode,
                                                     quint8 drivingMode,
                                                     quint8 aliveCounter,
                                                     quint8 bus = kDefaultControlBus);
    static QByteArray buildHostCanTxRequest(quint32 commandId, const ControlCanFrame& frame);
    static QByteArray buildHostHeartbeat(quint32 commandId, quint32 hostMonoMs);
    static QByteArray buildHostControlSession(quint32 commandId,
                                              quint8 action,
                                              quint8 requestedBus = kControlSessionAnyBus,
                                              quint16 leaseMs = 2000);
    static QString frameSummary(const ControlCanFrame& frame);

private:
    static quint16 crc16Ccitt(const quint8* data, qsizetype len);
};

} // namespace CanMonitorControl
