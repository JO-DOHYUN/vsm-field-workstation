#pragma once

#include "core/CoreViewTypes.h"

#include <QJsonObject>
#include <QString>

namespace CanMonitorCore {

struct CoreStartRequest {
    QString endpoint;
    QString captureDirectory;
    bool debugTapEnabled = false;
};

struct CoreStopReason {
    QString reason;
    QJsonObject diagnostics;
};

struct CoreControlCommand {
    QString commandType;
    QJsonObject payload;
};

class CaptureCoreDataPlane {
public:
    virtual ~CaptureCoreDataPlane() = default;

    virtual bool start(const CoreStartRequest& request) = 0;
    virtual void stop(const CoreStopReason& reason) = 0;
    virtual bool submitControlCommand(const CoreControlCommand& command) = 0;
    virtual ViewQueryResult queryView(const ViewQuery& query) const = 0;
};

} // namespace CanMonitorCore
