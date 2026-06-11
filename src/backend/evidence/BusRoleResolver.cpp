#include "evidence/BusRoleResolver.h"

namespace CanMonitorEvidence {

void BusRoleResolver::clear() {
    m_modelRules.clear();
    m_capabilityRoles.clear();
    m_operatorOverrides.clear();
    m_observedIdsByBus.clear();
}

void BusRoleResolver::clearModelRules() {
    m_modelRules.clear();
}

void BusRoleResolver::addModelRule(const QString& role, const QSet<quint32>& fingerprints, bool txAllowed) {
    const QString normalized = role.trimmed().toLower();
    if (normalized.isEmpty()) return;
    RoleRule rule;
    rule.role = normalized;
    rule.fingerprints = fingerprints;
    rule.txAllowed = txAllowed;
    m_modelRules.push_back(rule);
}

void BusRoleResolver::setCapabilityRole(quint8 bus, const QString& role, bool txAllowed) {
    const QString normalized = role.trimmed().toLower();
    if (normalized.isEmpty()) {
        m_capabilityRoles.remove(bus);
        return;
    }
    m_capabilityRoles.insert(bus, FixedRole{normalized, txAllowed});
}

void BusRoleResolver::clearCapabilityRoles() {
    m_capabilityRoles.clear();
}

void BusRoleResolver::setOperatorOverride(quint8 bus, const QString& role, bool txAllowed) {
    const QString normalized = role.trimmed().toLower();
    if (normalized.isEmpty()) {
        m_operatorOverrides.remove(bus);
        return;
    }
    m_operatorOverrides.insert(bus, FixedRole{normalized, txAllowed});
}

void BusRoleResolver::clearOperatorOverride(quint8 bus) {
    m_operatorOverrides.remove(bus);
}

void BusRoleResolver::observeCanId(quint8 bus, quint32 canId) {
    m_observedIdsByBus[bus].insert(canId & 0x1FFFFFFFu);
}

BusRoleResolver::Resolution BusRoleResolver::resolve(quint8 bus) const {
    const auto capabilityIt = m_capabilityRoles.constFind(bus);
    if (capabilityIt != m_capabilityRoles.cend()) {
        return fixedResolution(capabilityIt.value(), Source::Capability, QStringLiteral("capability descriptor"));
    }

    const Resolution observed = observedResolution(bus);
    if (observed.resolved) return observed;

    const auto overrideIt = m_operatorOverrides.constFind(bus);
    if (overrideIt != m_operatorOverrides.cend()) {
        return fixedResolution(overrideIt.value(), Source::OperatorOverride, QStringLiteral("operator override"));
    }

    return Resolution{false, QString(), false, Source::Unresolved, QStringLiteral("bus role unresolved")};
}

bool BusRoleResolver::txAllowed(quint8 bus) const {
    const Resolution resolution = resolve(bus);
    return resolution.resolved && resolution.txAllowed;
}

BusRoleResolver::Resolution BusRoleResolver::fixedResolution(const FixedRole& role,
                                                             Source source,
                                                             const QString& reason) const {
    return Resolution{true, role.role, role.txAllowed, source, reason};
}

BusRoleResolver::Resolution BusRoleResolver::observedResolution(quint8 bus) const {
    const auto observedIt = m_observedIdsByBus.constFind(bus);
    if (observedIt == m_observedIdsByBus.cend() || observedIt.value().isEmpty()) {
        return Resolution{false, QString(), false, Source::Unresolved, QStringLiteral("no observed fingerprint")};
    }

    QVector<const RoleRule*> matches;
    for (const RoleRule& rule : m_modelRules) {
        for (quint32 canId : observedIt.value()) {
            if (rule.fingerprints.contains(canId)) {
                matches.push_back(&rule);
                break;
            }
        }
    }

    if (matches.size() != 1) {
        return Resolution{false, QString(), false, Source::Unresolved,
                          matches.isEmpty() ? QStringLiteral("no fingerprint match")
                                            : QStringLiteral("ambiguous fingerprint match")};
    }

    const RoleRule* rule = matches.front();
    return Resolution{true, rule->role, rule->txAllowed, Source::ObservedFingerprint, QStringLiteral("observed fingerprint")};
}

} // namespace CanMonitorEvidence
