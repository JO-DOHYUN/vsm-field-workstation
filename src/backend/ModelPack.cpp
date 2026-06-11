#include "ModelPack.h"
#include "AppLogging.h"
#include "ModelPackValidator.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <algorithm>

namespace CanModel {
namespace {

QString normalizeLocalPath(const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};

    const QUrl url(trimmed);
    if (url.isValid() && url.isLocalFile()) {
        return QDir::fromNativeSeparators(url.toLocalFile());
    }

    if (trimmed.startsWith(QStringLiteral("file:/"), Qt::CaseInsensitive)) {
        const QUrl userUrl = QUrl::fromUserInput(trimmed);
        if (userUrl.isValid() && userUrl.isLocalFile()) {
            return QDir::fromNativeSeparators(userUrl.toLocalFile());
        }
    }

    return QDir::fromNativeSeparators(trimmed);
}

bool parseCanIdText(const QString& text, quint32* out) {
    if (!out) return false;
    bool ok = false;
    const QString trimmed = text.trimmed();
    quint32 id = 0;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        id = trimmed.mid(2).toUInt(&ok, 16);
    } else {
        id = trimmed.toUInt(&ok, 10);
    }
    if (!ok) return false;
    *out = id;
    return true;
}

bool parseCanIdValue(const QJsonValue& value, quint32* out) {
    if (!out) return false;
    if (value.isString()) return parseCanIdText(value.toString(), out);
    if (value.isDouble()) {
        *out = quint32(value.toInteger());
        return true;
    }
    return false;
}

QSet<quint32> defaultSystemBusFingerprints() {
    return QSet<quint32>{
        0x111u, 0x117u, 0x118u, 0x119u, 0x120u,
        0x2D0u, 0x401u, 0x503u, 0x520u, 0x600u
    };
}


QVector<qint64> parseInt64Array(const QJsonValue& value) {
    QVector<qint64> out;
    if (!value.isArray()) return out;
    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (const QJsonValue& item : arr) {
        if (item.isDouble()) out.push_back(item.toInteger());
        else if (item.isString()) {
            bool ok = false;
            const qint64 v = item.toString().trimmed().toLongLong(&ok, 0);
            if (ok) out.push_back(v);
        }
    }
    return out;
}

QStringList parseStringList(const QJsonValue& value) {
    QStringList out;
    if (!value.isArray()) return out;
    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (const QJsonValue& item : arr) {
        const QString t = item.toString().trimmed();
        if (!t.isEmpty()) out.push_back(t);
    }
    return out;
}

QSet<quint32> parseCanIdSet(const QJsonValue& value) {
    QSet<quint32> out;
    if (!value.isArray()) return out;
    const QJsonArray arr = value.toArray();
    for (const QJsonValue& item : arr) {
        quint32 id = 0;
        if (parseCanIdValue(item, &id)) out.insert(id & 0x1FFFFFFFu);
    }
    return out;
}

QStringList parseNormalizedRoleList(const QJsonValue& value) {
    QStringList out;
    const QStringList raw = parseStringList(value);
    for (const QString& item : raw) {
        const QString normalized = item.trimmed().toLower();
        if (!normalized.isEmpty() && !out.contains(normalized)) out.push_back(normalized);
    }
    return out;
}

QString bestRuleName(const QJsonObject& obj, quint32 id) {
    const QString ko = obj.value(QStringLiteral("name_ko")).toString().trimmed();
    if (!ko.isEmpty()) return ko;
    const QString en = obj.value(QStringLiteral("name_en")).toString().trimmed();
    if (!en.isEmpty()) return en;
    const QString generic = obj.value(QStringLiteral("name")).toString().trimmed();
    if (!generic.isEmpty()) return generic;
    return QStringLiteral("0x%1").arg(id, 0, 16).toUpper();
}

ControlPolicySpec parseControlPolicy(const QJsonObject& root) {
    ControlPolicySpec policy;
    if (!root.value(QStringLiteral("control_policy")).isObject()) {
        policy.allowedBusRoles = {QStringLiteral("system")};
        policy.busRoleRules.push_back(BusRoleRuleSpec{QStringLiteral("system"), defaultSystemBusFingerprints(), true});
        return policy;
    }

    const QJsonObject obj = root.value(QStringLiteral("control_policy")).toObject();
    policy.declared = true;
    policy.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    policy.profileName = obj.value(QStringLiteral("profile_name")).toString().trimmed();
    policy.targetRole = obj.value(QStringLiteral("target_role")).toString(QStringLiteral("system")).trimmed().toLower();
    if (policy.targetRole.isEmpty()) policy.targetRole = QStringLiteral("system");
    policy.allowedBusRoles = parseNormalizedRoleList(obj.value(QStringLiteral("allowed_bus_roles")));
    if (policy.allowedBusRoles.isEmpty()) policy.allowedBusRoles = {policy.targetRole};
    policy.maxRpm = std::clamp(obj.value(QStringLiteral("max_rpm")).toInt(10000), 0, 10000);
    policy.maxAbsSteeringDeg = std::clamp(obj.value(QStringLiteral("max_abs_steering_deg")).toDouble(90.0), 0.0, 90.0);

    const QJsonArray rules = obj.value(QStringLiteral("bus_role_rules")).toArray();
    for (const QJsonValue& value : rules) {
        if (!value.isObject()) continue;
        const QJsonObject ruleObj = value.toObject();
        BusRoleRuleSpec rule;
        rule.role = ruleObj.value(QStringLiteral("role")).toString().trimmed().toLower();
        rule.txAllowed = ruleObj.value(QStringLiteral("tx_allowed")).toBool(false);
        rule.fingerprints = parseCanIdSet(ruleObj.value(QStringLiteral("fingerprints")));
        if (!rule.role.isEmpty() && !rule.fingerprints.isEmpty()) policy.busRoleRules.push_back(rule);
    }
    if (policy.busRoleRules.isEmpty()) {
        policy.busRoleRules.push_back(BusRoleRuleSpec{QStringLiteral("system"), defaultSystemBusFingerprints(), true});
    }
    return policy;
}

PackMeta parseMeta(const QJsonObject& root) {
    PackMeta meta;
    const QString schema = root.value(QStringLiteral("schema")).toString().trimmed();
    if (!schema.isEmpty()) meta.schema = schema;
    meta.modelKey = root.value(QStringLiteral("model_key")).toString().trimmed();
    meta.modelName = root.value(QStringLiteral("model_name")).toString().trimmed();
    meta.modelVersion = root.value(QStringLiteral("model_version")).toString().trimmed();
    meta.vendor = root.value(QStringLiteral("vendor")).toString().trimmed();
    meta.notes = root.value(QStringLiteral("notes")).toString().trimmed();

    if (meta.modelName.isEmpty()) {
        meta.modelName = root.value(QStringLiteral("name")).toString().trimmed();
    }
    return meta;
}

} // namespace

bool ModelPackLoader::loadFile(const QString& path, ModelPack* outPack, QString* outError) {
    if (!outPack) {
        if (outError) *outError = QStringLiteral("output pack is null");
        return false;
    }

    const QString normalized = path.startsWith(QStringLiteral(":")) ? path : normalizeLocalPath(path);
    QFile file(normalized);
    if (!file.open(QIODevice::ReadOnly)) {
        if (outError) *outError = QStringLiteral("모델 파일 열기 실패: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (outError) *outError = QStringLiteral("모델 JSON 파싱 실패: %1").arg(err.errorString());
        qCWarning(logModel).noquote() << "Model pack JSON parse failed for" << normalized << ":" << err.errorString();
        return false;
    }

    return loadObject(doc.object(), outPack, outError);
}

bool ModelPackLoader::loadObject(const QJsonObject& root, ModelPack* outPack, QString* outError) {
    if (!outPack) {
        if (outError) *outError = QStringLiteral("output pack is null");
        return false;
    }

    const QVector<ValidationIssue> issues = ModelPackValidator::validate(root);
    for (const ValidationIssue& issue : issues) {
        if (issue.isError()) qCWarning(logModel).noquote() << issue.code << issue.message;
        else qCInfo(logModel).noquote() << issue.code << issue.message;
    }
    if (ModelPackValidator::hasErrors(issues)) {
        if (outError) *outError = ModelPackValidator::summarize(issues);
        return false;
    }

    ModelPack pack;
    pack.meta = parseMeta(root);
    pack.controlPolicy = parseControlPolicy(root);

    const QJsonArray rulesArray = root.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue& value : rulesArray) {
        if (!value.isObject()) continue;
        const QJsonObject obj = value.toObject();
        quint32 id = 0;
        if (!parseCanIdValue(obj.value(QStringLiteral("id")), &id)) continue;

        RuleSpec rule;
        rule.id = id;
        rule.name = bestRuleName(obj, id);
        if (obj.contains(QStringLiteral("expected_period_ms"))) rule.expectedPeriodMs = obj.value(QStringLiteral("expected_period_ms")).toDouble(-1.0);
        if (obj.contains(QStringLiteral("ttl_warn_ms"))) rule.ttlWarnMs = obj.value(QStringLiteral("ttl_warn_ms")).toDouble(-1.0);
        if (obj.contains(QStringLiteral("ttl_err_ms"))) rule.ttlErrMs = obj.value(QStringLiteral("ttl_err_ms")).toDouble(-1.0);
        if (obj.contains(QStringLiteral("period_err_warn_pct"))) rule.periodWarnPct = obj.value(QStringLiteral("period_err_warn_pct")).toDouble(-1.0);
        if (obj.contains(QStringLiteral("period_err_err_pct"))) rule.periodErrPct = obj.value(QStringLiteral("period_err_err_pct")).toDouble(-1.0);
        if (obj.contains(QStringLiteral("timing_enabled"))) rule.timingEnabled = obj.value(QStringLiteral("timing_enabled")).toBool(true);
        rule.timingMode = obj.value(QStringLiteral("timing_mode")).toString().trimmed();
        pack.rules.insert(id, rule);
    }

    const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue& mv : messages) {
        if (!mv.isObject()) continue;
        const QJsonObject mo = mv.toObject();
        quint32 id = 0;
        if (!parseCanIdValue(mo.value(QStringLiteral("id")), &id)) continue;

        SignalMessageSpec msg;
        msg.id = id;
        msg.name = mo.value(QStringLiteral("name")).toString().trimmed();
        const QJsonArray sigs = mo.value(QStringLiteral("signals")).toArray();
        for (const QJsonValue& sv : sigs) {
            if (!sv.isObject()) continue;
            const QJsonObject so = sv.toObject();
            SignalSpec sig;
            sig.name = so.value(QStringLiteral("name")).toString().trimmed();
            sig.byteIndex1Based = so.value(QStringLiteral("byte_index_1based")).toInt(1);
            sig.bitText = so.value(QStringLiteral("bit_text")).toString().trimmed();
            sig.lengthBits = so.value(QStringLiteral("length_bits")).toInt(8);
            sig.startBitLsb = so.value(QStringLiteral("start_bit_lsb")).toInt(0);
            const QJsonArray positions = so.value(QStringLiteral("bit_positions_lsb")).toArray();
            sig.bitPositionsLsb.reserve(positions.size());
            for (const QJsonValue& pv : positions) sig.bitPositionsLsb.push_back(pv.toInt());
            sig.scale = so.value(QStringLiteral("scale")).toDouble(1.0);
            sig.offset = so.value(QStringLiteral("offset")).toDouble(0.0);
            sig.signedValue = so.value(QStringLiteral("signed")).toBool(false);
            sig.rangeText = so.value(QStringLiteral("range_text")).toString().trimmed();
            sig.operatingText = so.value(QStringLiteral("operating_text")).toString().trimmed();
            sig.description = so.value(QStringLiteral("description")).toString().trimmed();
            sig.reserved = so.value(QStringLiteral("reserved")).toBool(false);
            sig.unit = so.value(QStringLiteral("unit")).toString().trimmed();
            sig.alarmMode = so.value(QStringLiteral("alarm_mode")).toString().trimmed();
            if (so.contains(QStringLiteral("warn_min"))) { sig.hasWarnMin = true; sig.warnMin = so.value(QStringLiteral("warn_min")).toDouble(); }
            if (so.contains(QStringLiteral("warn_max"))) { sig.hasWarnMax = true; sig.warnMax = so.value(QStringLiteral("warn_max")).toDouble(); }
            if (so.contains(QStringLiteral("err_min"))) { sig.hasErrMin = true; sig.errMin = so.value(QStringLiteral("err_min")).toDouble(); }
            if (so.contains(QStringLiteral("err_max"))) { sig.hasErrMax = true; sig.errMax = so.value(QStringLiteral("err_max")).toDouble(); }
            sig.inactiveRawValues = parseInt64Array(so.value(QStringLiteral("inactive_raw_values")));
            sig.inactiveLabels = parseStringList(so.value(QStringLiteral("inactive_labels")));
            sig.alarmSeverity = so.value(QStringLiteral("alarm_severity")).toString().trimmed();
            sig.alarmMessage = so.value(QStringLiteral("alarm_message")).toString().trimmed();
            sig.monitorOnly = so.value(QStringLiteral("monitor_only")).toBool(false);
            if (!sig.name.isEmpty()) msg.signalSpecs.push_back(sig);
        }
        if (!msg.signalSpecs.isEmpty() || !msg.name.isEmpty()) pack.messages.insert(id, msg);
    }

    if (pack.meta.modelName.isEmpty()) {
        if (!pack.rules.isEmpty()) pack.meta.modelName = QStringLiteral("External Model Pack");
        else if (!pack.messages.isEmpty()) pack.meta.modelName = QStringLiteral("Signal-only Model Pack");
    }
    if (pack.meta.modelKey.isEmpty()) {
        pack.meta.modelKey = pack.meta.modelName.toLower().replace(' ', '_');
    }

    if (!pack.hasContent()) {
        if (outError) *outError = QStringLiteral("모델 팩에 rules/messages가 없습니다");
        qCWarning(logModel).noquote() << "Model pack loaded without rules/messages content";
        return false;
    }

    *outPack = pack;
    qCInfo(logModel).noquote() << "Loaded model pack" << pack.meta.modelName
                               << "rules=" << pack.rules.size()
                               << "messages=" << pack.messages.size();
    return true;
}

} // namespace CanModel
