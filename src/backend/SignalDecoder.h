#pragma once

#include "AnalysisTypes.h"
#include "CanTypes.h"
#include "DetailListModel.h"
#include "ModelPack.h"

#include <QHash>
#include <QVector>

namespace CanMonitorAnalysis {

class SignalDecoder {
public:
    static QString displayNameForId(quint32 id,
                                    const QHash<quint32, CanModel::RuleSpec>& rules,
                                    const QHash<quint32, CanModel::SignalMessageSpec>& messages);

    static SignalPreviewResult makePreview(quint32 id,
                                           const FrameRecord& frame,
                                           const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                           bool modelEnabled);

    static QString makeVerbosePreview(quint32 id,
                                      const FrameRecord& frame,
                                      const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                      bool modelEnabled);

    static ValueAlarmResult makeValueAlarm(quint32 id,
                                           const FrameRecord& frame,
                                           const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                           bool modelEnabled);

    static QVector<DetailRow> makeDetailRows(quint32 id,
                                             const FrameRecord& frame,
                                             const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                             bool modelEnabled);
};

} // namespace CanMonitorAnalysis
