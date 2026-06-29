#include "core/RuntimeProfile.h"

#include <QByteArray>
#include <QtGlobal>

#include <utility>

namespace CanMonitorCore {

bool RuntimeTransportPolicy::serialWriteAllowed() const {
    return serialOpenMode.testFlag(QIODevice::WriteOnly) || hostTxEnabled || controlCycleEnabled;
}

QString RuntimeTransportPolicy::serialOpenModeText() const {
    if (serialOpenMode.testFlag(QIODevice::ReadOnly) && serialOpenMode.testFlag(QIODevice::WriteOnly)) {
        return QStringLiteral("read_write");
    }
    if (serialOpenMode.testFlag(QIODevice::ReadOnly)) {
        return QStringLiteral("read_only");
    }
    if (serialOpenMode.testFlag(QIODevice::WriteOnly)) {
        return QStringLiteral("write_only");
    }
    return QStringLiteral("not_open");
}

QJsonObject RuntimeTransportPolicy::toJson() const {
    return QJsonObject{{QStringLiteral("serial_open_mode"), serialOpenModeText()},
                       {QStringLiteral("serial_write_allowed"), serialWriteAllowed()},
                       {QStringLiteral("dtr_policy"), touchDtr ? (dtrAsserted ? QStringLiteral("assert_true") : QStringLiteral("assert_false")) : QStringLiteral("no_touch")},
                       {QStringLiteral("rts_policy"), touchRts ? (rtsAsserted ? QStringLiteral("assert_true") : QStringLiteral("assert_false")) : QStringLiteral("no_touch")},
                       {QStringLiteral("host_tx_enabled"), hostTxEnabled},
                       {QStringLiteral("control_enabled"), controlCycleEnabled},
                       {QStringLiteral("lab_gateway_enabled"), labGatewayEnabled},
                       {QStringLiteral("debug_tap_mode"), labGatewayEnabled ? QStringLiteral("lab_gateway_may_own_transport") : QStringLiteral("sidecar_only")}};
}

RuntimeProfile::RuntimeProfile(RuntimeProfileKind kind,
                               VehicleImpactState impactState,
                               QString key,
                               QString displayName,
                               QString summary,
                               RuntimeTransportPolicy policy,
                               bool passiveAcceptanceAllowed,
                               bool requiresCsmPassiveCapability,
                               bool requiresHardwareSafetyCase)
    : m_kind(kind)
    , m_impactState(impactState)
    , m_key(std::move(key))
    , m_displayName(std::move(displayName))
    , m_summary(std::move(summary))
    , m_policy(policy)
    , m_passiveAcceptanceAllowed(passiveAcceptanceAllowed)
    , m_requiresCsmPassiveCapability(requiresCsmPassiveCapability)
    , m_requiresHardwareSafetyCase(requiresHardwareSafetyCase) {}

QJsonObject RuntimeProfile::toJson() const {
    return QJsonObject{{QStringLiteral("runtime_profile"), m_key},
                       {QStringLiteral("profile_kind"), runtimeProfileKindToString(m_kind)},
                       {QStringLiteral("display_name"), m_displayName},
                       {QStringLiteral("summary"), m_summary},
                       {QStringLiteral("vehicle_impact_state"), vehicleImpactStateToString(m_impactState)},
                       {QStringLiteral("passive_acceptance_allowed"), m_passiveAcceptanceAllowed},
                       {QStringLiteral("requires_csm_passive_capability"), m_requiresCsmPassiveCapability},
                       {QStringLiteral("requires_hardware_safety_case"), m_requiresHardwareSafetyCase},
                       {QStringLiteral("transport_policy"), m_policy.toJson()}};
}

QString runtimeProfileKindToString(RuntimeProfileKind kind) {
    switch (kind) {
    case RuntimeProfileKind::PassiveProduct:
        return QStringLiteral("passive_product");
    case RuntimeProfileKind::FullInstrumented:
        return QStringLiteral("full_instrumented");
    }
    return QStringLiteral("unknown");
}

QString vehicleImpactStateToString(VehicleImpactState state) {
    switch (state) {
    case VehicleImpactState::BlockedUnknown:
        return QStringLiteral("blocked_unknown");
    case VehicleImpactState::ConfiguredPassive:
        return QStringLiteral("configured_passive");
    case VehicleImpactState::VerifiedPassive:
        return QStringLiteral("verified_passive");
    case VehicleImpactState::ActivePossible:
        return QStringLiteral("active_possible");
    }
    return QStringLiteral("unknown");
}

RuntimeProfile passiveProductProfile() {
    RuntimeTransportPolicy policy;
    policy.serialOpenMode = QIODevice::ReadOnly;
    policy.touchDtr = false;
    policy.dtrAsserted = false;
    policy.touchRts = false;
    policy.rtsAsserted = false;
    policy.hostTxEnabled = false;
    policy.controlCycleEnabled = false;
    policy.labGatewayEnabled = false;
    return RuntimeProfile(RuntimeProfileKind::PassiveProduct,
                          VehicleImpactState::BlockedUnknown,
                          QStringLiteral("passive_product"),
                          QStringLiteral("Passive Product"),
                          QStringLiteral("Read-only production profile: no host CAN TX, no control cycle, no DTR/RTS touch, no COM-owning lab gateway."),
                          policy,
                          false,
                          true,
                          true);
}

RuntimeProfile fullInstrumentedProfile() {
    RuntimeTransportPolicy policy;
    policy.serialOpenMode = QIODevice::ReadWrite;
    policy.touchDtr = true;
    policy.dtrAsserted = true;
    policy.touchRts = true;
    policy.rtsAsserted = true;
    policy.hostTxEnabled = true;
    policy.controlCycleEnabled = true;
    policy.labGatewayEnabled = true;
    return RuntimeProfile(RuntimeProfileKind::FullInstrumented,
                          VehicleImpactState::ActivePossible,
                          QStringLiteral("full_instrumented"),
                          QStringLiteral("Full Instrumented"),
                          QStringLiteral("Bench/lab profile: transport write, control, and COM-owning debug gateway are possible."),
                          policy,
                          false,
                          false,
                          true);
}

RuntimeProfile runtimeProfileFromString(const QString& value, bool* okOut) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() ||
        normalized == QStringLiteral("passive") ||
        normalized == QStringLiteral("passive_product") ||
        normalized == QStringLiteral("product")) {
        if (okOut) *okOut = true;
        return passiveProductProfile();
    }
    if (normalized == QStringLiteral("full") ||
        normalized == QStringLiteral("full_instrumented") ||
        normalized == QStringLiteral("instrumented") ||
        normalized == QStringLiteral("lab")) {
        if (okOut) *okOut = true;
        return fullInstrumentedProfile();
    }
    if (okOut) *okOut = false;
    return passiveProductProfile();
}

RuntimeProfile runtimeProfileFromEnvironmentOrDefault() {
    const QString envValue = QString::fromLocal8Bit(qgetenv("VSM_RUNTIME_PROFILE"));
    bool ok = false;
    RuntimeProfile profile = runtimeProfileFromString(envValue, &ok);
    if (!ok) {
        return passiveProductProfile();
    }
    return profile;
}

} // namespace CanMonitorCore
