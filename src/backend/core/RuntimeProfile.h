#pragma once

#include <QIODevice>
#include <QJsonObject>
#include <QString>

namespace CanMonitorCore {

enum class RuntimeProfileKind {
    PassiveProduct,
    FullInstrumented,
};

enum class VehicleImpactState {
    BlockedUnknown,
    ConfiguredPassive,
    VerifiedPassive,
    ActivePossible,
};

struct RuntimeTransportPolicy {
    QIODevice::OpenMode serialOpenMode = QIODevice::ReadOnly;
    bool touchDtr = false;
    bool dtrAsserted = false;
    bool touchRts = false;
    bool rtsAsserted = false;
    bool hostTxEnabled = false;
    bool controlCycleEnabled = false;
    bool labGatewayEnabled = false;

    bool serialWriteAllowed() const;
    QString serialOpenModeText() const;
    QJsonObject toJson() const;
};

class RuntimeProfile {
public:
    RuntimeProfile() = default;
    RuntimeProfile(RuntimeProfileKind kind,
                   VehicleImpactState impactState,
                   QString key,
                   QString displayName,
                   QString summary,
                   RuntimeTransportPolicy policy,
                   bool passiveAcceptanceAllowed,
                   bool requiresCsmPassiveCapability,
                   bool requiresHardwareSafetyCase);

    RuntimeProfileKind kind() const { return m_kind; }
    VehicleImpactState impactState() const { return m_impactState; }
    QString key() const { return m_key; }
    QString displayName() const { return m_displayName; }
    QString summary() const { return m_summary; }
    const RuntimeTransportPolicy& transportPolicy() const { return m_policy; }
    bool isPassiveProduct() const { return m_kind == RuntimeProfileKind::PassiveProduct; }
    bool passiveAcceptanceAllowed() const { return m_passiveAcceptanceAllowed; }
    bool requiresCsmPassiveCapability() const { return m_requiresCsmPassiveCapability; }
    bool requiresHardwareSafetyCase() const { return m_requiresHardwareSafetyCase; }
    QJsonObject toJson() const;

private:
    RuntimeProfileKind m_kind = RuntimeProfileKind::PassiveProduct;
    VehicleImpactState m_impactState = VehicleImpactState::BlockedUnknown;
    QString m_key = QStringLiteral("passive_product");
    QString m_displayName = QStringLiteral("Passive Product");
    QString m_summary = QStringLiteral("Passive-safe default profile");
    RuntimeTransportPolicy m_policy;
    bool m_passiveAcceptanceAllowed = false;
    bool m_requiresCsmPassiveCapability = true;
    bool m_requiresHardwareSafetyCase = true;
};

QString runtimeProfileKindToString(RuntimeProfileKind kind);
QString vehicleImpactStateToString(VehicleImpactState state);
RuntimeProfile passiveProductProfile();
RuntimeProfile fullInstrumentedProfile();
RuntimeProfile runtimeProfileFromString(const QString& value, bool* okOut = nullptr);
RuntimeProfile runtimeProfileFromEnvironmentOrDefault();

} // namespace CanMonitorCore
