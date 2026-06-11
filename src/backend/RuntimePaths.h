#pragma once

#include <QString>

namespace RuntimePaths {

QString normalizeLocalPath(const QString& raw);
QString appDataRoot();
QString configRoot();
QString projectRoot();
QString projectDataRoot();
QString logsRoot();
QString snapshotsRoot();

QString ensureAppDataRoot();
QString ensureConfigRoot();
QString ensureProjectDataRoot();
QString ensureLogsRoot();
QString ensureSnapshotsRoot();

} // namespace RuntimePaths
