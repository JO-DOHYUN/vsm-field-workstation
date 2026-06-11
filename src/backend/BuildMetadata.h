#pragma once

#include <QString>
#include <QVariantMap>

namespace BuildMetadata {

struct Info {
    QString organizationName;
    QString organizationDomain;
    QString applicationName;
    QString applicationId;
    QString version;
    QString baselineId;
    QString buildType;
    QString buildTimestampUtc;
    QString packageId;
};

Info current();
QVariantMap toVariantMap(const Info& info);
QString banner(const Info& info);

} // namespace BuildMetadata
