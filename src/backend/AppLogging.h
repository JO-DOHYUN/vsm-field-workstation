#pragma once

#include "BuildMetadata.h"

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logTransport)
Q_DECLARE_LOGGING_CATEGORY(logReplay)
Q_DECLARE_LOGGING_CATEGORY(logAnalysis)
Q_DECLARE_LOGGING_CATEGORY(logGraph)
Q_DECLARE_LOGGING_CATEGORY(logModel)
Q_DECLARE_LOGGING_CATEGORY(logDeploy)
Q_DECLARE_LOGGING_CATEGORY(logUi)

namespace AppLogging {

void initialize(const BuildMetadata::Info& info);
void shutdown();
QString sessionLogFilePath();

} // namespace AppLogging
