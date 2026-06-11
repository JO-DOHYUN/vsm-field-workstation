#include "BuildMetadata.h"

#include <QStringList>

namespace {

QString macroString(const char* text) {
    return text ? QString::fromUtf8(text) : QString();
}

} // namespace

namespace BuildMetadata {

Info current() {
    Info info;
    info.organizationName = macroString(CAN_MONITOR_ORG_NAME);
    info.organizationDomain = macroString(CAN_MONITOR_ORG_DOMAIN);
    info.applicationName = macroString(CAN_MONITOR_APP_NAME);
    info.applicationId = macroString(CAN_MONITOR_APP_ID);
    info.version = macroString(CAN_MONITOR_VERSION);
    info.baselineId = macroString(CAN_MONITOR_BASELINE_ID);
    info.buildType = macroString(CAN_MONITOR_BUILD_TYPE);
    info.buildTimestampUtc = macroString(CAN_MONITOR_BUILD_TIMESTAMP_UTC);

    QStringList packageParts;
    packageParts << info.applicationId << info.version << info.buildType << info.baselineId;
    info.packageId = packageParts.join(QStringLiteral("-"));
    return info;
}

QVariantMap toVariantMap(const Info& info) {
    QVariantMap out;
    out.insert(QStringLiteral("organization_name"), info.organizationName);
    out.insert(QStringLiteral("organization_domain"), info.organizationDomain);
    out.insert(QStringLiteral("application_name"), info.applicationName);
    out.insert(QStringLiteral("application_id"), info.applicationId);
    out.insert(QStringLiteral("version"), info.version);
    out.insert(QStringLiteral("baseline_id"), info.baselineId);
    out.insert(QStringLiteral("build_type"), info.buildType);
    out.insert(QStringLiteral("build_timestamp_utc"), info.buildTimestampUtc);
    out.insert(QStringLiteral("package_id"), info.packageId);
    return out;
}

QString banner(const Info& info) {
    return QStringLiteral("%1 %2 | %3 | %4 | built %5")
        .arg(info.applicationName,
             info.version,
             info.buildType,
             info.baselineId,
             info.buildTimestampUtc);
}

} // namespace BuildMetadata
