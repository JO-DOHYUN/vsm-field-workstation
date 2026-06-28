#pragma once

#include "../CanTypes.h"

#include <QMetaType>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <iterator>

namespace CanMonitorTransport {

struct CanRxLite {
    quint64 monoUs = 0;
    quint64 captureSeq = 0;
    quint32 canId = 0;
    quint8 bus = 0;
    quint8 dlc = 0;
    quint8 typedSeqLsb = 0;
    bool extended = false;
    bool rtr = false;
    bool hasCaptureSeq = false;
    quint8 data[8] = {};
};

inline FrameRecord frameRecordFromCanRxLite(const CanRxLite& frame) {
    FrameRecord out;
    out.tExtUs = frame.monoUs;
    out.canId = frame.canId;
    out.ext = frame.extended;
    out.rtr = frame.rtr;
    out.dlc = frame.dlc;
    out.bus = frame.bus;
    out.seq = frame.typedSeqLsb;
    out.hasCaptureSeq = frame.hasCaptureSeq;
    out.captureSeq = frame.captureSeq;
    std::copy(std::begin(frame.data), std::end(frame.data), std::begin(out.data));
    return out;
}

inline CanRxLite canRxLiteFromFrameRecord(const FrameRecord& frame) {
    CanRxLite out;
    out.monoUs = frame.tExtUs;
    out.captureSeq = frame.captureSeq;
    out.canId = frame.canId;
    out.bus = frame.bus;
    out.dlc = frame.dlc;
    out.typedSeqLsb = frame.seq;
    out.extended = frame.ext;
    out.rtr = frame.rtr;
    out.hasCaptureSeq = frame.hasCaptureSeq;
    std::copy(std::begin(frame.data), std::end(frame.data), std::begin(out.data));
    return out;
}

struct AnalysisFrameBatch {
    QVector<CanRxLite> frames;
    quint64 overrunFrames = 0;
};

struct RawLedgerFrameBatch {
    QVector<CanRxLite> frames;
    quint64 overrunFrames = 0;
};

} // namespace CanMonitorTransport

Q_DECLARE_METATYPE(CanMonitorTransport::CanRxLite)
Q_DECLARE_METATYPE(CanMonitorTransport::AnalysisFrameBatch)
Q_DECLARE_METATYPE(CanMonitorTransport::RawLedgerFrameBatch)
