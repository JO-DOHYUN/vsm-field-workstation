#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace CanMonitorEvidence {

class BusRoleResolver {
public:
    enum class Source {
        Unresolved = 0,
        Capability,
        ModelRule,
        ObservedFingerprint,
        OperatorOverride
    };

    struct Resolution {
        bool resolved = false;
        QString role;
        bool txAllowed = false;
        Source source = Source::Unresolved;
        QString reason;
    };

    void clear();
    void clearModelRules();
    void addModelRule(const QString& role, const QSet<quint32>& fingerprints, bool txAllowed);
    void setCapabilityRole(quint8 bus, const QString& role, bool txAllowed);
    void clearCapabilityRoles();
    void setOperatorOverride(quint8 bus, const QString& role, bool txAllowed);
    void clearOperatorOverride(quint8 bus);
    void observeCanId(quint8 bus, quint32 canId);

    Resolution resolve(quint8 bus) const;
    bool txAllowed(quint8 bus) const;

private:
    struct RoleRule {
        QString role;
        QSet<quint32> fingerprints;
        bool txAllowed = false;
    };

    struct FixedRole {
        QString role;
        bool txAllowed = false;
    };

    Resolution fixedResolution(const FixedRole& role, Source source, const QString& reason) const;
    Resolution observedResolution(quint8 bus) const;

    QVector<RoleRule> m_modelRules;
    QHash<quint8, FixedRole> m_capabilityRoles;
    QHash<quint8, FixedRole> m_operatorOverrides;
    QHash<quint8, QSet<quint32>> m_observedIdsByBus;
};

} // namespace CanMonitorEvidence
