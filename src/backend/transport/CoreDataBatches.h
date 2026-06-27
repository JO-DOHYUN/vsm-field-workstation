#pragma once

#include "../CanTypes.h"

#include <QMetaType>
#include <QtGlobal>

namespace CanMonitorTransport {

struct AnalysisFrameBatch {
    FrameRecordList frames;
    quint64 overrunFrames = 0;
};

struct RawLedgerFrameBatch {
    FrameRecordList frames;
    quint64 overrunFrames = 0;
};

} // namespace CanMonitorTransport

Q_DECLARE_METATYPE(CanMonitorTransport::AnalysisFrameBatch)
Q_DECLARE_METATYPE(CanMonitorTransport::RawLedgerFrameBatch)
