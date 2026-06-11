#include "ModelPackValidator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

namespace CanModel {
namespace {

bool parseCanIdValue(const QJsonValue& value, quint32* outId) {
    if (!outId) return false;
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        bool ok = false;
        quint32 id = 0;
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            id = text.mid(2).toUInt(&ok, 16);
        } else {
            id = text.toUInt(&ok, 10);
        }
        if (!ok) return false;
        *outId = id;
        return true;
    }
    if (value.isDouble()) {
        *outId = quint32(value.toInteger());
        return true;
    }
    return false;
}

QString signalNameKey(const QString& name) {
    return name.trimmed().toLower();
}

bool isPlaceholderSignalName(const QString& name) {
    const QString normalized = signalNameKey(name);
    return normalized == QStringLiteral("not defined") ||
           normalized == QStringLiteral("예비") ||
           normalized == QStringLiteral("reserved") ||
           normalized == QStringLiteral("unused") ||
           normalized == QStringLiteral("spare") ||
           normalized == QStringLiteral("n/a") ||
           normalized == QStringLiteral("na");
}

void addIssue(QVector<ValidationIssue>* issues,
              ValidationIssue::Severity severity,
              const QString& code,
              const QString& message) {
    if (!issues) return;
    issues->push_back(ValidationIssue{severity, code, message});
}

bool hasNumericField(const QJsonObject& obj, const QString& key) {
    return obj.contains(key) && obj.value(key).isDouble();
}

} // namespace

QVector<ValidationIssue> ModelPackValidator::validate(const QJsonObject& root) {
    QVector<ValidationIssue> issues;

    const QString schema = root.value(QStringLiteral("schema")).toString().trimmed();
    if (schema.isEmpty()) {
        addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("schema.missing"),
                 QStringLiteral("schema is required"));
    } else if (schema != QStringLiteral("can-monitor-model-pack.v1")) {
        addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("schema.unsupported"),
                 QStringLiteral("unsupported schema: %1").arg(schema));
    }

    if (root.contains(QStringLiteral("rules")) && !root.value(QStringLiteral("rules")).isArray()) {
        addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("rules.type"),
                 QStringLiteral("rules must be an array"));
    }
    if (root.contains(QStringLiteral("messages")) && !root.value(QStringLiteral("messages")).isArray()) {
        addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("messages.type"),
                 QStringLiteral("messages must be an array"));
    }

    const QJsonArray rules = root.value(QStringLiteral("rules")).toArray();
    const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
    if (rules.isEmpty() && messages.isEmpty()) {
        addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("pack.empty"),
                 QStringLiteral("at least one of rules/messages must contain data"));
    }

    QSet<quint32> ruleIds;
    QSet<quint32> messageIds;

    for (int index = 0; index < rules.size(); ++index) {
        if (!rules.at(index).isObject()) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("rules.entry_type"),
                     QStringLiteral("rules[%1] must be an object").arg(index));
            continue;
        }

        const QJsonObject rule = rules.at(index).toObject();
        quint32 id = 0;
        if (!parseCanIdValue(rule.value(QStringLiteral("id")), &id)) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("rules.id"),
                     QStringLiteral("rules[%1] has an invalid CAN id").arg(index));
            continue;
        }
        if (ruleIds.contains(id)) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("rules.duplicate_id"),
                     QStringLiteral("duplicate rule id 0x%1").arg(id, 0, 16).toUpper());
        }
        ruleIds.insert(id);

        const QString name = rule.value(QStringLiteral("name")).toString().trimmed();
        const QString nameKo = rule.value(QStringLiteral("name_ko")).toString().trimmed();
        const QString nameEn = rule.value(QStringLiteral("name_en")).toString().trimmed();
        if (name.isEmpty() && nameKo.isEmpty() && nameEn.isEmpty()) {
            addIssue(&issues, ValidationIssue::Severity::Warning, QStringLiteral("rules.name_missing"),
                     QStringLiteral("rule 0x%1 does not declare a display name").arg(id, 0, 16).toUpper());
        }

        const QStringList timingKeys = {
            QStringLiteral("expected_period_ms"),
            QStringLiteral("ttl_warn_ms"),
            QStringLiteral("ttl_err_ms"),
            QStringLiteral("period_err_warn_pct"),
            QStringLiteral("period_err_err_pct")
        };
        for (const QString& key : timingKeys) {
            if (hasNumericField(rule, key) && rule.value(key).toDouble() < 0.0) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("rules.negative_timing"),
                         QStringLiteral("rule 0x%1 has a negative timing field: %2")
                             .arg(id, 0, 16)
                             .arg(key)
                             .toUpper());
            }
        }
    }

    for (int index = 0; index < messages.size(); ++index) {
        if (!messages.at(index).isObject()) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("messages.entry_type"),
                     QStringLiteral("messages[%1] must be an object").arg(index));
            continue;
        }

        const QJsonObject message = messages.at(index).toObject();
        quint32 id = 0;
        if (!parseCanIdValue(message.value(QStringLiteral("id")), &id)) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("messages.id"),
                     QStringLiteral("messages[%1] has an invalid CAN id").arg(index));
            continue;
        }
        if (messageIds.contains(id)) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("messages.duplicate_id"),
                     QStringLiteral("duplicate message id 0x%1").arg(id, 0, 16).toUpper());
        }
        messageIds.insert(id);

        if (!message.contains(QStringLiteral("signals")) || !message.value(QStringLiteral("signals")).isArray()) {
            addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("messages.signals"),
                     QStringLiteral("message 0x%1 must contain a signals array").arg(id, 0, 16).toUpper());
            continue;
        }

        const QJsonArray signalsArray = message.value(QStringLiteral("signals")).toArray();
        if (signalsArray.isEmpty()) {
            addIssue(&issues, ValidationIssue::Severity::Warning, QStringLiteral("messages.empty_signals"),
                     QStringLiteral("message 0x%1 has an empty signals array").arg(id, 0, 16).toUpper());
        }

        QSet<QString> signalNames;
        for (int signalIndex = 0; signalIndex < signalsArray.size(); ++signalIndex) {
            if (!signalsArray.at(signalIndex).isObject()) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.entry_type"),
                         QStringLiteral("message 0x%1 signal[%2] must be an object")
                             .arg(id, 0, 16)
                             .arg(signalIndex)
                             .toUpper());
                continue;
            }

            const QJsonObject signal = signalsArray.at(signalIndex).toObject();
            const QString signalName = signal.value(QStringLiteral("name")).toString().trimmed();
            if (signalName.isEmpty()) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.name"),
                         QStringLiteral("message 0x%1 signal[%2] is missing name")
                             .arg(id, 0, 16)
                             .arg(signalIndex)
                             .toUpper());
            } else {
                const QString key = signalNameKey(signalName);
                const bool reserved = signal.value(QStringLiteral("reserved")).toBool(false);
                if (signalNames.contains(key) && !reserved && !isPlaceholderSignalName(signalName)) {
                    addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.duplicate_name"),
                             QStringLiteral("message 0x%1 contains duplicate signal '%2'")
                                 .arg(id, 0, 16)
                                  .arg(signalName)
                                  .toUpper());
                }
                if (!reserved && !isPlaceholderSignalName(signalName)) {
                    signalNames.insert(key);
                }
            }

            const int byteIndex1Based = signal.value(QStringLiteral("byte_index_1based")).toInt(-1);
            const int lengthBits = signal.value(QStringLiteral("length_bits")).toInt(-1);
            if (byteIndex1Based < 1 || byteIndex1Based > 8) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.byte_index"),
                         QStringLiteral("message 0x%1 signal '%2' has invalid byte_index_1based=%3")
                             .arg(id, 0, 16)
                             .arg(signalName)
                             .arg(byteIndex1Based)
                             .toUpper());
            }
            if (lengthBits <= 0 || lengthBits > 64) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.length_bits"),
                         QStringLiteral("message 0x%1 signal '%2' has invalid length_bits=%3")
                             .arg(id, 0, 16)
                             .arg(signalName)
                             .arg(lengthBits)
                             .toUpper());
            }

            const QJsonArray bitPositions = signal.value(QStringLiteral("bit_positions_lsb")).toArray();
            if (!signal.contains(QStringLiteral("start_bit_lsb")) && bitPositions.isEmpty()) {
                addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.bit_mapping"),
                         QStringLiteral("message 0x%1 signal '%2' is missing bit mapping")
                             .arg(id, 0, 16)
                             .arg(signalName)
                             .toUpper());
            }
            for (const QJsonValue& bitValue : bitPositions) {
                const int bit = bitValue.toInt(-1);
                if (bit < 0 || bit > 7) {
                    addIssue(&issues, ValidationIssue::Severity::Error, QStringLiteral("signals.bit_positions"),
                             QStringLiteral("message 0x%1 signal '%2' contains invalid bit position %3")
                                 .arg(id, 0, 16)
                                 .arg(signalName)
                                 .arg(bit)
                                 .toUpper());
                    break;
                }
            }

            const bool hasAlarmThreshold =
                signal.contains(QStringLiteral("warn_min")) ||
                signal.contains(QStringLiteral("warn_max")) ||
                signal.contains(QStringLiteral("err_min")) ||
                signal.contains(QStringLiteral("err_max"));
            const QString alarmMode = signal.value(QStringLiteral("alarm_mode")).toString().trimmed();
            if (hasAlarmThreshold && alarmMode.isEmpty()) {
                addIssue(&issues, ValidationIssue::Severity::Warning, QStringLiteral("signals.alarm_mode_missing"),
                         QStringLiteral("message 0x%1 signal '%2' has thresholds but no alarm_mode")
                             .arg(id, 0, 16)
                             .arg(signalName)
                             .toUpper());
            }
        }
    }

    for (auto it = ruleIds.cbegin(); it != ruleIds.cend(); ++it) {
        if (!messageIds.contains(*it)) {
            addIssue(&issues, ValidationIssue::Severity::Warning, QStringLiteral("pack.rule_only"),
                     QStringLiteral("rule-only id 0x%1 has no matching message entry").arg(*it, 0, 16).toUpper());
        }
    }
    for (auto it = messageIds.cbegin(); it != messageIds.cend(); ++it) {
        if (!ruleIds.contains(*it)) {
            addIssue(&issues, ValidationIssue::Severity::Warning, QStringLiteral("pack.message_only"),
                     QStringLiteral("message-only id 0x%1 has no matching timing rule entry").arg(*it, 0, 16).toUpper());
        }
    }

    return issues;
}

bool ModelPackValidator::hasErrors(const QVector<ValidationIssue>& issues) {
    for (const ValidationIssue& issue : issues) {
        if (issue.isError()) return true;
    }
    return false;
}

QString ModelPackValidator::summarize(const QVector<ValidationIssue>& issues) {
    QStringList parts;
    for (const ValidationIssue& issue : issues) {
        parts << QStringLiteral("%1: %2").arg(issue.code, issue.message);
    }
    return parts.join(QStringLiteral("; "));
}

} // namespace CanModel
