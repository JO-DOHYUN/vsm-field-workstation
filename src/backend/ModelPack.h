#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CanModel {

struct RuleSpec {
    quint32 id = 0;
    QString name;
    double expectedPeriodMs = -1.0;
    double ttlWarnMs = -1.0;
    double ttlErrMs = -1.0;
    double periodWarnPct = -1.0;
    double periodErrPct = -1.0;
    bool timingEnabled = true;
    QString timingMode;
};

struct SignalSpec {
    QString name;
    int byteIndex1Based = 1;
    QString bitText;
    int lengthBits = 8;
    int startBitLsb = 0;
    QVector<int> bitPositionsLsb;
    double scale = 1.0;
    double offset = 0.0;
    bool signedValue = false;
    QString rangeText;
    QString operatingText;
    QString description;
    bool reserved = false;
    QString unit;
    QString alarmMode;
    bool hasWarnMin = false;
    double warnMin = 0.0;
    bool hasWarnMax = false;
    double warnMax = 0.0;
    bool hasErrMin = false;
    double errMin = 0.0;
    bool hasErrMax = false;
    double errMax = 0.0;
    QVector<qint64> inactiveRawValues;
    QStringList inactiveLabels;
    QString alarmSeverity;
    QString alarmMessage;
    bool monitorOnly = false;
};

struct SignalMessageSpec {
    quint32 id = 0;
    QString name;
    QVector<SignalSpec> signalSpecs;
};

struct PackMeta {
    QString schema = QStringLiteral("can-monitor-model-pack.v1");
    QString modelKey;
    QString modelName;
    QString modelVersion;
    QString vendor;
    QString notes;
};

struct BusRoleRuleSpec {
    QString role;
    QSet<quint32> fingerprints;
    bool txAllowed = false;
};

struct ControlPolicySpec {
    bool declared = false;
    bool enabled = true;
    QString profileName;
    QString targetRole = QStringLiteral("system");
    QStringList allowedBusRoles;
    QVector<BusRoleRuleSpec> busRoleRules;
    int maxRpm = 10000;
    double maxAbsSteeringDeg = 90.0;

    bool roleAllowed(const QString& role) const {
        if (!enabled) return false;
        const QString normalized = role.trimmed().toLower();
        if (normalized.isEmpty()) return false;
        if (allowedBusRoles.isEmpty()) return true;
        return allowedBusRoles.contains(normalized);
    }
};

struct ModelPack {
    PackMeta meta;
    QHash<quint32, RuleSpec> rules;
    QHash<quint32, SignalMessageSpec> messages;
    ControlPolicySpec controlPolicy;

    bool hasContent() const { return !rules.isEmpty() || !messages.isEmpty(); }
};

class ModelPackLoader {
public:
    static bool loadFile(const QString& path, ModelPack* outPack, QString* outError = nullptr);
    static bool loadObject(const QJsonObject& root, ModelPack* outPack, QString* outError = nullptr);
};

} // namespace CanModel
