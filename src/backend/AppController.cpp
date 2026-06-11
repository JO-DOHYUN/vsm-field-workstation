#include "AppController.h"
#include "AppLogging.h"
#include "BuildMetadata.h"
#include "ControlCommandEncoder.h"
#include "FilePersistence.h"
#include "RuntimePaths.h"
#include "StorageRuntime.h"
#include "UiStateStore.h"
#include "TimingEvaluator.h"
#include "LevelState.h"
#include "SignalDecoder.h"
#include "AlarmManager.h"
#include "TypedReplayReader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace {
const QString kBundledModelPath = QStringLiteral(":/data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json");
constexpr int kControlBurstFrameGapMs = 2;
constexpr int kControlMinBurstIntervalMs = 160;
constexpr int kControlWorkerCyclePeriodMs = 200;
constexpr int kTypedEvidenceUiMinIntervalMs = 120;
constexpr int kRoutineControlWriteUiMinIntervalMs = 250;
constexpr int kHostTxQueueUiMinIntervalMs = 250;
constexpr int kControlKeyboardLegacyPulseMs = 120;
constexpr double kControlKeyboardSteerHoldDeg = 45.0;
constexpr int kLiveProjectionSoftBacklog = 4096;
constexpr int kLiveProjectionHardBacklog = 8192;
constexpr int kLiveProjectionMaxFlushFrames = 512;
constexpr int kLiveProjectionFlushBudgetMs = 4;

quint16 boundedFpsFromDelta(quint32 delta, quint64 elapsedUs) {
    if (elapsedUs == 0) return 0;
    const double fps = (double(delta) * 1'000'000.0) / double(elapsedUs);
    return quint16(std::clamp<int>(int(std::lround(fps)), 0, std::numeric_limits<quint16>::max()));
}

quint64 u32CounterDelta(quint32 current, quint32 previous) {
    if (current >= previous) return quint64(current - previous);
    return (quint64(1) << 32) - quint64(previous) + quint64(current);
}

QString normalizeControlKeyboardKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("w") || normalized == QStringLiteral("up") || normalized == QStringLiteral("forward")) {
        return QStringLiteral("w");
    }
    if (normalized == QStringLiteral("s") || normalized == QStringLiteral("down") || normalized == QStringLiteral("reverse")) {
        return QStringLiteral("s");
    }
    if (normalized == QStringLiteral("a") || normalized == QStringLiteral("left")) {
        return QStringLiteral("a");
    }
    if (normalized == QStringLiteral("d") || normalized == QStringLiteral("right")) {
        return QStringLiteral("d");
    }
    if (normalized == QStringLiteral("x") || normalized == QStringLiteral("escape") || normalized == QStringLiteral("neutral") || normalized == QStringLiteral("stop")) {
        return QStringLiteral("x");
    }
    if (normalized == QStringLiteral("space") || normalized == QStringLiteral("emergency") || normalized == QStringLiteral("estop")) {
        return QStringLiteral("space");
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("2") || normalized == QStringLiteral("3")) {
        return normalized;
    }
    return {};
}

QString controlAckStatusText(quint8 status) {
    switch (status) {
    case 0: return QStringLiteral("REJECTED");
    case 1: return QStringLiteral("ACCEPTED");
    case 2: return QStringLiteral("ACCEPTED_WRITTEN");
    case 3: return QStringLiteral("ACCEPTED_RATE_LIMITED");
    default: return QStringLiteral("STATUS_%1").arg(status);
    }
}

QString controlAckReasonText(quint8 reason) {
    switch (reason) {
    case 0: return QStringLiteral("OK");
    case 1: return QStringLiteral("BAD_LENGTH");
    case 2: return QStringLiteral("BAD_BUS");
    case 3: return QStringLiteral("UNSUPPORTED_FRAME");
    case 4: return QStringLiteral("DLC_OUT_OF_RANGE");
    case 5: return QStringLiteral("ID_NOT_ALLOWED");
    case 6: return QStringLiteral("CAN_NOT_READY");
    case 7: return QStringLiteral("CAN_WRITE_FAILED");
    case 8: return QStringLiteral("BAD_PROTOCOL");
    case 9: return QStringLiteral("SAFETY_NOT_ARMED");
    case 10: return QStringLiteral("HOST_TIMEOUT");
    case 11: return QStringLiteral("CONTROL_LEASE_EXPIRED");
    case 12: return QStringLiteral("SAFETY_LOCKOUT");
    case 13: return QStringLiteral("ESTOP_ASSERTED");
    case 14: return QStringLiteral("FIELD_POWER_LOST");
    case 15: return QStringLiteral("ENCODER_FAULT");
    case 16: return QStringLiteral("QUEUE_FULL");
    case 17: return QStringLiteral("TX_BUSY");
    case 18: return QStringLiteral("BUS_OFF");
    case 19: return QStringLiteral("ERROR_PASSIVE");
    case 20: return QStringLiteral("ROLE_UNRESOLVED");
    case 21: return QStringLiteral("POLICY_HASH_MISMATCH");
    case 22: return QStringLiteral("NEUTRAL_PROFILE_MISSING");
    case 23: return QStringLiteral("RATE_LIMITED");
    case 24: return QStringLiteral("UNSUPPORTED_COMMAND");
    default: return QStringLiteral("REASON_%1").arg(reason);
    }
}

QString controlAckDlcText(quint8 dlcFlags) {
    return QStringLiteral("DLC %1").arg(dlcFlags & 0x0F);
}

QString controlAckEvidenceHint(quint8 status, quint8 reason) {
    if (status == 0 && reason == 10) {
        return QStringLiteral("; blocked before CAN TX by board host lease/timeout gate");
    }
    if (status == 0) {
        return QStringLiteral("; blocked before CAN TX");
    }
    return QStringLiteral("; board accepted request; waiting for CAN_TX_RAW actual sent audit");
}

QString boardEventCodeText(quint16 code) {
    switch (code) {
    case 1: return QStringLiteral("BOOT");
    case 2: return QStringLiteral("CAN_BEGIN_FAILED");
    case 3: return QStringLiteral("CAN_RX_QUEUE_DROP");
    case 4: return QStringLiteral("ENCODER_FAULT_ASSERTED");
    case 5: return QStringLiteral("FIELD_POWER_LOST");
    case 6: return QStringLiteral("ESTOP_ASSERTED");
    case 7: return QStringLiteral("ENCODER_INDEX");
    case 8: return QStringLiteral("ENCODER_WRAP");
    case 9: return QStringLiteral("MCP2515_ERROR");
    case 10: return QStringLiteral("MCP2515_SPI_SNAPSHOT");
    case 11: return QStringLiteral("BUILTIN_CAN_BEGIN_FAILED");
    case 12: return QStringLiteral("BUILTIN_CAN_TX_FAILED");
    case 13: return QStringLiteral("HOST_FRAME_CRC_FAILED");
    case 14: return QStringLiteral("HOST_CAN_TX_REJECTED");
    case 15: return QStringLiteral("HOST_CAN_TX_ACCEPTED");
    case 16: return QStringLiteral("CAN0_BACKEND_UNAVAILABLE");
    case 17: return QStringLiteral("MCP2515_TX_FAILED");
    case 18: return QStringLiteral("SAFETY_STATE_CHANGED");
    case 19: return QStringLiteral("HOST_HEARTBEAT");
    case 20: return QStringLiteral("HOST_CONTROL_SESSION");
    case 21: return QStringLiteral("HOST_COMMAND_UNSUPPORTED");
    case 22: return QStringLiteral("FAULT_LOCKOUT_CLEARED");
    default: return QStringLiteral("BOARD_EVENT_%1").arg(code);
    }
}

bool isControlCommandCanId(quint32 canId) {
    return canId == 0x503u || (canId >= 0x510u && canId <= 0x513u);
}

QString capabilityBackendText(quint8 backend) {
    switch (backend) {
    case 1: return QStringLiteral("MCP2515");
    case 2: return QStringLiteral("ArduinoCAN");
    case 3: return QStringLiteral("STM32CAN");
    case 4: return QStringLiteral("pending");
    default: return QStringLiteral("backend%1").arg(backend);
    }
}

QString mapCountsText(const QMap<quint8, quint64>& counts, const QString& prefix) {
    QStringList parts;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        parts << QStringLiteral("%1%2=%3").arg(prefix).arg(it.key()).arg(it.value());
    }
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral(", "));
}

QString capabilityRoleHintText(quint8 roleHint) {
    switch (roleHint) {
    case 1: return QStringLiteral("system");
    case 2: return QStringLiteral("drive");
    case 3: return QStringLiteral("debug");
    default: return QStringLiteral("role?");
    }
}

QString capabilityResolverRole(quint8 roleHint) {
    switch (roleHint) {
    case 1: return QStringLiteral("system");
    case 2: return QStringLiteral("drive");
    case 3: return QStringLiteral("debug");
    default: return {};
    }
}

QString busRoleSourceText(CanMonitorEvidence::BusRoleResolver::Source source) {
    using Source = CanMonitorEvidence::BusRoleResolver::Source;
    switch (source) {
    case Source::Capability: return QStringLiteral("CAPABILITY");
    case Source::ModelRule: return QStringLiteral("model");
    case Source::ObservedFingerprint: return QStringLiteral("observed");
    case Source::OperatorOverride: return QStringLiteral("operator");
    case Source::Unresolved:
    default:
        return QStringLiteral("unresolved");
    }
}

QSet<quint32> systemBusFingerprints() {
    return QSet<quint32>{
        0x111u, 0x117u, 0x118u, 0x119u, 0x120u,
        0x2D0u, 0x401u, 0x503u, 0x520u, 0x600u
    };
}

bool isSystemBusFingerprint(quint32 canId) {
    static const QSet<quint32> kSystemIds = systemBusFingerprints();
    return kSystemIds.contains(canId & 0x1FFFFFFFu);
}

bool moveOrCopyReplace(const QString& src, const QString& dst) {
    if (src.trimmed().isEmpty()) return true;
    QString error;
    if (!FilePersistence::copyFileAtomically(src, dst, &error)) {
        qCWarning(logDeploy).noquote() << "File move staging failed:" << error;
        return false;
    }
    if (!FilePersistence::removeFileIfExists(src, &error)) {
        qCWarning(logDeploy).noquote() << "Source cleanup after staged move failed:" << error;
        return false;
    }
    return true;
}

void removeFileIfExists(const QString& path) {
    QString error;
    if (!FilePersistence::removeFileIfExists(path, &error) && !error.isEmpty()) {
        qCWarning(logDeploy).noquote() << "File cleanup failed:" << error;
    }
}

QString formatBytesCompact(quint64 bytes) {
    const double kb = double(bytes) / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    const double mb = kb / 1024.0;
    if (mb < 1024.0) return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 1));
    const double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(QString::number(gb, 'f', 2));
}

QString severityColor(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return QStringLiteral("#c0392b");
    if (severity == QStringLiteral("WARN")) return QStringLiteral("#d97706");
    if (severity == QStringLiteral("OK")) return QStringLiteral("#118a42");
    if (severity == QStringLiteral("관찰")) return QStringLiteral("#2563eb");
    if (severity == QStringLiteral("복구")) return QStringLiteral("#2563eb");
    if (severity == QStringLiteral("해제")) return QStringLiteral("#94a3b8");
    if (severity == QStringLiteral("미수신")) return QStringLiteral("#6b7280");
    return QStringLiteral("#52606d");
}

quint64 framePayloadFingerprint(const FrameRecord& fr) {
    quint64 fingerprint = quint64(fr.canId) ^ (quint64(fr.dlc) << 29) ^ (quint64(fr.bus) << 37);
    for (int i = 0; i < 8; ++i) {
        fingerprint = (fingerprint * 1315423911ULL) ^ quint64(fr.data[i] + (i * 17));
    }
    return fingerprint;
}

QString valueDetailSignatureForState(quint32 id, const QString& source, quint64 fingerprint,
                                     const CanMonitorAnalysis::TimingEvalResult& eval, const QVariantMap& alarmInfo) {
    const qint64 ageBucket = eval.ageMs < 0.0 ? -1 : qint64(std::floor(eval.ageMs / 250.0));
    const qint64 gapBucket = eval.gapMs < 0.0 ? -1 : qint64(std::floor(eval.gapMs / 50.0));
    const QString alarmSeverity = alarmInfo.value(QStringLiteral("severity")).toString();
    const QString alarmMessage = alarmInfo.value(QStringLiteral("message")).toString();
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(id)
        .arg(source)
        .arg(QString::number(fingerprint))
        .arg(eval.severity)
        .arg(eval.reason)
        .arg(ageBucket)
        .arg(alarmSeverity)
        .arg(alarmMessage + QStringLiteral("|") + QString::number(gapBucket));
}

int severityRank(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return 4;
    if (severity == QStringLiteral("WARN")) return 3;
    if (severity == QStringLiteral("OK")) return 2;
    if (severity == QStringLiteral("관찰")) return 1;
    if (severity == QStringLiteral("미수신")) return 1;
    return 0;
}

QString categoryLabel(const QString& category) {
    if (category == QStringLiteral("timing")) return QStringLiteral("주기");
    if (category == QStringLiteral("value")) return QStringLiteral("값");
    if (category == QStringLiteral("bus")) return QStringLiteral("버스");
    if (category == QStringLiteral("model")) return QStringLiteral("모델");
    if (category == QStringLiteral("replay")) return QStringLiteral("재생");
    return category.trimmed().isEmpty() ? QStringLiteral("기타") : category;
}

CanMonitorAnalysis::AlarmGroup* findAlarmGroup(QVector<CanMonitorAnalysis::AlarmGroup>& groups, const QString& key) {
    for (auto& group : groups) {
        if (group.key == key) return &group;
    }
    return nullptr;
}

template <typename StateMap>
StateMap makeCheckpointStateMap(const StateMap& src) {
    StateMap out = src;
    for (auto it = out.begin(); it != out.end(); ++it) {
        it.value().cachedTimingRow.clear();
        it.value().cachedPreviewInfo.clear();
        it.value().cachedValueAlarmInfo.clear();
        it.value().cachedValueRow.clear();
        it.value().timingDerivedDirty = true;
        it.value().valueDerivedDirty = true;
    }
    return out;
}

void resolveAlarmGroup(const QString& key,
                       const QString&,
                       const QString&,
                       QVector<CanMonitorAnalysis::AlarmGroup>& groups,
                       bool& rowsDirty) {
    if (key.isEmpty()) return;
    for (qsizetype i = 0; i < groups.size(); ++i) {
        if (groups[i].key != key) continue;
        groups.removeAt(i);
        rowsDirty = true;
        break;
    }
}

void syncAlarmGroup(quint32 id,
                    const QString& key,
                    const QString& severity,
                    const QString& category,
                    const QString& name,
                    const QString& source,
                    const QString& timeText,
                    const QString& message,
                    const QString& metricText,
                    double gaugePct,
                    QVector<CanMonitorAnalysis::AlarmGroup>& groups,
                    qint64& alarmSequence,
                    bool& rowsDirty) {
    if (key.isEmpty()) return;
    auto* group = findAlarmGroup(groups, key);
    const QString nowText = timeText.trimmed().isEmpty() ? QStringLiteral("-") : timeText.trimmed();

    if (!group) {
        CanMonitorAnalysis::AlarmGroup fresh;
        fresh.sequence = ++alarmSequence;
        fresh.key = key;
        fresh.id = id;
        fresh.timeText = nowText;
        fresh.severity = severity;
        fresh.severityColor = severityColor(severity);
        fresh.name = name;
        fresh.source = source;
        fresh.message = message;
        fresh.severityRank = severityRank(severity);
        fresh.active = true;
        fresh.updateCount = 1;
        fresh.category = category;
        fresh.metricText = metricText;
        fresh.gaugePct = gaugePct;
        fresh.history << QStringLiteral("[%1] %2").arg(nowText, message);
        groups.push_back(fresh);
        while (groups.size() > 160) groups.removeFirst();
        rowsDirty = true;
        return;
    }

    const bool changed = !group->active || group->severity != severity || group->message != message || group->metricText != metricText;
    group->active = true;
    group->timeText = nowText;
    group->severity = severity;
    group->severityColor = severityColor(severity);
    group->name = name;
    group->source = source;
    group->message = message;
    group->severityRank = severityRank(severity);
    group->category = category;
    group->metricText = metricText;
    group->gaugePct = gaugePct;
    group->updateCount += 1;
    if (changed) {
        group->history.prepend(QStringLiteral("[%1] %2").arg(nowText, message));
        while (group->history.size() > 32) group->history.removeLast();
        rowsDirty = true;
    }
}

QString fmtMs(double ms) {
    if (ms < 0.0) return QStringLiteral("-");
    return QString::number(ms, 'f', 1) + QStringLiteral(" ms");
}

QString fmtPct(double pct) {
    if (pct < 0.0) return QStringLiteral("-");
    return QString::number(pct, 'f', 1) + QStringLiteral(" %");
}

QString fmtWallAge(qint64 ms) {
    if (ms < 0) return QStringLiteral("-");
    if (ms < 1000) return QStringLiteral("%1 ms").arg(ms);
    if (ms < 60000) return QStringLiteral("%1 s").arg(QString::number(double(ms) / 1000.0, 'f', 1));
    return QStringLiteral("%1 min").arg(QString::number(double(ms) / 60000.0, 'f', 1));
}

QString normalizeAlarmMessageKey(QString text) {
    text = text.toLower().trimmed();
    text.replace(QRegularExpression(QStringLiteral("([+-]?\\d+(?:\\.\\d+)?)(?:\\s*(%|ms|v|a|c|soc|soh)?)")), QStringLiteral("#"));
    text.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return text.trimmed();
}

QString joinNonEmpty(const QStringList& parts, const QString& sep = QStringLiteral(" · ")) {
    QStringList filtered;
    for (const QString& part : parts) {
        if (!part.trimmed().isEmpty()) filtered << part.trimmed();
    }
    return filtered.join(sep);
}

static bool signalSpecHasAlarmDefinition(const CanModel::SignalSpec& sig) {
    const bool hasThreshold = sig.hasWarnMin || sig.hasWarnMax || sig.hasErrMin || sig.hasErrMax;
    const bool hasInactive = !sig.inactiveRawValues.isEmpty() || !sig.inactiveLabels.isEmpty();
    const bool namedMode = !sig.alarmMode.trimmed().isEmpty();
    const bool namedSeverity = !sig.alarmSeverity.trimmed().isEmpty();
    const bool namedMessage = !sig.alarmMessage.trimmed().isEmpty();
    return hasThreshold || hasInactive || namedMode || namedSeverity || namedMessage;
}

QJsonArray stringListToJson(const QStringList& list) {
    QJsonArray arr;
    for (const QString& item : list) arr.append(item);
    return arr;
}

bool parseCanIdText(const QString& text, quint32* out) {
    if (!out) return false;
    bool ok = false;
    const QString trimmed = text.trimmed();
    quint32 id = 0;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) id = trimmed.mid(2).toUInt(&ok, 16);
    else id = trimmed.toUInt(&ok, 10);
    if (!ok) return false;
    *out = id;
    return true;
}

DetailRow makeModelDetailRow(const QString& key, const QString& value, const QString& note = QString()) {
    return DetailRow{key, value, note};
}

QString graphSeriesKeyFor(quint32 id, int signalIndex) {
    return QStringLiteral("%1|%2").arg(idText(id)).arg(signalIndex);
}

QString graphCleanName(QString text) {
    text = text.trimmed();
    text.replace(QRegularExpression(QStringLiteral(R"([_\s]+)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return text.trimmed();
}

bool graphNameContains(const QString& text, const QStringList& needles) {
    const QString lower = text.trimmed().toLower();
    for (const QString& needle : needles) {
        if (lower.contains(needle)) return true;
    }
    return false;
}

QString graphInferUnit(const CanModel::SignalSpec& sig, const QString& cleanName) {
    if (!sig.unit.trimmed().isEmpty()) return sig.unit.trimmed();
    const QString joined = (cleanName + QLatin1Char(' ') + sig.operatingText + QLatin1Char(' ') + sig.rangeText + QLatin1Char(' ') + sig.description).toLower();
    if (joined.contains(QStringLiteral("rpm"))) return QStringLiteral("rpm");
    if (joined.contains(QStringLiteral("degree")) || joined.contains(QStringLiteral("deg")) || joined.contains(QStringLiteral("angle"))) return QStringLiteral("deg");
    if (joined.contains(QStringLiteral("0.01 a")) || joined.contains(QStringLiteral("current")) || joined.contains(QStringLiteral(" amp"))) return QStringLiteral("A");
    if (joined.contains(QStringLiteral("0.01 v")) || joined.contains(QStringLiteral("0.1 v")) || joined.contains(QStringLiteral("voltage")) || joined.contains(QStringLiteral(" volt"))) return QStringLiteral("V");
    if (joined.contains(QStringLiteral("temperature")) || joined.contains(QStringLiteral(" c")) || joined.contains(QStringLiteral("℃"))) return QStringLiteral("°C");
    if (joined.contains(QStringLiteral("encoder")) || joined.contains(QStringLiteral("enc"))) return QStringLiteral("count");
    return QString();
}

bool graphPairLowName(const QString& name) {
    const QString n = name.toLower();
    return n.contains(QStringLiteral("low byte")) || n.contains(QStringLiteral("lower byte")) || n.contains(QStringLiteral("하위바이트"));
}

bool graphPairHighName(const QString& name) {
    const QString n = name.toLower();
    return n.contains(QStringLiteral("high byte")) || n.contains(QStringLiteral("upper byte")) || n.contains(QStringLiteral("상위바이트"));
}

QString graphPairBaseName(QString name) {
    QString t = graphCleanName(name);
    t.replace(QRegularExpression(QStringLiteral(R"(\b(Low|High|Upper|Lower)\s*Byte\b)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QStringLiteral("상위바이트"), QString());
    t.replace(QStringLiteral("하위바이트"), QString());
    t.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return t.trimmed();
}

bool graphIsInterestingNumeric(const CanModel::SignalSpec& sig, const QString& cleanName) {
    if (sig.reserved) return false;
    const QString joined = (cleanName + QLatin1Char(' ') + sig.operatingText + QLatin1Char(' ') + sig.description).toLower();
    if (graphPairHighName(cleanName)) return false;
    if (graphNameContains(joined, {QStringLiteral("rpm"), QStringLiteral("angle"), QStringLiteral("encoder"), QStringLiteral("current"), QStringLiteral("voltage"), QStringLiteral("temperature"), QStringLiteral("speed"), QStringLiteral("torque"), QStringLiteral("command"), QStringLiteral("actual")})) return true;
    if (!sig.unit.trimmed().isEmpty()) return true;
    if (sig.lengthBits >= 12) return true;
    if (std::abs(sig.scale - 1.0) > 1e-12 || std::abs(sig.offset) > 1e-12) return true;
    return false;
}

quint64 graphExtractContiguousBits(const FrameRecord& frame, int startBit, int lengthBits) {
    quint64 value = 0;
    for (int i = 0; i < lengthBits && i < 64; ++i) {
        const int globalBit = startBit + i;
        const int byteIndex = globalBit / 8;
        const int bitIndex = globalBit % 8;
        if (byteIndex < 0 || byteIndex >= 8) break;
        if (byteIndex >= int(frame.dlc)) continue;
        if (frame.data[byteIndex] & (1u << bitIndex)) value |= (1ULL << i);
    }
    return value;
}

quint64 graphExtractExplicitBits(const FrameRecord& frame, int byteIndex0, QVector<int> bitPositions) {
    if (byteIndex0 < 0 || byteIndex0 >= 8 || byteIndex0 >= int(frame.dlc)) return 0;
    std::sort(bitPositions.begin(), bitPositions.end());
    quint64 value = 0;
    for (int i = 0; i < bitPositions.size() && i < 64; ++i) {
        const int bitIndex = bitPositions.at(i);
        if (bitIndex < 0 || bitIndex > 7) continue;
        if (frame.data[byteIndex0] & (1u << bitIndex)) value |= (1ULL << i);
    }
    return value;
}

qint64 graphSignExtend(quint64 raw, int bits) {
    if (bits <= 0 || bits >= 63) return qint64(raw);
    const quint64 signBit = (1ULL << (bits - 1));
    if ((raw & signBit) == 0) return qint64(raw);
    const quint64 mask = ~((1ULL << bits) - 1ULL);
    return qint64(raw | mask);
}

double graphInferScaleText(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return 1.0;
    const QRegularExpression re(QStringLiteral(R"((?:unit|resolution|res)\s*[:=]?\s*([+-]?\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(t);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    const QRegularExpression re2(QStringLiteral(R"((?:/|unit:?\s*)([+-]?0\.\d+))"), QRegularExpression::CaseInsensitiveOption);
    m = re2.match(t);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    return 1.0;
}

double graphEffectiveScale(const CanModel::SignalSpec& sig) {
    if (std::abs(sig.scale - 1.0) > 1e-12) return sig.scale;
    const double fromOp = graphInferScaleText(sig.operatingText);
    if (std::abs(fromOp - 1.0) > 1e-12) return fromOp;
    const double fromRange = graphInferScaleText(sig.rangeText);
    if (std::abs(fromRange - 1.0) > 1e-12) return fromRange;
    return 1.0;
}

QString graphDescriptorGroup(const QString& cleanName, const QString& unit) {
    const QString joined = (cleanName + QLatin1Char(' ') + unit).toLower();
    if (joined.contains(QStringLiteral("rpm"))) return QStringLiteral("RPM");
    if (joined.contains(QStringLiteral("angle")) || joined.contains(QStringLiteral("deg")) || joined.contains(QStringLiteral("steer"))) return QStringLiteral("ANGLE");
    if (joined.contains(QStringLiteral("encoder")) || joined.contains(QStringLiteral("count"))) return QStringLiteral("ENC");
    if (joined.contains(QStringLiteral("current")) || unit == QStringLiteral("A")) return QStringLiteral("CURR");
    if (joined.contains(QStringLiteral("voltage")) || unit == QStringLiteral("V")) return QStringLiteral("VOLT");
    if (joined.contains(QStringLiteral("temp")) || unit == QStringLiteral("°C")) return QStringLiteral("TEMP");
    return QStringLiteral("NUM");
}

bool graphIsEncoderLike(const AppController::GraphSignalDescriptor& desc) {
    const QString joined = (desc.name + QLatin1Char(' ') + desc.label + QLatin1Char(' ') + desc.unit + QLatin1Char(' ') + desc.group).toLower();
    return desc.group.startsWith(QStringLiteral("ENC"))
        || desc.unit == QStringLiteral("count")
        || joined.contains(QStringLiteral("encoder"))
        || joined.contains(QStringLiteral("enc "));
}

double graphWrapAwareDelta(double prevValue, double curValue, const AppController::GraphSignalDescriptor& desc) {
    double delta = curValue - prevValue;
    if (!graphIsEncoderLike(desc)) return delta;
    if (delta > 32768.0) delta -= 65536.0;
    else if (delta < -32768.0) delta += 65536.0;
    return delta;
}

bool graphModeIsDelta(const QString& mode) {
    return mode == QStringLiteral("delta") || mode == QStringLiteral("delta_abs");
}

bool graphModeIsRate(const QString& mode) {
    return mode == QStringLiteral("rate") || mode == QStringLiteral("rate_abs");
}

bool graphModeUsesAbsoluteMagnitude(const QString& mode) {
    return mode == QStringLiteral("delta_abs") || mode == QStringLiteral("rate_abs");
}

QString graphDerivedSignalKey(const QString& baseKey, const QString& mode) {
    return baseKey + QStringLiteral("|") + mode;
}

QString graphColorForIndex(int index) {
    static const QStringList palette = {
        QStringLiteral("#2563eb"), QStringLiteral("#d97706"), QStringLiteral("#16a34a"), QStringLiteral("#dc2626"),
        QStringLiteral("#7c3aed"), QStringLiteral("#0891b2"), QStringLiteral("#db2777"), QStringLiteral("#475569")
    };
    return palette.at(index % palette.size());
}

int graphRenderPointLimit(int windowMs) {
    if (windowMs <= 5000) return 180;
    if (windowMs <= 15000) return 240;
    if (windowMs <= 30000) return 320;
    return 400;
}

int graphExactRawPointLimit(int windowMs) {
    if (windowMs <= 5000) return 900;
    if (windowMs <= 15000) return 1200;
    if (windowMs <= 30000) return 1400;
    return 0;
}

int graphRefreshIntervalMs(int seriesCount, int windowMs) {
    int base = 150;
    if (seriesCount >= 2) base = 190;
    if (seriesCount >= 3) base = 250;
    if (seriesCount >= 4) base = 320;
    if (windowMs >= 15000) base += 30;
    if (windowMs >= 30000) base += 70;
    if (windowMs >= 60000) base += 110;
    return std::clamp(base, 140, 540);
}

int graphHistoryRetentionMs(int windowMs) {
    const int clampedWindow = std::clamp(windowMs, 1000, 60000);
    return std::clamp(clampedWindow * 2, 10000, 90000);
}

quint64 graphQuantizeBucketUs(quint64 requestedUs) {
    quint64 level = 1000ULL;
    while (level < requestedUs && level < (1000ULL << 14)) level <<= 1;
    return std::max<quint64>(1000ULL, level);
}

struct GraphStaticRange {
    bool valid = false;
    double yMin = 0.0;
    double yMax = 0.0;
};

template <typename It>
QVector<GraphBucketPoint> graphBuildStableBuckets(It beginIt, It endIt, quint64 startUs, quint64 bucketUs, int reserveCount) {
    QVector<GraphBucketPoint> out;
    out.reserve(std::max(1, reserveCount));
    if (beginIt == endIt || bucketUs == 0) return out;

    quint64 currentBucketIndex = std::numeric_limits<quint64>::max();
    GraphBucketPoint current;
    bool haveCurrent = false;

    for (auto it = beginIt; it != endIt; ++it) {
        const quint64 bucketIndex = it->frameUs / bucketUs;
        const double tMs = double(it->frameUs - startUs) / 1000.0;
        if (!haveCurrent || bucketIndex != currentBucketIndex) {
            if (haveCurrent) out.push_back(current);
            currentBucketIndex = bucketIndex;
            current = GraphBucketPoint{tMs, it->value, it->value, it->value};
            haveCurrent = true;
            continue;
        }
        current.tMs = tMs;
        current.minV = std::min(current.minV, it->value);
        current.maxV = std::max(current.maxV, it->value);
        current.closeV = it->value;
    }

    if (haveCurrent) out.push_back(current);
    return out;
}

QVector<GraphBucketCachePoint> graphBuildBucketCachePoints(const QVector<AppController::GraphPoint>& points, quint64 bucketUs) {
    QVector<GraphBucketCachePoint> out;
    out.reserve(points.size() / 4 + 4);
    if (points.isEmpty() || bucketUs == 0) return out;

    quint64 currentBucketIndex = std::numeric_limits<quint64>::max();
    GraphBucketCachePoint current;
    bool haveCurrent = false;
    for (const auto& pt : points) {
        const quint64 bucketIndex = pt.frameUs / bucketUs;
        if (!haveCurrent || bucketIndex != currentBucketIndex) {
            if (haveCurrent) out.push_back(current);
            currentBucketIndex = bucketIndex;
            current = GraphBucketCachePoint{bucketIndex, pt.frameUs, pt.value, pt.value, pt.value};
            haveCurrent = true;
            continue;
        }
        current.closeUs = pt.frameUs;
        current.minV = std::min(current.minV, pt.value);
        current.maxV = std::max(current.maxV, pt.value);
        current.closeV = pt.value;
    }
    if (haveCurrent) out.push_back(current);
    return out;
}

GraphStaticRange graphStaticRangeForSelection(const QVector<AppController::GraphSignalDescriptor>& descs) {
    if (descs.isEmpty()) return {};

    auto allMatch = [&](auto pred) {
        for (const auto& desc : descs) {
            if (!pred(desc)) return false;
        }
        return true;
    };

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENC") || d.unit == QStringLiteral("count") || d.name.toLower().contains(QStringLiteral("encoder"));
        })) {
        return {true, 0.0, 65535.0};
    }

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENCD");
        })) {
        return {};
    }

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENCR") || d.unit == QStringLiteral("count/s");
        })) {
        return {};
    }

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ANGLE") || d.unit == QStringLiteral("deg") || d.name.toLower().contains(QStringLiteral("angle"));
        })) {
        return {true, -110.0, 110.0};
    }

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("RPM") || d.unit == QStringLiteral("rpm") || d.name.toLower().contains(QStringLiteral("rpm"));
        })) {
        return {true, -7000.0, 7000.0};
    }

    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("VOLT") || d.unit == QStringLiteral("V") || d.name.toLower().contains(QStringLiteral("voltage"));
        })) {
        return {true, 0.0, 70.0};
    }

    return {};
}



double graphDetailZoomMinimumSpan(const QVector<AppController::GraphSignalDescriptor>& descs, double center) {
    if (descs.isEmpty()) return 1.0;
    auto allMatch = [&](auto pred) {
        for (const auto& desc : descs) {
            if (!pred(desc)) return false;
        }
        return true;
    };
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENC") || d.unit == QStringLiteral("count") || d.name.toLower().contains(QStringLiteral("encoder"));
        })) return 4.0;
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENCD");
        })) return 2.0;
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ENCR") || d.unit == QStringLiteral("count/s");
        })) return 20.0;
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("RPM") || d.unit == QStringLiteral("rpm") || d.name.toLower().contains(QStringLiteral("rpm"));
        })) return 10.0;
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("ANGLE") || d.unit == QStringLiteral("deg") || d.name.toLower().contains(QStringLiteral("angle"));
        })) return 0.4;
    if (allMatch([](const AppController::GraphSignalDescriptor& d) {
            return d.group == QStringLiteral("VOLT") || d.unit == QStringLiteral("V") || d.name.toLower().contains(QStringLiteral("voltage"));
        })) return 0.2;
    const double mag = std::max(1.0, std::abs(center));
    return std::max(0.01, mag * 0.01);
}

GraphStaticRange graphDetailZoomRange(double globalMin, double globalMax, const QVector<AppController::GraphSignalDescriptor>& descs) {
    if (!(std::isfinite(globalMin) && std::isfinite(globalMax))) return {};
    double lo = std::min(globalMin, globalMax);
    double hi = std::max(globalMin, globalMax);
    const double center = (lo + hi) * 0.5;
    double span = hi - lo;
    span = std::max(span, graphDetailZoomMinimumSpan(descs, center));
    const double pad = std::max(span * 0.12, span < 1.0 ? span * 0.25 : 0.0);
    lo = center - (span * 0.5) - pad;
    hi = center + (span * 0.5) + pad;
    if (lo == hi) {
        lo -= 1.0;
        hi += 1.0;
    }
    return {true, lo, hi};
}
bool graphDecodeDescriptorValue(const AppController::GraphSignalDescriptor& desc, const FrameRecord& frame, const QHash<quint32, CanModel::SignalMessageSpec>& messages, double* outValue) {
    if (!outValue) return false;
    const auto it = messages.constFind(desc.id);
    if (it == messages.cend()) return false;
    const auto& specs = it.value().signalSpecs;
    if (desc.signalIndex < 0 || desc.signalIndex >= specs.size()) return false;
    const auto& sig = specs.at(desc.signalIndex);
    if (sig.byteIndex1Based < 1 || sig.byteIndex1Based > 8) return false;

    quint64 raw = 0;
    const QString cleanName = graphCleanName(sig.name);
    if (graphPairLowName(cleanName) && desc.signalIndex + 1 < specs.size()) {
        const auto& next = specs.at(desc.signalIndex + 1);
        if (graphPairHighName(graphCleanName(next.name)) && graphPairBaseName(next.name).compare(graphPairBaseName(sig.name), Qt::CaseInsensitive) == 0) {
            const int lowByte = sig.byteIndex1Based - 1;
            const int highByte = next.byteIndex1Based - 1;
            if (lowByte < int(frame.dlc) && highByte < int(frame.dlc) && lowByte >= 0 && highByte >= 0) {
                raw = quint64(frame.data[lowByte]) | (quint64(frame.data[highByte]) << 8);
                const qint64 signedRaw = sig.signedValue ? graphSignExtend(raw, 16) : qint64(raw);
                *outValue = double(signedRaw) * graphEffectiveScale(sig) + sig.offset;
                return true;
            }
            return false;
        }
    }

    const int byteIndex0 = sig.byteIndex1Based - 1;
    if (!sig.bitPositionsLsb.isEmpty() && sig.lengthBits <= sig.bitPositionsLsb.size()) raw = graphExtractExplicitBits(frame, byteIndex0, sig.bitPositionsLsb);
    else raw = graphExtractContiguousBits(frame, byteIndex0 * 8 + sig.startBitLsb, sig.lengthBits);
    const qint64 signedRaw = sig.signedValue ? graphSignExtend(raw, sig.lengthBits) : qint64(raw);
    *outValue = double(signedRaw) * graphEffectiveScale(sig) + sig.offset;
    return true;
}

int compareQString(const QString& a, const QString& b) {
    const int c = QString::localeAwareCompare(a, b);
    if (c < 0) return -1;
    if (c > 0) return 1;
    return 0;
}

int compareInt64(qint64 a, qint64 b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

int compareUInt32(quint32 a, quint32 b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

int compareOptionalDouble(double a, double b) {
    const bool aValid = a >= 0.0;
    const bool bValid = b >= 0.0;
    if (aValid != bValid) return aValid ? -1 : 1;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

bool lessFromCompare(int cmp, bool descending) {
    if (cmp == 0) return false;
    return descending ? (cmp > 0) : (cmp < 0);
}

QString normalizedFilterText(const QString& text) {
    return text.trimmed();
}

bool containsFilterText(const QString& haystack, const QString& needle) {
    const QString n = needle.trimmed();
    if (n.isEmpty()) return true;
    return haystack.contains(n, Qt::CaseInsensitive);
}


bool isPreviewNoiseName(const QString& rawName) {
    const QString n = rawName.trimmed().toLower();
    if (n.isEmpty()) return true;
    return n.contains(QStringLiteral("reserved")) ||
           n.contains(QStringLiteral("not defined")) ||
           n.contains(QStringLiteral("undefined")) ||
           n.contains(QStringLiteral("unused")) ||
           n.contains(QStringLiteral("spare")) ||
           n.contains(QStringLiteral("dummy")) ||
           n.contains(QStringLiteral("미정")) ||
           n.contains(QStringLiteral("미사용")) ||
           n.contains(QStringLiteral("예비"));
}

QString cleanSignalName(const QString& rawName) {
    QString name = rawName.trimmed();
    name.replace(QRegularExpression(QStringLiteral(R"([_\s]+)")), QStringLiteral(" "));
    name.replace(QRegularExpression(QStringLiteral(R"(\s*\([^)]*byte[^)]*\))"), QRegularExpression::CaseInsensitiveOption), QString());
    name.replace(QRegularExpression(QStringLiteral(R"(\s*\([^)]*bit[^)]*\))"), QRegularExpression::CaseInsensitiveOption), QString());
    return name.trimmed();
}

QString fmtCompactNumber(double value) {
    const double absValue = std::abs(value);
    int decimals = 0;
    if (absValue < 1.0) decimals = 3;
    else if (absValue < 10.0) decimals = 2;
    else if (absValue < 100.0) decimals = 1;
    QString s = QString::number(value, 'f', decimals);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
        if (s.endsWith('.')) { s.chop(1); break; }
        s.chop(1);
    }
    return s;
}

QString shortenPreview(const QString& text, int maxLen = 18) {
    const QString t = text.trimmed();
    if (t.size() <= maxLen) return t;
    return t.left(maxLen - 1) + QStringLiteral("…");
}

bool parseNumericRangeText(const QString& text, double* outMin, double* outMax) {
    if (!outMin || !outMax) return false;
    const QRegularExpression re(QStringLiteral(R"(([+-]?\d+(?:\.\d+)?)\s*(?:to|~|-)\s*([+-]?\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return false;
    bool ok1 = false, ok2 = false;
    const double a = m.captured(1).toDouble(&ok1);
    const double b = m.captured(2).toDouble(&ok2);
    if (!ok1 || !ok2) return false;
    *outMin = a;
    *outMax = b;
    return true;
}

qint64 signExtendRaw(quint64 raw, int bits) {
    if (bits <= 0 || bits >= 63) return qint64(raw);
    const quint64 signBit = (1ULL << (bits - 1));
    if ((raw & signBit) == 0) return qint64(raw);
    const quint64 mask = ~((1ULL << bits) - 1ULL);
    return qint64(raw | mask);
}

quint64 extractContiguousBits(const FrameRecord& frame, int startBit, int lengthBits) {
    quint64 value = 0;
    for (int i = 0; i < lengthBits && i < 64; ++i) {
        const int globalBit = startBit + i;
        const int byteIndex = globalBit / 8;
        const int bitIndex = globalBit % 8;
        if (byteIndex < 0 || byteIndex >= 8) break;
        if (byteIndex >= int(frame.dlc)) continue;
        if (frame.data[byteIndex] & (1u << bitIndex)) {
            value |= (1ULL << i);
        }
    }
    return value;
}

quint64 extractExplicitBits(const FrameRecord& frame, int byteIndex0, QVector<int> bitPositions) {
    if (byteIndex0 < 0 || byteIndex0 >= 8 || byteIndex0 >= int(frame.dlc)) return 0;
    std::sort(bitPositions.begin(), bitPositions.end());
    quint64 value = 0;
    for (int i = 0; i < bitPositions.size() && i < 64; ++i) {
        const int bitIndex = bitPositions.at(i);
        if (bitIndex < 0 || bitIndex > 7) continue;
        if (frame.data[byteIndex0] & (1u << bitIndex)) {
            value |= (1ULL << i);
        }
    }
    return value;
}

QString enumLabelForRaw(const QString& operatingText, qint64 rawValue) {
    if (operatingText.trimmed().isEmpty()) return {};
    const QRegularExpression re(QStringLiteral(R"((\d+)\s*[:.\-]\s*([^,/]+))"));
    auto it = re.globalMatch(operatingText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok = false;
        const qint64 key = m.captured(1).toLongLong(&ok);
        if (ok && key == rawValue) {
            return m.captured(2).trimmed();
        }
    }
    return {};
}

double inferScaleFromText(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return 1.0;
    const QRegularExpression re(QStringLiteral(R"((?:unit|resolution|res)\s*[:=]?\s*([+-]?\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(t);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    const QRegularExpression re2(QStringLiteral(R"(([+-]?0\.\d+))"));
    const auto m2 = re2.match(t);
    if (m2.hasMatch()) {
        bool ok = false;
        const double v = m2.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    return 1.0;
}

QString compactStateLabel(QString text) {
    QString t = cleanSignalName(text);
    t.replace(QRegularExpression(QStringLiteral(R"(^\s*(FAULT|WARNING|WARN|ERROR|ALARM)\s*[-:]?\s*)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QRegularExpression(QStringLiteral(R"(\(CHARGE\))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" CHG"));
    t.replace(QRegularExpression(QStringLiteral(R"(\(DISCHARGE\))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" DSG"));
    const QList<QPair<QString, QString>> reps = {
        {QStringLiteral("OVER VOLTAGE"), QStringLiteral("OVP")},
        {QStringLiteral("UNDER VOLTAGE"), QStringLiteral("UVP")},
        {QStringLiteral("OVER CURRENT CHG"), QStringLiteral("OCC")},
        {QStringLiteral("OVER CURRENT DSG"), QStringLiteral("OCD")},
        {QStringLiteral("OVER CURRENT"), QStringLiteral("OCP")},
        {QStringLiteral("OVER TEMP CHG"), QStringLiteral("OTC")},
        {QStringLiteral("OVER TEMP DSG"), QStringLiteral("OTD")},
        {QStringLiteral("OVER TEMP"), QStringLiteral("OVT")},
        {QStringLiteral("UNDER TEMP"), QStringLiteral("UVT")},
        {QStringLiteral("SHORT PROTECTION"), QStringLiteral("SCP")},
        {QStringLiteral("SHORT"), QStringLiteral("SCP")},
        {QStringLiteral("PROTECTION"), QString()},
        {QStringLiteral("NORMAL"), QStringLiteral("정상")},
        {QStringLiteral("NOT DEFINED"), QString()},
        {QStringLiteral("DISCHARGE FET ON"), QStringLiteral("DSG FET")},
        {QStringLiteral("CHARGE FET ON"), QStringLiteral("CHG FET")}
    };
    QString upper = t.toUpper();
    for (const auto& p : reps) upper.replace(p.first, p.second);
    upper.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return upper.trimmed();
}

QString compactSignalName(QString text) {
    QString t = cleanSignalName(text);
    t.replace(QRegularExpression(QStringLiteral(R"(\bLow Byte\b|\bHigh Byte\b|\bUpper Byte\b|\bLower Byte\b)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QStringLiteral("Average"), QStringLiteral("Avg"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Minimum"), QStringLiteral("Min"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Maximum"), QStringLiteral("Max"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Difference"), QStringLiteral("Δ"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Voltage"), QStringLiteral("V"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Current"), QStringLiteral("I"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Temperature"), QStringLiteral("Temp"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Capacity"), QStringLiteral("Cap"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Counter"), QStringLiteral("Cnt"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Encoder"), QStringLiteral("Enc"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Battery"), QStringLiteral("Batt"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Motor"), QStringLiteral("MTR"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Hardware"), QStringLiteral("HW"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Software"), QStringLiteral("SW"), Qt::CaseInsensitive);
    t.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return shortenPreview(t.trimmed(), 14);
}

enum class PairPart { None, Low, High };

PairPart detectPairPart(const QString& name) {
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("low byte")) || n.contains(QStringLiteral("lower byte")) || n.contains(QStringLiteral("하위바이트"))) return PairPart::Low;
    if (n.contains(QStringLiteral("high byte")) || n.contains(QStringLiteral("upper byte")) || n.contains(QStringLiteral("상위바이트"))) return PairPart::High;
    return PairPart::None;
}

QString pairBaseName(QString name) {
    QString t = cleanSignalName(name);
    t.replace(QRegularExpression(QStringLiteral(R"(\b(Low|High|Upper|Lower)\s*Byte\b)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QStringLiteral("상위바이트"), QString());
    t.replace(QStringLiteral("하위바이트"), QString());
    t.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return t.trimmed();
}

bool looksInactiveLabel(const QString& text) {
    const QString t = text.trimmed().toLower();
    return t.isEmpty() || t == QStringLiteral("0") || t == QStringLiteral("off") || t == QStringLiteral("inactive") ||
           t == QStringLiteral("normal") || t == QStringLiteral("정상") || t == QStringLiteral("none") ||
           t == QStringLiteral("disable") || t == QStringLiteral("disabled") || t == QStringLiteral("ready");
}

QString previewTokenColor(const QString& name, const QString& valueText, bool rangeBad) {
    const QString joined = (name + QLatin1Char(' ') + valueText).toLower();
    if (rangeBad || joined.contains(QStringLiteral("fault")) || joined.contains(QStringLiteral("error")) || joined.contains(QStringLiteral("ovp")) || joined.contains(QStringLiteral("uvp")) || joined.contains(QStringLiteral("ocp")) || joined.contains(QStringLiteral("occ")) || joined.contains(QStringLiteral("ocd")) || joined.contains(QStringLiteral("ovt")) || joined.contains(QStringLiteral("otc")) || joined.contains(QStringLiteral("otd")) || joined.contains(QStringLiteral("uvt")) || joined.contains(QStringLiteral("scp"))) return QStringLiteral("#c0392b");
    if (joined.contains(QStringLiteral("warn")) || joined.contains(QStringLiteral("warning"))) return QStringLiteral("#d97706");
    if (joined.contains(QStringLiteral("정상")) || joined.contains(QStringLiteral("ok")) || joined.contains(QStringLiteral("normal"))) return QStringLiteral("#118a42");
    return QStringLiteral("#0f4c81");
}

}


QString AppController::fmtReplayUs(quint64 us) {
    const quint64 totalMs = us / 1000ULL;
    const quint64 minutes = totalMs / 60000ULL;
    const quint64 seconds = (totalMs / 1000ULL) % 60ULL;
    const quint64 millis = totalMs % 1000ULL;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

int AppController::replayIndexForProgress(double progress) const {
    const int frameCount = m_replay.frameCount();
    if (frameCount <= 0) return -1;
    const double clamped = std::clamp(progress, 0.0, 1.0);
    if (frameCount == 1) return 0;
    return std::clamp(int(clamped * double(frameCount - 1)), 0, frameCount - 1);
}

void AppController::clearReplaySeekState() {
    m_replaySeekTimer.stop();
    m_pendingReplaySeekIndex = -1;
    cancelReplayRebuild(true);
}

int AppController::replayCheckpointStride() const {
    const int frameCount = m_replay.frameCount();
    if (frameCount <= 0) return 160;
    if (frameCount <= 2000) return 96;
    return std::clamp(frameCount / 128, 128, 384);
}

void AppController::cancelReplayRebuild(bool restoreSnapshot) {
    const bool hadActive = m_replayRebuildActive;
    m_replayRebuildTimer.stop();
    m_replayRebuildActive = false;
    m_replayRebuildTargetIndex = -1;
    m_replayRebuildNextIndex = 0;
    m_replayRebuildViewStart = 0;
    m_replayRebuildReason.clear();
    m_replayProcessingIndex = -1;
    m_replayProcessingUs = 0;
    if (restoreSnapshot && hadActive && m_replaySnapshotValid) {
        restoreReplaySnapshotState();
        markAllAnalysisDirty(true);
        refreshTimingRows();
        refreshValueRows();
        refreshAlarmRows();
    }
}

void AppController::maybeStoreReplayCheckpoint(int index) {
    if (index < 0) return;
    const int stride = replayCheckpointStride();
    if (index != 0 && (index % stride) != 0) return;
    if (!m_replayCheckpoints.isEmpty() && m_replayCheckpoints.back().index == index) return;

    ReplayCheckpoint checkpoint;
    checkpoint.index = index;
    checkpoint.displayedUs = m_replayDisplayedUs;
    checkpoint.alarmSequence = m_replayAlarmSequence;
    checkpoint.timingMarkerCount = m_replayTimingIssueMarkers.size();
    checkpoint.valueMarkerCount = m_replayValueIssueMarkers.size();
    checkpoint.alarmMarkerCount = m_replayAlarmIssueMarkers.size();
    checkpoint.states = makeCheckpointStateMap(m_replayStates);
    checkpoint.alarmGroups = m_replayAlarmGroups;
    m_replayCheckpoints.push_back(std::move(checkpoint));

    while (m_replayCheckpoints.size() > 256) {
        m_replayCheckpoints.removeFirst();
    }
}

void AppController::clearReplayIssueMarkers() {
    const bool hadAny = !m_replayTimingIssueMarkers.isEmpty() || !m_replayValueIssueMarkers.isEmpty() || !m_replayAlarmIssueMarkers.isEmpty();
    m_replayTimingIssueMarkers.clear();
    m_replayValueIssueMarkers.clear();
    m_replayAlarmIssueMarkers.clear();
    if (hadAny) emit replayIssueMarkersChanged();
}

void AppController::clearReplayTypedDiagnostics() {
    m_replayTypedCaptureState.clear();
    m_replayTypedDiagnosticsSummary.clear();
    m_replayTypedDiagnostics.clear();
}

void AppController::setReplayTypedDiagnosticsFromReader(const TypedReplayReader& reader, int canRxFrameCount) {
    const auto& summary = reader.summary();
    m_replayTypedCaptureState = summary.captureState;
    m_replayTypedDiagnosticsSummary = summary.diagnosticSummary;
    m_replayTypedDiagnostics.clear();

    auto appendRow = [this](const QString& key,
                            const QString& value,
                            const QString& level = QStringLiteral("OK"),
                            const QString& label = QString(),
                            const QString& note = QString()) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("value"), value);
        row.insert(QStringLiteral("level"), level);
        row.insert(QStringLiteral("label"), label.isEmpty() ? key : label);
        row.insert(QStringLiteral("note"), note);
        m_replayTypedDiagnostics.push_back(row);
    };

    QMap<quint8, quint64> canRxByBus;
    QMap<quint8, quint64> canTxByBus;
    QMap<quint8, QMap<quint8, quint64>> canRxDlcByBus;
    QMap<quint16, quint64> boardEventCounts;
    QStringList capabilityBuses;
    std::optional<TypedBoardHealthRecord> firstHealth;
    std::optional<TypedBoardHealthRecord> lastHealth;
    for (const TypedReplayReader::RecordEntry& entry : reader.records()) {
        if (const auto can = decodeTypedCanRaw(entry.record)) {
            if (can->txAudit) {
                canTxByBus[can->bus] += 1;
            } else {
                canRxByBus[can->bus] += 1;
                canRxDlcByBus[can->bus][can->dlc] += 1;
            }
            continue;
        }
        if (const auto event = decodeTypedBoardEvent(entry.record)) {
            boardEventCounts[event->code] += 1;
            continue;
        }
        if (const auto health = decodeTypedBoardHealth(entry.record)) {
            if (!firstHealth) firstHealth = *health;
            lastHealth = *health;
            continue;
        }
        if (const auto capability = decodeTypedCapability(entry.record)) {
            QStringList buses;
            for (const TypedCapabilityBusDescriptor& bus : capability->buses) {
                buses << QStringLiteral("bus%1 %2 %3 rx%4 tx%5 ctrl%6 dlc%7 term%8 iso%9")
                              .arg(bus.busId)
                              .arg(capabilityBackendText(bus.backend))
                              .arg(capabilityRoleHintText(bus.roleHint))
                              .arg(bus.rxSupported ? 1 : 0)
                              .arg(bus.txSupported ? 1 : 0)
                              .arg(bus.controlTxAllowed ? 1 : 0)
                              .arg(bus.maxLiveDlc)
                              .arg(bus.terminationPolicy)
                              .arg(bus.isolationPolicy);
            }
            if (!buses.isEmpty()) capabilityBuses = buses;
        }
    }

    QStringList counts;
    for (auto it = summary.typeCounts.cbegin(); it != summary.typeCounts.cend(); ++it) {
        counts << QStringLiteral("%1=%2").arg(typedRecordTypeName(it.key())).arg(it.value());
    }

    quint64 canRxRecordCount = 0;
    int dlcBucketCount = 0;
    bool onlyDlc8 = !canRxDlcByBus.isEmpty();
    for (auto busIt = canRxDlcByBus.cbegin(); busIt != canRxDlcByBus.cend(); ++busIt) {
        for (auto dlcIt = busIt.value().cbegin(); dlcIt != busIt.value().cend(); ++dlcIt) {
            canRxRecordCount += dlcIt.value();
            ++dlcBucketCount;
            if (dlcIt.key() != 8) onlyDlc8 = false;
        }
    }
    const bool projectionMatches = canRxRecordCount == quint64(std::max(0, canRxFrameCount));
    const bool parserFaults = summary.crcFailures > 0 || summary.lengthFailures > 0 || summary.trailingBytes > 0 || summary.bytesDropped > 0;
    const bool sidecarPartial = summary.metaPart || summary.indexPart || summary.eventsPart || summary.streamPart;
    const bool sidecarMissing = !summary.metaPresent || !summary.indexPresent || !summary.eventsPresent;
    QString verdictLevel = QStringLiteral("OK");
    QStringList verdictParts;
    if (!summary.hasRecords || canRxFrameCount <= 0 || !projectionMatches || parserFaults) {
        verdictLevel = QStringLiteral("ERR");
        verdictParts << QStringLiteral("재생 근거 확인 필요");
    } else if (summary.captureState != QStringLiteral("FINALIZED") || sidecarPartial || sidecarMissing || summary.seqGaps > 0 || onlyDlc8) {
        verdictLevel = QStringLiteral("WARN");
        verdictParts << QStringLiteral("사용 가능하나 현장 확인 필요");
    } else {
        verdictParts << QStringLiteral("재생 근거 정상");
    }
    if (summary.captureState == QStringLiteral("PARTIAL")) verdictParts << QStringLiteral("부분 저장");
    else verdictParts << summary.captureState;
    verdictParts << QStringLiteral("CAN_RX %1/%2 projection").arg(canRxFrameCount).arg(canRxRecordCount);
    verdictParts << (projectionMatches ? QStringLiteral("projection 일치") : QStringLiteral("projection 불일치"));
    if (dlcBucketCount > 1) verdictParts << QStringLiteral("DLC 다양성 확인");
    else if (onlyDlc8) verdictParts << QStringLiteral("DLC 8만 관찰");
    if (summary.seqGaps > 0) verdictParts << QStringLiteral("seq gap %1").arg(summary.seqGaps);
    if (parserFaults) verdictParts << QStringLiteral("parser fault 존재");
    appendRow(QStringLiteral("operator_verdict"), verdictParts.join(QStringLiteral(" | ")), verdictLevel,
              QStringLiteral("판정"),
              QStringLiteral("CAN_RX projection, DLC 분포, parser fault, sidecar 상태를 합친 현장용 요약"));

    appendRow(QStringLiteral("capture"), summary.diagnosticSummary,
              summary.captureState == QStringLiteral("FINALIZED") ? QStringLiteral("OK") : QStringLiteral("WARN"),
              QStringLiteral("캡처"));
    appendRow(QStringLiteral("timeline"),
              QStringLiteral("mono %1..%2 us | duration %3 ms | records %4 | seq %5..%6")
                  .arg(summary.firstMonoUs)
                  .arg(summary.lastMonoUs)
                  .arg(summary.durationUs / 1000.0, 0, 'f', 1)
                  .arg(summary.recordCount)
                  .arg(summary.firstSeq)
                  .arg(summary.lastSeq),
              summary.hasRecords ? QStringLiteral("OK") : QStringLiteral("ERR"),
              QStringLiteral("타임라인"));
    appendRow(QStringLiteral("meta"),
              summary.metaPresent
                  ? QStringLiteral("%1%2 | stream %3 | created %4")
                        .arg(summary.metaPart ? QStringLiteral("part ") : QString())
                        .arg(summary.metaFormat.isEmpty() ? QStringLiteral("format unknown") : summary.metaFormat)
                        .arg(summary.metaStreamFile.isEmpty() ? QStringLiteral("unknown") : summary.metaStreamFile)
                        .arg(summary.metaCreatedLocal.isEmpty() ? QStringLiteral("unknown") : summary.metaCreatedLocal)
                  : QStringLiteral("missing"),
              summary.metaPresent && !summary.metaPart ? QStringLiteral("OK") : QStringLiteral("WARN"),
              QStringLiteral("메타"));
    appendRow(QStringLiteral("index"),
              summary.indexPresent
                  ? QStringLiteral("%1entries %2 | bytes %3 | offsets %4..%5 | mono %6..%7 | mismatch %8 | remainder %9")
                        .arg(summary.indexPart ? QStringLiteral("part ") : QString())
                        .arg(summary.indexEntryCount)
                        .arg(summary.indexByteCount)
                        .arg(summary.indexFirstOffset)
                        .arg(summary.indexLastOffset)
                        .arg(summary.indexFirstMonoUs)
                        .arg(summary.indexLastMonoUs)
                        .arg(summary.indexMismatchCount)
                        .arg(summary.indexRemainderBytes)
                  : QStringLiteral("missing"),
              summary.indexPresent && !summary.indexPart && summary.indexSizeAligned && summary.indexMismatchCount == 0
                  ? QStringLiteral("OK")
                  : QStringLiteral("WARN"),
              QStringLiteral("인덱스"));
    appendRow(QStringLiteral("events"),
              summary.eventsPresent
                  ? QStringLiteral("%1lines %2 | first %3 | last %4")
                        .arg(summary.eventsPart ? QStringLiteral("part ") : QString())
                        .arg(summary.eventLineCount)
                        .arg(summary.firstEventText.isEmpty() ? QStringLiteral("-") : summary.firstEventText)
                        .arg(summary.lastEventText.isEmpty() ? QStringLiteral("-") : summary.lastEventText)
                  : QStringLiteral("missing"),
              summary.eventsPresent && !summary.eventsPart ? QStringLiteral("OK") : QStringLiteral("WARN"),
              QStringLiteral("이벤트"));
    appendRow(QStringLiteral("can_rx_projection"), QStringLiteral("%1 replay frame(s)").arg(canRxFrameCount),
              canRxFrameCount > 0 && projectionMatches ? QStringLiteral("OK") : QStringLiteral("ERR"),
              QStringLiteral("CAN_RX 투영"),
              projectionMatches ? QStringLiteral("typed CAN_RX record 수와 replay frame 수가 일치") : QStringLiteral("typed CAN_RX record 수와 replay frame 수가 다름"));
    appendRow(QStringLiteral("types"), counts.isEmpty() ? QStringLiteral("none") : counts.join(QStringLiteral(", ")),
              QStringLiteral("OK"), QStringLiteral("레코드 타입"));
    appendRow(QStringLiteral("can_bus"),
              QStringLiteral("RX %1 | TX %2")
                  .arg(mapCountsText(canRxByBus, QStringLiteral("bus")))
                  .arg(mapCountsText(canTxByBus, QStringLiteral("bus"))),
              canRxFrameCount > 0 ? QStringLiteral("OK") : QStringLiteral("ERR"),
              QStringLiteral("CAN 버스"));
    QStringList dlcParts;
    for (auto busIt = canRxDlcByBus.cbegin(); busIt != canRxDlcByBus.cend(); ++busIt) {
        dlcParts << QStringLiteral("bus%1 [%2]").arg(busIt.key()).arg(mapCountsText(busIt.value(), QStringLiteral("dlc")));
    }
    appendRow(QStringLiteral("can_dlc"), dlcParts.isEmpty() ? QStringLiteral("none") : dlcParts.join(QStringLiteral(" | ")),
              dlcParts.isEmpty() ? QStringLiteral("WARN") : QStringLiteral("OK"),
              QStringLiteral("CAN DLC"));
    appendRow(QStringLiteral("can_dlc_verdict"),
              dlcParts.isEmpty()
                  ? QStringLiteral("DLC evidence missing")
                  : (onlyDlc8
                         ? QStringLiteral("All observed CAN_RX frames are DLC 8; verify sender/test mode if variable DLC was expected.")
                         : QStringLiteral("Variable DLC preserved in typed replay: %1 bucket(s)").arg(dlcBucketCount)),
              dlcParts.isEmpty() ? QStringLiteral("WARN") : (onlyDlc8 ? QStringLiteral("WARN") : QStringLiteral("OK")),
              QStringLiteral("DLC 판정"),
              QStringLiteral("랜덤 DLC 송신/재생 길이 오류 의심을 빠르게 확인하기 위한 요약"));
    if (firstHealth && lastHealth) {
        const quint64 rxDelta = u32CounterDelta(lastHealth->canRxTotal, firstHealth->canRxTotal);
        const quint64 droppedDelta = u32CounterDelta(lastHealth->canDroppedTotal, firstHealth->canDroppedTotal);
        const quint64 overflowDelta = u32CounterDelta(lastHealth->canFifoOverflowTotal, firstHealth->canFifoOverflowTotal);
        appendRow(QStringLiteral("board_health"),
                  QStringLiteral("rx +%1, dropped +%2, fifo_overflow +%3, last safety %4 flags 0x%5 fault 0x%6 queue %7")
                      .arg(rxDelta)
                      .arg(droppedDelta)
                      .arg(overflowDelta)
                      .arg(lastHealth->safetyState)
                      .arg(int(lastHealth->flags), 0, 16)
                      .arg(qulonglong(lastHealth->faultFlags), 0, 16)
                      .arg(lastHealth->queueDepth),
                  (droppedDelta == 0 && lastHealth->faultFlags == 0) ? QStringLiteral("OK") : QStringLiteral("WARN"),
                  QStringLiteral("보드 헬스"));
    }
    if (!boardEventCounts.isEmpty()) {
        QStringList events;
        for (auto it = boardEventCounts.cbegin(); it != boardEventCounts.cend(); ++it) {
            events << QStringLiteral("%1=%2").arg(boardEventCodeText(it.key())).arg(it.value());
        }
        appendRow(QStringLiteral("board_events"), events.join(QStringLiteral(", ")),
                  boardEventCounts.contains(12) || boardEventCounts.contains(17) ? QStringLiteral("ERR") : QStringLiteral("WARN"),
                  QStringLiteral("보드 이벤트"));
    }
    if (!capabilityBuses.isEmpty()) {
        appendRow(QStringLiteral("capability_bus"), capabilityBuses.join(QStringLiteral(" | ")),
                  QStringLiteral("OK"), QStringLiteral("Capability 버스"));
    }
    appendRow(QStringLiteral("seq"), QStringLiteral("%1..%2, gaps %3")
                  .arg(summary.firstSeq)
                  .arg(summary.lastSeq)
                  .arg(summary.seqGaps),
              summary.seqGaps == 0 ? QStringLiteral("OK") : QStringLiteral("WARN"),
              QStringLiteral("시퀀스"));
    appendRow(QStringLiteral("sidecars"), QStringLiteral("meta %1, index %2, events %3")
                  .arg(summary.metaPresent ? QStringLiteral("ok") : QStringLiteral("missing"))
                  .arg(summary.indexPresent ? QStringLiteral("%1").arg(summary.indexEntryCount) : QStringLiteral("missing"))
                  .arg(summary.eventsPresent ? QStringLiteral("%1").arg(summary.eventLineCount) : QStringLiteral("missing")),
              summary.metaPresent && summary.indexPresent && summary.eventsPresent && !sidecarPartial ? QStringLiteral("OK") : QStringLiteral("WARN"),
              QStringLiteral("Sidecar"));
    appendRow(QStringLiteral("faults"), QStringLiteral("crc %1, len %2, tail %3, dropped %4")
                  .arg(summary.crcFailures)
                  .arg(summary.lengthFailures)
                  .arg(summary.trailingBytes)
                  .arg(summary.bytesDropped),
              (summary.crcFailures == 0 && summary.lengthFailures == 0 && summary.trailingBytes == 0)
                  ? QStringLiteral("OK")
                  : QStringLiteral("WARN"),
              QStringLiteral("Parser faults"));
    if (!reader.faults().isEmpty()) {
        const auto& fault = reader.faults().first();
        appendRow(QStringLiteral("first_fault"),
                  QStringLiteral("%1 at offset %2 | %3").arg(fault.code).arg(fault.offset).arg(fault.message),
                  QStringLiteral("WARN"),
                  QStringLiteral("첫 fault"));
    }
}

void AppController::appendReplayIssueMarker(const QString& kindValue, quint32 id, const QString& severity, const QString& note) {
    if (m_replayProcessingIndex < 0 || m_replayProcessingUs == 0) return;
    const QString kind = kindValue.trimmed().toLower();
    QVector<ReplayIssueMarker>* target = nullptr;
    if (kind == QStringLiteral("timing")) target = &m_replayTimingIssueMarkers;
    else if (kind == QStringLiteral("value")) target = &m_replayValueIssueMarkers;
    else target = &m_replayAlarmIssueMarkers;

    const QString trimmedNote = note.trimmed();
    if (!target->isEmpty()) {
        const ReplayIssueMarker& last = target->back();
        if (last.index == m_replayProcessingIndex && last.id == id && last.severity == severity && last.note == trimmedNote) {
            return;
        }
    }

    ReplayIssueMarker marker;
    marker.index = m_replayProcessingIndex;
    marker.frameUs = m_replayProcessingUs;
    marker.id = id;
    marker.kind = kind;
    marker.severity = severity;
    marker.note = trimmedNote;
    target->push_back(std::move(marker));
    emit replayIssueMarkersChanged();
}

const QVector<AppController::ReplayIssueMarker>& AppController::replayIssueMarkersForKind(const QString& kindValue) const {
    static const QVector<ReplayIssueMarker> empty;
    const QString kind = kindValue.trimmed().toLower();
    if (kind == QStringLiteral("timing")) return m_replayTimingIssueMarkers;
    if (kind == QStringLiteral("value")) return m_replayValueIssueMarkers;
    if (kind == QStringLiteral("alarm")) return m_replayAlarmIssueMarkers;
    return empty;
}

void AppController::captureReplaySnapshotState() {
    m_replaySnapshotValid = true;
    m_replaySnapshotStates = m_replayStates;
    m_replaySnapshotAlarmGroups = m_replayAlarmGroups;
    m_replaySnapshotAlarmSequence = m_replayAlarmSequence;
    m_replaySnapshotDisplayedUs = m_replayDisplayedUs;
    m_replaySnapshotAnalyzedIndex = m_replayAnalyzedIndex;
    m_replaySnapshotTimingIssueMarkers = m_replayTimingIssueMarkers;
    m_replaySnapshotValueIssueMarkers = m_replayValueIssueMarkers;
    m_replaySnapshotAlarmIssueMarkers = m_replayAlarmIssueMarkers;
}

void AppController::clearReplaySnapshotState() {
    m_replaySnapshotValid = false;
    m_replaySnapshotStates.clear();
    m_replaySnapshotAlarmGroups.clear();
    m_replaySnapshotAlarmSequence = 0;
    m_replaySnapshotDisplayedUs = 0;
    m_replaySnapshotAnalyzedIndex = -1;
    m_replaySnapshotTimingIssueMarkers.clear();
    m_replaySnapshotValueIssueMarkers.clear();
    m_replaySnapshotAlarmIssueMarkers.clear();
}

void AppController::restoreReplaySnapshotState() {
    if (!m_replaySnapshotValid) return;
    m_replayStates = m_replaySnapshotStates;
    m_replayAlarmGroups = m_replaySnapshotAlarmGroups;
    m_replayAlarmSequence = m_replaySnapshotAlarmSequence;
    m_replayDisplayedUs = m_replaySnapshotDisplayedUs;
    m_replayPlayAnchorUs = m_replaySnapshotDisplayedUs;
    m_replayAnalyzedIndex = m_replaySnapshotAnalyzedIndex;
    m_replayTimingIssueMarkers = m_replaySnapshotTimingIssueMarkers;
    m_replayValueIssueMarkers = m_replaySnapshotValueIssueMarkers;
    m_replayAlarmIssueMarkers = m_replaySnapshotAlarmIssueMarkers;
    if (m_replayLoaded && m_replayFrameCount > 0) {
        const int idx = std::clamp(m_replaySnapshotAnalyzedIndex, 0, std::max(0, m_replayFrameCount - 1));
        m_replay.setCurrentIndex(idx);
        updateReplayCursor(idx, m_replayFrameCount, m_replaySnapshotDisplayedUs, m_replayDurationUs, m_replay.frameCount() > 1 ? (double(idx) / double(m_replay.frameCount() - 1)) : 0.0);
    }
    emit replayIssueMarkersChanged();
}

const QHash<quint32, AppController::IdState>& AppController::replaySnapshotStateMap() const {
    return (m_replayRebuildActive && m_replaySnapshotValid) ? m_replaySnapshotStates : m_replayStates;
}

const QVector<CanMonitorAnalysis::AlarmGroup>& AppController::replaySnapshotAlarmGroups() const {
    return (m_replayRebuildActive && m_replaySnapshotValid) ? m_replaySnapshotAlarmGroups : m_replayAlarmGroups;
}

const QVector<AppController::ReplayIssueMarker>& AppController::replaySnapshotMarkersForKind(const QString& kindValue) const {
    const QString kind = kindValue.trimmed().toLower();
    if (!(m_replayRebuildActive && m_replaySnapshotValid)) return replayIssueMarkersForKind(kind);
    static const QVector<ReplayIssueMarker> empty;
    if (kind == QStringLiteral("timing")) return m_replaySnapshotTimingIssueMarkers;
    if (kind == QStringLiteral("value")) return m_replaySnapshotValueIssueMarkers;
    if (kind == QStringLiteral("alarm")) return m_replaySnapshotAlarmIssueMarkers;
    return empty;
}

quint64 AppController::replaySnapshotDisplayedUs() const {
    return (m_replayRebuildActive && m_replaySnapshotValid) ? m_replaySnapshotDisplayedUs : m_replayDisplayedUs;
}

int AppController::replaySnapshotObservedIdCount() const {
    return replaySnapshotStateMap().size();
}

qint64 AppController::pendingLiveFrameCount() const {
    return qint64(m_pendingLiveFrames.size()) - qint64(m_pendingLiveFrameOffset);
}

void AppController::appendPendingLiveFrames(const FrameRecordList& frames) {
    if (frames.isEmpty()) return;
    if (m_pendingLiveFrameOffset > 0 &&
        (m_pendingLiveFrameOffset >= 4096 || (m_pendingLiveFrameOffset * 2) >= m_pendingLiveFrames.size())) {
        compactPendingLiveFrames();
    }
    m_pendingLiveFrames.reserve(std::min<qsizetype>(kLiveProjectionHardBacklog, m_pendingLiveFrames.size() + frames.size()));
    for (const FrameRecord& frame : frames) {
        m_pendingLiveFrames.push_back(frame);
    }
    if (m_pendingLiveFrames.size() > kLiveProjectionHardBacklog && m_pendingLiveFrameOffset > 0) {
        m_pendingLiveFrames = m_pendingLiveFrames.mid(m_pendingLiveFrameOffset);
        m_pendingLiveFrameOffset = 0;
    }
    const int excess = std::max(0, int(m_pendingLiveFrames.size()) - kLiveProjectionHardBacklog);
    if (excess > 0) {
        m_liveProjectionDroppedFrames += quint64(excess);
        m_pendingLiveFrames.erase(m_pendingLiveFrames.begin(), m_pendingLiveFrames.begin() + excess);
        m_pendingLiveFrameOffset = 0;
    }
    m_liveProjectionMaxBacklog = std::max(m_liveProjectionMaxBacklog, int(pendingLiveFrameCount()));
}

int AppController::liveFlushChunkForBacklog(qint64 backlog) const {
    Q_UNUSED(backlog);
    return std::min(m_liveFlushChunk, kLiveProjectionMaxFlushFrames);
}

int AppController::liveFlushBudgetForBacklog(qint64 backlog) const {
    Q_UNUSED(backlog);
    return std::min(m_liveFlushBudgetMs, kLiveProjectionFlushBudgetMs);
}

void AppController::compactPendingLiveFrames() {
    if (m_pendingLiveFrameOffset <= 0) return;
    if (m_pendingLiveFrameOffset >= m_pendingLiveFrames.size()) {
        m_pendingLiveFrames.clear();
        m_pendingLiveFrameOffset = 0;
        return;
    }
    if (m_pendingLiveFrameOffset < 4096 && (m_pendingLiveFrameOffset * 2) < m_pendingLiveFrames.size()) return;
    m_pendingLiveFrames = m_pendingLiveFrames.mid(m_pendingLiveFrameOffset);
    m_pendingLiveFrameOffset = 0;
}

void AppController::flushPendingLiveFrames() {
    const qint64 backlogBefore = pendingLiveFrameCount();
    if (backlogBefore <= 0) {
        compactPendingLiveFrames();
        return;
    }

    QElapsedTimer budget;
    budget.start();
    const QString liveSource = QStringLiteral("live");
    const int sourceStartOffset = m_pendingLiveFrameOffset;
    const int targetChunk = liveFlushChunkForBacklog(backlogBefore);
    const int budgetMs = liveFlushBudgetForBacklog(backlogBefore);
    int processed = 0;
    while (m_pendingLiveFrameOffset < m_pendingLiveFrames.size()) {
        const FrameRecord& fr = m_pendingLiveFrames[m_pendingLiveFrameOffset++];
        ensureTimeAnchorForFrame(liveSource, fr.tExtUs);
        ingestFrame(fr, liveSource);
        ++processed;
        if (processed >= targetChunk) break;
        if (processed >= m_liveFlushMinChunk && budget.elapsed() >= budgetMs) break;
    }

    const int sourceEndOffset = m_pendingLiveFrameOffset;
    const int processedCount = std::max(0, sourceEndOffset - sourceStartOffset);
    const int elapsedMs = int(budget.elapsed());
    m_liveProjectionLastFlushMs = elapsedMs;
    if (m_pendingLiveFrameOffset < m_pendingLiveFrames.size() &&
        (processed >= targetChunk || (processed >= m_liveFlushMinChunk && elapsedMs >= budgetMs))) {
        ++m_liveProjectionFlushBudgetHits;
    }
    const int viewCount = std::min(processedCount, m_liveViewChunk);
    if (viewCount > 0) {
        const int viewStartOffset = sourceEndOffset - viewCount;
        if (processedCount > viewCount) {
            m_liveSampledViewDrops += processedCount - viewCount;
        }
        FrameRecordList viewChunk;
        viewChunk.reserve(viewCount);
        QStringList timeTexts;
        timeTexts.reserve(viewCount);
        for (int index = viewStartOffset; index < sourceEndOffset; ++index) {
            const FrameRecord& fr = m_pendingLiveFrames.at(index);
            viewChunk.push_back(fr);
            timeTexts << timeTextForSourceUs(liveSource, fr.tExtUs);
        }
        queueLiveViewBatch(viewChunk, timeTexts);
    }

    compactPendingLiveFrames();

    const qint64 backlogAfter = pendingLiveFrameCount();
    if (backlogAfter > 0) m_liveFlushTimer.start(backlogAfter > kLiveProjectionSoftBacklog ? 0 : 8);
}

void AppController::queueLiveViewBatch(const FrameRecordList& frames, const QStringList& timeTexts) {
    if (frames.isEmpty() || timeTexts.isEmpty()) return;

    const int count = std::min(int(frames.size()), int(timeTexts.size()));
    const int keepLimit = std::max(1, m_liveViewChunk * 2);
    m_pendingLiveViewFrames.reserve(keepLimit);
    m_pendingLiveViewTimeTexts.reserve(keepLimit);

    if (count >= keepLimit) {
        m_pendingLiveViewFrames.clear();
        m_pendingLiveViewTimeTexts.clear();
        const int start = count - keepLimit;
        for (int i = start; i < count; ++i) {
            m_pendingLiveViewFrames.push_back(frames.at(i));
            m_pendingLiveViewTimeTexts.push_back(timeTexts.at(i));
        }
        if (count > keepLimit) m_liveSampledViewDrops += count - keepLimit;
    } else {
        for (int i = 0; i < count; ++i) {
            m_pendingLiveViewFrames.push_back(frames.at(i));
            m_pendingLiveViewTimeTexts.push_back(timeTexts.at(i));
        }

        const int excess = int(m_pendingLiveViewFrames.size()) - keepLimit;
        if (excess > 0) {
            m_pendingLiveViewFrames.erase(m_pendingLiveViewFrames.begin(), m_pendingLiveViewFrames.begin() + excess);
            m_pendingLiveViewTimeTexts.erase(m_pendingLiveViewTimeTexts.begin(), m_pendingLiveViewTimeTexts.begin() + excess);
            m_liveSampledViewDrops += excess;
        }
    }

    int flushDelayMs = 45;
    if (projectionBackpressureActive()) flushDelayMs = std::max(flushDelayMs, 90);
    if (!m_liveViewFlushTimer.isActive()) m_liveViewFlushTimer.start(flushDelayMs);
}

void AppController::flushQueuedLiveViewBatch() {
    if (m_pendingLiveViewFrames.isEmpty()) return;
    if (m_liveUiPaused || !m_livePanelActive) {
        m_pendingLiveViewFrames.clear();
        m_pendingLiveViewTimeTexts.clear();
        return;
    }

    FrameRecordList batch;
    QStringList timeTexts;
    batch.swap(m_pendingLiveViewFrames);
    timeTexts.swap(m_pendingLiveViewTimeTexts);
    m_liveFrames.appendLiveBatch(batch, timeTexts);
}

void AppController::processReplayRebuildStep() {
    if (!m_replayRebuildActive || !m_replayLoaded) return;
    const auto& frames = m_replay.frames();
    if (frames.empty()) {
        cancelReplayRebuild(false);
        return;
    }

    const int target = std::clamp(m_replayRebuildTargetIndex, 0, int(frames.size()) - 1);
    QElapsedTimer budget;
    budget.start();
    int processed = 0;
    while (m_replayRebuildNextIndex <= target) {
        const int i = m_replayRebuildNextIndex;
        const FrameRecord& fr = frames[size_t(i)];
        m_replayProcessingIndex = i;
        m_replayProcessingUs = fr.tExtUs;
        advanceReplayHistoryToUs(fr.tExtUs);
        ensureTimeAnchorForFrame(QStringLiteral("replay"), fr.tExtUs);
        ingestFrame(fr, QStringLiteral("replay"));
        auto& state = m_replayStates[fr.canId];
        syncReplayValueAlarm(fr.canId, state);
        m_replayDisplayedUs = fr.tExtUs;
        m_replayPlayAnchorUs = fr.tExtUs;
        m_replayAnalyzedIndex = i;
        maybeStoreReplayCheckpoint(i);
        ++m_replayRebuildNextIndex;
        ++processed;
        if (processed >= m_replayRebuildChunk) break;
        if (processed >= m_replayRebuildMinChunk && budget.elapsed() >= m_replayRebuildBudgetMs) break;
    }
    m_replayProcessingIndex = -1;
    m_replayProcessingUs = 0;

    const int cursorIndex = (m_replayAnalyzedIndex >= 0) ? m_replayAnalyzedIndex : m_replay.currentIndex();
    const quint64 cursorUs = (m_replayDisplayedUs > 0) ? m_replayDisplayedUs : m_replay.currentUs();
    const double cursorProgress = (m_replay.frameCount() > 1 && cursorIndex >= 0)
        ? (double(cursorIndex) / double(m_replay.frameCount() - 1))
        : m_replay.progress();
    updateReplayCursor(cursorIndex, m_replay.frameCount(), cursorUs, m_replay.durationUs(), cursorProgress);
    emit replayStateChanged();

    if (m_replayRebuildNextIndex <= target) {
        if ((m_replayRebuildNextIndex % std::max(1, m_replayRebuildChunk * 2)) == 0 || m_replayRebuildNextIndex == target) {
            setStatus(QStringLiteral("재생 분석 재구성 중: %1 / %2").arg(m_replayRebuildNextIndex).arg(target + 1));
        }
        m_replayRebuildTimer.start(1);
        return;
    }

    const int viewStart = std::max(0, target - 699);
    FrameRecordList visibleFrames;
    QStringList visibleTimeTexts;
    visibleFrames.reserve(target - viewStart + 1);
    visibleTimeTexts.reserve(target - viewStart + 1);
    for (int i = viewStart; i <= target; ++i) {
        const FrameRecord& fr = frames[size_t(i)];
        visibleFrames.push_back(fr);
        visibleTimeTexts.push_back(timeTextForSourceUs(QStringLiteral("replay"), fr.tExtUs));
    }

    m_replayFrames.clear();
    m_replayFrames.appendReplayBatch(visibleFrames, visibleTimeTexts);
    rebuildReplayGraphHistoryWindow();
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    captureReplaySnapshotState();
    const QString doneReason = m_replayRebuildReason;
    maybeStoreReplayCheckpoint(target);
    cancelReplayRebuild(false);
    m_replay.setCurrentIndex(target);
    updateReplayCursor(target, m_replay.frameCount(), m_replayDisplayedUs, m_replay.durationUs(),
        m_replay.frameCount() > 1 ? (double(target) / double(m_replay.frameCount() - 1)) : 0.0);
    emit replayStateChanged();
    requestGraphRefresh(false);
    if (!doneReason.isEmpty()) setStatus(doneReason);
}

void AppController::ensureTimeAnchorForFrame(const QString& source, quint64 frameUs) {
    if (source == QStringLiteral("replay")) {
        if (m_replayBaseFrameUs == 0) m_replayBaseFrameUs = frameUs;
        if (!m_replayBaseDateTime.isValid()) m_replayBaseDateTime = QDateTime::currentDateTime();
        return;
    }
    if (m_liveBaseFrameUs == 0) {
        m_liveBaseFrameUs = frameUs;
        m_liveBaseDateTime = QDateTime::currentDateTime();
    }
    if (frameUs > m_liveLatestUs) m_liveLatestUs = frameUs;
}

void AppController::loadReplayTimeMeta(const QString& replayBinPath) {
    m_replayBaseDateTime = {};
    m_replayBaseFrameUs = 0;
    const QFileInfo fi(replayBinPath);
    const QString metaPath = ReplayRuntime::metaPathFor(replayBinPath);
    QFile metaFile(metaPath);
    if (!metaFile.open(QIODevice::ReadOnly)) {
        m_replayBaseDateTime = fi.lastModified();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
    const QJsonObject obj = doc.object();
    QString created = obj.value(QStringLiteral("created_local")).toString().trimmed();
    if (created.isEmpty()) created = obj.value(QStringLiteral("created_utc")).toString().trimmed();
    QDateTime parsed = QDateTime::fromString(created, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(created, Qt::ISODate);
    m_replayBaseDateTime = parsed.isValid() ? parsed : fi.lastModified();
}

qint64 AppController::analysisNowMsForSource(const QString& source) const {
    if (source == QStringLiteral("replay")) {
        return qint64(replayAnalysisUs() / 1000ULL);
    }
    if (m_liveLatestUs > 0) return qint64(m_liveLatestUs / 1000ULL);
    if (m_liveBaseFrameUs > 0) return qint64(m_liveBaseFrameUs / 1000ULL);
    return 0;
}

QString AppController::timeTextForSourceUs(const QString& source, quint64 frameUs) const {
    return timeTextForSourceMs(source, qint64(frameUs / 1000ULL));
}

QString AppController::timeTextForSourceMs(const QString& source, qint64 frameMs) const {
    QDateTime baseDt;
    qint64 baseMs = 0;
    if (source == QStringLiteral("replay")) {
        baseDt = m_replayBaseDateTime;
        baseMs = qint64(m_replayBaseFrameUs / 1000ULL);
    } else {
        baseDt = m_liveBaseDateTime;
        baseMs = qint64(m_liveBaseFrameUs / 1000ULL);
    }
    if (baseDt.isValid() && baseMs > 0 && frameMs >= baseMs) {
        return baseDt.addMSecs(frameMs - baseMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    }
    if (baseMs > 0 && frameMs >= baseMs) {
        return fmtReplayUs(quint64(frameMs - baseMs) * 1000ULL);
    }
    return fmtReplayUs(quint64(std::max<qint64>(0, frameMs)) * 1000ULL);
}

quint64 AppController::replayAnalysisUs() const {
    quint64 shownUs = replaySnapshotDisplayedUs() > 0 ? replaySnapshotDisplayedUs() : m_replayCurrentUs;
    if (!m_replayPlaying || !m_replayPlayClock.isValid()) return shownUs;

    const quint64 advancedUs = m_replayPlayAnchorUs + quint64(std::max<qint64>(0, qint64(double(m_replayPlayClock.elapsed()) * 1000.0 * m_replaySpeed)));
    const quint64 ceilingUs = (m_replayCurrentUs > m_replayPlayAnchorUs) ? m_replayCurrentUs : shownUs;
    return std::min(advancedUs, ceilingUs);
}

AppController::AppController(QObject* parent) : QObject(parent) {
    m_uptime.start();
    m_logTargetDirectory = defaultLogDirectory();
    m_timingModel.setRoles({QStringLiteral("key"), QStringLiteral("idText"), QStringLiteral("name"), QStringLiteral("severity"), QStringLiteral("severityColor"), QStringLiteral("expectedMsText"), QStringLiteral("lastGapMsText"), QStringLiteral("ageMsText"), QStringLiteral("source"), QStringLiteral("reason"), QStringLiteral("metricText"), QStringLiteral("gaugePct"), QStringLiteral("eventCount"), QStringLiteral("history")});
    m_valueModel.setRoles({QStringLiteral("key"), QStringLiteral("idText"), QStringLiteral("name"), QStringLiteral("severity"), QStringLiteral("severityColor"), QStringLiteral("source"), QStringLiteral("bus"), QStringLiteral("dataHex"), QStringLiteral("gapText"), QStringLiteral("ageText"), QStringLiteral("reason"), QStringLiteral("previewText"), QStringLiteral("summaryText"), QStringLiteral("summaryRich"), QStringLiteral("valueMetricText"), QStringLiteral("valueGaugePct")});
    m_alarmModel.setRoles({QStringLiteral("key"), QStringLiteral("timeText"), QStringLiteral("severity"), QStringLiteral("severityColor"), QStringLiteral("idText"), QStringLiteral("name"), QStringLiteral("source"), QStringLiteral("message"), QStringLiteral("active"), QStringLiteral("count"), QStringLiteral("metricText"), QStringLiteral("gaugePct"), QStringLiteral("category"), QStringLiteral("categoryLabel"), QStringLiteral("history")});
    m_graphCatalogModel.setRoles({QStringLiteral("key"), QStringLiteral("idText"), QStringLiteral("label"), QStringLiteral("name"), QStringLiteral("unit"), QStringLiteral("group"), QStringLiteral("mode"), QStringLiteral("color"), QStringLiteral("selected")});
    seedBusRoleResolver();
    loadModelFile(kBundledModelPath);
    m_liveFrameView.setSourceModel(&m_liveFrames);
    m_replayFrameView.setSourceModel(&m_replayFrames);
    m_replaySeekTimer.setSingleShot(true);
    m_replaySeekTimer.setInterval(110);
    connect(&m_replaySeekTimer, &QTimer::timeout, this, [this]() {
        if (!m_replayLoaded || m_pendingReplaySeekIndex < 0) return;
        const int target = m_pendingReplaySeekIndex;
        m_pendingReplaySeekIndex = -1;
        jumpReplayToIndex(target, QStringLiteral("재생 지점 이동: %1 / %2").arg(target + 1).arg(m_replay.frameCount()));
    });

    m_replayRebuildTimer.setSingleShot(true);
    connect(&m_replayRebuildTimer, &QTimer::timeout, this, [this]() {
        processReplayRebuildStep();
    });

    m_liveFlushTimer.setSingleShot(true);
    m_liveFlushTimer.setInterval(12);
    connect(&m_liveFlushTimer, &QTimer::timeout, this, [this]() {
        flushPendingLiveFrames();
    });

    m_boardHealthWatchdogTimer.setInterval(250);
    connect(&m_boardHealthWatchdogTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected || m_transportModeKey != QStringLiteral("typed")) return;
        const bool previousBoardAlive = m_evidenceRuntime.boardAlive();
        const bool previousControlCapable = m_evidenceRuntime.controlCapable();
        m_evidenceRuntime.advanceWallTimeMs(quint64(QDateTime::currentMSecsSinceEpoch()));
        const bool boardAliveChanged = previousBoardAlive != m_evidenceRuntime.boardAlive();
        const bool controlCapableChanged = previousControlCapable != m_evidenceRuntime.controlCapable();
        if (m_controlRuntime.armed() && !m_evidenceRuntime.controlCapable()) {
            m_controlRuntime.setArmed(false);
            m_controlKeepaliveTimer.stop();
            refreshControlStatus(QStringLiteral("Control disarmed: %1").arg(m_evidenceRuntime.reason()));
        }
        if (boardAliveChanged || controlCapableChanged) {
            emit typedEvidenceChanged();
            emit controlStateChanged();
            requestDerivedSummaryRefresh(false);
        }
    });
    m_boardHealthWatchdogTimer.start();

    m_graphRefreshTimer.setSingleShot(true);
    m_graphRefreshTimer.setInterval(180);
    connect(&m_graphRefreshTimer, &QTimer::timeout, this, [this]() {
        flushGraphRefresh();
    });

    m_graphOverviewBuildTimer.setSingleShot(true);
    m_graphOverviewBuildTimer.setInterval(0);
    connect(&m_graphOverviewBuildTimer, &QTimer::timeout, this, [this]() {
        processGraphOverviewBuildStep();
    });

    m_controlKeepaliveTimer.setTimerType(Qt::PreciseTimer);
    m_controlKeepaliveTimer.setInterval(kControlWorkerCyclePeriodMs);
    connect(&m_controlKeepaliveTimer, &QTimer::timeout, this, [this]() {
        sendControlKeepaliveTick();
    });

    m_controlPatternTimer.setTimerType(Qt::PreciseTimer);
    m_controlPatternTimer.setSingleShot(true);
    connect(&m_controlPatternTimer, &QTimer::timeout, this, [this]() {
        processControlPatternStep();
    });

    m_worker = m_transportRuntime.createWorker();
    connect(m_worker, &SerialWorker::stateChanged, this, [this](bool ok, const QString& msg) {
        const bool previousReplayActive = replayAnalysisActive();
        m_connected = ok;
        m_evidenceRuntime.setSerialOpen(ok);
        m_transportSession.setConnected(ok);
        updateTransportDiagnostics();
        if (ok) m_evidenceRuntime.advanceWallTimeMs(quint64(QDateTime::currentMSecsSinceEpoch()));
        if (!ok) {
            m_controlRuntime.setArmed(false);
            m_controlRuntime.setTestRunning(false);
            m_controlKeepaliveTimer.stop();
            m_controlPatternTimer.stop();
            m_controlPatternSteps.clear();
            m_controlPatternIndex = 0;
            m_controlRuntime.clearBurstWallMs();
            refreshControlStatus(QStringLiteral("Control disarmed on disconnect"));
            m_lastStats = StatsRecord{};
            m_lastTypedHealthMonoUs = 0;
            m_lastTypedHealthCanRxTotal = 0;
            m_lastTypedHealthSerialTxTotal = 0;
            m_pendingLiveFrames.clear();
            m_pendingLiveFrameOffset = 0;
            m_liveFlushTimer.stop();
            m_lastLiveFrameWallMs = -1;
            m_lastLiveStatsWallMs = -1;
            m_liveSampledViewDrops = 0;
            m_liveProjectionObservedFrames = 0;
            m_liveProjectionProjectedFrames = 0;
            m_liveProjectionWorkerSampledFrames = 0;
            m_liveProjectionWorkerDroppedFrames = 0;
            m_liveProjectionObservedControlEvidenceRecords = 0;
            m_liveProjectionProjectedControlEvidenceRecords = 0;
            m_liveProjectionSampledControlEvidenceRecords = 0;
            m_liveProjectionDroppedFrames = 0;
            m_liveProjectionFlushBudgetHits = 0;
            m_liveProjectionMaxBacklog = 0;
            m_liveProjectionLastFlushMs = 0;
            m_lastRoutineControlWriteNotifyWallMs = 0;
            m_lastHostTxQueueNotifyWallMs = 0;
            clearGraphHistory(QStringLiteral("live"));
            if (m_logRecordingActive || m_logStopping) {
                m_logRecordingActive = false;
                m_logStopping = false;
                if (!m_logTypedSession && !m_logTempPath.isEmpty()) m_logPendingSave = true;
                requestLogStateRefresh(true);
            }
        }
        emit connectedChanged();
        emit transportDiagnosticsChanged();
        emit typedEvidenceChanged();
        emit controlStateChanged();
        requestLiveStatsRefresh(true);
        handleAnalysisSourceMaybeChanged(previousReplayActive);
        requestGraphRefresh(true);
        setStatus(msg);
    });
    connect(m_worker, &SerialWorker::errorOccurred, this, [this](const QString& msg) {
        setStatus(msg);
    });
    connect(m_worker, &SerialWorker::loggingStateChanged, this, [this](bool active, const QString& path) {
        m_logRecordingActive = active;
        if (active) {
            if (!path.isEmpty()) m_logTempPath = path;
            m_logStopping = false;
            m_logSaving = false;
            m_logPendingSave = false;
            setStatus(QStringLiteral("로그 기록 중 · 임시 버퍼 적재 중"));
        } else {
            m_logStopping = false;
            if (!m_logTempPath.isEmpty() && (QFileInfo::exists(m_logTempPath) || m_logRecordedFrameCount > 0)) {
                m_logPendingSave = true;
                setStatus(QStringLiteral("로그 종료 완료 · 저장 위치 선택"));
            }
        }
        requestLogStateRefresh(true);
    });
    connect(m_worker, &SerialWorker::loggingProgress, this, [this](quint64 bytesWritten, quint64 frameCount) {
        m_logRecordedBytes = bytesWritten;
        m_logRecordedFrameCount = frameCount;
        requestLogStateRefresh(false);
    });
    connect(m_worker, &SerialWorker::statsReceived, this, [this](const StatsRecord& st) {
        m_lastStats = st;
        m_lastLiveStatsWallMs = QDateTime::currentMSecsSinceEpoch();
        ensureTimeAnchorForFrame(QStringLiteral("live"), st.tExtUs);
        if (st.tExtUs > m_liveLatestUs) m_liveLatestUs = st.tExtUs;
        syncLiveBusHealthAlarms();
        requestLiveStatsRefresh(false);
        requestDerivedSummaryRefresh(false);
    });
    connect(m_worker, &SerialWorker::framesReceived, this, [this](const FrameRecordList& frames) {
        if (frames.isEmpty()) return;
        m_lastLiveFrameWallMs = QDateTime::currentMSecsSinceEpoch();
        appendPendingLiveFrames(frames);
        if (!m_liveFlushTimer.isActive()) {
            m_liveFlushTimer.start(pendingLiveFrameCount() > m_liveFlushChunk ? 0 : 6);
        }
    });
    connect(m_worker, &SerialWorker::typedRecordsReceived, this, [this](const TypedRecordList& records) {
        if (records.isEmpty()) return;
        const bool previousBoardAlive = m_evidenceRuntime.boardAlive();
        const bool previousControlCapable = m_evidenceRuntime.controlCapable();
        const quint64 nowWallMs = quint64(QDateTime::currentMSecsSinceEpoch());
        for (const auto& record : records) {
            ++m_typedRecordCount;
            m_typedTypeCounts[record.header.recordType] += 1;
            m_typedLastMonoUs = typedRecordMonoUs(record);
            m_typedLastRecordType = typedRecordTypeName(record.header.recordType);

            const TypedRecordType recordType = record.header.type();
            if (recordType == TypedRecordType::CanRxRaw || recordType == TypedRecordType::CanTxRaw) {
                const auto can = decodeTypedCanRaw(record);
                if (!can) continue;
                const QString canText = QStringLiteral("%1 BUS %2 %3 DLC %4")
                    .arg(can->txAudit ? QStringLiteral("TX") : QStringLiteral("RX"))
                    .arg(can->bus)
                    .arg(idText(can->canId))
                    .arg(can->dlc);
                if (can->txAudit) {
                    m_typedCanTxByBus[can->bus] += 1;
                    m_typedLastCanTxSummary = canText;
                    if (isControlCommandCanId(can->canId)) {
                        const quint32 matchedCommandId = m_controlAudit.takeAcceptedCommandId(can->canId);
                        m_controlAudit.noteTxAudit(matchedCommandId > 0);
                        const bool auditUiDue = m_controlAudit.txAuditUiDue(matchedCommandId == 0, qint64(nowWallMs));
                        if (auditUiDue) {
                            const QString auditSummary = QStringLiteral("AUDIT %1 %2")
                                .arg(canText, hexBytes(can->data, can->dlc));
                            appendControlEvidenceEvent(QStringLiteral("CAN_TX_RAW"),
                                                       matchedCommandId > 0 ? QStringLiteral("ok") : QStringLiteral("warn"),
                                                       matchedCommandId > 0
                                                           ? QStringLiteral("실제 CAN 송신 audit 확인")
                                                           : QStringLiteral("ACK 매칭 없는 CAN_TX_RAW audit"),
                                                       auditSummary,
                                                       matchedCommandId,
                                                       can->canId,
                                                       can->bus);
                            emit controlStateChanged();
                        }
                    }
                } else {
                    m_typedCanRxByBus[can->bus] += 1;
                    m_typedLastCanRxSummary = canText;
                    observeBusRoleFingerprint(can->bus, can->canId);
                    if (isControlCommandCanId(can->canId)) {
                        if (m_controlAudit.noteFeedbackIfDue(can->canId, qint64(nowWallMs))) {
                            appendControlEvidenceEvent(QStringLiteral("CAN_RX_FEEDBACK"),
                                                       QStringLiteral("info"),
                                                       QStringLiteral("Control CAN_RX observed; feedback/echo candidate only"),
                                                       QStringLiteral("%1 %2").arg(canText, hexBytes(can->data, can->dlc)),
                                                       0,
                                                       can->canId,
                                                       can->bus);
                        }
                    }
                }
            } else if (recordType == TypedRecordType::ControlAck) {
                const auto ack = decodeTypedControlAck(record);
                if (!ack) continue;
                if (ack->status != 0 && isControlCommandCanId(ack->targetCanId)) {
                    m_controlAudit.rememberAcceptedAck(ack->targetCanId, ack->commandId);
                }
                m_controlAudit.noteAck(ack->status != 0);
                const bool ackUiDue = m_controlAudit.ackUiDue(ack->status == 0, qint64(nowWallMs));
                if (ackUiDue) {
                    const QString ackSummary = QStringLiteral("ACK #%1 %2 reason %3 BUS %4 %5 %6%7")
                        .arg(ack->commandId)
                        .arg(controlAckStatusText(ack->status))
                        .arg(controlAckReasonText(ack->reason))
                        .arg(ack->targetBus)
                        .arg(idText(ack->targetCanId))
                        .arg(controlAckDlcText(ack->targetDlcFlags))
                        .arg(controlAckEvidenceHint(ack->status, ack->reason));
                    appendControlEvidenceEvent(QStringLiteral("CONTROL_ACK"),
                                               ack->status == 0 ? QStringLiteral("error") : QStringLiteral("info"),
                                               ack->status == 0 ? QStringLiteral("보드 요청 거부") : QStringLiteral("보드 요청 수락"),
                                               ackSummary,
                                               ack->commandId,
                                               ack->targetCanId,
                                               ack->targetBus);
                    emit controlStateChanged();
                }
            } else if (recordType == TypedRecordType::BoardEvent) {
                const auto boardEvent = decodeTypedBoardEvent(record);
                if (boardEvent && (boardEvent->code == 12 || boardEvent->code == 17)) {
                    const QString detailHex = QStringLiteral("0x%1")
                        .arg(boardEvent->detail, 4, 16, QLatin1Char('0'))
                        .toUpper();
                    const QString auditSummary = QStringLiteral("NO CAN_TX_RAW: %1 detail 0x%2 counter %3")
                        .arg(boardEventCodeText(boardEvent->code))
                        .arg(detailHex.mid(2))
                        .arg(boardEvent->counter);
                    appendControlEvidenceEvent(QStringLiteral("BOARD_EVENT"),
                                               QStringLiteral("error"),
                                               QStringLiteral("Actual CAN TX failed before audit"),
                                               auditSummary);
                    refreshControlStatus(QStringLiteral("Control TX failed: %1").arg(boardEventCodeText(boardEvent->code)));
                }
            } else if (recordType == TypedRecordType::Capability) {
                const auto capability = decodeTypedCapability(record);
                if (!capability) continue;
                m_evidenceRuntime.ingestCapability(*capability, nowWallMs);
                const QString previousBusSummary = m_controlBusSummary;
                const bool previousBusAllowed = controlTargetBusAllowed();
                const int previousTargetBus = controlTargetBus();
                updateControlBusCapability(*capability);
                if (m_controlBusSummary != previousBusSummary ||
                    previousBusAllowed != controlTargetBusAllowed() ||
                    previousTargetBus != controlTargetBus()) {
                    emit controlStateChanged();
                }
            } else if (recordType == TypedRecordType::BoardHealth) {
                const auto health = decodeTypedBoardHealth(record);
                if (!health) continue;
                m_evidenceRuntime.ingestBoardHealth(*health, nowWallMs);

                StatsRecord typedStats = m_lastStats;
                typedStats.tExtUs = health->monoUs;
                typedStats.droppedTotal = health->canDroppedTotal;
                typedStats.fifoOverflowTotal = health->canFifoOverflowTotal;
                if (m_lastTypedHealthMonoUs > 0 && health->monoUs > m_lastTypedHealthMonoUs) {
                    const quint64 elapsedUs = health->monoUs - m_lastTypedHealthMonoUs;
                    const quint64 rxDelta = u32CounterDelta(health->canRxTotal, m_lastTypedHealthCanRxTotal);
                    const quint64 txDelta = u32CounterDelta(health->serialRecordTxTotal, m_lastTypedHealthSerialTxTotal);
                    typedStats.rxFps1s = boundedFpsFromDelta(quint32(std::min<quint64>(rxDelta, std::numeric_limits<quint32>::max())), elapsedUs);
                    typedStats.txFps1s = boundedFpsFromDelta(quint32(std::min<quint64>(txDelta, std::numeric_limits<quint32>::max())), elapsedUs);
                }
                const quint64 streamRxCount = m_typedTypeCounts.value(static_cast<quint8>(TypedRecordType::CanRxRaw));
                if (!m_typedRxHealthParityAnchored) {
                    m_typedRxHealthParityAnchored = true;
                    m_typedRxHealthAnchorBoardTotal = health->canRxTotal;
                    m_typedRxHealthAnchorStreamCount = streamRxCount;
                    m_typedRxHealthBoardDelta = 0;
                    m_typedRxHealthStreamDelta = 0;
                    m_typedRxHealthMissing = 0;
                } else {
                    m_typedRxHealthBoardDelta = u32CounterDelta(health->canRxTotal, m_typedRxHealthAnchorBoardTotal);
                    m_typedRxHealthStreamDelta = streamRxCount >= m_typedRxHealthAnchorStreamCount
                        ? (streamRxCount - m_typedRxHealthAnchorStreamCount)
                        : 0;
                    m_typedRxHealthMissing = qint64(m_typedRxHealthBoardDelta) - qint64(m_typedRxHealthStreamDelta);
                }
                m_lastStats = typedStats;
                m_lastTypedHealthMonoUs = health->monoUs;
                m_lastTypedHealthCanRxTotal = health->canRxTotal;
                m_lastTypedHealthSerialTxTotal = health->serialRecordTxTotal;
                m_lastLiveStatsWallMs = QDateTime::currentMSecsSinceEpoch();
                ensureTimeAnchorForFrame(QStringLiteral("live"), health->monoUs);
                if (health->monoUs > m_liveLatestUs) m_liveLatestUs = health->monoUs;
                syncLiveBusHealthAlarms();
                requestLiveStatsRefresh(false);
            }
        }
        const bool boardAliveChanged = previousBoardAlive != m_evidenceRuntime.boardAlive();
        const bool controlCapableChanged = previousControlCapable != m_evidenceRuntime.controlCapable();
        if (m_controlRuntime.armed() && !m_evidenceRuntime.controlCapable()) {
            m_controlRuntime.setArmed(false);
            m_controlKeepaliveTimer.stop();
            refreshControlStatus(QStringLiteral("Control disarmed: %1").arg(m_evidenceRuntime.reason()));
        }
        const bool typedUiDue = m_lastTypedEvidenceNotifyWallMs <= 0
            || (qint64(nowWallMs) - m_lastTypedEvidenceNotifyWallMs) >= kTypedEvidenceUiMinIntervalMs;
        if (boardAliveChanged || controlCapableChanged || typedUiDue) {
            m_lastTypedEvidenceNotifyWallMs = qint64(nowWallMs);
            emit typedEvidenceChanged();
        }
        if (boardAliveChanged || controlCapableChanged) emit controlStateChanged();
        requestDerivedSummaryRefresh(false);
    });
    connect(m_worker, &SerialWorker::typedProjectionStatusChanged, this, [this](quint64 observedCanRxFrames,
                                                                                 quint64 projectedCanRxFrames,
                                                                                 quint64 sampledCanRxFrames,
                                                                                 quint64 workerDroppedCanRxFrames,
                                                                                 quint64 observedBus0CanRxFrames,
                                                                                 quint64 observedBus1CanRxFrames,
                                                                                 quint64 observedControlEvidenceRecords,
                                                                                 quint64 projectedControlEvidenceRecords,
                                                                                 quint64 sampledControlEvidenceRecords) {
        m_typedTypeCounts[static_cast<quint8>(TypedRecordType::CanRxRaw)] =
            std::max(m_typedTypeCounts.value(static_cast<quint8>(TypedRecordType::CanRxRaw)), observedCanRxFrames);
        m_typedCanRxByBus[0] = std::max(m_typedCanRxByBus.value(0), observedBus0CanRxFrames);
        m_typedCanRxByBus[1] = std::max(m_typedCanRxByBus.value(1), observedBus1CanRxFrames);
        m_liveProjectionObservedFrames = observedCanRxFrames;
        m_liveProjectionProjectedFrames = projectedCanRxFrames;
        m_liveProjectionWorkerSampledFrames = sampledCanRxFrames;
        m_liveProjectionWorkerDroppedFrames = workerDroppedCanRxFrames;
        m_liveProjectionObservedControlEvidenceRecords = observedControlEvidenceRecords;
        m_liveProjectionProjectedControlEvidenceRecords = projectedControlEvidenceRecords;
        m_liveProjectionSampledControlEvidenceRecords = sampledControlEvidenceRecords;
        requestLiveStatsRefresh(false);
        emit typedEvidenceChanged();
    });
    connect(m_worker, &SerialWorker::typedTransportStatusChanged, this, [this](quint64 frames,
                                                                               quint64 bytesDropped,
                                                                               quint64 crcFailures,
                                                                               quint64 lengthFailures,
                                                                               quint64 versionWarnings,
                                                                               quint64 seqGaps) {
        m_typedRecordCount = std::max(m_typedRecordCount, frames);
        m_typedBytesDropped = bytesDropped;
        m_typedCrcFailures = crcFailures;
        m_typedLengthFailures = lengthFailures;
        m_typedVersionWarnings = versionWarnings;
        m_typedSeqGaps = seqGaps;
        m_transportSession.updateTypedStatus(frames,
                                             bytesDropped,
                                             crcFailures,
                                             lengthFailures,
                                             versionWarnings,
                                             seqGaps);
        updateTransportDiagnostics();
        emit typedEvidenceChanged();
        emit transportDiagnosticsChanged();
        requestDerivedSummaryRefresh(false);
    });
    connect(m_worker, &SerialWorker::hostTxQueueChanged, this, [this](quint64 queuedFrames,
                                                                       quint64 queuedBytes,
                                                                       quint64 enqueuedFrames,
                                                                       quint64 writtenFrames,
                                                                       quint64 droppedFrames) {
        m_transportSession.updateHostTxQueue(queuedFrames, queuedBytes, enqueuedFrames, writtenFrames, droppedFrames);
        const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
        const bool notifyDue = droppedFrames > 0
            || m_lastHostTxQueueNotifyWallMs <= 0
            || nowWallMs - m_lastHostTxQueueNotifyWallMs >= kHostTxQueueUiMinIntervalMs;
        if (notifyDue) {
            m_lastHostTxQueueNotifyWallMs = nowWallMs;
            updateTransportDiagnostics();
            emit transportDiagnosticsChanged();
        }
    });
    connect(m_worker, &SerialWorker::typedStorageStateChanged, this, [this](bool active, const QString& path) {
        m_logTypedSession = true;
        m_logRecordingActive = active;
        m_logStopping = false;
        m_logSaving = false;
        m_logPendingSave = false;
        if (!path.isEmpty()) {
            m_logTempPath = path;
            m_logPath = path;
            m_logSuggestedSavePath = path;
            if (!active) m_logLastSavedPath = path;
            emit logPathChanged();
        }
        setStatus(active
            ? QStringLiteral("Typed capture recording: %1").arg(path)
            : QStringLiteral("Typed capture finalized: %1").arg(path));
        requestLogStateRefresh(true);
    });
    connect(m_worker, &SerialWorker::typedStorageProgress, this, [this](quint64 bytesWritten, quint64 recordCount) {
        m_logRecordedBytes = bytesWritten;
        m_logRecordedFrameCount = recordCount;
        requestLogStateRefresh(false);
    });
    connect(m_worker, &SerialWorker::hostFrameWriteResult, this, [this](bool ok, const QString& summary, quint64 bytesWritten) {
        m_controlAudit.noteHostWriteResult(ok);
        const bool routineWrite = ok
            && (summary.contains(QStringLiteral("worker control cycle")) ||
                summary.contains(QStringLiteral("worker HOST_HEARTBEAT")) ||
                summary.contains(QStringLiteral("RENEW_LEASE")));
        const QString writeSummary = QStringLiteral("%1 | serial %2 | %3 bytes")
            .arg(summary, ok ? QStringLiteral("write ok") : QStringLiteral("write failed"))
            .arg(bytesWritten);
        if (!routineWrite) {
            m_controlRuntime.setLastCommandSummary(writeSummary);
            appendControlEvidenceEvent(QStringLiteral("HOST_WRITE"),
                                       ok ? QStringLiteral("info") : QStringLiteral("error"),
                                       ok ? QStringLiteral("Qt serial write accepted") : QStringLiteral("Qt serial write failed"),
                                       m_controlRuntime.lastCommandSummary());
            refreshControlStatus(ok ? QStringLiteral("요청 전송됨: 실제 성공은 CAN_TX_RAW audit 기준")
                                    : QStringLiteral("제어 요청 write 실패"));
        } else {
            const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
            if (m_lastRoutineControlWriteNotifyWallMs <= 0 ||
                nowWallMs - m_lastRoutineControlWriteNotifyWallMs >= kRoutineControlWriteUiMinIntervalMs) {
                m_lastRoutineControlWriteNotifyWallMs = nowWallMs;
                emit controlStateChanged();
            }
        }
    });
    connect(&m_replay, &ReplayEngine::replayFrame, this, [this](const FrameRecord& fr) {
        if (m_replayDisplayedUs > 0 && fr.tExtUs < m_replayDisplayedUs) {
            m_replayStates.clear();
            m_replayAlarmGroups.clear();
            m_replayAlarmSequence = 0;
            m_replayAnalyzedIndex = -1;
            markAllAnalysisDirty(true);
        }
        m_replayProcessingIndex = std::clamp(m_replay.currentIndex(), 0, std::max(0, m_replay.frameCount() - 1));
        m_replayProcessingUs = fr.tExtUs;
        advanceReplayHistoryToUs(fr.tExtUs);
        ensureTimeAnchorForFrame(QStringLiteral("replay"), fr.tExtUs);
        const QString timeText = timeTextForSourceUs(QStringLiteral("replay"), fr.tExtUs);
        m_recentFrames.appendReplay(fr, timeText);
        m_replayFrames.appendReplay(fr, timeText);
        ingestFrame(fr, QStringLiteral("replay"));
        auto& state = m_replayStates[fr.canId];
        syncReplayValueAlarm(fr.canId, state);
        m_replayDisplayedUs = fr.tExtUs;
        m_replayPlayAnchorUs = fr.tExtUs;
        m_replayPlayClock.restart();
        if (m_replayAnalyzedIndex < m_replay.frameCount() - 1) ++m_replayAnalyzedIndex;
        m_replayProcessingIndex = -1;
        m_replayProcessingUs = 0;
    });
    connect(&m_replay, &ReplayEngine::replayLoaded, this, [this](bool ok, const QString& msg) {
        cancelReplayRebuild(false);
        if (ok && m_replay.frameCount() > 0 && !m_replay.frames().empty()) {
            ensureTimeAnchorForFrame(QStringLiteral("replay"), m_replay.frames().front().tExtUs);
        }
        setReplayLoaded(ok);
        setReplayPlaying(false);
        m_replayDisplayedUs = 0;
        m_replayPlayAnchorUs = 0;
        m_replayAnalyzedIndex = -1;
        updateReplayCursor(0, m_replay.frameCount(), 0, m_replay.durationUs(), 0.0);
        if (ok && m_replay.frameCount() > 0) {
            restartGraphOverviewBuild(true);
            rebuildReplayToIndex(0, QStringLiteral("재생 로드: 시작 지점 분석 준비"));
        } else {
            clearGraphOverviewState();
            emit replayStateChanged();
            setStatus(msg);
        }
    });
    connect(&m_replay, &ReplayEngine::replayCursorChanged, this, [this](int index, int frameCount, quint64 currentUs, quint64 durationUs, double progress) {
        updateReplayCursor(index, frameCount, currentUs, durationUs, progress);
    });
    connect(&m_replay, &ReplayEngine::replayLoopChanged, this, [this](bool enabled) {
        m_replayLoop = enabled;
        emit replayStateChanged();
        if (!m_restoringSession) saveSessionState();
    });
    connect(&m_replay, &ReplayEngine::replayFinished, this, [this]() {
        setReplayAnalysisHeld(true);
        setReplayPlaying(false);
        refreshTimingRows();
        refreshValueRows();
        refreshAlarmRows();
        setStatus(QStringLiteral("재생 완료 · 마지막 분석 고정"));
    });

    m_analysisTimer.setInterval(90);
    connect(&m_analysisTimer, &QTimer::timeout, this, [this]() {
        processTimingAnalysisSlice();
    });
    m_analysisTimer.start();

    m_liveViewFlushTimer.setSingleShot(true);
    connect(&m_liveViewFlushTimer, &QTimer::timeout, this, [this]() {
        flushQueuedLiveViewBatch();
    });

    m_derivedSummaryTimer.setSingleShot(true);
    m_derivedSummaryTimer.setInterval(220);
    connect(&m_derivedSummaryTimer, &QTimer::timeout, this, [this]() {
        flushDerivedSummaryRefresh();
    });

    m_logStateTimer.setSingleShot(true);
    m_logStateTimer.setInterval(260);
    connect(&m_logStateTimer, &QTimer::timeout, this, [this]() {
        flushLogStateRefresh();
    });

    m_liveStatsTimer.setSingleShot(true);
    m_liveStatsTimer.setInterval(140);
    connect(&m_liveStatsTimer, &QTimer::timeout, this, [this]() {
        flushLiveStatsRefresh();
    });

    m_valueRefreshTimer.setInterval(220);
    connect(&m_valueRefreshTimer, &QTimer::timeout, this, [this]() {
        if (analysisPaused()) return;
        if (m_replayRebuildActive && replayAnalysisActive()) return;
        if (m_valueRowsDirty) {
            if (!m_valueViewHeld && projectionDue(m_lastValueProjectionWallMs, valueProjectionIntervalMs())) refreshValueRows();
        } else if (m_valueDetailsDirty && !m_valueViewHeld && m_valuePanelActive) {
            maybeRefreshValueDetails(false);
        }
    });
    m_valueRefreshTimer.start();

    m_alarmRefreshTimer.setInterval(520);
    connect(&m_alarmRefreshTimer, &QTimer::timeout, this, [this]() {
        if (analysisPaused()) return;
        if (m_replayRebuildActive && replayAnalysisActive()) return;
        if (m_alarmRowsDirty && !m_alarmViewHeld && projectionDue(m_lastAlarmProjectionWallMs, alarmProjectionIntervalMs())) refreshAlarmRows();
    });
    m_alarmRefreshTimer.start();

    connect(this, &AppController::sortOptionsChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); });
    connect(this, &AppController::filtersChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); });
    connect(this, &AppController::rulesChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });
    connect(this, &AppController::signalDbChanged, this, &AppController::derivedSummaryChanged);
    connect(this, &AppController::connectedChanged, this, &AppController::derivedSummaryChanged);
    connect(this, &AppController::replayStateChanged, this, &AppController::derivedSummaryChanged);
    connect(this, &AppController::derivedSummaryChanged, this, [this]() { updateOperatorRecentEventsLocked(); });
    connect(this, &AppController::logStateChanged, this, [this]() { updateOperatorRecentEventsLocked(); });
    connect(this, &AppController::replayStateChanged, this, [this]() { updateOperatorRecentEventsLocked(); });
    m_operatorPulseTimer.setInterval(1000);
    connect(&m_operatorPulseTimer, &QTimer::timeout, this, [this]() { emit operatorRecentEventsChanged(); });
    m_operatorPulseTimer.start();
    connect(this, &AppController::liveUiPausedChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });
    connect(&m_liveFrameView, &FrameFilterProxyModel::idFilterChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });
    connect(&m_replayFrameView, &FrameFilterProxyModel::idFilterChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });
    connect(&m_liveFrameView, &FrameFilterProxyModel::busFilterChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });
    connect(&m_replayFrameView, &FrameFilterProxyModel::busFilterChanged, this, [this]() { if (!m_restoringSession) saveSessionState(); emit derivedSummaryChanged(); });

    m_transportRuntime.startWorkerThread(QThread::TimeCriticalPriority);
    m_transportRuntime.setTransportModeKey(m_transportModeKey);
    refreshPorts();
    restoreSessionState();
    updateReplayCursor(0, m_replay.frameCount(), 0, m_replay.durationUs(), 0.0);
    refreshDerivedSummaryCache();
    emit derivedSummaryChanged();
}

AppController::~AppController() {
    prepareControlSafeStopForDisconnect(QStringLiteral("application shutdown safety stop"));
    saveSessionState();
    m_session.sync();
    m_transportRuntime.shutdown();
    m_worker = nullptr;
}

void AppController::rebuildTimingEvalIdCache(const QString& source) {
    auto& ids = timingEvalIdsForSource(source);
    auto& states = stateMapForSource(source);

    QSet<quint32> uniqueIds;
    uniqueIds.reserve((m_modelEnabled ? m_rules.size() : 0) + states.size());
    if (m_modelEnabled) {
        for (auto it = m_rules.cbegin(); it != m_rules.cend(); ++it) uniqueIds.insert(it.key());
    }
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (!it.value().seen) continue;
        if (!shouldTrackTimingForId(it.key())) continue;
        uniqueIds.insert(it.key());
    }

    ids.clear();
    ids.reserve(uniqueIds.size());
    for (quint32 id : uniqueIds) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    int& cursor = timingEvalCursorForSource(source);
    const int idCount = int(ids.size());
    cursor = (idCount <= 0) ? 0 : std::clamp(cursor, 0, idCount - 1);
    timingEvalCacheWallMsForSource(source) = QDateTime::currentMSecsSinceEpoch();
}

QVector<quint32>& AppController::timingEvalIdsForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_replayTimingEvalIds : m_liveTimingEvalIds;
}

int& AppController::timingEvalCursorForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_replayTimingEvalCursor : m_liveTimingEvalCursor;
}

qint64& AppController::timingEvalCacheWallMsForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_lastReplayTimingEvalCacheWallMs : m_lastLiveTimingEvalCacheWallMs;
}

void AppController::processTimingAnalysisSlice() {
    if (analysisPaused()) return;
    if (m_replayRebuildActive && replayAnalysisActive()) return;

    const QString sourceKey = activeAnalysisSourceKey();
    const qint64 nowMs = analysisNowMsForSource(sourceKey);
    if (nowMs <= 0) return;

    auto& ids = timingEvalIdsForSource(sourceKey);
    auto& states = stateMapForSource(sourceKey);
    int& cursor = timingEvalCursorForSource(sourceKey);
    qint64& cacheWallMs = timingEvalCacheWallMsForSource(sourceKey);

    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    const bool cacheStale = cacheWallMs < 0 || (nowWallMs - cacheWallMs) >= 2500;
    if (ids.isEmpty() || cacheStale || states.size() > ids.size() || (m_modelEnabled && m_rules.size() > ids.size())) {
        rebuildTimingEvalIdCache(sourceKey);
    }
    if (ids.isEmpty()) return;
    if (cursor < 0 || cursor >= ids.size()) cursor = 0;

    int sliceBudgetMs = (m_timingPanelActive || m_overviewPanelActive) ? 3 : 2;
    int maxVisits = (m_timingPanelActive || m_overviewPanelActive) ? 96 : 40;
    if (projectionBackpressureActive()) {
        sliceBudgetMs = std::min(sliceBudgetMs, 1);
        maxVisits = std::min(maxVisits, 24);
    }

    bool needRefresh = m_timingRowsDirty;
    QElapsedTimer budget;
    budget.start();
    int visited = 0;
    while (visited < ids.size()) {
        const quint32 id = ids.at(cursor);
        cursor = (cursor + 1) % ids.size();
        ++visited;

        auto it = states.find(id);
        if (it == states.end()) {
            if (visited >= maxVisits || budget.elapsed() >= sliceBudgetMs) break;
            continue;
        }
        IdState& state = it.value();
        if (!state.seen || !shouldTrackTimingForId(id)) {
            if (visited >= maxVisits || budget.elapsed() >= sliceBudgetMs) break;
            continue;
        }
        if (!state.timingDerivedDirty && state.nextTimingEvalMs > nowMs) {
            if (visited >= maxVisits || budget.elapsed() >= sliceBudgetMs) break;
            continue;
        }

        needRefresh = analyzeTimingState(id, state, sourceKey, nowMs) || needRefresh;
        if (visited >= maxVisits || budget.elapsed() >= sliceBudgetMs) break;
    }

    if (needRefresh && !m_timingViewHeld && projectionDue(m_lastTimingProjectionWallMs, timingProjectionIntervalMs())) {
        refreshTimingRows();
    }
}

bool AppController::timingScopeActive() const {
    return m_overviewPanelActive || m_timingPanelActive;
}

bool AppController::valueScopeActive() const {
    return m_overviewPanelActive || m_valuePanelActive || m_alarmPanelActive;
}

bool AppController::alarmScopeActive() const {
    return m_overviewPanelActive || m_alarmPanelActive;
}

int AppController::timingProjectionIntervalMs() const {
    int interval = m_timingPanelActive ? 240 : 1050;
    const qint64 backlog = pendingLiveFrameCount();
    if (backlog > (m_liveFlushChunk * 2)) interval += 320;
    else if (backlog > m_liveFlushChunk) interval += 160;
    if (projectionBackpressureActive()) interval = std::max(interval, m_timingPanelActive ? 760 : 1480);
    return interval;
}

int AppController::valueProjectionIntervalMs() const {
    int interval = m_valuePanelActive ? 170 : 1850;
    if (m_valuePanelActive && m_livePanelActive) interval = std::max(interval, 220);
    const qint64 backlog = pendingLiveFrameCount();
    if (backlog > (m_liveFlushChunk * 2)) interval += 260;
    else if (backlog > m_liveFlushChunk) interval += 120;
    if (projectionBackpressureActive()) interval = std::max(interval, m_valuePanelActive ? 560 : 2200);
    return interval;
}

int AppController::valueDetailProjectionIntervalMs() const {
    int interval = m_valuePanelActive ? 260 : 1200;
    if (m_valuePanelActive && m_livePanelActive) interval = std::max(interval, 340);
    const qint64 backlog = pendingLiveFrameCount();
    if (backlog > (m_liveFlushChunk * 2)) interval += 220;
    else if (backlog > m_liveFlushChunk) interval += 120;
    if (projectionBackpressureActive()) interval = std::max(interval, m_valuePanelActive ? 620 : 1500);
    return interval;
}

int AppController::alarmProjectionIntervalMs() const {
    int interval = m_alarmPanelActive ? 420 : 2200;
    const qint64 backlog = pendingLiveFrameCount();
    if (backlog > (m_liveFlushChunk * 2)) interval += 480;
    else if (backlog > m_liveFlushChunk) interval += 240;
    if (projectionBackpressureActive()) interval = std::max(interval, m_alarmPanelActive ? 980 : 2600);
    return interval;
}

bool AppController::projectionDue(qint64 lastMs, int minIntervalMs) const {
    if (lastMs < 0 || minIntervalMs <= 0) return true;
    return (QDateTime::currentMSecsSinceEpoch() - lastMs) >= minIntervalMs;
}


bool AppController::projectionBackpressureActive() const {
    const qint64 backlog = pendingLiveFrameCount();
    if (backlog > kLiveProjectionSoftBacklog) return true;
    if (m_transportModeKey == QStringLiteral("typed") && m_lastStats.rxFps1s >= 1200) return true;
    return false;
}

bool AppController::timingStructureSyncAllowed() const {
    if (m_overviewPanelActive || m_timingPanelActive || m_timingReorderRequested) return true;
    if (!projectionBackpressureActive()) return true;
    return projectionDue(m_lastTimingStructureSyncWallMs, 2600);
}

bool AppController::valueStructureSyncAllowed() const {
    if (m_overviewPanelActive || m_valuePanelActive || m_valueReorderRequested) return true;
    if (!projectionBackpressureActive()) return true;
    return projectionDue(m_lastValueStructureSyncWallMs, 2800);
}

bool AppController::alarmStructureSyncAllowed() const {
    if (m_overviewPanelActive || m_alarmPanelActive || m_alarmReorderRequested) return true;
    if (!projectionBackpressureActive()) return true;
    return projectionDue(m_lastAlarmStructureSyncWallMs, 3200);
}

void AppController::syncLiveBusHealthAlarms() {
    auto& groups = m_liveAlarmGroups;
    qint64& sequence = m_liveAlarmSequence;
    const qint64 nowMs = analysisNowMsForSource(QStringLiteral("live"));

    if (m_lastStats.busOff1s > 0) {
        syncAlarmGroup(0, QStringLiteral("bus|bus_off"), QStringLiteral("ERR"), QStringLiteral("bus"),
                       QStringLiteral("버스 health"), QStringLiteral("live"),
                       timeTextForSourceUs(QStringLiteral("live"), m_lastStats.tExtUs > 0 ? m_lastStats.tExtUs : m_liveLatestUs),
                       QStringLiteral("bus-off 감지 · 1s=%1").arg(m_lastStats.busOff1s),
                       QStringLiteral("BO %1").arg(m_lastStats.busOff1s),
                       std::min(100.0, double(m_lastStats.busOff1s) * 25.0),
                       groups, sequence, m_alarmRowsDirty);
        if (!m_busOffAlarmActive) noteBusAlarmEvent(QStringLiteral("live"));
        m_busOffAlarmActive = true;
    } else if (m_busOffAlarmActive) {
        resolveAlarmGroup(QStringLiteral("bus|bus_off"), QStringLiteral("bus-off 복귀"), QStringLiteral("bus"), groups, m_alarmRowsDirty);
        m_busOffAlarmActive = false;
    }

    if (m_lastStats.errPassive1s > 0) {
        syncAlarmGroup(0, QStringLiteral("bus|err_passive"), QStringLiteral("WARN"), QStringLiteral("bus"),
                       QStringLiteral("버스 health"), QStringLiteral("live"),
                       timeTextForSourceUs(QStringLiteral("live"), m_lastStats.tExtUs > 0 ? m_lastStats.tExtUs : m_liveLatestUs),
                       QStringLiteral("err-passive 감지 · 1s=%1").arg(m_lastStats.errPassive1s),
                       QStringLiteral("EP %1").arg(m_lastStats.errPassive1s),
                       std::min(100.0, double(m_lastStats.errPassive1s) * 20.0),
                       groups, sequence, m_alarmRowsDirty);
        if (!m_errPassiveAlarmActive) noteBusAlarmEvent(QStringLiteral("live"));
        m_errPassiveAlarmActive = true;
    } else if (m_errPassiveAlarmActive) {
        resolveAlarmGroup(QStringLiteral("bus|err_passive"), QStringLiteral("err-passive 복귀"), QStringLiteral("bus"), groups, m_alarmRowsDirty);
        m_errPassiveAlarmActive = false;
    }

    if (m_lastStats.droppedTotal > m_lastDroppedTotalObserved) {
        const quint32 delta = m_lastStats.droppedTotal - m_lastDroppedTotalObserved;
        syncAlarmGroup(0, QStringLiteral("bus|drop"), QStringLiteral("WARN"), QStringLiteral("bus"),
                       QStringLiteral("버스 health"), QStringLiteral("live"),
                       timeTextForSourceUs(QStringLiteral("live"), m_lastStats.tExtUs > 0 ? m_lastStats.tExtUs : m_liveLatestUs),
                       QStringLiteral("드롭 증가 +%1 · 총 %2").arg(delta).arg(m_lastStats.droppedTotal),
                       QStringLiteral("+%1").arg(delta),
                       std::min(100.0, double(delta) * 12.5),
                       groups, sequence, m_alarmRowsDirty);
        ++m_liveBusAlarmEventCount;
        m_lastDropBumpMs = nowMs;
        m_dropAlarmActive = true;
    } else if (m_dropAlarmActive && m_lastDropBumpMs >= 0 && (nowMs - m_lastDropBumpMs) > 1800) {
        resolveAlarmGroup(QStringLiteral("bus|drop"), QStringLiteral("최근 드롭 증가 멈춤"), QStringLiteral("bus"), groups, m_alarmRowsDirty);
        m_dropAlarmActive = false;
    }
    m_lastDroppedTotalObserved = m_lastStats.droppedTotal;

    if (m_lastStats.fifoOverflowTotal > m_lastFifoOverflowTotalObserved) {
        const quint32 delta = m_lastStats.fifoOverflowTotal - m_lastFifoOverflowTotalObserved;
        syncAlarmGroup(0, QStringLiteral("bus|fifo"), QStringLiteral("WARN"), QStringLiteral("bus"),
                       QStringLiteral("버스 health"), QStringLiteral("live"),
                       timeTextForSourceUs(QStringLiteral("live"), m_lastStats.tExtUs > 0 ? m_lastStats.tExtUs : m_liveLatestUs),
                       QStringLiteral("FIFO overflow 증가 +%1 · 총 %2").arg(delta).arg(m_lastStats.fifoOverflowTotal),
                       QStringLiteral("+%1").arg(delta),
                       std::min(100.0, double(delta) * 12.5),
                       groups, sequence, m_alarmRowsDirty);
        ++m_liveBusAlarmEventCount;
        m_lastFifoBumpMs = nowMs;
        m_fifoAlarmActive = true;
    } else if (m_fifoAlarmActive && m_lastFifoBumpMs >= 0 && (nowMs - m_lastFifoBumpMs) > 1800) {
        resolveAlarmGroup(QStringLiteral("bus|fifo"), QStringLiteral("최근 FIFO 증가 멈춤"), QStringLiteral("bus"), groups, m_alarmRowsDirty);
        m_fifoAlarmActive = false;
    }
    m_lastFifoOverflowTotalObserved = m_lastStats.fifoOverflowTotal;

    if (m_alarmRowsDirty && !replayAnalysisActive() && !m_alarmViewHeld && alarmScopeActive()) refreshAlarmRows();
}

void AppController::updateNextTimingEvalMs(quint32 id, IdState& state, qint64 nowMs) {
    state.nextTimingEvalMs = std::numeric_limits<qint64>::max();
    if (!state.seen) return;
    if (!shouldTrackTimingForId(id)) return;
    const auto ruleIt = m_rules.constFind(id);
    const RuleSpec* rule = (m_modelEnabled && ruleIt != m_rules.cend()) ? &ruleIt.value() : nullptr;
    if (!rule || !rule->timingEnabled || rule->timingMode.compare(QStringLiteral("monitor"), Qt::CaseInsensitive) == 0) return;

    auto considerDue = [&](double thresholdMs, int targetBucket) {
        if (thresholdMs <= 0.0) return;
        if (state.lastTimingAgeBucket >= targetBucket) return;
        const qint64 dueMs = state.lastLocalSeenMs + qint64(std::ceil(thresholdMs));
        if (dueMs <= nowMs) {
            state.nextTimingEvalMs = nowMs;
        } else {
            state.nextTimingEvalMs = std::min(state.nextTimingEvalMs, dueMs);
        }
    };

    considerDue(rule->ttlWarnMs, 1);
    considerDue(rule->ttlErrMs, 2);
}

bool AppController::analyzeTimingState(quint32 id, IdState& state, const QString& source, qint64 nowMs) {
    if (!state.seen) return false;
    if (!shouldTrackTimingForId(id)) return false;

    const QString prevSeverity = state.lastSeverity;
    const QString prevReason = state.lastReason;
    const int prevAgeBucket = state.lastTimingAgeBucket;
    const int prevEventCount = state.timingEventCount;

    const EvalResult eval = evaluateId(id, &state, nowMs);
    updateTimingHistory(state, id, eval, source, nowMs);
    state.lastSeverity = eval.severity;
    state.lastReason = eval.reason;
    const auto ruleIt = m_rules.constFind(id);
    const RuleSpec* rule = (m_modelEnabled && ruleIt != m_rules.cend()) ? &ruleIt.value() : nullptr;
    const int newAgeBucket = CanMonitorAnalysis::TimingEvaluator::timingAgeBucket(rule, eval.ageMs);
    state.lastTimingAgeBucket = newAgeBucket;
    updateNextTimingEvalMs(id, state, nowMs);

    const bool semanticChanged =
        prevSeverity != eval.severity ||
        prevReason != eval.reason ||
        prevAgeBucket != newAgeBucket ||
        prevEventCount != state.timingEventCount ||
        state.cachedTimingRow.isEmpty();
    state.timingDerivedDirty = semanticChanged;

    if (semanticChanged) {
        const bool selectedValueMatch = m_hasSelectedValueId && m_selectedValueCanId == id;
        if (selectedValueMatch) m_valueDetailsDirty = true;
        if (valueScopeActive() || selectedValueMatch) {
            const bool valueAlarmActive = !state.activeValueAlarmKey.isEmpty();
            if (!valueAlarmActive || state.cachedValueRow.isEmpty()) {
                state.valueDerivedDirty = true;
                m_valueRowsDirty = true;
            }
        }
    }
    return semanticChanged;
}

bool AppController::syncValueAlarmState(quint32 id, IdState& state, const QString& source, bool allowReplayMarkers) {
    const qint64 nowMs = qint64(state.lastBoardSeenUs / 1000ULL);
    const EvalResult eval = evaluateId(id, &state, nowMs);
    const bool selectedValueMatch = (m_hasSelectedValueId && m_selectedValueCanId == id);
    const bool needPreviewCache = selectedValueMatch;
    const bool alarmCapable = hasAlarmCapableSignals(id);

    const quint64 fingerprint = framePayloadFingerprint(state.lastFrame);
    const bool payloadChanged = (state.lastValueFingerprint != fingerprint);
    state.lastValueFingerprint = fingerprint;

    CanMonitorAnalysis::SignalPreviewResult preview;
    if (needPreviewCache) {
        preview = CanMonitorAnalysis::SignalDecoder::makePreview(id, state.lastFrame, m_signalMessages, m_modelEnabled);
        state.cachedPreviewInfo = QVariantMap{{QStringLiteral("plain"), preview.plain}, {QStringLiteral("rich"), preview.rich}};
        state.cachedPreviewFingerprint = fingerprint;
    }

    CanMonitorAnalysis::ValueAlarmResult alarm;
    if (alarmCapable) {
        alarm = CanMonitorAnalysis::SignalDecoder::makeValueAlarm(id, state.lastFrame, m_signalMessages, m_modelEnabled);
        state.cachedValueAlarmInfo = alarm.toVariantMap();
        state.cachedValueAlarmFingerprint = fingerprint;
    } else {
        state.cachedValueAlarmInfo.clear();
        state.cachedValueAlarmFingerprint = 0;
    }

    const bool valueScopeVisible = valueScopeActive() || selectedValueMatch;
    const bool valueSemanticChanged =
        state.lastValueRenderedSeverity != (alarm.active ? alarm.severity : eval.severity) ||
        state.lastValueRenderedReason != (alarm.active ? alarm.message : eval.reason);
    if (valueScopeVisible && (payloadChanged || valueSemanticChanged || state.cachedValueRow.isEmpty())) {
        state.valueDerivedDirty = true;
        m_valueRowsDirty = true;
    }
    if (selectedValueMatch && (payloadChanged || valueSemanticChanged || state.cachedPreviewInfo.isEmpty())) {
        m_valueDetailsDirty = true;
    }

    QVector<CanMonitorAnalysis::AlarmGroup>& alarmGroups = alarmGroupsForSource(source);
    qint64& alarmSequence = (source == QStringLiteral("replay")) ? m_replayAlarmSequence : m_liveAlarmSequence;

    if (alarm.active) {
        const bool newAlarmEvent = state.activeValueAlarmKey.isEmpty() || state.activeValueAlarmKey != alarm.alarmKey;
        if (newAlarmEvent) {
            state.valueAlarmEventCount += 1;
            if (allowReplayMarkers) {
                appendReplayIssueMarker(QStringLiteral("value"), id, alarm.severity, alarm.message);
                appendReplayIssueMarker(QStringLiteral("alarm"), id, alarm.severity, alarm.message);
            }
        }
        CanMonitorAnalysis::AlarmManager::syncValueAlarm(id, alarm, eval.name, source,
                                                        timeTextForSourceUs(source, state.lastBoardSeenUs),
                                                        state.lastLocalSeenMs,
                                                        state.activeValueAlarmKey, state.lastValueAlarmSeenMs,
                                                        alarmGroups, alarmSequence, m_alarmRowsDirty);
    } else {
        const QString previewPlain = needPreviewCache ? preview.plain : QString();
        CanMonitorAnalysis::AlarmManager::resolveValueAlarm(id, previewPlain, state.activeValueAlarmKey, alarmGroups, m_alarmRowsDirty);
    }
    state.lastValueRenderedSeverity = alarm.active ? alarm.severity : eval.severity;
    state.lastValueRenderedReason = alarm.active ? alarm.message : eval.reason;
    return alarm.active;
}

void AppController::advanceReplayHistoryToUs(quint64 frameUs) {
    const qint64 nowMs = qint64(frameUs / 1000ULL);
    bool touched = false;
    for (auto it = m_replayStates.begin(); it != m_replayStates.end(); ++it) {
        IdState& state = it.value();
        if (!state.seen) continue;
        if (!shouldTrackTimingForId(it.key())) continue;
        if (state.nextTimingEvalMs > nowMs) continue;
        touched = analyzeTimingState(it.key(), state, QStringLiteral("replay"), nowMs) || touched;
    }
    if (touched) m_timingRowsDirty = true;
}

void AppController::syncReplayValueAlarm(quint32 id, IdState& state) {
    if (!hasAlarmCapableSignals(id)) {
        if (!state.activeValueAlarmKey.isEmpty()) {
            CanMonitorAnalysis::AlarmManager::resolveValueAlarm(id, QString(), state.activeValueAlarmKey, m_replayAlarmGroups, m_alarmRowsDirty);
        }
        const qint64 nowMs = qint64(state.lastBoardSeenUs / 1000ULL);
        const EvalResult eval = evaluateId(id, &state, nowMs);
        const quint64 fingerprint = framePayloadFingerprint(state.lastFrame);
        const bool payloadChanged = (state.lastValueFingerprint != fingerprint);
        state.lastValueFingerprint = fingerprint;
        state.cachedValueAlarmInfo.clear();
        state.cachedValueAlarmFingerprint = 0;
        const bool selectedValueMatch = m_hasSelectedValueId && m_selectedValueCanId == id;
        const bool valueScopeVisible = valueScopeActive() || selectedValueMatch;
        const bool valueSemanticChanged =
            state.lastValueRenderedSeverity != eval.severity ||
            state.lastValueRenderedReason != eval.reason;
        if (valueScopeVisible && (payloadChanged || valueSemanticChanged || state.cachedValueRow.isEmpty())) {
            state.valueDerivedDirty = true;
            m_valueRowsDirty = true;
        }
        if (selectedValueMatch && (payloadChanged || valueSemanticChanged)) m_valueDetailsDirty = true;
        state.lastValueRenderedSeverity = eval.severity;
        state.lastValueRenderedReason = eval.reason;
        return;
    }
    syncValueAlarmState(id, state, QStringLiteral("replay"), true);
}

bool AppController::rebuildReplayToIndex(int index, const QString& reasonText) {
    if (!m_replayLoaded) return false;
    const auto& frames = m_replay.frames();
    if (frames.empty()) return false;

    cancelReplayRebuild(true);
    if (!m_replayStates.isEmpty() || !m_replayAlarmGroups.isEmpty() || !m_replayTimingIssueMarkers.isEmpty() || !m_replayValueIssueMarkers.isEmpty() || !m_replayAlarmIssueMarkers.isEmpty() || m_replayDisplayedUs > 0) captureReplaySnapshotState();
    else clearReplaySnapshotState();

    const int clamped = std::clamp(index, 0, int(frames.size()) - 1);
    if (!m_replayRebuildActive && m_replayAnalyzedIndex == clamped) {
        m_replay.setCurrentIndex(clamped);
        updateReplayCursor(clamped, m_replay.frameCount(), m_replayDisplayedUs, m_replay.durationUs(), m_replay.frameCount() > 1 ? (double(clamped) / double(m_replay.frameCount() - 1)) : 0.0);
        if (!reasonText.isEmpty()) setStatus(reasonText);
        emit replayStateChanged();
        return true;
    }
    int startIndex = 0;
    bool restoredCheckpoint = false;
    for (int i = m_replayCheckpoints.size() - 1; i >= 0; --i) {
        const ReplayCheckpoint& checkpoint = m_replayCheckpoints[size_t(i)];
        if (checkpoint.index > clamped) continue;
        m_replayStates = checkpoint.states;
        m_replayAlarmGroups = checkpoint.alarmGroups;
        m_replayAlarmSequence = checkpoint.alarmSequence;
        m_replayDisplayedUs = checkpoint.displayedUs;
        m_replayPlayAnchorUs = checkpoint.displayedUs;
        m_replayAnalyzedIndex = checkpoint.index;
        if (m_replayTimingIssueMarkers.size() > checkpoint.timingMarkerCount) m_replayTimingIssueMarkers.resize(checkpoint.timingMarkerCount);
        if (m_replayValueIssueMarkers.size() > checkpoint.valueMarkerCount) m_replayValueIssueMarkers.resize(checkpoint.valueMarkerCount);
        if (m_replayAlarmIssueMarkers.size() > checkpoint.alarmMarkerCount) m_replayAlarmIssueMarkers.resize(checkpoint.alarmMarkerCount);
        startIndex = checkpoint.index + 1;
        restoredCheckpoint = true;
        emit replayIssueMarkersChanged();
        break;
    }

    if (!restoredCheckpoint) {
        m_replayStates.clear();
        m_replayAlarmGroups.clear();
        m_replayAlarmSequence = 0;
        m_replayDisplayedUs = 0;
        m_replayPlayAnchorUs = 0;
        m_replayAnalyzedIndex = -1;
        clearReplayIssueMarkers();
    }

    markAllAnalysisDirty(true);
    if (clamped == 0 && !restoredCheckpoint) maybeStoreReplayCheckpoint(0);

    m_replayRebuildActive = true;
    m_replayRebuildTargetIndex = clamped;
    m_replayRebuildNextIndex = startIndex;
    m_replayRebuildViewStart = std::max(0, clamped - 699);
    m_replayRebuildReason = reasonText.isEmpty()
        ? QStringLiteral("재생 지점 이동: %1 / %2").arg(clamped + 1).arg(frames.size())
        : reasonText;

    const int remaining = std::max(0, clamped - startIndex + 1);
    const int instantThreshold = std::max(64, replayCheckpointStride() / 2);

    if (m_replayPlaying) {
        m_replayRebuildChunk = 512;
        m_replayRebuildMinChunk = 96;
        m_replayRebuildBudgetMs = 6;
    } else if (remaining <= 1200) {
        m_replayRebuildChunk = 896;
        m_replayRebuildMinChunk = 128;
        m_replayRebuildBudgetMs = 8;
    } else if (remaining <= 6000) {
        m_replayRebuildChunk = 1536;
        m_replayRebuildMinChunk = 192;
        m_replayRebuildBudgetMs = 10;
    } else {
        m_replayRebuildChunk = 2048;
        m_replayRebuildMinChunk = 256;
        m_replayRebuildBudgetMs = 11;
    }

    setStatus(QStringLiteral("재생 분석 재구성 준비: %1 / %2").arg(std::max(0, startIndex)).arg(clamped + 1));
    emit replayStateChanged();

    if (remaining <= instantThreshold) {
        processReplayRebuildStep();
    } else {
        m_replayRebuildTimer.start(0);
    }
    return true;
}

bool AppController::jumpReplayToIndex(int index, const QString& reasonText) {
    if (!m_replayLoaded) return false;
    clearReplaySeekState();
    const auto& frames = m_replay.frames();
    if (frames.empty()) return false;
    const int clamped = std::clamp(index, 0, int(frames.size()) - 1);

    setReplayAnalysisHeld(true);
    setReplayPlaying(false);
    m_replay.pause();
    clearGraphHistory(QStringLiteral("replay"));
    m_replay.setCurrentIndex(clamped);
    return rebuildReplayToIndex(clamped,
        reasonText.isEmpty() ? QStringLiteral("재생 지점 이동: %1 / %2").arg(clamped + 1).arg(frames.size()) : reasonText);
}

QString AppController::rulesSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제");
    if (m_rules.isEmpty()) return QStringLiteral("모델 기준 미로드");
    return QStringLiteral("기준 %1건 로드").arg(m_rules.size());
}

QString AppController::rulesSourceSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제");
    if (m_rules.isEmpty()) return QStringLiteral("모델 기준 미로드");
    return m_rulesActiveSource.isEmpty() ? QStringLiteral("모델 소스 미상") : m_rulesActiveSource;
}

QString AppController::modelSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제");
    const QString head = m_modelMeta.modelName.trimmed().isEmpty() ? QStringLiteral("모델") : m_modelMeta.modelName.trimmed();
    QStringList meta;
    if (!m_modelMeta.vendor.trimmed().isEmpty()) meta << m_modelMeta.vendor.trimmed();
    if (!m_modelMeta.modelVersion.trimmed().isEmpty()) meta << m_modelMeta.modelVersion.trimmed();
    const QString metaText = meta.isEmpty() ? QString() : QStringLiteral(" · %1").arg(meta.join(QStringLiteral(" / ")));
    return QStringLiteral("%1%2 · 기준 %3건 / 해석 ID %4건").arg(head, metaText).arg(m_rules.size()).arg(m_signalMessages.size());
}

QString AppController::modelSourceSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제");
    return rulesSourceSummary();
}

QString AppController::signalDbSummary() const {
    if (!m_modelEnabled) return QStringLiteral("시그널 해석 해제");
    if (m_signalMessages.isEmpty()) return QStringLiteral("시그널 DB 미로드");
    const QString name = m_modelMeta.modelName.isEmpty() ? QStringLiteral("시그널") : m_modelMeta.modelName;
    QString metaText;
    if (!m_modelMeta.modelVersion.trimmed().isEmpty()) metaText = QStringLiteral(" · %1").arg(m_modelMeta.modelVersion.trimmed());
    return QStringLiteral("%1%2 · %3 ID / %4 신호").arg(name, metaText).arg(m_signalMessages.size()).arg([this]() { int total = 0; for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) total += it.value().signalSpecs.size(); return total; }());
}

int AppController::countIssueRows(const StableMapListModel& model) const {
    int count = 0;
    for (int i = 0; i < model.rowCount(); ++i) {
        const QVariantMap row = model.get(i);
        const QString severity = row.value(QStringLiteral("severity")).toString();
        const bool active = row.value(QStringLiteral("active")).toBool();
        if (active || severity == QStringLiteral("WARN") || severity == QStringLiteral("ERR")) ++count;
    }
    return count;
}

QString AppController::makeTopSummary(const StableMapListModel& model, const QString& textField) const {
    QStringList lines;
    const int limit = std::min(model.rowCount(), 3);
    for (int i = 0; i < limit; ++i) {
        const QVariantMap row = model.get(i);
        const QString id = row.value(QStringLiteral("idText")).toString();
        const QString severity = row.value(QStringLiteral("severity")).toString();
        const QString body = row.value(textField).toString().trimmed();
        if (id.isEmpty() && body.isEmpty()) continue;
        const QString sevPart = severity.isEmpty() ? QString() : QStringLiteral("[%1] ").arg(severity);
        lines << QStringLiteral("%1%2 %3").arg(sevPart, id, body).trimmed();
    }
    return lines.isEmpty() ? QStringLiteral("표시 항목 없음") : lines.join(QStringLiteral("\n"));
}

int AppController::timingIssueCount() const { return m_timingSummaryCache.level.activeCount; }
int AppController::valueIssueCount() const { return m_valueSummaryCache.level.activeCount; }
int AppController::activeAlarmCount() const { return m_alarmSummaryCache.level.activeCount; }
int AppController::replayObservedIdCount() const { return replaySnapshotObservedIdCount(); }
int AppController::replayTimingMarkerCount() const { return replaySnapshotMarkersForKind(QStringLiteral("timing")).size(); }
int AppController::replayValueMarkerCount() const { return replaySnapshotMarkersForKind(QStringLiteral("value")).size(); }
int AppController::replayAlarmMarkerCount() const { return replaySnapshotMarkersForKind(QStringLiteral("alarm")).size(); }
int AppController::timingCumulativeCount() const { return cumulativeTimingCountFor(replayAnalysisActive() ? replaySnapshotStateMap() : activeStateMap()); }
int AppController::valueCumulativeCount() const { return cumulativeValueAlarmCountFor(replayAnalysisActive() ? replaySnapshotStateMap() : activeStateMap()); }
int AppController::alarmCumulativeCount() const {
    const QString sourceKey = activeAnalysisSourceKey();
    const int busCount = (sourceKey == QStringLiteral("replay")) ? m_replayBusAlarmEventCount : m_liveBusAlarmEventCount;
    return valueCumulativeCount() + busCount;
}
int AppController::timingWarnCount() const { return m_timingSummaryCache.warnCount; }
int AppController::timingErrCount() const { return m_timingSummaryCache.errCount; }
int AppController::valueWarnCount() const { return m_valueSummaryCache.warnCount; }
int AppController::valueErrCount() const { return m_valueSummaryCache.errCount; }
int AppController::alarmWarnCount() const { return m_alarmSummaryCache.warnCount; }
int AppController::alarmErrCount() const { return m_alarmSummaryCache.errCount; }
QString AppController::timingLevel() const { return m_timingSummaryCache.level.level; }
QString AppController::valueLevel() const { return m_valueSummaryCache.level.level; }
QString AppController::alarmLevel() const { return m_alarmSummaryCache.level.level; }
QString AppController::systemLevel() const { return m_systemSummaryCache.level; }
QString AppController::topTimingSummary() const { return m_timingSummaryCache.level.summary; }
QString AppController::topValueSummary() const { return m_valueSummaryCache.level.summary; }
QString AppController::topAlarmSummary() const { return m_alarmSummaryCache.level.summary; }
QString AppController::topTimingId() const { return m_timingSummaryCache.topId; }
QString AppController::topValueId() const { return m_valueSummaryCache.topId; }
QString AppController::topAlarmId() const { return m_alarmSummaryCache.topId; }

QString AppController::viewStateSummaryFor(const AnalysisViewState& state) const {
    QStringList filters;
    if (!state.timingFilterId.isEmpty()) filters << QStringLiteral("주기ID %1").arg(state.timingFilterId);
    if (!state.timingFilterSeverity.isEmpty()) filters << QStringLiteral("주기상태 %1").arg(state.timingFilterSeverity);
    if (!state.timingFilterName.isEmpty()) filters << QStringLiteral("주기이름 %1").arg(state.timingFilterName);
    if (!state.timingFilterReason.isEmpty()) filters << QStringLiteral("주기사유 %1").arg(state.timingFilterReason);
    if (!state.valueFilterId.isEmpty()) filters << QStringLiteral("값ID %1").arg(state.valueFilterId);
    if (!state.valueFilterSeverity.isEmpty()) filters << QStringLiteral("값상태 %1").arg(state.valueFilterSeverity);
    if (!state.valueFilterName.isEmpty()) filters << QStringLiteral("값이름 %1").arg(state.valueFilterName);
    if (!state.valueFilterRaw.isEmpty()) filters << QStringLiteral("RAW %1").arg(state.valueFilterRaw);
    if (!state.valueFilterReason.isEmpty()) filters << QStringLiteral("해석 %1").arg(state.valueFilterReason);
    if (!state.alarmFilterId.isEmpty()) filters << QStringLiteral("경보ID %1").arg(state.alarmFilterId);
    if (!state.alarmFilterSeverity.isEmpty()) filters << QStringLiteral("경보상태 %1").arg(state.alarmFilterSeverity);
    if (!state.alarmFilterName.isEmpty()) filters << QStringLiteral("경보이름 %1").arg(state.alarmFilterName);
    if (!state.alarmFilterMessage.isEmpty()) filters << QStringLiteral("경보메시지 %1").arg(state.alarmFilterMessage);
    if (!state.alarmFilterText.isEmpty()) filters << QStringLiteral("경보통합 %1").arg(state.alarmFilterText);
    if (filters.isEmpty()) filters << QStringLiteral("분석 필터 없음");

    QString selectionText = QStringLiteral("선택 ID 없음");
    if (state.hasSelectedValueId) selectionText = QStringLiteral("상세 선택 %1").arg(idText(state.selectedValueCanId));
    return QStringLiteral("%1 · %2").arg(joinNonEmpty(filters), selectionText);
}

QJsonObject AppController::viewStateToJson(const AnalysisViewState& state) {
    QJsonObject out;
    out.insert(QStringLiteral("timing_id"), state.timingFilterId);
    out.insert(QStringLiteral("timing_severity"), state.timingFilterSeverity);
    out.insert(QStringLiteral("timing_name"), state.timingFilterName);
    out.insert(QStringLiteral("timing_reason"), state.timingFilterReason);
    out.insert(QStringLiteral("timing_expected"), state.timingFilterExpected);
    out.insert(QStringLiteral("timing_gap"), state.timingFilterGap);
    out.insert(QStringLiteral("timing_age"), state.timingFilterAge);
    out.insert(QStringLiteral("timing_source"), state.timingFilterSource);
    out.insert(QStringLiteral("value_id"), state.valueFilterId);
    out.insert(QStringLiteral("value_severity"), state.valueFilterSeverity);
    out.insert(QStringLiteral("value_name"), state.valueFilterName);
    out.insert(QStringLiteral("value_source"), state.valueFilterSource);
    out.insert(QStringLiteral("value_raw"), state.valueFilterRaw);
    out.insert(QStringLiteral("value_gap"), state.valueFilterGap);
    out.insert(QStringLiteral("value_reason"), state.valueFilterReason);
    out.insert(QStringLiteral("alarm_id"), state.alarmFilterId);
    out.insert(QStringLiteral("alarm_severity"), state.alarmFilterSeverity);
    out.insert(QStringLiteral("alarm_time"), state.alarmFilterTime);
    out.insert(QStringLiteral("alarm_name"), state.alarmFilterName);
    out.insert(QStringLiteral("alarm_source"), state.alarmFilterSource);
    out.insert(QStringLiteral("alarm_message"), state.alarmFilterMessage);
    out.insert(QStringLiteral("alarm_text"), state.alarmFilterText);
    out.insert(QStringLiteral("has_selected_value_id"), state.hasSelectedValueId);
    out.insert(QStringLiteral("selected_value_id"), state.hasSelectedValueId ? idText(state.selectedValueCanId) : QString());
    return out;
}

int AppController::cumulativeTimingCountFor(const QHash<quint32, IdState>& states) const {
    int total = 0;
    for (auto it = states.cbegin(); it != states.cend(); ++it) total += std::max(0, it.value().timingEventCount);
    return total;
}

int AppController::cumulativeValueAlarmCountFor(const QHash<quint32, IdState>& states) const {
    int total = 0;
    for (auto it = states.cbegin(); it != states.cend(); ++it) total += std::max(0, it.value().valueAlarmEventCount);
    return total;
}

bool AppController::shouldTrackTimingForId(quint32 id) const {
    if (!m_modelEnabled) return true;
    return m_rules.contains(id);
}

bool AppController::hasAlarmCapableSignals(quint32 id) const {
    return m_alarmCapableSignalIds.contains(id);
}

void AppController::noteBusAlarmEvent(const QString& source) {
    if (source == QStringLiteral("replay")) ++m_replayBusAlarmEventCount;
    else ++m_liveBusAlarmEventCount;
}

QString AppController::analysisContextText() const {
    const QString sourceKey = activeAnalysisSourceKey();
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    QString inputText;
    if (!m_connected) inputText = QStringLiteral("수신 source: live 미연결");
    else if (m_lastLiveFrameWallMs > 0 && (nowWallMs - m_lastLiveFrameWallMs) <= 1200) inputText = QStringLiteral("수신 source: live frame 수신 중 · 마지막 frame %1 전").arg(fmtWallAge(nowWallMs - m_lastLiveFrameWallMs));
    else if (m_lastLiveStatsWallMs > 0 && (nowWallMs - m_lastLiveStatsWallMs) <= 2000) inputText = QStringLiteral("수신 source: stats 최근 수신 · 마지막 stats %1 전 / frame 지연").arg(fmtWallAge(nowWallMs - m_lastLiveStatsWallMs));
    else inputText = QStringLiteral("수신 source: 포트 연결 상태지만 최근 frame 없음");

    QString displayText = sourceKey == QStringLiteral("replay") ? QStringLiteral("표시 source: replay frame/analysis") : QStringLiteral("표시 source: live frame/analysis");
    if (m_replayLoaded && sourceKey == QStringLiteral("replay") && m_connected && !m_replayPlaying) {
        displayText += QStringLiteral(" · 라이브 수신은 계속되지만 분석은 replay 고정");
    }

    QStringList parts;
    parts << inputText;
    parts << QStringLiteral("분석 source: %1").arg(analysisSourceText());
    parts << displayText;
    parts << QStringLiteral("관찰 ID L/R %1/%2").arg(liveObservedIdCount()).arg(replayObservedIdCount());
    parts << QStringLiteral("카운트 활성 T/V/A %1/%2/%3 · 누적 T/V/A %4/%5/%6")
                .arg(timingIssueCount()).arg(valueIssueCount()).arg(activeAlarmCount())
                .arg(timingCumulativeCount()).arg(valueCumulativeCount()).arg(alarmCumulativeCount());
    parts << QStringLiteral("표시 행 T/V/A %1/%2/%3").arg(m_timingModel.count()).arg(m_valueModel.count()).arg(m_alarmModel.count());
    parts << QStringLiteral("현재 컨텍스트 %1").arg(viewStateSummaryFor(viewStateForSource(sourceKey)));
    if (pendingLiveFrameCount() > 0) parts << QStringLiteral("live 큐 %1프레임").arg(pendingLiveFrameCount());
    if (m_liveProjectionWorkerSampledFrames > 0) parts << QStringLiteral("live projection 샘플링 누적 %1프레임").arg(m_liveProjectionWorkerSampledFrames);
    if (m_liveProjectionSampledControlEvidenceRecords > 0) parts << QStringLiteral("control evidence projection 샘플링 누적 %1건").arg(m_liveProjectionSampledControlEvidenceRecords);
    if (m_liveSampledViewDrops > 0) parts << QStringLiteral("live 표시 샘플링 누적 %1프레임").arg(m_liveSampledViewDrops);
    const quint64 projectionDrops = m_liveProjectionDroppedFrames + m_liveProjectionWorkerDroppedFrames;
    if (projectionDrops > 0) parts << QStringLiteral("live projection drop 누적 %1프레임").arg(projectionDrops);
    if (m_replayRebuildActive) parts << QStringLiteral("replay 재구성 중 · 표시 스냅샷 %1 / 목표 %2").arg(std::max(0, m_replaySnapshotAnalyzedIndex + 1)).arg(std::max(0, m_replayRebuildTargetIndex + 1));
    if (m_replayLoaded) parts << QStringLiteral("재생 마커 %1").arg(replayIssueSummary());
    if (sourceKey == QStringLiteral("replay")) parts << replayCursorSummary();
    else if (m_liveUiPaused) parts << QStringLiteral("라이브 화면 정지 고정");
    return joinNonEmpty(parts);
}

QString AppController::activeViewStateSummary() const {
    const QString sourceKey = activeAnalysisSourceKey();
    const FrameFilterProxyModel& frameView = sourceKey == QStringLiteral("replay") ? m_replayFrameView : m_liveFrameView;
    const QString frameFilter = frameView.idFilter();
    const QString busPart = frameView.busFilter() < 0
        ? QStringLiteral("전체 버스")
        : QStringLiteral("버스 %1").arg(frameView.busFilter());
    const QString framePart = frameFilter.isEmpty() ? QStringLiteral("프레임 필터 없음") : QStringLiteral("프레임 필터 %1").arg(frameFilter);
    return QStringLiteral("%1 · %2 · %3").arg(busPart, framePart, viewStateSummaryFor(viewStateForSource(sourceKey)));
}

QString AppController::replayCursorSummary() const {
    if (!m_replayLoaded) return QStringLiteral("재생 없음");
    const int current = m_replayFrameCount > 0 ? std::max(0, m_replayCurrentIndex + 1) : 0;
    QString stateText = m_replayPlaying ? QStringLiteral("재생 중") : (m_replayAnalysisHeld ? QStringLiteral("정지 고정") : QStringLiteral("로드 대기"));
    if (m_replayRebuildActive) {
        stateText = QStringLiteral("재구성 중 · 표시 스냅샷 %1 / 목표 %2").arg(std::max(0, m_replaySnapshotAnalyzedIndex + 1)).arg(std::max(0, m_replayRebuildTargetIndex + 1));
    }
    return QStringLiteral("%1 · 프레임 %2/%3 · %4 / %5 · %6 %% · 활성 T/V/A %7/%8/%9 · 누적 T/V/A %10/%11/%12 · 마커 T/V/A %13/%14/%15")
        .arg(stateText)
        .arg(current)
        .arg(m_replayFrameCount)
        .arg(replayCurrentTimeText(), replayDurationText())
        .arg(QString::number(m_replayProgress * 100.0, 'f', 1))
        .arg(timingIssueCount()).arg(valueIssueCount()).arg(activeAlarmCount())
        .arg(timingCumulativeCount()).arg(valueCumulativeCount()).arg(alarmCumulativeCount())
        .arg(replayTimingMarkerCount()).arg(replayValueMarkerCount()).arg(replayAlarmMarkerCount());
}

QString AppController::replaySnapshotSummary() const {
    if (!m_replayLoaded) return QStringLiteral("재생 스냅샷 없음");
    if (!m_replayRebuildActive) {
        return QStringLiteral("현재 표시/카운트/히스토리는 프레임 %1 기준 스냅샷").arg(std::max(0, m_replayCurrentIndex + 1));
    }
    const int shown = std::max(0, m_replaySnapshotAnalyzedIndex + 1);
    const int target = std::max(0, m_replayRebuildTargetIndex + 1);
    return QStringLiteral("재구성 완료 전까지 모든 탭은 프레임 %1 스냅샷을 유지하고, 완료 시 프레임 %2 기준 실제 누적 이력으로 일괄 전환").arg(shown).arg(target);
}

QVariantList AppController::replayIssueMarkers() const {
    QVariantList merged;
    if (!m_replayLoaded || m_replayFrameCount <= 0) return merged;

    QVector<ReplayIssueMarker> all;
    const auto& timingMarkers = replaySnapshotMarkersForKind(QStringLiteral("timing"));
    const auto& valueMarkers = replaySnapshotMarkersForKind(QStringLiteral("value"));
    const auto& alarmMarkers = replaySnapshotMarkersForKind(QStringLiteral("alarm"));
    all.reserve(timingMarkers.size() + valueMarkers.size() + alarmMarkers.size());
    all += timingMarkers;
    all += valueMarkers;
    all += alarmMarkers;
    if (all.isEmpty()) return merged;

    std::sort(all.begin(), all.end(), [](const ReplayIssueMarker& a, const ReplayIssueMarker& b) {
        if (a.index != b.index) return a.index < b.index;
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.id != b.id) return a.id < b.id;
        return a.note < b.note;
    });

    const int maxMarkers = 180;
    const int step = std::max(1, int(std::ceil(double(all.size()) / double(maxMarkers))));
    for (int i = 0; i < all.size(); ++i) {
        const ReplayIssueMarker& marker = all[size_t(i)];
        const bool mustKeep = (i == 0 || i == all.size() - 1 || marker.severity == QStringLiteral("ERR"));
        if (!mustKeep && step > 1 && (i % step) != 0) continue;
        QVariantMap row;
        row.insert(QStringLiteral("kind"), marker.kind);
        row.insert(QStringLiteral("severity"), marker.severity);
        row.insert(QStringLiteral("index"), marker.index);
        row.insert(QStringLiteral("idText"), idText(marker.id));
        row.insert(QStringLiteral("note"), marker.note);
        row.insert(QStringLiteral("timeText"), timeTextForSourceUs(QStringLiteral("replay"), marker.frameUs));
        row.insert(QStringLiteral("progress"), m_replayFrameCount > 1 ? double(marker.index) / double(m_replayFrameCount - 1) : 0.0);
        merged.push_back(row);
    }
    return merged;
}

QString AppController::replayIssueSummary() const {
    if (!m_replayLoaded) return QStringLiteral("재생 이슈 마커 없음");
    return QStringLiteral("주기 %1 · 값 %2 · 경보 %3").arg(replayTimingMarkerCount()).arg(replayValueMarkerCount()).arg(replayAlarmMarkerCount());
}


namespace {
QString formatElapsedCompact(qint64 ms) {
    if (ms < 0) return QStringLiteral("-");
    qint64 sec = ms / 1000;
    const qint64 day = sec / 86400; sec %= 86400;
    const qint64 hour = sec / 3600; sec %= 3600;
    const qint64 min = sec / 60; sec %= 60;
    if (day > 0) return QStringLiteral("%1d %2h").arg(day).arg(hour);
    if (hour > 0) return QStringLiteral("%1h %2m").arg(hour).arg(min);
    if (min > 0) return QStringLiteral("%1m %2s").arg(min).arg(sec);
    return QStringLiteral("%1s").arg(sec);
}
}

QString AppController::sessionUptimeText() const {
    if (!m_uptime.isValid()) return QStringLiteral("-");
    return formatElapsedCompact(m_uptime.elapsed());
}

QVariantList AppController::operatorRecentEvents() const {
    QVariantList out;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    out.reserve(m_recentOperatorEvents.size());
    for (const RecentOperatorEvent& ev : m_recentOperatorEvents) {
        QVariantMap row;
        row.insert(QStringLiteral("category"), ev.category);
        row.insert(QStringLiteral("level"), ev.level);
        row.insert(QStringLiteral("summary"), ev.summary);
        row.insert(QStringLiteral("detail"), ev.detail);
        row.insert(QStringLiteral("timeText"), QDateTime::fromMSecsSinceEpoch(ev.wallMs).toString(QStringLiteral("HH:mm:ss")));
        row.insert(QStringLiteral("ageText"), formatElapsedCompact(std::max<qint64>(0, nowMs - ev.wallMs)));
        out.push_back(row);
    }
    return out;
}

QString AppController::operatorRecentSummary() const {
    if (m_recentOperatorEvents.isEmpty()) return QStringLiteral("최근 상태 변화 없음");
    const RecentOperatorEvent& ev = m_recentOperatorEvents.constFirst();
    return QStringLiteral("[%1] %2").arg(ev.category, ev.summary);
}

void AppController::pushOperatorRecentEvent(const QString& category, const QString& level, const QString& summary, const QString& detail) {
    const QString trimmedSummary = summary.trimmed();
    if (trimmedSummary.isEmpty()) return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_recentOperatorEvents.isEmpty()) {
        const RecentOperatorEvent& last = m_recentOperatorEvents.constFirst();
        if (last.category == category && last.level == level && last.summary == trimmedSummary && last.detail == detail) {
            return;
        }
    }
    RecentOperatorEvent ev;
    ev.wallMs = nowMs;
    ev.category = category;
    ev.level = level;
    ev.summary = trimmedSummary;
    ev.detail = detail.trimmed();
    m_recentOperatorEvents.prepend(ev);
    while (m_recentOperatorEvents.size() > 18) m_recentOperatorEvents.removeLast();
    emit operatorRecentEventsChanged();
}

void AppController::updateOperatorRecentEventsLocked() {
    const QString sourceKey = sourceStateLevel() + QStringLiteral("|") + sourceStateText();
    const QString analysisKey = analysisModeLevel() + QStringLiteral("|") + analysisModeText();
    const QString busKey = busHealthLevel() + QStringLiteral("|") + busHealthText();
    const QString actionKey = operatorActionLevel() + QStringLiteral("|") + primaryIssueKind() + QStringLiteral("|") + primaryIssueId() + QStringLiteral("|") + operatorActionText();
    const QString logKey = loggingStateLevel() + QStringLiteral("|") + loggingStateText();
    const QString replayKey = QStringLiteral("%1|%2|%3|%4")
        .arg(m_replayLoaded ? QStringLiteral("loaded") : QStringLiteral("none"),
             m_replayPlaying ? QStringLiteral("playing") : QStringLiteral("stopped"),
             m_replayRebuildActive ? QStringLiteral("rebuild") : QStringLiteral("steady"),
             m_replayAnalysisHeld ? QStringLiteral("held") : QStringLiteral("follow"));
    const QString modelKey = QStringLiteral("%1|%2|%3")
        .arg(m_modelEnabled ? QStringLiteral("on") : QStringLiteral("off"),
             modelActive() ? modelName() : QStringLiteral("모델 해제"),
             modelDiagnosticsLevel());

    if (m_recentSourceKey != sourceKey) {
        m_recentSourceKey = sourceKey;
        pushOperatorRecentEvent(QStringLiteral("INPUT"), sourceStateLevel(), sourceStateText(), analysisSourceText());
    }
    if (m_recentAnalysisKey != analysisKey) {
        m_recentAnalysisKey = analysisKey;
        pushOperatorRecentEvent(QStringLiteral("ANALYSIS"), analysisModeLevel(), analysisModeText(), replaySnapshotSummary());
    }
    if (m_recentBusKey != busKey) {
        m_recentBusKey = busKey;
        pushOperatorRecentEvent(QStringLiteral("BUS"), busHealthLevel(), busHealthText(), liveStatsSummary());
    }
    if (m_recentActionKey != actionKey) {
        m_recentActionKey = actionKey;
        const QString detail = primaryIssueId().isEmpty() ? operatorActionText() : (primaryIssueId() + QStringLiteral(" · ") + operatorActionText());
        pushOperatorRecentEvent(QStringLiteral("ISSUE"), operatorActionLevel(), primaryIssueSummary(), detail);
    }
    if (m_recentLogKey != logKey) {
        m_recentLogKey = logKey;
        pushOperatorRecentEvent(QStringLiteral("LOG"), loggingStateLevel(), loggingStateText(), logActionText());
    }
    if (m_recentReplayKey != replayKey) {
        m_recentReplayKey = replayKey;
        pushOperatorRecentEvent(QStringLiteral("REPLAY"), m_replayLoaded ? (m_replayPlaying ? QStringLiteral("OK") : QStringLiteral("INFO")) : QStringLiteral("INFO"), replayCursorSummary(), replayIssueSummary());
    }
    if (m_recentModelKey != modelKey) {
        m_recentModelKey = modelKey;
        pushOperatorRecentEvent(QStringLiteral("MODEL"), modelDiagnosticsLevel(), modelActive() ? modelName() : QStringLiteral("모델 해제"), modelDiagnosticsSummary());
    }
}

QString AppController::sessionSummary() const {
    const QString modelPart = modelActive() ? modelName() : QStringLiteral("모델 해제");
    const QString linkPart = connected() ? QStringLiteral("포트 연결") : QStringLiteral("포트 미연결");
    const QString replayPart = replayPlaying() ? QStringLiteral("재생 중") : (replayLoaded() ? QStringLiteral("재생 로드") : QStringLiteral("재생 없음"));
    const QString analysisPart = analysisSourceText();
    const QString levelPart = QStringLiteral("SYS:%1 / T:%2 / V:%3 / A:%4").arg(systemLevel(), timingLevel(), valueLevel(), alarmLevel());
    const QString statsPart = QStringLiteral("RX/TX %1/%2 · DROP %3 · FIFO %4").arg(liveRxFps()).arg(liveTxFps()).arg(droppedTotal()).arg(fifoOverflowTotal());
    const QString idPart = QStringLiteral("관찰 ID L/R %1/%2").arg(liveObservedIdCount()).arg(replayObservedIdCount());
    const QString countPart = QStringLiteral("활성 T/V/A %1/%2/%3 · 누적 %4/%5/%6").arg(timingIssueCount()).arg(valueIssueCount()).arg(activeAlarmCount()).arg(timingCumulativeCount()).arg(valueCumulativeCount()).arg(alarmCumulativeCount());
    const QString framePart = QStringLiteral("프레임 L/R %1/%2").arg(m_liveFrames.count()).arg(m_replayFrames.count());
    return joinNonEmpty({modelPart, linkPart, QStringLiteral("%1 · %2").arg(replayPart, analysisPart), levelPart, statsPart, idPart, countPart, framePart});
}

QString AppController::modelModeText() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제 · 주기 관찰 + RAW 표시");
    if (!rulesLoaded() && !signalDbLoaded()) return QStringLiteral("모델 비어 있음");
    if (rulesLoaded() && signalDbLoaded()) return QStringLiteral("주기 기준 + 값 해석 모두 활성");
    if (rulesLoaded()) return QStringLiteral("주기 기준 전용 · 값 해석 일부/없음");
    if (signalDbLoaded()) return QStringLiteral("값 해석 전용 · 주기 기준 없음");
    return QStringLiteral("부분 모델");
}

QString AppController::busHealthLevel() const {
    if (m_lastStats.busOff1s > 0) return QStringLiteral("ERR");
    if (m_lastStats.errPassive1s > 0 || m_dropAlarmActive || m_fifoAlarmActive) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

QString AppController::busHealthText() const {
    QStringList parts;
    if (m_lastStats.busOff1s > 0) parts << QStringLiteral("bus-off 1s=%1").arg(m_lastStats.busOff1s);
    if (m_lastStats.errPassive1s > 0) parts << QStringLiteral("err-passive 1s=%1").arg(m_lastStats.errPassive1s);
    if (m_dropAlarmActive) parts << QStringLiteral("드롭 증가 감지");
    if (m_fifoAlarmActive) parts << QStringLiteral("FIFO overflow 증가 감지");
    if (parts.isEmpty()) {
        return QStringLiteral("RX %1 / TX %2 · DROP %3 · FIFO %4 · EP %5 · BO %6")
            .arg(liveRxFps()).arg(liveTxFps()).arg(droppedTotal()).arg(fifoOverflowTotal()).arg(errPassiveCount()).arg(busOffCount());
    }
    return parts.join(QStringLiteral(" · "));
}

QString AppController::analysisReliabilityText() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제 · RAW/주기 관찰은 가능하지만 값 의미 해석 신뢰도는 제한됨");
    if (!rulesLoaded() && signalDbLoaded()) return QStringLiteral("값 해석만 가능 · 주기 기준/TTL 판단은 제한됨");
    if (rulesLoaded() && !signalDbLoaded()) return QStringLiteral("주기/TTL은 가능 · 값 의미/범위 근거는 제한됨");
    if (busHealthLevel() == QStringLiteral("ERR")) return QStringLiteral("버스 health ERR · 값 이상과 통신 이상을 분리해서 봐야 함");
    if (busHealthLevel() == QStringLiteral("WARN")) return QStringLiteral("버스 health WARN · drop/FIFO/err-passive 영향 가능");
    if (replayAnalysisActive()) {
        return m_replayAnalysisHeld
            ? QStringLiteral("재생 정지 고정 · 현재 프레임까지 누적된 해석 상태를 유지 중")
            : QStringLiteral("재생 기반 분석 · 현재 재생 구간 누적 상태 기준");
    }
    if (m_liveUiPaused) return QStringLiteral("라이브 정지 고정 · 새 수신은 멈추고 현재 근거를 유지 중");
    return QStringLiteral("모델/버스 상태가 양호해 현재 해석 근거가 비교적 안정적");
}

QString AppController::rootCauseSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제 · RAW/주기 관찰 우선, 값 의미 판단은 보류");
    if (busHealthLevel() == QStringLiteral("ERR")) {
        return QStringLiteral("최상위 원인: 버스 health ERR · %1").arg(busHealthText());
    }
    if (busHealthLevel() == QStringLiteral("WARN")) {
        if (m_alarmSummaryCache.level.level == QStringLiteral("ERR") || m_alarmSummaryCache.level.level == QStringLiteral("WARN")) {
            return QStringLiteral("버스 흔들림 우선 확인 후 경보 해석: %1").arg(topAlarmSummary().split(QStringLiteral("\n")).value(0));
        }
        return QStringLiteral("최상위 원인 후보: 버스 health WARN · %1").arg(busHealthText());
    }
    if (m_alarmSummaryCache.level.level == QStringLiteral("ERR") || m_alarmSummaryCache.level.level == QStringLiteral("WARN")) {
        return QStringLiteral("현재 활성 경보 우선: %1").arg(topAlarmSummary().split(QStringLiteral("\n")).value(0));
    }
    if (m_valueSummaryCache.level.level == QStringLiteral("ERR") || m_valueSummaryCache.level.level == QStringLiteral("WARN")) {
        return QStringLiteral("값 이상 우선: %1").arg(topValueSummary().split(QStringLiteral("\n")).value(0));
    }
    if (m_timingSummaryCache.level.level == QStringLiteral("ERR") || m_timingSummaryCache.level.level == QStringLiteral("WARN")) {
        return QStringLiteral("주기 이슈 우선: %1").arg(topTimingSummary().split(QStringLiteral("\n")).value(0));
    }
    if (replayAnalysisActive()) return QStringLiteral("재생 기준으로 현재 프레임까지는 뚜렷한 최상위 이슈 없음");
    return QStringLiteral("현재 최상위 이슈 없음");
}

QString AppController::primaryIssueKind() const {
    if (!m_modelEnabled) return QStringLiteral("setup");
    if (busHealthLevel() == QStringLiteral("ERR") || busHealthLevel() == QStringLiteral("WARN")) return QStringLiteral("bus");
    if (m_alarmSummaryCache.level.level == QStringLiteral("ERR") || m_alarmSummaryCache.level.level == QStringLiteral("WARN")) return QStringLiteral("alarm");
    if (m_valueSummaryCache.level.level == QStringLiteral("ERR") || m_valueSummaryCache.level.level == QStringLiteral("WARN")) return QStringLiteral("value");
    if (m_timingSummaryCache.level.level == QStringLiteral("ERR") || m_timingSummaryCache.level.level == QStringLiteral("WARN")) return QStringLiteral("timing");
    if (!connected() && !replayLoaded()) return QStringLiteral("setup");
    if (replayAnalysisActive()) return QStringLiteral("replay");
    if (logPendingSave()) return QStringLiteral("log");
    return QStringLiteral("none");
}

QString AppController::primaryIssueId() const {
    const QString kind = primaryIssueKind();
    if (kind == QStringLiteral("alarm")) return topAlarmId();
    if (kind == QStringLiteral("value")) return topValueId();
    if (kind == QStringLiteral("timing")) return topTimingId();
    return QString();
}

QString AppController::primaryIssueSummary() const {
    const QString kind = primaryIssueKind();
    if (kind == QStringLiteral("bus")) return busHealthText();
    if (kind == QStringLiteral("alarm")) return topAlarmSummary().split(QStringLiteral("\n")).value(0);
    if (kind == QStringLiteral("value")) return topValueSummary().split(QStringLiteral("\n")).value(0);
    if (kind == QStringLiteral("timing")) return topTimingSummary().split(QStringLiteral("\n")).value(0);
    if (kind == QStringLiteral("setup")) return QStringLiteral("입력 source 또는 모델 구성이 완전하지 않음");
    if (kind == QStringLiteral("replay")) return replayCursorSummary();
    if (kind == QStringLiteral("log")) return logStatusSummary();
    return QStringLiteral("현재 최상위 이슈 없음");
}

QString AppController::operatorHeadline() const {
    const QString source = sourceStateText();
    const QString mode = analysisModeText();
    const QString cause = rootCauseSummary();
    return joinNonEmpty({source, mode, cause});
}

QString AppController::operatorActionLevel() const {
    const QString kind = primaryIssueKind();
    if (kind == QStringLiteral("bus")) return busHealthLevel();
    if (kind == QStringLiteral("alarm")) return alarmLevel();
    if (kind == QStringLiteral("value")) return valueLevel();
    if (kind == QStringLiteral("timing")) return timingLevel();
    if (kind == QStringLiteral("setup")) return QStringLiteral("WARN");
    if (kind == QStringLiteral("log")) return QStringLiteral("INFO");
    if (kind == QStringLiteral("replay")) return QStringLiteral("INFO");
    return systemLevel();
}

QString AppController::operatorActionText() const {
    const QString kind = primaryIssueKind();
    const QString idText = primaryIssueId();
    if (!connected() && !replayLoaded()) {
        return QStringLiteral("다음 조치: 포트를 연결하거나 BIN 파일을 로드해 입력 source를 먼저 확보");
    }
    if (!m_modelEnabled) {
        return QStringLiteral("다음 조치: 모델을 선택해 값 의미/범위/경보 근거를 함께 활성화");
    }
    if (m_replayRebuildActive) {
        return QStringLiteral("다음 조치: 재생 재구성 완료 후 주기/값/경보 탭에서 근거를 확인");
    }
    if (kind == QStringLiteral("bus")) {
        return QStringLiteral("다음 조치: BUS 상태를 먼저 확인하고 drop/FIFO/err-passive 영향 하에서 값·경보를 분리 해석");
    }
    if (kind == QStringLiteral("alarm")) {
        return idText.isEmpty()
            ? QStringLiteral("다음 조치: 경보 탭에서 현재 활성 경보의 발생 근거와 이력을 확인")
            : QStringLiteral("다음 조치: 경보 탭에서 %1 근거/이력부터 확인").arg(idText);
    }
    if (kind == QStringLiteral("value")) {
        return idText.isEmpty()
            ? QStringLiteral("다음 조치: 값 탭에서 해석 전체와 현재 판정/RAW를 함께 확인")
            : QStringLiteral("다음 조치: 값 탭에서 %1 해석 전체와 현재 판정/RAW를 확인").arg(idText);
    }
    if (kind == QStringLiteral("timing")) {
        return idText.isEmpty()
            ? QStringLiteral("다음 조치: 주기 탭에서 expected/gap/age/source를 먼저 확인")
            : QStringLiteral("다음 조치: 주기 탭에서 %1 expected/gap/age/source를 확인").arg(idText);
    }
    if (kind == QStringLiteral("replay")) {
        return QStringLiteral("다음 조치: 재생 마커 이동 또는 재생 재개로 문제 구간을 더 좁혀 확인");
    }
    if (kind == QStringLiteral("log")) {
        return QStringLiteral("다음 조치: 저장 또는 폐기를 결정해 현재 세션 로그 상태를 정리");
    }
    if (logRecordingActive()) {
        return QStringLiteral("다음 조치: 현재 상태가 중요하면 로그를 유지하고, 구간 종료 시 중지·저장");
    }
    if (replayLoaded() && !replayPlaying()) {
        return QStringLiteral("다음 조치: 필요 시 재생 시작 또는 issue marker 이동으로 구간 검토");
    }
    return QStringLiteral("다음 조치: 현재 상태는 비교적 안정적 · 개요/주기/값/경보를 순회하며 근거를 점검");
}

QString AppController::primaryIssueTargetTab() const {
    const QString kind = primaryIssueKind();
    if (kind == QStringLiteral("timing")) return QStringLiteral("timing");
    if (kind == QStringLiteral("value")) return QStringLiteral("value");
    if (kind == QStringLiteral("alarm")) return QStringLiteral("alarm");
    if (kind == QStringLiteral("replay")) return QStringLiteral("replay");
    if (kind == QStringLiteral("log")) return QStringLiteral("live");
    if (kind == QStringLiteral("setup")) return QStringLiteral("settings");
    if (kind == QStringLiteral("bus")) return QStringLiteral("overview");
    return QStringLiteral("overview");
}

QString AppController::primaryIssueMarkerKind() const {
    const QString kind = primaryIssueKind();
    if (kind == QStringLiteral("timing")) return QStringLiteral("timing");
    if (kind == QStringLiteral("value")) return QStringLiteral("value");
    if (kind == QStringLiteral("alarm")) return QStringLiteral("alarm");
    return QString();
}

bool AppController::primaryIssueSeekAvailable() const {
    if (!replayLoaded()) return false;
    const QString markerKind = primaryIssueMarkerKind();
    if (markerKind == QStringLiteral("timing")) return replayTimingMarkerCount() > 0;
    if (markerKind == QStringLiteral("value")) return replayValueMarkerCount() > 0;
    if (markerKind == QStringLiteral("alarm")) return replayAlarmMarkerCount() > 0;
    return false;
}

QString AppController::operatorFocusText() const {
    const QString target = primaryIssueTargetTab();
    const QString idText = primaryIssueId();
    if (target == QStringLiteral("timing")) {
        return idText.isEmpty()
            ? QStringLiteral("우선 보기: 주기 탭")
            : QStringLiteral("우선 보기: 주기 탭 · %1").arg(idText);
    }
    if (target == QStringLiteral("value")) {
        return idText.isEmpty()
            ? QStringLiteral("우선 보기: 값 탭")
            : QStringLiteral("우선 보기: 값 탭 · %1").arg(idText);
    }
    if (target == QStringLiteral("alarm")) {
        return idText.isEmpty()
            ? QStringLiteral("우선 보기: 경보 탭")
            : QStringLiteral("우선 보기: 경보 탭 · %1").arg(idText);
    }
    if (target == QStringLiteral("replay")) return QStringLiteral("우선 보기: 재생 탭");
    if (target == QStringLiteral("live")) return QStringLiteral("우선 보기: 라이브 탭");
    if (target == QStringLiteral("settings")) return QStringLiteral("우선 보기: 설정 탭");
    return QStringLiteral("우선 보기: 개요 탭");
}

bool AppController::seekReplayPrimaryIssue(int direction) {
    const QString markerKind = primaryIssueMarkerKind();
    if (markerKind.isEmpty()) return false;
    return seekReplayIssue(markerKind, direction);
}

QString AppController::sourceStateLevel() const {
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    if (m_connected && m_lastLiveFrameWallMs > 0 && (nowWallMs - m_lastLiveFrameWallMs) <= 1200) return QStringLiteral("OK");
    if (m_connected) return QStringLiteral("WARN");
    if (m_replayLoaded) return QStringLiteral("WARN");
    return QStringLiteral("ERR");
}

QString AppController::sourceStateText() const {
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    if (m_connected && m_lastLiveFrameWallMs > 0 && (nowWallMs - m_lastLiveFrameWallMs) <= 1200) {
        return QStringLiteral("입력 live 정상 · 마지막 frame %1 전").arg(fmtWallAge(nowWallMs - m_lastLiveFrameWallMs));
    }
    if (m_connected && m_lastLiveStatsWallMs > 0 && (nowWallMs - m_lastLiveStatsWallMs) <= 2000) {
        return QStringLiteral("입력 stats만 최근 수신 · 마지막 stats %1 전 / frame 지연").arg(fmtWallAge(nowWallMs - m_lastLiveStatsWallMs));
    }
    if (m_connected) return QStringLiteral("입력 포트 연결 상태지만 최근 frame 없음");
    if (m_replayLoaded) return QStringLiteral("live 입력 없음 · 재생 파일 기반만 사용 가능");
    return QStringLiteral("입력 source 없음 · 포트 연결 또는 BIN 로드 필요");
}

QString AppController::analysisModeLevel() const {
    if (m_replayRebuildActive) return QStringLiteral("WARN");
    if (replayAnalysisActive()) return QStringLiteral("WARN");
    if (m_liveUiPaused) return QStringLiteral("WARN");
    return m_connected ? QStringLiteral("OK") : (m_replayLoaded ? QStringLiteral("WARN") : QStringLiteral("ERR"));
}

QString AppController::analysisModeText() const {
    if (m_replayRebuildActive) {
        return QStringLiteral("재생 재구성 중 · 표시/카운트는 스냅샷 유지 후 완료 시 전환");
    }
    if (replayAnalysisActive()) {
        if (m_replayPlaying) return QStringLiteral("재생 진행 중 · 현재 프레임까지 누적 상태 기준");
        if (m_replayAnalysisHeld) return QStringLiteral("재생 정지 고정 · 현재 프레임까지 누적 상태 유지");
        return QStringLiteral("재생 로드됨 · 재생 시작 전");
    }
    if (m_liveUiPaused) return QStringLiteral("라이브 화면 정지 · 새 수신은 계속되고 표시만 고정");
    if (m_connected) return QStringLiteral("라이브 분석 중 · 새 수신 기준 즉시 갱신");
    return QStringLiteral("분석 대기 · 활성 source 없음");
}

int AppController::modelSignalCount() const {
    int total = 0;
    for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) total += it.value().signalSpecs.size();
    return total;
}

int AppController::modelAlarmSignalCount() const {
    int total = 0;
    for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) {
        for (const auto& sig : it.value().signalSpecs) {
            const bool hasThreshold = sig.hasWarnMin || sig.hasWarnMax || sig.hasErrMin || sig.hasErrMax;
            const bool hasInactive = !sig.inactiveRawValues.isEmpty() || !sig.inactiveLabels.isEmpty();
            const bool namedMode = !sig.alarmMode.trimmed().isEmpty();
            const bool namedSeverity = !sig.alarmSeverity.trimmed().isEmpty();
            const bool namedMessage = !sig.alarmMessage.trimmed().isEmpty();
            if (hasThreshold || hasInactive || namedMode || namedSeverity || namedMessage) ++total;
        }
    }
    return total;
}

int AppController::modelMonitorOnlySignalCount() const {
    int total = 0;
    for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) {
        for (const auto& sig : it.value().signalSpecs) {
            if (sig.monitorOnly) ++total;
        }
    }
    return total;
}

int AppController::modelTimingRuleCount() const {
    int total = 0;
    for (auto it = m_rules.cbegin(); it != m_rules.cend(); ++it) {
        const auto& rule = it.value();
        if (!rule.timingEnabled) continue;
        if (rule.expectedPeriodMs > 0.0 || rule.ttlWarnMs > 0.0 || rule.ttlErrMs > 0.0) ++total;
    }
    return total;
}

QString AppController::modelDiagnosticsLevel() const {
    if (!m_modelEnabled) return QStringLiteral("INFO");
    if (!rulesLoaded() && !signalDbLoaded()) return QStringLiteral("ERR");
    if (!rulesLoaded() || !signalDbLoaded()) return QStringLiteral("WARN");
    if (modelTimingRuleCount() <= 0 || modelSignalCount() <= 0) return QStringLiteral("WARN");
    if (modelAlarmSignalCount() <= 0) return QStringLiteral("WARN");
    return QStringLiteral("OK");
}

QString AppController::modelDiagnosticsSummary() const {
    if (!m_modelEnabled) return QStringLiteral("모델 해제 · 주기 관찰/RAW 확인 모드");

    QStringList parts;
    parts << QStringLiteral("rules %1").arg(m_rules.size());
    parts << QStringLiteral("timing-active %1").arg(modelTimingRuleCount());
    parts << QStringLiteral("messages %1").arg(m_signalMessages.size());
    parts << QStringLiteral("signals %1").arg(modelSignalCount());
    parts << QStringLiteral("alarm-signals %1").arg(modelAlarmSignalCount());
    if (modelMonitorOnlySignalCount() > 0) parts << QStringLiteral("monitor-only %1").arg(modelMonitorOnlySignalCount());

    QStringList notes;
    if (!rulesLoaded()) notes << QStringLiteral("주기 기준 없음");
    if (!signalDbLoaded()) notes << QStringLiteral("값 해석 시그널 없음");
    if (rulesLoaded() && modelTimingRuleCount() <= 0) notes << QStringLiteral("timing_enabled 규칙이 비활성/부족");
    if (signalDbLoaded() && modelAlarmSignalCount() <= 0) notes << QStringLiteral("명시 alarm 필드 없음");
    if (notes.isEmpty()) notes << QStringLiteral("모델팩 품질 양호");
    return QStringLiteral("%1 · %2").arg(parts.join(QStringLiteral(" · ")), notes.join(QStringLiteral(" / ")));
}

QString AppController::sessionFilePath() const {
    return m_session.filePath();
}

QString AppController::defaultLogDirectory() const {
    return StorageRuntime::defaultLogDirectory();
}

QString AppController::defaultSnapshotDirectory() const {
    return StorageRuntime::defaultSnapshotDirectory();
}

QString AppController::suggestedSnapshotPath() const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(defaultSnapshotDirectory()).filePath(QStringLiteral("analysis_snapshot_%1.json").arg(stamp));
}

QString AppController::logTargetDirectory() const {
    return m_logTargetDirectory.trimmed().isEmpty() ? defaultLogDirectory() : m_logTargetDirectory;
}

QString AppController::logTargetPreview() const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const StorageRuntime::LogSessionPaths paths = (m_transportModeKey == QStringLiteral("typed"))
        ? StorageRuntime::makeTypedCapturePaths(stamp, logTargetDirectory(), m_logTargetName)
        : StorageRuntime::makeLegacyLogPaths(stamp, m_modelEnabled, logTargetDirectory(), m_logTargetName);
    return paths.suggestedSavePath;
}

QString AppController::replayOpenDirectory() const {
    return m_replayRuntime.openDirectory(m_session);
}

QString AppController::logPhaseText() const {
    if (m_logSaving) return QStringLiteral("로그 저장 중");
    if (m_logStopping) return QStringLiteral("로그 종료 중");
    if (m_logRecordingActive) return QStringLiteral("로그 기록 중");
    if (m_logPendingSave) return QStringLiteral("로그 저장 대기");
    if (!m_logLastSavedPath.isEmpty()) return QStringLiteral("마지막 로그 준비됨");
    return QStringLiteral("로그 대기");
}

QString AppController::logActionText() const {
    QString path;
    if (m_logSaving || m_logPendingSave) path = m_logSuggestedSavePath;
    else if (m_logRecordingActive || m_logStopping) path = m_logTempPath;
    else if (!m_logPath.isEmpty()) path = m_logPath;
    else path = m_logLastSavedPath;

    if (m_logSaving) {
        return path.isEmpty()
            ? QStringLiteral("임시 로그를 최종 저장 위치로 이동 중입니다.")
            : QStringLiteral("임시 로그를 최종 저장 위치로 이동 중입니다: %1").arg(path);
    }
    if (m_logStopping) {
        return QStringLiteral("수집을 멈추고 임시 로그를 정리하는 중입니다. 완료되면 저장 위치 선택 상태로 전환됩니다.");
    }
    if (m_logRecordingActive) {
        QStringList parts;
        parts << (m_logTypedSession
            ? QStringLiteral("현재 typed evidence capture를 기록 중입니다.")
            : QStringLiteral("현재 수신 프레임을 임시 BIN 로그로 기록 중입니다."));
        if (m_logRecordedFrameCount > 0) parts << QStringLiteral("%1 frame").arg(m_logRecordedFrameCount);
        if (m_logRecordedBytes > 0) parts << formatBytesCompact(m_logRecordedBytes);
        if (!path.isEmpty()) parts << path;
        return parts.join(QStringLiteral(" · "));
    }
    if (m_logPendingSave) {
        return path.isEmpty()
            ? QStringLiteral("기록은 끝났습니다. 저장 위치를 선택해 BIN 로그를 확정하거나 폐기하세요.")
            : QStringLiteral("기록은 끝났습니다. 저장 위치를 선택해 BIN 로그를 확정하세요: %1").arg(path);
    }
    if (!m_logLastSavedPath.isEmpty()) {
        return QStringLiteral("최근 저장 로그: %1").arg(m_logLastSavedPath);
    }
    return QStringLiteral("포트를 연결한 뒤 로그 시작을 누르면 아래 대상에 기록합니다: %1").arg(logTargetPreview());
}

QString AppController::defaultTempLogDirectory() const {
    return StorageRuntime::defaultTempLogDirectory();
}

void AppController::setLogTargetDirectory(const QString& directory) {
    const QString normalized = RuntimePaths::normalizeLocalPath(directory);
    const QString next = normalized.trimmed().isEmpty()
        ? defaultLogDirectory()
        : QDir::fromNativeSeparators(normalized);
    if (m_logTargetDirectory == next) return;
    QDir().mkpath(next);
    m_logTargetDirectory = next;
    if (!m_restoringSession) saveSessionState();
    emit logTargetChanged();
    emit logStateChanged();
}

void AppController::setLogTargetName(const QString& name) {
    const QString next = name.trimmed();
    if (m_logTargetName == next) return;
    m_logTargetName = next;
    if (!m_restoringSession) saveSessionState();
    emit logTargetChanged();
    emit logStateChanged();
}

bool AppController::moveOrReplaceFile(const QString& src, const QString& dst) {
    return moveOrCopyReplace(src, dst);
}

void AppController::removeIfExists(const QString& path) {
    removeFileIfExists(path);
}


QString AppController::logStatusSummary() const {
    QStringList parts;
    parts << logPhaseText();
    if (m_logRecordedFrameCount > 0) parts << QStringLiteral("%1 frame").arg(m_logRecordedFrameCount);
    if (m_logRecordedBytes > 0) parts << formatBytesCompact(m_logRecordedBytes);
    QString path;
    if (m_logSaving || m_logPendingSave) path = m_logSuggestedSavePath;
    else if (m_logRecordingActive || m_logStopping) path = m_logTempPath;
    else if (!m_logPath.isEmpty()) path = m_logPath;
    else path = m_logLastSavedPath;
    if (!path.isEmpty()) parts << path;
    return parts.join(QStringLiteral(" · "));
}

QString AppController::loggingStateLevel() const {
    if (m_logRecordingActive) return QStringLiteral("OK");
    if (m_logStopping || m_logSaving || m_logPendingSave) return QStringLiteral("WARN");
    return m_connected ? QStringLiteral("OK") : QStringLiteral("WARN");
}

QString AppController::loggingStateText() const {
    if (m_logRecordingActive) {
        return QStringLiteral("로그 기록 중 · %1 frame · %2 · 중지 시 저장")
            .arg(m_logRecordedFrameCount)
            .arg(formatBytesCompact(m_logRecordedBytes));
    }
    if (m_logStopping) return QStringLiteral("로그 종료 중 · 기록 버퍼 정리 후 저장 대기 진입");
    if (m_logSaving) return QStringLiteral("로그 저장 중 · 파일 저장 완료까지 대기");
    if (m_logPendingSave) {
        const QString target = !m_logSuggestedSavePath.isEmpty() ? m_logSuggestedSavePath : QStringLiteral("파일명 미지정");
        return QStringLiteral("로그 저장 대기 · %1 · 저장 또는 폐기 필요").arg(target);
    }
    return m_connected
        ? QStringLiteral("로그 대기 · 지금 시작 가능")
        : QStringLiteral("로그 대기 · 포트 연결 필요");
}

QString AppController::transportModeText() const {
    return m_transportModeKey == QStringLiteral("typed")
        ? QStringLiteral("TYPED STREAM")
        : QStringLiteral("LEGACY 20B");
}

void AppController::setTransportMode(const QString& mode) {
    const QString key = CanMonitorTransport::TransportRuntime::normalizeModeKey(mode);
    if (m_transportModeKey == key) return;
    if (m_connected) {
        setStatus(QStringLiteral("전송 모드는 연결 해제 후 변경하세요"));
        return;
    }

    m_transportModeKey = key;
    resetTypedEvidenceState();
    QString error;
    if (!m_transportRuntime.setTransportModeKey(key, &error)) {
        setStatus(error);
    }
    emit transportModeChanged();
    emit typedEvidenceChanged();
    emit controlStateChanged();
    setStatus(m_transportModeKey == QStringLiteral("typed")
        ? QStringLiteral("전송 모드: typed evidence · 보드 binary stream 수신 대기")
        : QStringLiteral("전송 모드: legacy 20-byte"));
}

qulonglong AppController::typedTransportFaultCount() const {
    return qulonglong(m_typedBytesDropped + m_typedCrcFailures + m_typedLengthFailures + m_typedVersionWarnings + m_typedSeqGaps);
}

QString AppController::typedCanSummary() const {
    if (m_transportModeKey != QStringLiteral("typed")) return QStringLiteral("CAN evidence inactive");

    const quint64 rx = m_typedTypeCounts.value(static_cast<quint8>(TypedRecordType::CanRxRaw));
    const quint64 tx = m_typedTypeCounts.value(static_cast<quint8>(TypedRecordType::CanTxRaw));
    QStringList parts;
    parts << QStringLiteral("CAN RX %1").arg(rx);
    parts << QStringLiteral("TX %1").arg(tx);

    QStringList rxBus;
    for (auto it = m_typedCanRxByBus.cbegin(); it != m_typedCanRxByBus.cend(); ++it) {
        if (it.value() > 0) rxBus << QStringLiteral("B%1:%2").arg(it.key()).arg(it.value());
    }
    QStringList txBus;
    for (auto it = m_typedCanTxByBus.cbegin(); it != m_typedCanTxByBus.cend(); ++it) {
        if (it.value() > 0) txBus << QStringLiteral("B%1:%2").arg(it.key()).arg(it.value());
    }
    if (!rxBus.isEmpty()) parts << QStringLiteral("RX bus %1").arg(rxBus.join(QStringLiteral(",")));
    if (!txBus.isEmpty()) parts << QStringLiteral("TX bus %1").arg(txBus.join(QStringLiteral(",")));
    if (m_typedRxHealthParityAnchored && m_typedRxHealthBoardDelta > 0) {
        parts << QStringLiteral("RX parity boardΔ %1 streamΔ %2 miss %3")
            .arg(m_typedRxHealthBoardDelta)
            .arg(m_typedRxHealthStreamDelta)
            .arg(m_typedRxHealthMissing);
    }
    if (!m_typedLastCanRxSummary.isEmpty()) parts << QStringLiteral("last %1").arg(m_typedLastCanRxSummary);
    if (!m_typedLastCanTxSummary.isEmpty()) parts << QStringLiteral("last %1").arg(m_typedLastCanTxSummary);
    return parts.join(QStringLiteral(" | "));
}

QString AppController::typedEvidenceSummary() const {
    if (m_transportModeKey != QStringLiteral("typed")) return QStringLiteral("Legacy 20B");

    QStringList typeParts;
    const auto appendType = [this, &typeParts](TypedRecordType type) {
        const quint8 key = static_cast<quint8>(type);
        const quint64 count = m_typedTypeCounts.value(key);
        if (count > 0) typeParts << QStringLiteral("%1 %2").arg(typedRecordTypeName(key)).arg(count);
    };
    appendType(TypedRecordType::CanRxRaw);
    appendType(TypedRecordType::CanTxRaw);
    appendType(TypedRecordType::AdcSample);
    appendType(TypedRecordType::ControlAck);
    appendType(TypedRecordType::BoardEvent);
    appendType(TypedRecordType::BoardHealth);
    appendType(TypedRecordType::Capability);

    QStringList parts;
    parts << QStringLiteral("Typed rec %1").arg(m_typedRecordCount);
    if (!m_typedLastRecordType.isEmpty()) parts << QStringLiteral("last %1 @ %2us").arg(m_typedLastRecordType).arg(m_typedLastMonoUs);
    if (!typeParts.isEmpty()) parts << typeParts.join(QStringLiteral(", "));
    const qulonglong faults = typedTransportFaultCount();
    if (faults > 0) {
        parts << QStringLiteral("fault %1 · drop %2 crc %3 len %4 seq %5 ver %6")
                     .arg(faults)
                     .arg(m_typedBytesDropped)
                     .arg(m_typedCrcFailures)
                     .arg(m_typedLengthFailures)
                     .arg(m_typedSeqGaps)
                     .arg(m_typedVersionWarnings);
    } else {
        parts << QStringLiteral("fault 0");
    }
    return parts.join(QStringLiteral(" · "));
}

QString AppController::boardConnectionSummary() const {
    const auto state = m_evidenceRuntime.snapshot();
    QStringList parts;
    parts << (state.serialOpen ? QStringLiteral("serial open") : QStringLiteral("serial closed"));
    parts << (state.capabilitySeen ? QStringLiteral("CAPABILITY seen") : QStringLiteral("waiting CAPABILITY"));
    parts << (state.healthSeen ? QStringLiteral("BOARD_HEALTH seen") : QStringLiteral("waiting BOARD_HEALTH"));
    if (state.capabilitySeen) {
        parts << QStringLiteral("profile %1.%2").arg(state.profileMajor).arg(state.profileMinor);
    }
    if (state.healthSeen) {
        parts << QStringLiteral("safety %1").arg(state.safetyState);
        parts << QStringLiteral("fault 0x%1").arg(state.faultFlags, 8, 16, QLatin1Char('0')).toUpper();
        parts << QStringLiteral("drop %1").arg(m_lastStats.droppedTotal);
        parts << QStringLiteral("fifo %1").arg(m_lastStats.fifoOverflowTotal);
        if (state.lastHealthWallMs > 0) parts << QStringLiteral("health age %1ms").arg(state.healthAgeMs);
    }
    parts << (state.boardAlive ? QStringLiteral("board alive") : QStringLiteral("not alive"));
    parts << (state.controlCapable ? QStringLiteral("control capable") : QStringLiteral("control gated"));
    if (!state.reason.isEmpty()) parts << state.reason;
    return parts.join(QStringLiteral(" | "));
}

QString AppController::controlStatusSummary() const {
    return m_controlRuntime.statusSummary(m_transportModeKey,
                                          m_connected,
                                          controlEvidenceReady(),
                                          controlEvidenceBlockReason());
}

bool AppController::controlReady() const {
    return m_connected && m_transportModeKey == QStringLiteral("typed") && controlEvidenceReady();
}

QString AppController::controlPolicyTargetRole() const {
    const QString role = m_controlPolicy.targetRole.trimmed().toLower();
    return role.isEmpty() ? QStringLiteral("system") : role;
}

int AppController::controlPolicyMaxRpm() const {
    return std::clamp(m_controlPolicy.maxRpm, 0, 10000);
}

double AppController::controlPolicyMaxAbsSteeringDeg() const {
    return std::clamp(m_controlPolicy.maxAbsSteeringDeg, 0.0, 90.0);
}

QSet<quint32> AppController::controlPolicyFingerprints() const {
    QSet<quint32> out;
    for (const CanModel::BusRoleRuleSpec& rule : m_controlPolicy.busRoleRules) {
        for (quint32 id : rule.fingerprints) out.insert(id & 0x1FFFFFFFu);
    }
    if (out.isEmpty()) out = systemBusFingerprints();
    return out;
}

bool AppController::controlPolicyAllowsTargetBus() const {
    if (!m_controlPolicy.enabled) return false;
    const auto resolution = m_busRoleResolver.resolve(quint8(controlTargetBus()));
    if (!resolution.resolved || !resolution.txAllowed) return false;
    return m_controlPolicy.roleAllowed(resolution.role);
}

QString AppController::controlPolicySummary() const {
    QStringList parts;
    parts << (m_controlPolicy.declared ? QStringLiteral("model policy") : QStringLiteral("default policy"));
    parts << (m_controlPolicy.enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    if (!m_controlPolicy.profileName.trimmed().isEmpty()) {
        parts << QStringLiteral("profile %1").arg(m_controlPolicy.profileName.trimmed());
    }
    parts << QStringLiteral("target role %1").arg(controlPolicyTargetRole());
    parts << QStringLiteral("allowed roles %1").arg(m_controlPolicy.allowedBusRoles.isEmpty()
        ? QStringLiteral("any")
        : m_controlPolicy.allowedBusRoles.join(QStringLiteral(",")));
    parts << QStringLiteral("limit rpm %1 steer +/- %2deg")
        .arg(controlPolicyMaxRpm())
        .arg(QString::number(controlPolicyMaxAbsSteeringDeg(), 'f', 1));
    parts << QStringLiteral("rules %1").arg(m_controlPolicy.busRoleRules.size());
    return parts.join(QStringLiteral(" | "));
}

QString AppController::controlBlockReason() const {
    if (m_transportModeKey != QStringLiteral("typed")) {
        return QStringLiteral("typed evidence stream required");
    }
    if (!m_connected) {
        return QStringLiteral("COM not connected");
    }
    if (!controlEvidenceReady()) {
        return controlEvidenceBlockReason();
    }
    return QStringLiteral("ready");
}

QString AppController::controlOperatorSummary() const {
    QStringList parts;
    parts << (controlReady()
        ? QStringLiteral("제어 가능")
        : QStringLiteral("제어 차단: %1").arg(controlBlockReason()));
    parts << (m_controlRuntime.armed() ? QStringLiteral("ARM") : QStringLiteral("standby"));
    parts << (m_controlAudit.actualTxConfirmed()
        ? QStringLiteral("CAN_TX_RAW 확인")
        : QStringLiteral("CAN_TX_RAW 미확인"));
    parts << (m_controlAudit.faultActive()
        ? QStringLiteral("fault/block active: %1").arg(m_controlAudit.lastFaultSummary())
        : QStringLiteral("fault/block 없음"));
    return parts.join(QStringLiteral(" | "));
}

QString AppController::controlActionVerdict() const {
    return m_controlRuntime.actionVerdict(m_transportModeKey,
                                          m_connected,
                                          m_evidenceRuntime.boardAlive(),
                                          m_evidenceRuntime.controlCapable(),
                                          controlTargetBusAllowed(),
                                          m_controlAudit.actualTxConfirmed(),
                                          m_controlAudit.faultActive(),
                                          controlBlockReason());
}

QString AppController::controlEvidenceStatsSummary() const {
    return m_controlAudit.statsSummary();
}

QVariantList AppController::controlEvidenceStages() const {
    return m_controlAudit.stages();
}

QVariantList AppController::controlOperatorChecklist() const {
    QVariantList rows = m_controlRuntime.operatorChecklist(m_transportModeKey,
                                                           m_connected,
                                                           m_evidenceRuntime.boardAlive(),
                                                           m_evidenceRuntime.controlCapable(),
                                                           controlTargetBusAllowed(),
                                                           m_controlAudit.actualTxConfirmed(),
                                                           m_controlAudit.faultActive(),
                                                           controlBlockReason());
    const QVariantList policyRows = controlPolicyChecklist();
    for (int i = policyRows.size() - 1; i >= 0; --i) rows.insert(4, policyRows.at(i));
    return rows;
}

QVariantList AppController::controlPolicyChecklist() const {
    auto row = [](const QString& key,
                  const QString& title,
                  const QString& level,
                  const QString& state,
                  const QString& detail,
                  bool ok,
                  bool blocking) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), key);
        item.insert(QStringLiteral("title"), title);
        item.insert(QStringLiteral("level"), level);
        item.insert(QStringLiteral("state"), state);
        item.insert(QStringLiteral("detail"), detail);
        item.insert(QStringLiteral("ok"), ok);
        item.insert(QStringLiteral("blocking"), blocking);
        return item;
    };

    const bool allowed = controlPolicyAllowsTargetBus();
    const auto resolution = m_busRoleResolver.resolve(quint8(controlTargetBus()));
    const QString roleText = resolution.resolved ? resolution.role : QStringLiteral("unresolved");
    QVariantList rows;
    rows << row(QStringLiteral("policy"),
                QStringLiteral("Model policy"),
                allowed ? QStringLiteral("ok") : QStringLiteral("error"),
                allowed ? QStringLiteral("ALLOW") : QStringLiteral("BLOCK"),
                allowed ? QStringLiteral("%1; bus%2 role %3")
                              .arg(controlPolicySummary())
                              .arg(controlTargetBus())
                              .arg(roleText)
                        : QStringLiteral("%1; bus%2 role %3 not allowed")
                              .arg(controlPolicySummary())
                              .arg(controlTargetBus())
                              .arg(roleText),
                allowed,
                !allowed);
    return rows;
}

void AppController::refreshControlStatus(const QString& status) {
    m_controlRuntime.setStatusText(status);
    emit controlStateChanged();
}

void AppController::appendControlEvidenceEvent(const QString& stage,
                                               const QString& level,
                                               const QString& summary,
                                               const QString& detail,
                                               quint32 commandId,
                                               quint32 canId,
                                               quint8 bus) {
    m_controlAudit.appendEvent(stage, level, summary, detail, commandId, canId, bus);
}

bool AppController::controlEvidenceReady() const {
    return m_evidenceRuntime.controlCapable() && controlTargetBusAllowed();
}

QString AppController::controlEvidenceBlockReason() const {
    if (!m_evidenceRuntime.controlCapable()) return m_evidenceRuntime.reason();
    if (!controlTargetBusAllowed()) {
        return QStringLiteral("target bus %1 is not resolved/allowed for control TX (%2)")
            .arg(controlTargetBus())
            .arg(controlBusResolutionSummary());
    }
    return m_evidenceRuntime.reason();
}

bool AppController::controlTargetBusAllowed() const {
    if (m_controlCapabilityHasBusDescriptors && !m_controlTxAllowedBuses.contains(controlTargetBus())) {
        return false;
    }

    const auto resolution = m_busRoleResolver.resolve(quint8(controlTargetBus()));
    return resolution.resolved && resolution.txAllowed && controlPolicyAllowsTargetBus();
}

void AppController::updateControlBusCapability(const TypedCapabilityRecord& capability) {
    m_controlTxAllowedBuses.clear();
    m_controlCapabilityHasBusDescriptors = !capability.buses.isEmpty();
    m_busRoleResolver.clearCapabilityRoles();

    QStringList busParts;
    for (const auto& bus : capability.buses) {
        if (bus.controlTxAllowed && bus.txSupported) {
            m_controlTxAllowedBuses.insert(int(bus.busId));
        }
        const QString resolverRole = capabilityResolverRole(bus.roleHint);
        if (!resolverRole.isEmpty()) {
            m_busRoleResolver.setCapabilityRole(bus.busId, resolverRole, bus.controlTxAllowed && bus.txSupported);
        }
        busParts << QStringLiteral("bus%1 %2 %3 rx%4 tx%5 ctrl%6 %7kbps")
            .arg(bus.busId)
            .arg(capabilityBackendText(bus.backend))
            .arg(capabilityRoleHintText(bus.roleHint))
            .arg(bus.rxSupported ? 1 : 0)
            .arg(bus.txSupported ? 1 : 0)
            .arg(bus.controlTxAllowed ? 1 : 0)
            .arg(bus.nominalBitrate / 1000);
    }

    if (busParts.isEmpty()) {
        m_controlBusSummary = QStringLiteral("CAPABILITY has no bus descriptors; falling back to global CAN_TX_RAW support");
    } else {
        m_controlBusSummary = busParts.join(QStringLiteral(" | "));
    }
    autoSelectSystemControlBus();
    m_controlBusSummary += QStringLiteral(" | target %1").arg(controlBusResolutionSummary());
}

void AppController::seedBusRoleResolver() {
    m_busRoleResolver.clearModelRules();
    if (m_controlPolicy.busRoleRules.isEmpty()) {
        m_busRoleResolver.addModelRule(QStringLiteral("system"), systemBusFingerprints(), true);
    } else {
        for (const CanModel::BusRoleRuleSpec& rule : m_controlPolicy.busRoleRules) {
            m_busRoleResolver.addModelRule(rule.role, rule.fingerprints, rule.txAllowed);
        }
    }
    if (m_controlRuntime.targetBusManualOverride()) {
        m_busRoleResolver.setOperatorOverride(quint8(controlTargetBus()), controlPolicyTargetRole(), true);
    }
}

void AppController::observeBusRoleFingerprint(quint8 bus, quint32 canId) {
    const quint32 normalizedId = canId & 0x1FFFFFFFu;
    if (!isSystemBusFingerprint(normalizedId) && !controlPolicyFingerprints().contains(normalizedId)) return;

    const int previousTargetBus = controlTargetBus();
    const bool previousAllowed = controlTargetBusAllowed();
    const quint64 previousHits = m_systemFingerprintHitsByBus.value(bus);

    m_systemFingerprintHitsByBus[bus] += 1;
    m_busRoleResolver.observeCanId(bus, normalizedId);
    const bool changedTarget = autoSelectSystemControlBus();
    if ((changedTarget || previousHits == 0) &&
        !m_controlBusSummary.isEmpty() &&
        m_controlBusSummary != QStringLiteral("waiting for CAPABILITY bus descriptors")) {
        m_controlBusSummary = m_controlBusSummary.section(QStringLiteral(" | target "), 0, 0)
            + QStringLiteral(" | target %1").arg(controlBusResolutionSummary());
    }

    if (changedTarget ||
        previousTargetBus != controlTargetBus() ||
        previousAllowed != controlTargetBusAllowed() ||
        previousHits == 0) {
        emit controlStateChanged();
    }
}

bool AppController::autoSelectSystemControlBus() {
    if (m_controlRuntime.targetBusManualOverride()) return false;

    int bestBus = -1;
    quint64 bestHits = 0;
    for (auto it = m_systemFingerprintHitsByBus.cbegin(); it != m_systemFingerprintHitsByBus.cend(); ++it) {
        const int bus = int(it.key());
        if (m_controlCapabilityHasBusDescriptors && !m_controlTxAllowedBuses.contains(bus)) {
            continue;
        }
        const auto resolution = m_busRoleResolver.resolve(it.key());
        if (!resolution.resolved || resolution.role != controlPolicyTargetRole() || !resolution.txAllowed || !m_controlPolicy.roleAllowed(resolution.role)) {
            continue;
        }
        if (bestBus < 0 || it.value() > bestHits || (it.value() == bestHits && bus == controlTargetBus())) {
            bestBus = bus;
            bestHits = it.value();
        }
    }

    if (bestBus < 0 || bestBus == controlTargetBus()) return false;
    m_controlRuntime.setTargetBus(bestBus, false);
    m_controlRuntime.setStatusText(QStringLiteral("Auto-selected system CAN bus %1 from observed evidence").arg(bestBus));
    return true;
}

QString AppController::controlBusResolutionSummary() const {
    const auto resolution = m_busRoleResolver.resolve(quint8(controlTargetBus()));
    if (!resolution.resolved) {
        return QStringLiteral("bus%1 unresolved").arg(controlTargetBus());
    }
    const quint64 hits = m_systemFingerprintHitsByBus.value(quint8(controlTargetBus()));
    QStringList parts;
    parts << QStringLiteral("bus%1 role %2").arg(controlTargetBus()).arg(resolution.role);
    parts << QStringLiteral("via %1").arg(busRoleSourceText(resolution.source));
    parts << QStringLiteral("tx%1").arg(resolution.txAllowed ? 1 : 0);
    if (hits > 0) parts << QStringLiteral("system-id hits %1").arg(hits);
    if (m_controlRuntime.targetBusManualOverride()) parts << QStringLiteral("manual");
    if (m_controlCapabilityHasBusDescriptors) {
        parts << QStringLiteral("cap%1").arg(m_controlTxAllowedBuses.contains(controlTargetBus()) ? 1 : 0);
    }
    parts << QStringLiteral("policy%1").arg(controlPolicyAllowsTargetBus() ? 1 : 0);
    return parts.join(QStringLiteral(" "));
}

void AppController::queueControlHostFrame(const QByteArray& frame,
                                          const QString& summary,
                                          const QString& stage,
                                          quint32 commandId,
                                          quint32 canId,
                                          quint8 bus) {
    if (frame.isEmpty()) return;
    m_controlAudit.noteHostFrameQueued();
    if (!stage.isEmpty()) {
        appendControlEvidenceEvent(stage,
                                   QStringLiteral("info"),
                                   summary,
                                   QStringLiteral("Host typed downlink queued"),
                                   commandId,
                                   canId,
                                   bus);
    }
    QString error;
    if (!m_transportRuntime.sendHostFrame(frame, summary, &error)) {
        appendControlEvidenceEvent(QStringLiteral("HOST_WRITE"),
                                   QStringLiteral("error"),
                                   QStringLiteral("Qt serial write queue failed"),
                                   error,
                                   commandId,
                                   canId,
                                   bus);
        refreshControlStatus(error);
    }
}

void AppController::sendControlHeartbeat(const QString& reason) {
    if (!m_connected || m_transportModeKey != QStringLiteral("typed")) return;
    const quint32 commandId = m_controlRuntime.nextCommandId();
    const quint32 hostMonoMs = quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFLL);
    const QByteArray frame = CanMonitorControl::ControlCommandEncoder::buildHostHeartbeat(commandId, hostMonoMs);
    const QString summary = QStringLiteral("#%1 %2 HOST_HEARTBEAT").arg(commandId).arg(reason);
    m_controlRuntime.setLastHeartbeatWallMs(QDateTime::currentMSecsSinceEpoch());
    queueControlHostFrame(frame,
                          summary,
                          reason == QStringLiteral("heartbeat keepalive") ? QString() : QStringLiteral("HEARTBEAT"),
                          commandId);
}

void AppController::sendControlSession(quint8 action, const QString& reason) {
    if (!m_connected || m_transportModeKey != QStringLiteral("typed")) return;
    const quint32 commandId = m_controlRuntime.nextCommandId();
    const QByteArray frame = CanMonitorControl::ControlCommandEncoder::buildHostControlSession(
        commandId,
        action,
        CanMonitorControl::kControlSessionAnyBus,
        2000);
    const QString actionText = action == CanMonitorControl::kControlSessionArm
        ? QStringLiteral("ARM")
        : (action == CanMonitorControl::kControlSessionRenewLease ? QStringLiteral("RENEW_LEASE") : QStringLiteral("DISARM"));
    const QString summary = QStringLiteral("#%1 %2 HOST_CONTROL_SESSION %3 lease 2000ms")
        .arg(commandId)
        .arg(reason, actionText);
    if (action == CanMonitorControl::kControlSessionArm || action == CanMonitorControl::kControlSessionRenewLease) {
        m_controlRuntime.setLastLeaseRenewWallMs(QDateTime::currentMSecsSinceEpoch());
    }
    queueControlHostFrame(frame, summary, QStringLiteral("SESSION"), commandId);
}

void AppController::maintainControlLeaseAndHeartbeat(bool forceHeartbeat, bool forceLeaseRenew) {
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    if (forceHeartbeat || nowWallMs - m_controlRuntime.lastHeartbeatWallMs() >= 50) {
        sendControlHeartbeat(forceHeartbeat ? QStringLiteral("heartbeat prime") : QStringLiteral("heartbeat keepalive"));
    }
    if (m_controlRuntime.armed() && (forceLeaseRenew || nowWallMs - m_controlRuntime.lastLeaseRenewWallMs() >= 250)) {
        sendControlSession(CanMonitorControl::kControlSessionRenewLease, QStringLiteral("lease keepalive"));
    }
}

void AppController::setControlArmed(bool armed) {
    if (armed && (!m_connected || m_transportModeKey != QStringLiteral("typed"))) {
        m_controlRuntime.setArmed(false);
        refreshControlStatus(QStringLiteral("Control arm rejected: connect in typed stream first"));
        return;
    }
    if (armed && !controlEvidenceReady()) {
        m_controlRuntime.setArmed(false);
        refreshControlStatus(QStringLiteral("Control arm rejected: %1").arg(controlEvidenceBlockReason()));
        return;
    }
    if (!armed) {
        m_controlKeyboardHeldKeys.clear();
        m_controlKeyboardSessionActive = false;
        m_controlRuntime.setTestRunning(false);
        m_controlPatternTimer.stop();
        m_controlPatternSteps.clear();
        m_controlPatternIndex = 0;
        m_controlRuntime.clearBurstWallMs();
        setControlTargetCommand(0, 0, 0.0, 1, 1, QStringLiteral("disarm neutral"));
        sendControlKeepaliveTick(true, true);
        sendControlSession(CanMonitorControl::kControlSessionDisarm, QStringLiteral("operator disarm"));
        stopControlKeepalive();
    }
    m_controlRuntime.setArmed(armed);
    if (armed) {
        m_controlKeyboardHeldKeys.clear();
        m_controlKeyboardSessionActive = false;
        setControlTargetCommand(0, 0, 0.0, 1, 1, QStringLiteral("arm neutral"));
        maintainControlLeaseAndHeartbeat(true, false);
        sendControlSession(CanMonitorControl::kControlSessionArm, QStringLiteral("operator arm"));
        startControlKeepalive();
    }
    refreshControlStatus(armed ? QStringLiteral("Control armed by operator") : QStringLiteral("Control standby"));
}

void AppController::setControlTargetBus(int bus) {
    const int nextBus = std::clamp(bus, 0, 255);
    if (controlTargetBus() == nextBus) return;
    m_controlRuntime.setTargetBus(nextBus, true);
    m_busRoleResolver.setOperatorOverride(quint8(controlTargetBus()), controlPolicyTargetRole(), true);
    if (!m_controlBusSummary.isEmpty() && m_controlBusSummary != QStringLiteral("waiting for CAPABILITY bus descriptors")) {
        m_controlBusSummary = m_controlBusSummary.section(QStringLiteral(" | target "), 0, 0)
            + QStringLiteral(" | target %1").arg(controlBusResolutionSummary());
    }
    refreshControlStatus(controlTargetBusAllowed()
        ? QStringLiteral("Control target bus updated")
        : QStringLiteral("Control target bus is not advertised for TX"));
}

void AppController::setControlTargetRpm(int rpm) {
    m_controlRuntime.setTargetRpm(std::clamp(rpm, 0, controlPolicyMaxRpm()));
    refreshControlStatus(QStringLiteral("Control target updated"));
}

void AppController::setControlTargetSteeringDeg(double deg) {
    const double maxAbs = controlPolicyMaxAbsSteeringDeg();
    m_controlRuntime.setTargetSteeringDeg(std::clamp(deg, -maxAbs, maxAbs));
    refreshControlStatus(QStringLiteral("Control target updated"));
}

void AppController::sendControlBurst(int signedCommand,
                                     int rpm,
                                     double steeringDeg,
                                     quint8 motorMode,
                                     quint8 drivingMode,
                                     const QString& reason) {
    if (!m_connected || m_transportModeKey != QStringLiteral("typed")) {
        appendControlEvidenceEvent(QStringLiteral("BLOCKED"),
                                   QStringLiteral("error"),
                                   QStringLiteral("typed stream connection required"),
                                   QStringLiteral("Control request was not sent"));
        refreshControlStatus(QStringLiteral("Control blocked: typed stream connection required"));
        return;
    }
    if (!controlEvidenceReady()) {
        appendControlEvidenceEvent(QStringLiteral("BLOCKED"),
                                   QStringLiteral("error"),
                                   controlEvidenceBlockReason(),
                                   QStringLiteral("Control request was not sent"));
        refreshControlStatus(QStringLiteral("Control blocked: %1").arg(controlEvidenceBlockReason()));
        return;
    }
    if (!m_controlRuntime.armed() && motorMode != 0) {
        appendControlEvidenceEvent(QStringLiteral("BLOCKED"),
                                   QStringLiteral("error"),
                                   QStringLiteral("arm required before motion command"),
                                   QStringLiteral("Control request was not sent"));
        refreshControlStatus(QStringLiteral("Control blocked: arm required before motion command"));
        return;
    }

    const int safeRpm = std::clamp(rpm, 0, controlPolicyMaxRpm());
    const double maxAbsSteer = controlPolicyMaxAbsSteeringDeg();
    const double safeSteer = std::clamp(steeringDeg, -maxAbsSteer, maxAbsSteer);
    const auto frames = CanMonitorControl::ControlCommandEncoder::makeControlBurst(
        signedCommand,
        safeRpm,
        safeSteer,
        motorMode,
        drivingMode,
        m_controlRuntime.nextAliveCounter(),
        quint8(controlTargetBus()));

    QStringList summaries;
    summaries.reserve(frames.size());
    for (const auto& frame : frames) {
        const int frameIndex = summaries.size();
        const quint32 commandId = m_controlRuntime.nextCommandId();
        const QByteArray hostFrame = CanMonitorControl::ControlCommandEncoder::buildHostCanTxRequest(commandId, frame);
        const QString summary = QStringLiteral("#%1 %2 %3")
            .arg(commandId)
            .arg(reason)
            .arg(CanMonitorControl::ControlCommandEncoder::frameSummary(frame));
        const quint32 canId = frame.canId;
        const quint8 bus = frame.bus;
        summaries << summary;
        appendControlEvidenceEvent(QStringLiteral("REQUEST"),
                                   QStringLiteral("info"),
                                   reason,
                                   summary,
                                   commandId,
                                   canId,
                                   bus);
        QTimer::singleShot(frameIndex * kControlBurstFrameGapMs, Qt::PreciseTimer, this, [this, hostFrame, summary, commandId, canId, bus]() {
            queueControlHostFrame(hostFrame, summary, QString(), commandId, canId, bus);
        });
    }

    m_controlRuntime.setLastCommandSummary(summaries.join(QStringLiteral(" / ")));
    refreshControlStatus(QStringLiteral("Control request queued; success requires CAN_TX_RAW audit"));
}

void AppController::setControlTargetCommand(int signedCommand,
                                            int rpm,
                                            double steeringDeg,
                                            quint8 motorMode,
                                            quint8 drivingMode,
                                            const QString& reason) {
    const int safeRpm = std::clamp(rpm, 0, controlPolicyMaxRpm());
    const double maxAbsSteer = controlPolicyMaxAbsSteeringDeg();
    const double safeSteer = std::clamp(steeringDeg, -maxAbsSteer, maxAbsSteer);
    m_controlRuntime.setCurrentIntent(signedCommand, safeRpm, safeSteer, motorMode, drivingMode);
    m_controlRuntime.setStatusText(QStringLiteral("Control target: %1").arg(reason));
    emit controlStateChanged();
    if (m_controlRuntime.armed()) updateControlWorkerCycleTarget();
}

void AppController::updateControlWorkerCycleTarget() {
    const auto intent = m_controlRuntime.currentIntent();
    const quint8 bus = quint8(controlTargetBus());
    QString error;
    if (!m_transportRuntime.updateControlCycle(intent.signedCommand,
                                               intent.rpm,
                                               intent.steeringDeg,
                                               intent.motorMode,
                                               intent.drivingMode,
                                               bus,
                                               &error)) {
        refreshControlStatus(error);
    }
}

void AppController::sendControlKeepaliveTick(bool force, bool resetSlew) {
    if (!m_controlRuntime.armed() || !m_connected || m_transportModeKey != QStringLiteral("typed")) return;
    if (!controlEvidenceReady()) {
        m_controlRuntime.setArmed(false);
        stopControlKeepalive();
        refreshControlStatus(QStringLiteral("Control disarmed: %1").arg(controlEvidenceBlockReason()));
        return;
    }
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_controlRuntime.lastBurstWallMs() > 0 && nowWallMs - m_controlRuntime.lastBurstWallMs() < kControlMinBurstIntervalMs) {
        return;
    }
    m_controlRuntime.setLastBurstWallMs(nowWallMs);
    if (force) {
        const auto intent = m_controlRuntime.currentIntent();
        const quint8 bus = quint8(controlTargetBus());
        QString error;
        if (!m_transportRuntime.sendControlCycleBurstOnce(intent.signedCommand,
                                                          intent.rpm,
                                                          intent.steeringDeg,
                                                          intent.motorMode,
                                                          intent.drivingMode,
                                                          bus,
                                                          QStringLiteral("worker forced burst"),
                                                          resetSlew,
                                                          &error)) {
            refreshControlStatus(error);
        }
    } else {
        updateControlWorkerCycleTarget();
    }
}

void AppController::startControlKeepalive() {
    m_controlKeepaliveTimer.stop();
    const auto intent = m_controlRuntime.currentIntent();
    const quint8 bus = quint8(controlTargetBus());
    QString error;
    if (!m_transportRuntime.startControlCycle(intent.signedCommand,
                                              intent.rpm,
                                              intent.steeringDeg,
                                              intent.motorMode,
                                              intent.drivingMode,
                                              bus,
                                              kControlWorkerCyclePeriodMs,
                                              kControlBurstFrameGapMs,
                                              &error)) {
        refreshControlStatus(error);
        return;
    }
    m_controlRuntime.setLastBurstWallMs(QDateTime::currentMSecsSinceEpoch());
}

void AppController::stopControlKeepalive() {
    m_controlKeepaliveTimer.stop();
    m_transportRuntime.stopControlCycle();
}

void AppController::applyControlKeyboardHeldState(const QString& reason, bool forceBurst) {
    const bool forward = m_controlKeyboardHeldKeys.contains(QStringLiteral("w"))
        && !m_controlKeyboardHeldKeys.contains(QStringLiteral("s"));
    const bool reverse = m_controlKeyboardHeldKeys.contains(QStringLiteral("s"))
        && !m_controlKeyboardHeldKeys.contains(QStringLiteral("w"));
    const bool left = m_controlKeyboardHeldKeys.contains(QStringLiteral("a"))
        && !m_controlKeyboardHeldKeys.contains(QStringLiteral("d"));
    const bool right = m_controlKeyboardHeldKeys.contains(QStringLiteral("d"))
        && !m_controlKeyboardHeldKeys.contains(QStringLiteral("a"));

    const int targetRpm = controlTargetRpm();
    const int signedCommand = forward ? targetRpm : (reverse ? -targetRpm : 0);
    const int commandRpm = (forward || reverse) ? targetRpm : 0;
    const double keyboardSteer = std::min(kControlKeyboardSteerHoldDeg, controlPolicyMaxAbsSteeringDeg());
    const double steer = left ? -keyboardSteer : (right ? keyboardSteer : 0.0);
    const bool anyHeld = !m_controlKeyboardHeldKeys.isEmpty();
    const bool anyMotion = signedCommand != 0 || std::abs(steer) > 0.001;
    const quint8 drivingMode = anyMotion ? quint8(2) : quint8(1);

    m_controlRuntime.setTargetSteeringDeg(steer);
    setControlTargetCommand(signedCommand, commandRpm, steer, 1, drivingMode, reason);
    m_controlKeyboardSessionActive = anyHeld;
    sendControlKeepaliveTick(!anyMotion && forceBurst);
}

void AppController::controlKeyboardPress(const QString& key) {
    const QString normalized = normalizeControlKeyboardKey(key);
    if (normalized == QStringLiteral("space")) {
        controlEmergencyStop();
        return;
    }
    if (normalized == QStringLiteral("1")) {
        controlSendNeutral();
        return;
    }
    if (normalized == QStringLiteral("2")) {
        applyControlSteeringPreset(90.0, QStringLiteral("keyboard preset 2 crab +90"));
        return;
    }
    if (normalized == QStringLiteral("3")) {
        applyControlSteeringPreset(-90.0, QStringLiteral("keyboard preset 3 crab -90"));
        return;
    }
    if (normalized.isEmpty() || normalized == QStringLiteral("x")) {
        controlSendNeutral();
        return;
    }

    if (m_controlKeyboardHeldKeys.contains(normalized)) return;
    m_controlKeyboardHeldKeys.insert(normalized);
    m_controlKeyboardSessionActive = true;
    applyControlKeyboardHeldState(QStringLiteral("keyboard hold %1").arg(normalized.toUpper()), true);
}

void AppController::controlKeyboardRelease(const QString& key) {
    const QString normalized = normalizeControlKeyboardKey(key);
    if (normalized == QStringLiteral("space") ||
        normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("2") ||
        normalized == QStringLiteral("3")) {
        return;
    }
    if (normalized.isEmpty() || normalized == QStringLiteral("x")) {
        controlKeyboardReleaseAll();
        return;
    }

    const bool removed = m_controlKeyboardHeldKeys.remove(normalized);
    if (!removed && !m_controlKeyboardSessionActive) return;
    applyControlKeyboardHeldState(QStringLiteral("keyboard release %1").arg(normalized.toUpper()), true);
}

void AppController::controlKeyboardReleaseAll() {
    if (m_controlKeyboardHeldKeys.isEmpty() && !m_controlKeyboardSessionActive) return;
    m_controlKeyboardHeldKeys.clear();
    applyControlKeyboardHeldState(QStringLiteral("keyboard release all"), true);
}

void AppController::controlKeyboardCommand(const QString& key) {
    const QString normalized = normalizeControlKeyboardKey(key);
    if (normalized == QStringLiteral("space") ||
        normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("2") ||
        normalized == QStringLiteral("3")) {
        controlKeyboardPress(normalized);
        return;
    }
    if (normalized.isEmpty() || normalized == QStringLiteral("x")) {
        controlSendNeutral();
        return;
    }

    controlKeyboardPress(normalized);
    QTimer::singleShot(kControlKeyboardLegacyPulseMs, this, [this, normalized]() {
        controlKeyboardRelease(normalized);
    });
}

void AppController::controlSendManual() {
    m_controlKeyboardHeldKeys.clear();
    m_controlKeyboardSessionActive = false;
    const int rpm = controlTargetRpm();
    setControlTargetCommand(rpm, rpm, controlTargetSteeringDeg(), 1, 2, QStringLiteral("manual"));
    sendControlKeepaliveTick();
}

void AppController::controlSendNeutral() {
    sendControlSafetyStop(QStringLiteral("neutral"), false);
}

void AppController::controlEmergencyStop() {
    sendControlSafetyStop(QStringLiteral("operator space stop"), false);
}

void AppController::sendControlSafetyStop(const QString& reason, bool disarmAfter) {
    m_controlKeyboardHeldKeys.clear();
    m_controlKeyboardSessionActive = false;
    m_controlRuntime.setTestRunning(false);
    m_controlPatternTimer.stop();
    m_controlPatternSteps.clear();
    m_controlPatternIndex = 0;
    setControlTargetCommand(0, 0, 0.0, 1, 1, reason);
    sendControlKeepaliveTick(true, true);
    if (disarmAfter) {
        sendControlSession(CanMonitorControl::kControlSessionDisarm, reason);
        m_controlRuntime.setArmed(false);
        stopControlKeepalive();
        emit controlStateChanged();
    }
}

bool AppController::prepareControlSafeStopForDisconnect(const QString& reason) {
    const auto intent = m_controlRuntime.currentIntent();
    const bool hasActiveIntent = intent.signedCommand != 0 ||
        intent.rpm != 0 ||
        std::abs(intent.steeringDeg) > 0.001 ||
        m_controlRuntime.armed() ||
        m_controlRuntime.testRunning() ||
        m_controlKeyboardSessionActive ||
        !m_controlKeyboardHeldKeys.isEmpty();
    if (!hasActiveIntent) return false;
    sendControlSafetyStop(reason, true);
    return m_connected && m_transportModeKey == QStringLiteral("typed");
}

void AppController::applyControlSteeringPreset(double steeringDeg, const QString& reason) {
    m_controlKeyboardHeldKeys.clear();
    m_controlKeyboardSessionActive = false;
    m_controlRuntime.setTestRunning(false);
    m_controlPatternTimer.stop();
    m_controlPatternSteps.clear();
    m_controlPatternIndex = 0;
    m_controlRuntime.setTargetSteeringDeg(steeringDeg);
    setControlTargetCommand(0, 0, steeringDeg, 1, 6, reason);
    sendControlKeepaliveTick(true, true);
}

void AppController::controlRunPattern(const QString& patternKey) {
    if (!m_controlRuntime.armed()) {
        refreshControlStatus(QStringLiteral("Pattern blocked: arm required"));
        return;
    }
    m_controlKeyboardHeldKeys.clear();
    m_controlKeyboardSessionActive = false;
    const QString key = patternKey.trimmed().toLower();
    m_controlPatternSteps.clear();
    m_controlPatternIndex = 0;

    if (key == QStringLiteral("sweep")) {
        m_controlPatternSteps = {
            {600, -30.0, 1, 2, 1200, QStringLiteral("safe sweep -30")},
            {600, 30.0, 1, 2, 1400, QStringLiteral("safe sweep +30")},
            {0, 0.0, 1, 1, 900, QStringLiteral("safe sweep neutral")}
        };
    } else if (key == QStringLiteral("variable")) {
        m_controlPatternSteps = {
            {500, -20.0, 1, 2, 1000, QStringLiteral("variable low -20")},
            {1200, 20.0, 1, 2, 1200, QStringLiteral("variable mid +20")},
            {1800, -35.0, 1, 4, 1400, QStringLiteral("variable four-wheel -35")},
            {0, 0.0, 1, 1, 900, QStringLiteral("variable neutral")}
        };
    } else if (key == QStringLiteral("spin")) {
        m_controlPatternSteps = {
            {500, -45.0, 1, 7, 1600, QStringLiteral("low-speed pivot -45")},
            {500, 45.0, 1, 7, 1800, QStringLiteral("low-speed pivot +45")},
            {0, 0.0, 1, 1, 1000, QStringLiteral("pivot neutral")}
        };
    } else {
        refreshControlStatus(QStringLiteral("Unknown control pattern: %1").arg(patternKey));
        return;
    }

    m_controlRuntime.setTestRunning(true);
    refreshControlStatus(QStringLiteral("Control pattern started: %1").arg(key));
    processControlPatternStep();
}

void AppController::controlStopPattern() {
    m_controlRuntime.setTestRunning(false);
    m_controlPatternTimer.stop();
    m_controlPatternSteps.clear();
    m_controlPatternIndex = 0;
    controlSendNeutral();
}

void AppController::processControlPatternStep() {
    if (!m_controlRuntime.testRunning() || m_controlPatternSteps.isEmpty()) return;
    if (m_controlPatternIndex >= m_controlPatternSteps.size()) {
        m_controlRuntime.setTestRunning(false);
        m_controlPatternSteps.clear();
        m_controlPatternIndex = 0;
        controlSendNeutral();
        refreshControlStatus(QStringLiteral("Control pattern completed"));
        return;
    }

    const ControlPatternStep step = m_controlPatternSteps.at(m_controlPatternIndex++);
    const int signedCommand = step.motorMode == 2 ? -step.rpm : step.rpm;
    m_controlRuntime.setTargetRpm(step.rpm);
    m_controlRuntime.setTargetSteeringDeg(step.steeringDeg);
    setControlTargetCommand(signedCommand, step.rpm, step.steeringDeg, step.motorMode, step.drivingMode, step.label);
    sendControlKeepaliveTick();
    if (m_controlRuntime.testRunning()) m_controlPatternTimer.start(std::max(120, step.holdMs));
}

QString AppController::replayCurrentTimeText() const {
    if (!m_replayLoaded) return QStringLiteral("-");
    const quint64 currentUs = replayAnalysisUs();
    const QString absText = timeTextForSourceUs(QStringLiteral("replay"), currentUs);
    if (m_replayBaseFrameUs > 0 && currentUs >= m_replayBaseFrameUs) {
        return QStringLiteral("%1 (+%2)").arg(absText, fmtReplayUs(currentUs - m_replayBaseFrameUs));
    }
    return absText;
}

QString AppController::replayDurationText() const {
    return fmtReplayUs(m_replayDurationUs);
}

QString AppController::graphOverviewStartText() const {
    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty()) return QStringLiteral("-");
    return timeTextForSourceUs(QStringLiteral("replay"), frames.front().tExtUs);
}

QString AppController::graphOverviewEndText() const {
    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty()) return QStringLiteral("-");
    return timeTextForSourceUs(QStringLiteral("replay"), frames.back().tExtUs);
}

QString AppController::graphOverviewDurationText() const {
    return m_replayLoaded ? replayDurationText() : QStringLiteral("-");
}

double AppController::graphOverviewCursorMs() const {
    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty()) return -1.0;
    const quint64 startUs = frames.front().tExtUs;
    quint64 cursorUs = replaySnapshotDisplayedUs();
    if (cursorUs == 0) cursorUs = (m_replayDisplayedUs > 0) ? m_replayDisplayedUs : m_replay.currentUs();
    if (cursorUs <= startUs) return 0.0;
    return double(cursorUs - startUs) / 1000.0;
}

QString AppController::analysisSourceText() const {
    if (replayAnalysisActive()) {
        if (m_replayRebuildActive) return QStringLiteral("재생 분석 · 재구성 중 (표시 스냅샷 %1 / 목표 %2)").arg(std::max(0, m_replaySnapshotAnalyzedIndex + 1)).arg(std::max(0, m_replayRebuildTargetIndex + 1));
        if (m_replayPlaying) return QStringLiteral("재생 분석 · 재생 중");
        if (m_replayAnalysisHeld) return QStringLiteral("재생 분석 · 정지 고정");
        return QStringLiteral("재생 분석 · 대기 고정");
    }
    return m_liveUiPaused ? QStringLiteral("라이브 분석 · 정지 고정") : QStringLiteral("라이브 분석");
}

QString AppController::liveStatsSummary() const {
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    QString inputState;
    if (!m_connected) inputState = QStringLiteral("live 미연결");
    else if (m_lastLiveFrameWallMs > 0 && (nowWallMs - m_lastLiveFrameWallMs) <= 1200) inputState = QStringLiteral("live frame 수신 중 · frame %1 전").arg(fmtWallAge(nowWallMs - m_lastLiveFrameWallMs));
    else if (m_lastLiveStatsWallMs > 0 && (nowWallMs - m_lastLiveStatsWallMs) <= 2000) inputState = QStringLiteral("stats 최근 수신 · stats %1 전 / frame 지연").arg(fmtWallAge(nowWallMs - m_lastLiveStatsWallMs));
    else inputState = QStringLiteral("포트 연결 / 최근 frame 없음");
    QStringList parts;
    parts << inputState;
    parts << QStringLiteral("RX %1 fps").arg(liveRxFps());
    parts << QStringLiteral("TX %1 fps").arg(liveTxFps());
    parts << QStringLiteral("DROP %1").arg(droppedTotal());
    parts << QStringLiteral("FIFO %1").arg(fifoOverflowTotal());
    parts << QStringLiteral("EP %1").arg(errPassiveCount());
    parts << QStringLiteral("BO %1").arg(busOffCount());
    if (pendingLiveFrameCount() > 0) parts << QStringLiteral("큐 %1").arg(pendingLiveFrameCount());
    if (m_liveProjectionWorkerSampledFrames > 0) parts << QStringLiteral("projection 샘플링 %1").arg(m_liveProjectionWorkerSampledFrames);
    if (m_liveProjectionSampledControlEvidenceRecords > 0) parts << QStringLiteral("control 샘플링 %1").arg(m_liveProjectionSampledControlEvidenceRecords);
    if (m_liveSampledViewDrops > 0) parts << QStringLiteral("표시 샘플링 %1").arg(m_liveSampledViewDrops);
    const quint64 projectionDrops = m_liveProjectionDroppedFrames + m_liveProjectionWorkerDroppedFrames;
    if (projectionDrops > 0) parts << QStringLiteral("projection drop %1").arg(projectionDrops);
    return joinNonEmpty(parts);
}

QString AppController::normalizeOverviewBody(const QString& text) {
    QString out = text;
    out.replace(QLatin1Char('\n'), QStringLiteral(" · "));
    out = out.simplified();
    if (out.length() > 84) out = out.left(81) + QStringLiteral("...");
    return out;
}


bool AppController::replayAnalysisActive() const {
    return m_replayLoaded && (!m_connected || m_liveUiPaused || m_replayPlaying || m_replayAnalysisHeld);
}

bool AppController::analysisPaused() const {
    if (replayAnalysisActive()) return !m_replayPlaying;
    return m_liveUiPaused;
}

void AppController::setReplayAnalysisHeld(bool held) {
    if (m_replayAnalysisHeld == held) return;
    const bool previousReplayActive = replayAnalysisActive();
    m_replayAnalysisHeld = held;
    emit replayStateChanged();
    handleAnalysisSourceMaybeChanged(previousReplayActive);
    requestGraphRefresh(true);
    emit derivedSummaryChanged();
}

QHash<quint32, AppController::IdState>& AppController::stateMapForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_replayStates : m_liveStates;
}

const QHash<quint32, AppController::IdState>& AppController::activeStateMap() const {
    return replayAnalysisActive() ? m_replayStates : m_liveStates;
}

QHash<quint32, AppController::IdState>& AppController::activeStateMap() {
    return replayAnalysisActive() ? m_replayStates : m_liveStates;
}

QVector<CanMonitorAnalysis::AlarmGroup>& AppController::alarmGroupsForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_replayAlarmGroups : m_liveAlarmGroups;
}

const QVector<CanMonitorAnalysis::AlarmGroup>& AppController::activeAlarmGroups() const {
    return replayAnalysisActive() ? m_replayAlarmGroups : m_liveAlarmGroups;
}

QVector<CanMonitorAnalysis::AlarmGroup>& AppController::activeAlarmGroups() {
    return replayAnalysisActive() ? m_replayAlarmGroups : m_liveAlarmGroups;
}

QString AppController::activeAnalysisSourceKey() const {
    return replayAnalysisActive() ? QStringLiteral("replay") : QStringLiteral("live");
}

AppController::AnalysisViewState& AppController::viewStateForSource(const QString& source) {
    return source == QStringLiteral("replay") ? m_replayViewState : m_liveViewState;
}

const AppController::AnalysisViewState& AppController::viewStateForSource(const QString& source) const {
    return source == QStringLiteral("replay") ? m_replayViewState : m_liveViewState;
}

bool AppController::applyViewState(const AnalysisViewState& state) {
    bool filterStateChanged = false;
    auto assignText = [&filterStateChanged](QString& target, const QString& value) {
        if (target == value) return;
        target = value;
        filterStateChanged = true;
    };

    assignText(m_timingFilterId, state.timingFilterId);
    assignText(m_timingFilterSeverity, state.timingFilterSeverity);
    assignText(m_timingFilterName, state.timingFilterName);
    assignText(m_timingFilterReason, state.timingFilterReason);
    assignText(m_timingFilterExpected, state.timingFilterExpected);
    assignText(m_timingFilterGap, state.timingFilterGap);
    assignText(m_timingFilterAge, state.timingFilterAge);
    assignText(m_timingFilterSource, state.timingFilterSource);
    assignText(m_valueFilterId, state.valueFilterId);
    assignText(m_valueFilterSeverity, state.valueFilterSeverity);
    assignText(m_valueFilterName, state.valueFilterName);
    assignText(m_valueFilterSource, state.valueFilterSource);
    assignText(m_valueFilterRaw, state.valueFilterRaw);
    assignText(m_valueFilterGap, state.valueFilterGap);
    assignText(m_valueFilterReason, state.valueFilterReason);
    assignText(m_alarmFilterId, state.alarmFilterId);
    assignText(m_alarmFilterSeverity, state.alarmFilterSeverity);
    assignText(m_alarmFilterTime, state.alarmFilterTime);
    assignText(m_alarmFilterName, state.alarmFilterName);
    assignText(m_alarmFilterSource, state.alarmFilterSource);
    assignText(m_alarmFilterMessage, state.alarmFilterMessage);
    assignText(m_alarmFilterText, state.alarmFilterText);

    bool selectionChanged = false;
    if (m_hasSelectedValueId != state.hasSelectedValueId || m_selectedValueCanId != state.selectedValueCanId) {
        m_hasSelectedValueId = state.hasSelectedValueId;
        m_selectedValueCanId = state.selectedValueCanId;
        selectionChanged = true;
        m_lastValueDetailSignature.clear();
        m_lastValueDetailProjectionWallMs = -1;
        m_valueDetailsDirty = true;
    }

    if (filterStateChanged) emit filtersChanged();
    if (selectionChanged) emit selectedValueIdChanged();
    return filterStateChanged || selectionChanged;
}

void AppController::syncActiveViewSelection() {
    AnalysisViewState& state = viewStateForSource(activeAnalysisSourceKey());
    state.hasSelectedValueId = m_hasSelectedValueId;
    state.selectedValueCanId = m_selectedValueCanId;
}

void AppController::markAllAnalysisDirty(bool reorder) {
    m_timingRowsDirty = true;
    m_valueRowsDirty = true;
    m_alarmRowsDirty = true;
    m_valueDetailsDirty = true;
    m_lastValueDetailSignature.clear();
    m_lastValueDetailProjectionWallMs = -1;
    m_lastLiveTimingEvalCacheWallMs = -1;
    m_lastReplayTimingEvalCacheWallMs = -1;
    if (reorder) {
        m_timingReorderRequested = true;
        m_valueReorderRequested = true;
        m_alarmReorderRequested = true;
    }
    for (auto it = m_liveStates.begin(); it != m_liveStates.end(); ++it) {
        it.value().timingDerivedDirty = true;
        it.value().valueDerivedDirty = true;
    }
    for (auto it = m_replayStates.begin(); it != m_replayStates.end(); ++it) {
        it.value().timingDerivedDirty = true;
        it.value().valueDerivedDirty = true;
    }
}

void AppController::handleAnalysisSourceMaybeChanged(bool previousReplayActive) {
    const bool currentReplayActive = replayAnalysisActive();
    if (previousReplayActive == currentReplayActive) return;
    applyViewState(viewStateForSource(activeAnalysisSourceKey()));
    markAllAnalysisDirty(true);
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    requestDerivedSummaryRefresh(true);
    requestGraphRefresh(true);
}

void AppController::refreshDerivedSummaryCache() {
    auto buildCache = [this](const StableMapListModel& model, const QString& textField) {
        SummaryCache cache;
        cache.level = CanMonitorAnalysis::LevelState::fromModel(model, textField);
        cache.level.summary = normalizeOverviewBody(cache.level.summary);
        const int rc = model.rowCount();
        if (rc > 0) cache.topId = model.get(0).value(QStringLiteral("idText")).toString();
        for (int i = 0; i < rc; ++i) {
            const QVariantMap row = model.get(i);
            const QString sev = row.value(QStringLiteral("severity")).toString();
            if (sev == QStringLiteral("ERR")) ++cache.errCount;
            else if (sev == QStringLiteral("WARN")) ++cache.warnCount;
        }
        return cache;
    };

    m_timingSummaryCache = buildCache(m_timingModel, QStringLiteral("reason"));
    m_valueSummaryCache = buildCache(m_valueModel, QStringLiteral("summaryText"));
    m_alarmSummaryCache = buildCache(m_alarmModel, QStringLiteral("message"));

    m_systemSummaryCache.activeCount = m_timingSummaryCache.level.activeCount
                                     + m_valueSummaryCache.level.activeCount
                                     + m_alarmSummaryCache.level.activeCount;

    QString systemLevelText = QStringLiteral("NONE");
    QString systemSummaryText = QStringLiteral("표시 항목 없음");

    const auto pickSystem = [&](const QString& level, const QString& summary) {
        systemLevelText = level;
        systemSummaryText = normalizeOverviewBody(summary);
    };

    if (busHealthLevel() == QStringLiteral("ERR")) {
        pickSystem(QStringLiteral("ERR"), QStringLiteral("[ERR] BUS %1").arg(busHealthText()));
    } else if (m_alarmSummaryCache.level.level == QStringLiteral("ERR")) {
        pickSystem(QStringLiteral("ERR"), m_alarmSummaryCache.level.summary);
    } else if (m_valueSummaryCache.level.level == QStringLiteral("ERR")) {
        pickSystem(QStringLiteral("ERR"), m_valueSummaryCache.level.summary);
    } else if (m_timingSummaryCache.level.level == QStringLiteral("ERR")) {
        pickSystem(QStringLiteral("ERR"), m_timingSummaryCache.level.summary);
    } else if (busHealthLevel() == QStringLiteral("WARN")) {
        pickSystem(QStringLiteral("WARN"), QStringLiteral("[WARN] BUS %1").arg(busHealthText()));
    } else if (m_alarmSummaryCache.level.level == QStringLiteral("WARN")) {
        pickSystem(QStringLiteral("WARN"), m_alarmSummaryCache.level.summary);
    } else if (m_valueSummaryCache.level.level == QStringLiteral("WARN")) {
        pickSystem(QStringLiteral("WARN"), m_valueSummaryCache.level.summary);
    } else if (m_timingSummaryCache.level.level == QStringLiteral("WARN")) {
        pickSystem(QStringLiteral("WARN"), m_timingSummaryCache.level.summary);
    } else if (m_timingModel.rowCount() > 0 || m_valueModel.rowCount() > 0 || m_alarmModel.rowCount() > 0) {
        pickSystem(QStringLiteral("OK"), QStringLiteral("활성 이슈 없음 · 현재 분석 기준 유지"));
    }

    m_systemSummaryCache.level = systemLevelText;
    m_systemSummaryCache.color = CanMonitorAnalysis::LevelState::colorForLevel(systemLevelText);
    m_systemSummaryCache.summary = systemSummaryText;
}

void AppController::requestDerivedSummaryRefresh(bool immediate) {
    if (immediate) {
        m_derivedSummaryDirty = false;
        if (m_derivedSummaryTimer.isActive()) m_derivedSummaryTimer.stop();
        refreshDerivedSummaryCache();
        emit derivedSummaryChanged();
        return;
    }

    m_derivedSummaryDirty = true;
    if (m_derivedSummaryTimer.isActive()) return;

    int delayMs = 180;
    if (projectionBackpressureActive()) delayMs = std::max(delayMs, 320);
    m_derivedSummaryTimer.start(delayMs);
}

void AppController::flushDerivedSummaryRefresh() {
    if (!m_derivedSummaryDirty) return;
    m_derivedSummaryDirty = false;
    refreshDerivedSummaryCache();
    emit derivedSummaryChanged();
}

void AppController::requestLogStateRefresh(bool immediate) {
    if (immediate) {
        m_logStateDirty = false;
        if (m_logStateTimer.isActive()) m_logStateTimer.stop();
        emit logStateChanged();
        return;
    }

    m_logStateDirty = true;
    if (m_logStateTimer.isActive()) return;

    int delayMs = 220;
    if (projectionBackpressureActive()) delayMs = std::max(delayMs, 320);
    m_logStateTimer.start(delayMs);
}

void AppController::flushLogStateRefresh() {
    if (!m_logStateDirty) return;
    m_logStateDirty = false;
    emit logStateChanged();
}

void AppController::requestLiveStatsRefresh(bool immediate) {
    if (immediate) {
        m_liveStatsDirty = false;
        if (m_liveStatsTimer.isActive()) m_liveStatsTimer.stop();
        updateTransportDiagnostics();
        emit liveStatsChanged();
        emit transportDiagnosticsChanged();
        return;
    }

    m_liveStatsDirty = true;
    if (m_liveStatsTimer.isActive()) return;

    int delayMs = 120;
    if (projectionBackpressureActive()) delayMs = std::max(delayMs, 180);
    m_liveStatsTimer.start(delayMs);
}

void AppController::flushLiveStatsRefresh() {
    if (!m_liveStatsDirty) return;
    m_liveStatsDirty = false;
    updateTransportDiagnostics();
    emit liveStatsChanged();
    emit transportDiagnosticsChanged();
}

void AppController::updateTransportDiagnostics() {
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    m_transportSession.updateCaptureStorage(m_logRecordingActive, m_logRecordedBytes, m_logRecordedFrameCount);
    m_transportSession.updateBoardHealth(m_lastStats.droppedTotal,
                                         m_lastStats.fifoOverflowTotal,
                                         m_lastLiveStatsWallMs > 0 ? nowWallMs - m_lastLiveStatsWallMs : -1);
    m_transportSession.updateLiveRuntime(nowWallMs,
                                         m_lastLiveFrameWallMs,
                                         m_lastLiveStatsWallMs,
                                         int(pendingLiveFrameCount()),
                                         m_liveSampledViewDrops,
                                         m_liveProjectionProjectedFrames,
                                         m_liveProjectionWorkerSampledFrames,
                                         m_liveProjectionDroppedFrames + m_liveProjectionWorkerDroppedFrames,
                                         m_liveProjectionObservedControlEvidenceRecords,
                                         m_liveProjectionProjectedControlEvidenceRecords,
                                         m_liveProjectionSampledControlEvidenceRecords,
                                         m_liveProjectionMaxBacklog,
                                         m_liveProjectionFlushBudgetHits,
                                         m_liveProjectionLastFlushMs);
}

void AppController::restoreSessionState() {
    m_restoringSession = true;

    {
        const UiStateStore store;
        const AppUiState::Snapshot snapshot = store.load(m_session);

        if (!snapshot.modelEnabled) clearModel();
        else if (!snapshot.modelPath.trimmed().isEmpty()) setRulesPath(snapshot.modelPath);
        else if (snapshot.modelBundled) {
            const bool bundledAlreadyActive =
                m_rulesUsingBundled &&
                m_rulesActivePath == kBundledModelPath &&
                !m_rules.isEmpty();
            if (!bundledAlreadyActive) useBundledModel();
        }

        setTimingSortMode(snapshot.timingSortMode);
        setTimingSortDescending(snapshot.timingSortDescending);
        setValueSortMode(snapshot.valueSortMode);
        setValueSortDescending(snapshot.valueSortDescending);
        setAlarmSortMode(snapshot.alarmSortMode);
        setAlarmSortDescending(snapshot.alarmSortDescending);

        auto applyStoredViewState = [this](const AppUiState::AnalysisViewStateSnapshot& stored, AnalysisViewState& state) {
            state.timingFilterId = stored.timingFilterId;
            state.timingFilterSeverity = stored.timingFilterSeverity;
            state.timingFilterName = stored.timingFilterName;
            state.timingFilterReason = stored.timingFilterReason;
            state.timingFilterExpected = stored.timingFilterExpected;
            state.timingFilterGap = stored.timingFilterGap;
            state.timingFilterAge = stored.timingFilterAge;
            state.timingFilterSource = stored.timingFilterSource;
            state.valueFilterId = stored.valueFilterId;
            state.valueFilterSeverity = stored.valueFilterSeverity;
            state.valueFilterName = stored.valueFilterName;
            state.valueFilterSource = stored.valueFilterSource;
            state.valueFilterRaw = stored.valueFilterRaw;
            state.valueFilterGap = stored.valueFilterGap;
            state.valueFilterReason = stored.valueFilterReason;
            state.alarmFilterId = stored.alarmFilterId;
            state.alarmFilterSeverity = stored.alarmFilterSeverity;
            state.alarmFilterTime = stored.alarmFilterTime;
            state.alarmFilterName = stored.alarmFilterName;
            state.alarmFilterSource = stored.alarmFilterSource;
            state.alarmFilterMessage = stored.alarmFilterMessage;
            state.alarmFilterText = stored.alarmFilterText;
            quint32 selectedCanId = 0;
            state.hasSelectedValueId = parseCanIdText(stored.selectedValueId, &selectedCanId);
            state.selectedValueCanId = state.hasSelectedValueId ? selectedCanId : 0;
        };

        applyStoredViewState(snapshot.liveViewState, m_liveViewState);
        applyStoredViewState(snapshot.replayViewState, m_replayViewState);
        applyViewState(viewStateForSource(activeAnalysisSourceKey()));

        m_liveFrameView.setIdFilter(snapshot.liveFrameIdFilter);
        m_replayFrameView.setIdFilter(snapshot.replayFrameIdFilter);
        m_liveFrameView.setBusFilter(snapshot.liveFrameBusFilter);
        m_replayFrameView.setBusFilter(snapshot.replayFrameBusFilter);
        m_replaySpeed = snapshot.replaySpeed;
        m_replayLoop = snapshot.replayLoop;
        m_replay.setLoop(m_replayLoop);
        m_logTargetDirectory = snapshot.logTargetDirectory.trimmed().isEmpty()
            ? defaultLogDirectory()
            : QDir::fromNativeSeparators(RuntimePaths::normalizeLocalPath(snapshot.logTargetDirectory));
        m_logTargetName = snapshot.logTargetName.trimmed();
        setLiveUiPaused(snapshot.liveUiPaused);
        m_replayRuntime.restoreSession(m_session);

        m_restoringSession = false;
        refreshTimingRows();
        refreshValueRows();
        refreshAlarmRows();
        emit derivedSummaryChanged();
        return;
    }

    const bool modelEnabled = m_session.value(QStringLiteral("model/enabled"), true).toBool();
    const bool modelBundled = m_session.value(QStringLiteral("model/useBundled"), true).toBool();
    const QString modelPath = m_session.value(QStringLiteral("model/path")).toString();
    if (!modelEnabled) clearModel();
    else if (!modelPath.trimmed().isEmpty()) setRulesPath(modelPath);
    else if (modelBundled) useBundledModel();

    setTimingSortMode(m_session.value(QStringLiteral("sort/timingMode"), m_timingSortMode).toString());
    setTimingSortDescending(m_session.value(QStringLiteral("sort/timingDesc"), m_timingSortDescending).toBool());
    setValueSortMode(m_session.value(QStringLiteral("sort/valueMode"), m_valueSortMode).toString());
    setValueSortDescending(m_session.value(QStringLiteral("sort/valueDesc"), m_valueSortDescending).toBool());
    setAlarmSortMode(m_session.value(QStringLiteral("sort/alarmMode"), m_alarmSortMode).toString());
    setAlarmSortDescending(m_session.value(QStringLiteral("sort/alarmDesc"), m_alarmSortDescending).toBool());

    auto loadViewState = [this](const QString& prefix, AnalysisViewState& state) {
        state.timingFilterId = m_session.value(prefix + QStringLiteral("/timingId")).toString();
        state.timingFilterSeverity = m_session.value(prefix + QStringLiteral("/timingSeverity")).toString();
        state.timingFilterName = m_session.value(prefix + QStringLiteral("/timingName")).toString();
        state.timingFilterReason = m_session.value(prefix + QStringLiteral("/timingReason")).toString();
        state.timingFilterExpected = m_session.value(prefix + QStringLiteral("/timingExpected")).toString();
        state.timingFilterGap = m_session.value(prefix + QStringLiteral("/timingGap")).toString();
        state.timingFilterAge = m_session.value(prefix + QStringLiteral("/timingAge")).toString();
        state.timingFilterSource = m_session.value(prefix + QStringLiteral("/timingSource")).toString();
        state.valueFilterId = m_session.value(prefix + QStringLiteral("/valueId")).toString();
        state.valueFilterSeverity = m_session.value(prefix + QStringLiteral("/valueSeverity")).toString();
        state.valueFilterName = m_session.value(prefix + QStringLiteral("/valueName")).toString();
        state.valueFilterSource = m_session.value(prefix + QStringLiteral("/valueSource")).toString();
        state.valueFilterRaw = m_session.value(prefix + QStringLiteral("/valueRaw")).toString();
        state.valueFilterGap = m_session.value(prefix + QStringLiteral("/valueGap")).toString();
        state.valueFilterReason = m_session.value(prefix + QStringLiteral("/valueReason")).toString();
        state.alarmFilterId = m_session.value(prefix + QStringLiteral("/alarmId")).toString();
        state.alarmFilterSeverity = m_session.value(prefix + QStringLiteral("/alarmSeverity")).toString();
        state.alarmFilterTime = m_session.value(prefix + QStringLiteral("/alarmTime")).toString();
        state.alarmFilterName = m_session.value(prefix + QStringLiteral("/alarmName")).toString();
        state.alarmFilterSource = m_session.value(prefix + QStringLiteral("/alarmSource")).toString();
        state.alarmFilterMessage = m_session.value(prefix + QStringLiteral("/alarmMessage")).toString();
        state.alarmFilterText = m_session.value(prefix + QStringLiteral("/alarmText")).toString();
        const QString selectedId = m_session.value(prefix + QStringLiteral("/selectedValueId")).toString();
        quint32 selectedCanId = 0;
        state.hasSelectedValueId = parseCanIdText(selectedId, &selectedCanId);
        state.selectedValueCanId = state.hasSelectedValueId ? selectedCanId : 0;
    };

    loadViewState(QStringLiteral("filter/live"), m_liveViewState);
    loadViewState(QStringLiteral("filter/replay"), m_replayViewState);
    if (m_liveViewState.timingFilterId.isEmpty() && m_liveViewState.valueFilterId.isEmpty() && m_liveViewState.alarmFilterId.isEmpty() &&
        m_liveViewState.timingFilterSeverity.isEmpty() && m_liveViewState.valueFilterSeverity.isEmpty() && m_liveViewState.alarmFilterSeverity.isEmpty() &&
        m_liveViewState.timingFilterName.isEmpty() && m_liveViewState.valueFilterName.isEmpty() && m_liveViewState.alarmFilterName.isEmpty() &&
        m_liveViewState.timingFilterReason.isEmpty() && m_liveViewState.valueFilterReason.isEmpty() && m_liveViewState.alarmFilterMessage.isEmpty() &&
        m_liveViewState.timingFilterExpected.isEmpty() && m_liveViewState.timingFilterGap.isEmpty() && m_liveViewState.timingFilterAge.isEmpty() &&
        m_liveViewState.timingFilterSource.isEmpty() && m_liveViewState.valueFilterSource.isEmpty() && m_liveViewState.valueFilterRaw.isEmpty() &&
        m_liveViewState.valueFilterGap.isEmpty() && m_liveViewState.alarmFilterTime.isEmpty() && m_liveViewState.alarmFilterSource.isEmpty() &&
        m_liveViewState.alarmFilterText.isEmpty()) {
        // 이전 세션 형식 호환: 구버전 단일 필터를 live 컨텍스트로 흡수
        m_liveViewState.timingFilterId = m_session.value(QStringLiteral("filter/timingId")).toString();
        m_liveViewState.timingFilterSeverity = m_session.value(QStringLiteral("filter/timingSeverity")).toString();
        m_liveViewState.timingFilterName = m_session.value(QStringLiteral("filter/timingName")).toString();
        m_liveViewState.timingFilterReason = m_session.value(QStringLiteral("filter/timingReason")).toString();
        m_liveViewState.timingFilterExpected = m_session.value(QStringLiteral("filter/timingExpected")).toString();
        m_liveViewState.timingFilterGap = m_session.value(QStringLiteral("filter/timingGap")).toString();
        m_liveViewState.timingFilterAge = m_session.value(QStringLiteral("filter/timingAge")).toString();
        m_liveViewState.timingFilterSource = m_session.value(QStringLiteral("filter/timingSource")).toString();
        m_liveViewState.valueFilterId = m_session.value(QStringLiteral("filter/valueId")).toString();
        m_liveViewState.valueFilterSeverity = m_session.value(QStringLiteral("filter/valueSeverity")).toString();
        m_liveViewState.valueFilterName = m_session.value(QStringLiteral("filter/valueName")).toString();
        m_liveViewState.valueFilterSource = m_session.value(QStringLiteral("filter/valueSource")).toString();
        m_liveViewState.valueFilterRaw = m_session.value(QStringLiteral("filter/valueRaw")).toString();
        m_liveViewState.valueFilterGap = m_session.value(QStringLiteral("filter/valueGap")).toString();
        m_liveViewState.valueFilterReason = m_session.value(QStringLiteral("filter/valueReason")).toString();
        m_liveViewState.alarmFilterId = m_session.value(QStringLiteral("filter/alarmId")).toString();
        m_liveViewState.alarmFilterSeverity = m_session.value(QStringLiteral("filter/alarmSeverity")).toString();
        m_liveViewState.alarmFilterTime = m_session.value(QStringLiteral("filter/alarmTime")).toString();
        m_liveViewState.alarmFilterName = m_session.value(QStringLiteral("filter/alarmName")).toString();
        m_liveViewState.alarmFilterSource = m_session.value(QStringLiteral("filter/alarmSource")).toString();
        m_liveViewState.alarmFilterMessage = m_session.value(QStringLiteral("filter/alarmMessage")).toString();
        m_liveViewState.alarmFilterText = m_session.value(QStringLiteral("filter/alarmText")).toString();
    }

    applyViewState(viewStateForSource(activeAnalysisSourceKey()));

    m_liveFrameView.setIdFilter(m_session.value(QStringLiteral("frame/liveIdFilter")).toString());
    m_replayFrameView.setIdFilter(m_session.value(QStringLiteral("frame/replayIdFilter")).toString());
    m_liveFrameView.setBusFilter(m_session.value(QStringLiteral("frame/liveBusFilter"), -1).toInt());
    m_replayFrameView.setBusFilter(m_session.value(QStringLiteral("frame/replayBusFilter"), -1).toInt());
    m_replaySpeed = m_session.value(QStringLiteral("replay/speed"), 1.0).toDouble();
    m_replayLoop = m_session.value(QStringLiteral("replay/loop"), false).toBool();
    m_replay.setLoop(m_replayLoop);
    m_logTargetDirectory = RuntimePaths::normalizeLocalPath(m_session.value(QStringLiteral("log/targetDirectory"), defaultLogDirectory()).toString());
    if (m_logTargetDirectory.trimmed().isEmpty()) m_logTargetDirectory = defaultLogDirectory();
    m_logTargetName = m_session.value(QStringLiteral("log/targetName")).toString().trimmed();
    setLiveUiPaused(m_session.value(QStringLiteral("ui/livePaused"), false).toBool());

    m_restoringSession = false;
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    emit derivedSummaryChanged();
}

void AppController::saveSessionState() const {
    if (m_restoringSession) return;

    {
        auto captureViewState = [this](const AnalysisViewState& state) {
            AppUiState::AnalysisViewStateSnapshot stored;
            stored.timingFilterId = state.timingFilterId;
            stored.timingFilterSeverity = state.timingFilterSeverity;
            stored.timingFilterName = state.timingFilterName;
            stored.timingFilterReason = state.timingFilterReason;
            stored.timingFilterExpected = state.timingFilterExpected;
            stored.timingFilterGap = state.timingFilterGap;
            stored.timingFilterAge = state.timingFilterAge;
            stored.timingFilterSource = state.timingFilterSource;
            stored.valueFilterId = state.valueFilterId;
            stored.valueFilterSeverity = state.valueFilterSeverity;
            stored.valueFilterName = state.valueFilterName;
            stored.valueFilterSource = state.valueFilterSource;
            stored.valueFilterRaw = state.valueFilterRaw;
            stored.valueFilterGap = state.valueFilterGap;
            stored.valueFilterReason = state.valueFilterReason;
            stored.alarmFilterId = state.alarmFilterId;
            stored.alarmFilterSeverity = state.alarmFilterSeverity;
            stored.alarmFilterTime = state.alarmFilterTime;
            stored.alarmFilterName = state.alarmFilterName;
            stored.alarmFilterSource = state.alarmFilterSource;
            stored.alarmFilterMessage = state.alarmFilterMessage;
            stored.alarmFilterText = state.alarmFilterText;
            stored.selectedValueId = state.hasSelectedValueId ? idText(state.selectedValueCanId) : QString();
            return stored;
        };

        AppUiState::Snapshot snapshot;
        snapshot.modelEnabled = m_modelEnabled;
        snapshot.modelBundled = m_rulesUsingBundled;
        snapshot.modelPath = m_rulesPath;
        snapshot.timingSortMode = m_timingSortMode;
        snapshot.timingSortDescending = m_timingSortDescending;
        snapshot.valueSortMode = m_valueSortMode;
        snapshot.valueSortDescending = m_valueSortDescending;
        snapshot.alarmSortMode = m_alarmSortMode;
        snapshot.alarmSortDescending = m_alarmSortDescending;
        snapshot.liveViewState = captureViewState(m_liveViewState);
        snapshot.replayViewState = captureViewState(m_replayViewState);
        snapshot.liveFrameIdFilter = m_liveFrameView.idFilter();
        snapshot.replayFrameIdFilter = m_replayFrameView.idFilter();
        snapshot.liveFrameBusFilter = m_liveFrameView.busFilter();
        snapshot.replayFrameBusFilter = m_replayFrameView.busFilter();
        snapshot.replaySpeed = m_replaySpeed;
        snapshot.replayLoop = m_replayLoop;
        snapshot.liveUiPaused = m_liveUiPaused;
        snapshot.logTargetDirectory = logTargetDirectory();
        snapshot.logTargetName = m_logTargetName;

        const UiStateStore store;
        store.save(m_session, snapshot);
        return;
    }
    m_session.setValue(QStringLiteral("model/enabled"), m_modelEnabled);
    m_session.setValue(QStringLiteral("model/useBundled"), m_rulesUsingBundled);
    m_session.setValue(QStringLiteral("model/path"), m_rulesPath);

    m_session.setValue(QStringLiteral("sort/timingMode"), m_timingSortMode);
    m_session.setValue(QStringLiteral("sort/timingDesc"), m_timingSortDescending);
    m_session.setValue(QStringLiteral("sort/valueMode"), m_valueSortMode);
    m_session.setValue(QStringLiteral("sort/valueDesc"), m_valueSortDescending);
    m_session.setValue(QStringLiteral("sort/alarmMode"), m_alarmSortMode);
    m_session.setValue(QStringLiteral("sort/alarmDesc"), m_alarmSortDescending);

    auto saveViewState = [this](const QString& prefix, const AnalysisViewState& state) {
        m_session.setValue(prefix + QStringLiteral("/timingId"), state.timingFilterId);
        m_session.setValue(prefix + QStringLiteral("/timingSeverity"), state.timingFilterSeverity);
        m_session.setValue(prefix + QStringLiteral("/timingName"), state.timingFilterName);
        m_session.setValue(prefix + QStringLiteral("/timingReason"), state.timingFilterReason);
        m_session.setValue(prefix + QStringLiteral("/timingExpected"), state.timingFilterExpected);
        m_session.setValue(prefix + QStringLiteral("/timingGap"), state.timingFilterGap);
        m_session.setValue(prefix + QStringLiteral("/timingAge"), state.timingFilterAge);
        m_session.setValue(prefix + QStringLiteral("/timingSource"), state.timingFilterSource);
        m_session.setValue(prefix + QStringLiteral("/valueId"), state.valueFilterId);
        m_session.setValue(prefix + QStringLiteral("/valueSeverity"), state.valueFilterSeverity);
        m_session.setValue(prefix + QStringLiteral("/valueName"), state.valueFilterName);
        m_session.setValue(prefix + QStringLiteral("/valueSource"), state.valueFilterSource);
        m_session.setValue(prefix + QStringLiteral("/valueRaw"), state.valueFilterRaw);
        m_session.setValue(prefix + QStringLiteral("/valueGap"), state.valueFilterGap);
        m_session.setValue(prefix + QStringLiteral("/valueReason"), state.valueFilterReason);
        m_session.setValue(prefix + QStringLiteral("/alarmId"), state.alarmFilterId);
        m_session.setValue(prefix + QStringLiteral("/alarmSeverity"), state.alarmFilterSeverity);
        m_session.setValue(prefix + QStringLiteral("/alarmTime"), state.alarmFilterTime);
        m_session.setValue(prefix + QStringLiteral("/alarmName"), state.alarmFilterName);
        m_session.setValue(prefix + QStringLiteral("/alarmSource"), state.alarmFilterSource);
        m_session.setValue(prefix + QStringLiteral("/alarmMessage"), state.alarmFilterMessage);
        m_session.setValue(prefix + QStringLiteral("/alarmText"), state.alarmFilterText);
        m_session.setValue(prefix + QStringLiteral("/selectedValueId"), state.hasSelectedValueId ? idText(state.selectedValueCanId) : QString());
    };

    saveViewState(QStringLiteral("filter/live"), m_liveViewState);
    saveViewState(QStringLiteral("filter/replay"), m_replayViewState);

    // 구버전 키도 함께 유지해서 하위 호환 저장
    m_session.setValue(QStringLiteral("filter/timingId"), m_liveViewState.timingFilterId);
    m_session.setValue(QStringLiteral("filter/timingSeverity"), m_liveViewState.timingFilterSeverity);
    m_session.setValue(QStringLiteral("filter/timingName"), m_liveViewState.timingFilterName);
    m_session.setValue(QStringLiteral("filter/timingReason"), m_liveViewState.timingFilterReason);
    m_session.setValue(QStringLiteral("filter/timingExpected"), m_liveViewState.timingFilterExpected);
    m_session.setValue(QStringLiteral("filter/timingGap"), m_liveViewState.timingFilterGap);
    m_session.setValue(QStringLiteral("filter/timingAge"), m_liveViewState.timingFilterAge);
    m_session.setValue(QStringLiteral("filter/timingSource"), m_liveViewState.timingFilterSource);
    m_session.setValue(QStringLiteral("filter/valueId"), m_liveViewState.valueFilterId);
    m_session.setValue(QStringLiteral("filter/valueSeverity"), m_liveViewState.valueFilterSeverity);
    m_session.setValue(QStringLiteral("filter/valueName"), m_liveViewState.valueFilterName);
    m_session.setValue(QStringLiteral("filter/valueSource"), m_liveViewState.valueFilterSource);
    m_session.setValue(QStringLiteral("filter/valueRaw"), m_liveViewState.valueFilterRaw);
    m_session.setValue(QStringLiteral("filter/valueGap"), m_liveViewState.valueFilterGap);
    m_session.setValue(QStringLiteral("filter/valueReason"), m_liveViewState.valueFilterReason);
    m_session.setValue(QStringLiteral("filter/alarmId"), m_liveViewState.alarmFilterId);
    m_session.setValue(QStringLiteral("filter/alarmSeverity"), m_liveViewState.alarmFilterSeverity);
    m_session.setValue(QStringLiteral("filter/alarmTime"), m_liveViewState.alarmFilterTime);
    m_session.setValue(QStringLiteral("filter/alarmName"), m_liveViewState.alarmFilterName);
    m_session.setValue(QStringLiteral("filter/alarmSource"), m_liveViewState.alarmFilterSource);
    m_session.setValue(QStringLiteral("filter/alarmMessage"), m_liveViewState.alarmFilterMessage);
    m_session.setValue(QStringLiteral("filter/alarmText"), m_liveViewState.alarmFilterText);

    m_session.setValue(QStringLiteral("frame/liveIdFilter"), m_liveFrameView.idFilter());
    m_session.setValue(QStringLiteral("frame/replayIdFilter"), m_replayFrameView.idFilter());
    m_session.setValue(QStringLiteral("frame/liveBusFilter"), m_liveFrameView.busFilter());
    m_session.setValue(QStringLiteral("frame/replayBusFilter"), m_replayFrameView.busFilter());
    m_session.setValue(QStringLiteral("replay/speed"), m_replaySpeed);
    m_session.setValue(QStringLiteral("replay/loop"), m_replayLoop);
    m_session.setValue(QStringLiteral("ui/livePaused"), m_liveUiPaused);
    m_session.setValue(QStringLiteral("log/targetDirectory"), logTargetDirectory());
    m_session.setValue(QStringLiteral("log/targetName"), m_logTargetName);
    m_session.sync();
}

void AppController::clearSavedSession() {
    m_session.clear();
    m_session.sync();
    m_replayRuntime.clearSession(m_session);
    setStatus(QStringLiteral("저장된 세션 설정 초기화"));
    emit replayPathChanged();
    emit derivedSummaryChanged();
}

void AppController::resetViewState(AnalysisViewState& state) {
    state = AnalysisViewState{};
}

void AppController::emitFilterStateChanged() {
    emit filtersChanged();
    emit selectedValueIdChanged();
    emit derivedSummaryChanged();
}

void AppController::applyActiveViewStateFromSource() {
    const AnalysisViewState& state = viewStateForSource(activeAnalysisSourceKey());
    m_timingFilterId = state.timingFilterId;
    m_timingFilterSeverity = state.timingFilterSeverity;
    m_timingFilterName = state.timingFilterName;
    m_timingFilterReason = state.timingFilterReason;
    m_timingFilterExpected = state.timingFilterExpected;
    m_timingFilterGap = state.timingFilterGap;
    m_timingFilterAge = state.timingFilterAge;
    m_timingFilterSource = state.timingFilterSource;
    m_valueFilterId = state.valueFilterId;
    m_valueFilterSeverity = state.valueFilterSeverity;
    m_valueFilterName = state.valueFilterName;
    m_valueFilterSource = state.valueFilterSource;
    m_valueFilterRaw = state.valueFilterRaw;
    m_valueFilterGap = state.valueFilterGap;
    m_valueFilterReason = state.valueFilterReason;
    m_alarmFilterId = state.alarmFilterId;
    m_alarmFilterSeverity = state.alarmFilterSeverity;
    m_alarmFilterTime = state.alarmFilterTime;
    m_alarmFilterName = state.alarmFilterName;
    m_alarmFilterSource = state.alarmFilterSource;
    m_alarmFilterMessage = state.alarmFilterMessage;
    m_alarmFilterText = state.alarmFilterText;
    m_hasSelectedValueId = state.hasSelectedValueId;
    m_selectedValueCanId = state.hasSelectedValueId ? state.selectedValueCanId : 0;
}

void AppController::resetAnalysisContext() {
    setReplayAnalysisHeld(false);
    setTimingViewHeld(false);
    setValueViewHeld(false);
    setAlarmViewHeld(false);
    setLiveUiPaused(false);
    setStatus(QStringLiteral("분석 고정/보류 상태 초기화"));
    emit replayStateChanged();
    emitFilterStateChanged();
    refreshTimingRows();
    refreshValueRows();
    if (m_alarmRowsDirty) refreshAlarmRows();
}

void AppController::resetAllAnalysisFilters() {
    resetViewState(m_liveViewState);
    resetViewState(m_replayViewState);
    applyActiveViewStateFromSource();
    m_liveFrameView.setIdFilter(QString());
    m_replayFrameView.setIdFilter(QString());
    m_liveFrameView.setBusFilter(-1);
    m_replayFrameView.setBusFilter(-1);
    m_timingReorderRequested = true;
    m_valueReorderRequested = true;
    m_alarmReorderRequested = true;
    m_valueDetailsDirty = true;
    m_lastValueDetailSignature.clear();
    m_lastValueDetailProjectionWallMs = -1;
    emitFilterStateChanged();
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    maybeRefreshValueDetails(true);
    setStatus(QStringLiteral("LIVE/REPLAY 전체 분석 필터 초기화"));
}

void AppController::setPanelActive(const QString& key, bool active) {
    const QString normalized = key.trimmed().toLower();
    bool* target = nullptr;
    if (normalized == QStringLiteral("overview")) target = &m_overviewPanelActive;
    else if (normalized == QStringLiteral("live")) target = &m_livePanelActive;
    else if (normalized == QStringLiteral("timing")) target = &m_timingPanelActive;
    else if (normalized == QStringLiteral("value")) target = &m_valuePanelActive;
    else if (normalized == QStringLiteral("graph")) target = &m_graphPageActive;
    else if (normalized == QStringLiteral("alarm")) target = &m_alarmPanelActive;
    else return;

    if (*target == active) return;
    *target = active;

    if (!active) {
        if (normalized == QStringLiteral("graph")) m_graphRefreshTimer.stop();
        return;
    }
    if (analysisPaused()) return;

    if (normalized == QStringLiteral("live")) {
        flushQueuedLiveViewBatch();
    }
    if (normalized == QStringLiteral("overview") || normalized == QStringLiteral("timing")) {
        m_timingRowsDirty = true;
    }
    if (normalized == QStringLiteral("overview") || normalized == QStringLiteral("value") || normalized == QStringLiteral("alarm")) {
        m_valueRowsDirty = true;
    }
    if (normalized == QStringLiteral("overview") || normalized == QStringLiteral("alarm")) {
        m_alarmRowsDirty = true;
    }

    if ((normalized == QStringLiteral("overview") || normalized == QStringLiteral("timing")) && timingScopeActive()) {
        if (m_timingRowsDirty && !m_timingViewHeld) {
            m_lastTimingProjectionWallMs = -1;
            refreshTimingRows();
        }
    }

    if ((normalized == QStringLiteral("overview") || normalized == QStringLiteral("value") || normalized == QStringLiteral("alarm")) && valueScopeActive()) {
        if (m_valueRowsDirty && !m_valueViewHeld) {
            m_lastValueProjectionWallMs = -1;
            refreshValueRows();
        } else if (m_valueDetailsDirty && !m_valueViewHeld && normalized == QStringLiteral("value")) {
            m_lastValueDetailProjectionWallMs = -1;
            maybeRefreshValueDetails(true);
        }
    }

    if ((normalized == QStringLiteral("overview") || normalized == QStringLiteral("alarm")) && alarmScopeActive()) {
        if (m_alarmRowsDirty && !m_alarmViewHeld) {
            m_lastAlarmProjectionWallMs = -1;
            refreshAlarmRows();
        }
    }

    if (normalized == QStringLiteral("graph")) {
        m_lastGraphRefreshWallMs = -1;
        if (activeAnalysisSourceKey() == QStringLiteral("replay")) rebuildReplayGraphHistoryWindow();
        requestGraphRefresh(true);
    }
}

void AppController::focusTimingId(const QString& idTextValue) {
    QString target = normalizedFilterText(idTextValue);
    if (target.isEmpty()) target = topTimingId();
    if (target.isEmpty()) return;
    setTimingFilterId(target);
}

void AppController::focusValueId(const QString& idTextValue) {
    QString target = normalizedFilterText(idTextValue);
    if (target.isEmpty()) target = topValueId();
    if (target.isEmpty()) return;
    setValueFilterId(target);
    selectValueId(target);
}

void AppController::focusAlarmId(const QString& idTextValue) {
    QString target = normalizedFilterText(idTextValue);
    if (target.isEmpty()) target = topAlarmId();
    if (target.isEmpty()) return;
    setAlarmFilterId(target);
}

bool AppController::seekReplayIssue(const QString& kind, int direction) {
    const QString normalized = kind.trimmed().toLower();
    const QVector<ReplayIssueMarker>& markers = replaySnapshotMarkersForKind(normalized);
    if (!markers.isEmpty() && m_replayLoaded) {
        const int count = markers.size();
        const int start = std::clamp(m_replay.currentIndex(), 0, std::max(0, m_replay.frameCount() - 1));
        int found = -1;
        if (direction >= 0) {
            for (const ReplayIssueMarker& marker : markers) {
                if (marker.index > start) {
                    found = marker.index;
                    break;
                }
            }
            if (found < 0) found = markers.front().index;
        } else {
            for (int i = count - 1; i >= 0; --i) {
                if (markers[size_t(i)].index < start) {
                    found = markers[size_t(i)].index;
                    break;
                }
            }
            if (found < 0) found = markers.back().index;
        }
        const ReplayIssueMarker* picked = nullptr;
        for (const ReplayIssueMarker& marker : markers) {
            if (marker.index == found) { picked = &marker; break; }
        }
        if (picked) {
            if (normalized == QStringLiteral("timing")) focusTimingId(idText(picked->id));
            else if (normalized == QStringLiteral("value")) focusValueId(idText(picked->id));
            else focusAlarmId(idText(picked->id));
            const bool wrapped = (direction >= 0) ? (found <= start) : (found >= start);
            return jumpReplayToIndex(found,
                                     QStringLiteral("재생 %1 이슈 이동: %2 %3 · 프레임 %4/%5%6")
                                         .arg(categoryLabel(normalized))
                                         .arg(idText(picked->id))
                                         .arg(picked->severity)
                                         .arg(found + 1)
                                         .arg(m_replay.frameCount())
                                         .arg(wrapped ? QStringLiteral(" · 순환") : QString()));
        }
    }

    QString target;
    if (normalized == QStringLiteral("timing")) target = topTimingId();
    else if (normalized == QStringLiteral("value")) target = topValueId();
    else target = topAlarmId();
    if (target.isEmpty()) {
        setStatus(QStringLiteral("해당 범주의 이슈 ID가 없습니다"));
        return false;
    }
    return seekReplayId(target, direction);
}

bool AppController::jumpReplayToFrameIndex(int frameIndex, const QString& reasonText) {
    if (!m_replayLoaded) return false;
    return jumpReplayToIndex(frameIndex, reasonText.isEmpty()
                                           ? QStringLiteral("재생 지점 이동: %1 / %2").arg(frameIndex + 1).arg(m_replay.frameCount())
                                           : reasonText);
}

bool AppController::seekReplayId(const QString& idTextValue, int direction) {
    if (!m_replayLoaded) {
        setStatus(QStringLiteral("재생 파일을 먼저 로드하세요"));
        return false;
    }

    quint32 targetId = 0;
    if (!parseCanIdText(idTextValue, &targetId)) {
        setStatus(QStringLiteral("이동할 ID 형식이 올바르지 않습니다: %1").arg(idTextValue));
        return false;
    }

    const auto& frames = m_replay.frames();
    if (frames.empty()) return false;

    const int count = int(frames.size());
    const int start = std::clamp(m_replay.currentIndex(), 0, count - 1);
    const int step = direction >= 0 ? 1 : -1;
    int found = -1;
    for (int hop = 1; hop < count; ++hop) {
        int idx = start + hop * step;
        while (idx < 0) idx += count;
        while (idx >= count) idx -= count;
        if (frames[size_t(idx)].canId == targetId) {
            found = idx;
            break;
        }
    }
    if (found < 0) {
        setStatus(QStringLiteral("재생에서 %1 발생 지점을 찾지 못했습니다").arg(idText(targetId)));
        return false;
    }

    focusTimingId(idText(targetId));
    focusValueId(idText(targetId));
    focusAlarmId(idText(targetId));
    const bool wrapped = (direction >= 0) ? (found <= start) : (found >= start);
    return jumpReplayToIndex(found,
                             QStringLiteral("재생 ID 이동: %1 · 프레임 %2/%3%4")
                                 .arg(idText(targetId))
                                 .arg(found + 1)
                                 .arg(frames.size())
                                 .arg(wrapped ? QStringLiteral(" · 순환 검색") : QString()));
}

QString AppController::displayNameForId(quint32 id) const {
    return CanMonitorAnalysis::SignalDecoder::displayNameForId(id, m_rules, m_signalMessages);
}

QString AppController::selectedValueId() const {
    return m_hasSelectedValueId ? idText(m_selectedValueCanId) : QString();
}

void AppController::setRulesPath(const QString& path) {
    const QString normalized = RuntimePaths::normalizeLocalPath(path);
    if (normalized.isEmpty()) {
        if (!m_rulesPath.isEmpty()) {
            m_rulesPath.clear();
            emit rulesPathChanged();
            emit modelPathChanged();
        }
        if (m_rulesUsingBundled && m_rulesActivePath == kBundledModelPath && !m_rules.isEmpty()) return;
        m_modelEnabled = true;
        if (loadModelFile(kBundledModelPath)) {
            setStatus(QStringLiteral("기본 모델 적용"));
        }
        return;
    }

    if (normalized == m_rulesPath && !m_rulesUsingBundled && m_modelEnabled) return;
    m_modelEnabled = true;
    if (loadModelFile(normalized)) {
        m_rulesPath = normalized;
        m_timingReorderRequested = true;
        m_valueReorderRequested = true;
        m_alarmReorderRequested = true;
        emit rulesPathChanged();
        emit modelPathChanged();
    }
}


void AppController::useBundledModel() {
    m_modelEnabled = true;
    if (!m_rulesPath.isEmpty()) {
        m_rulesPath.clear();
        emit rulesPathChanged();
        emit modelPathChanged();
    }
    loadModelFile(kBundledModelPath);
    m_timingReorderRequested = true;
    m_valueReorderRequested = true;
    m_alarmReorderRequested = true;
    emit rulesChanged();
    emit signalDbChanged();
    setStatus(QStringLiteral("기본 모델 적용"));
    refreshDerivedSummaryCache();
    emit derivedSummaryChanged();
    refreshTimingRows();
    refreshValueRows();
    syncLiveBusHealthAlarms();
    refreshAlarmRows();
    requestGraphRefresh(true);
}

void AppController::clearModel() {
    if (!m_modelEnabled) return;
    m_modelEnabled = false;
    invalidateValueDetailSignalCache();
    m_liveAlarmGroups.clear();
    m_replayAlarmGroups.clear();
    m_liveAlarmSequence = 0;
    m_replayAlarmSequence = 0;
    m_liveBusAlarmEventCount = 0;
    m_replayBusAlarmEventCount = 0;
    m_dropAlarmActive = false;
    m_fifoAlarmActive = false;
    m_errPassiveAlarmActive = false;
    m_busOffAlarmActive = false;
    m_modelMeta = {};
    m_alarmCapableSignalIds.clear();
    rebuildGraphCatalog();
    clearGraphHistory();
    clearGraphOverviewState();
    m_alarmModel.clear();
    auto resetStateMap = [](QHash<quint32, IdState>& states) {
        for (auto it = states.begin(); it != states.end(); ++it) {
            it.value().cachedTimingRow.clear();
            it.value().cachedPreviewInfo.clear();
            it.value().cachedValueAlarmInfo.clear();
            it.value().cachedValueRow.clear();
            it.value().timingDerivedDirty = true;
            it.value().valueDerivedDirty = true;
            it.value().activeValueAlarmKey.clear();
            it.value().lastValueAlarmMessage.clear();
            it.value().lastValueAlarmSeenMs = -1;
            it.value().valueAlarmEventCount = 0;
            it.value().lastValueFingerprint = 0;
            it.value().lastValueRenderedSeverity.clear();
            it.value().lastValueRenderedReason.clear();
            it.value().activeTimingAlarmKey.clear();
            it.value().lastTimingAlarmSeenMs = -1;
            it.value().timingEvents.clear();
            it.value().timingEventCount = 0;
            it.value().lastTimingIssueKey.clear();
            it.value().timingIssueLatched = false;
        }
    };
    resetStateMap(m_liveStates);
    resetStateMap(m_replayStates);
    m_alarmCapableSignalIds.clear();
    for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) {
        for (const auto& sig : it.value().signalSpecs) {
            if (signalSpecHasAlarmDefinition(sig)) {
                m_alarmCapableSignalIds.insert(it.key());
                break;
            }
        }
    }
    markAllAnalysisDirty(true);
    emit rulesChanged();
    emit signalDbChanged();
    setStatus(QStringLiteral("모델 해제"));
    refreshDerivedSummaryCache();
    emit derivedSummaryChanged();
    refreshTimingRows();
    refreshValueRows();
    syncLiveBusHealthAlarms();
    refreshAlarmRows();
}


void AppController::rebuildGraphCatalog() {
    m_graphSignals.clear();
    m_graphKeysById.clear();
    m_graphPresetSpecs.clear();
    m_graphPresetCache.clear();

    QVector<quint32> ids = m_signalMessages.keys().toVector();
    std::sort(ids.begin(), ids.end());
    int nextColorIndex = 0;
    for (quint32 id : ids) {
        const auto it = m_signalMessages.constFind(id);
        if (it == m_signalMessages.cend()) continue;
        const auto& specs = it.value().signalSpecs;
        for (int i = 0; i < specs.size(); ++i) {
            const auto& sig = specs.at(i);
            const QString cleanName = graphCleanName(sig.name);
            if (!graphIsInterestingNumeric(sig, cleanName)) continue;

            GraphSignalDescriptor desc;
            desc.key = graphSeriesKeyFor(id, i);
            desc.id = id;
            desc.signalIndex = i;
            desc.name = graphPairLowName(cleanName) ? graphPairBaseName(cleanName) : cleanName;
            desc.unit = graphInferUnit(sig, desc.name);
            desc.label = QStringLiteral("%1 · %2").arg(idText(id), desc.name);
            desc.group = graphDescriptorGroup(desc.name, desc.unit);
            desc.historyKey = desc.key;
            desc.mode = QStringLiteral("raw");
            desc.colorIndex = nextColorIndex++;

            m_graphSignals.insert(desc.key, desc);
            m_graphKeysById.insert(id, desc.key);

            const bool encoderLike = desc.group == QStringLiteral("ENC")
                || desc.unit == QStringLiteral("count")
                || desc.name.toLower().contains(QStringLiteral("encoder"));
            if (encoderLike) {
                GraphSignalDescriptor deltaDesc = desc;
                deltaDesc.key = graphDerivedSignalKey(desc.key, QStringLiteral("delta"));
                deltaDesc.name = desc.name + QStringLiteral(" Δcount");
                deltaDesc.label = QStringLiteral("%1 · %2").arg(idText(id), deltaDesc.name);
                deltaDesc.group = QStringLiteral("ENCD");
                deltaDesc.unit = QStringLiteral("count");
                deltaDesc.historyKey = desc.key;
                deltaDesc.mode = QStringLiteral("delta");
                deltaDesc.colorIndex = nextColorIndex++;
                m_graphSignals.insert(deltaDesc.key, deltaDesc);
                m_graphKeysById.insert(id, deltaDesc.key);

                GraphSignalDescriptor deltaAbsDesc = deltaDesc;
                deltaAbsDesc.key = graphDerivedSignalKey(desc.key, QStringLiteral("delta_abs"));
                deltaAbsDesc.name = desc.name + QStringLiteral(" |Δcount|");
                deltaAbsDesc.label = QStringLiteral("%1 · %2").arg(idText(id), deltaAbsDesc.name);
                deltaAbsDesc.mode = QStringLiteral("delta_abs");
                deltaAbsDesc.colorIndex = nextColorIndex++;
                m_graphSignals.insert(deltaAbsDesc.key, deltaAbsDesc);
                m_graphKeysById.insert(id, deltaAbsDesc.key);

                GraphSignalDescriptor rateDesc = desc;
                rateDesc.key = graphDerivedSignalKey(desc.key, QStringLiteral("rate"));
                rateDesc.name = desc.name + QStringLiteral(" Δcount/s");
                rateDesc.label = QStringLiteral("%1 · %2").arg(idText(id), rateDesc.name);
                rateDesc.group = QStringLiteral("ENCR");
                rateDesc.unit = QStringLiteral("count/s");
                rateDesc.historyKey = desc.key;
                rateDesc.mode = QStringLiteral("rate");
                rateDesc.colorIndex = nextColorIndex++;
                m_graphSignals.insert(rateDesc.key, rateDesc);
                m_graphKeysById.insert(id, rateDesc.key);

                GraphSignalDescriptor rateAbsDesc = rateDesc;
                rateAbsDesc.key = graphDerivedSignalKey(desc.key, QStringLiteral("rate_abs"));
                rateAbsDesc.name = desc.name + QStringLiteral(" |Δcount/s|");
                rateAbsDesc.label = QStringLiteral("%1 · %2").arg(idText(id), rateAbsDesc.name);
                rateAbsDesc.mode = QStringLiteral("rate_abs");
                rateAbsDesc.colorIndex = nextColorIndex++;
                m_graphSignals.insert(rateAbsDesc.key, rateAbsDesc);
                m_graphKeysById.insert(id, rateAbsDesc.key);
            }
        }
    }

    auto keyForIdName = [this](quint32 id, const QString& needle, const QString& mode = QStringLiteral("raw")) -> QString {
        const auto keys = m_graphKeysById.values(id);
        for (const QString& key : keys) {
            const auto it = m_graphSignals.constFind(key);
            if (it == m_graphSignals.cend()) continue;
            if (it.value().mode != mode) continue;
            if (it.value().name.toLower().contains(needle.toLower())) return key;
        }
        return QString();
    };

    for (int i = 0; i < 4; ++i) {
        const quint32 cmdId = 0x123 + (0x100 * i);
        const quint32 fbId = 0x150 + (0x100 * i);
        const QString cmdKey = keyForIdName(cmdId, QStringLiteral("rpm"));
        const QString fbKey = keyForIdName(fbId, QStringLiteral("rpm"));
        if (!cmdKey.isEmpty() && !fbKey.isEmpty()) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("drive_rpm_%1").arg(i + 1);
            preset.title = QStringLiteral("구동 RPM %1 · 명령/실제").arg(i + 1);
            preset.seriesKeys = QStringList{cmdKey, fbKey};
            m_graphPresetSpecs.push_back(preset);
        }
    }

    for (int i = 0; i < 4; ++i) {
        const quint32 cmdId = 0x202 + i;
        const quint32 fbId = 0x182 + i;
        const QString cmdKey = keyForIdName(cmdId, QStringLiteral("angle"));
        const QString fbKey = keyForIdName(fbId, QStringLiteral("angle"));
        if (!cmdKey.isEmpty() && !fbKey.isEmpty()) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("steer_angle_%1").arg(i + 1);
            preset.title = QStringLiteral("조향 Angle %1 · 명령/피드백").arg(i + 1);
            preset.seriesKeys = QStringList{cmdKey, fbKey};
            m_graphPresetSpecs.push_back(preset);
        }
    }

    {
        QStringList rawKeys;
        QStringList deltaKeys;
        QStringList deltaAbsKeys;
        QStringList rateKeys;
        QStringList rateAbsKeys;
        for (quint32 id : {quint32(0x117), quint32(0x118)}) {
            for (const QString& needle : {QStringLiteral("encoder")}) {
                const auto keys = m_graphKeysById.values(id);
                for (const QString& key : keys) {
                    const auto it = m_graphSignals.constFind(key);
                    if (it == m_graphSignals.cend()) continue;
                    if (!it.value().name.toLower().contains(needle)) continue;
                    if (it.value().mode == QStringLiteral("raw")) rawKeys << key;
                    else if (it.value().mode == QStringLiteral("delta")) deltaKeys << key;
                    else if (it.value().mode == QStringLiteral("delta_abs")) deltaAbsKeys << key;
                    else if (it.value().mode == QStringLiteral("rate")) rateKeys << key;
                    else if (it.value().mode == QStringLiteral("rate_abs")) rateAbsKeys << key;
                }
            }
        }
        rawKeys.removeDuplicates();
        deltaKeys.removeDuplicates();
        deltaAbsKeys.removeDuplicates();
        rateKeys.removeDuplicates();
        rateAbsKeys.removeDuplicates();
        auto sortKeys = [this](QStringList* list) {
            std::sort(list->begin(), list->end(), [this](const QString& a, const QString& b) {
                const auto ia = m_graphSignals.value(a);
                const auto ib = m_graphSignals.value(b);
                if (ia.id != ib.id) return ia.id < ib.id;
                if (ia.signalIndex != ib.signalIndex) return ia.signalIndex < ib.signalIndex;
                return ia.key < ib.key;
            });
        };
        sortKeys(&rawKeys);
        sortKeys(&deltaKeys);
        sortKeys(&deltaAbsKeys);
        sortKeys(&rateKeys);
        sortKeys(&rateAbsKeys);
        if (rawKeys.size() >= 4) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("system_encoder_raw4");
            preset.title = QStringLiteral("시스템 엔코더 RAW 4선");
            preset.seriesKeys = rawKeys.mid(0, 4);
            m_graphPresetSpecs.push_back(preset);
        }
        if (deltaAbsKeys.size() >= 4) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("system_encoder_delta4");
            preset.title = QStringLiteral("시스템 엔코더 증분 4선 (절대값)");
            preset.seriesKeys = deltaAbsKeys.mid(0, 4);
            m_graphPresetSpecs.push_back(preset);
        }
        if (deltaKeys.size() >= 4) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("system_encoder_delta4_signed");
            preset.title = QStringLiteral("시스템 엔코더 증분 4선 (부호유지)");
            preset.seriesKeys = deltaKeys.mid(0, 4);
            m_graphPresetSpecs.push_back(preset);
        }
        if (rateAbsKeys.size() >= 4) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("system_encoder_rate4");
            preset.title = QStringLiteral("시스템 엔코더 증분속도 4선 (절대값)");
            preset.seriesKeys = rateAbsKeys.mid(0, 4);
            m_graphPresetSpecs.push_back(preset);
        }
        if (rateKeys.size() >= 4) {
            GraphPresetSpec preset;
            preset.key = QStringLiteral("system_encoder_rate4_signed");
            preset.title = QStringLiteral("시스템 엔코더 증분속도 4선 (부호유지)");
            preset.seriesKeys = rateKeys.mid(0, 4);
            m_graphPresetSpecs.push_back(preset);
        }
    }

    QStringList validSelected;
    for (const QString& key : m_graphSelectedKeys) {
        if (m_graphSignals.contains(key)) validSelected << key;
    }

    bool presetStillValid = false;
    for (const auto& preset : m_graphPresetSpecs) {
        if (preset.key == m_graphPresetKey) {
            presetStillValid = true;
            break;
        }
    }
    if (!presetStillValid) m_graphPresetKey = QStringLiteral("manual");

    m_graphSelectedKeys = validSelected;

    QVector<QVariantMap> graphCatalogRows;
    m_graphCatalogCache.clear();
    QVector<GraphSignalDescriptor> ordered = m_graphSignals.values().toVector();
    std::sort(ordered.begin(), ordered.end(), [](const GraphSignalDescriptor& a, const GraphSignalDescriptor& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.id != b.id) return a.id < b.id;
        if (a.signalIndex != b.signalIndex) return a.signalIndex < b.signalIndex;
        return a.key < b.key;
    });
    graphCatalogRows.reserve(ordered.size());
    for (const auto& desc : ordered) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), desc.key);
        row.insert(QStringLiteral("idText"), idText(desc.id));
        row.insert(QStringLiteral("label"), desc.label);
        row.insert(QStringLiteral("name"), desc.name);
        row.insert(QStringLiteral("unit"), desc.unit);
        row.insert(QStringLiteral("group"), desc.group);
        row.insert(QStringLiteral("mode"), desc.mode);
        row.insert(QStringLiteral("color"), graphColorForIndex(desc.colorIndex));
        row.insert(QStringLiteral("selected"), m_graphSelectedKeys.contains(desc.key));
        graphCatalogRows.push_back(row);
        m_graphCatalogCache.push_back(row);
    }
    m_graphCatalogModel.setRows(graphCatalogRows);

    QVariantMap manual;
    manual.insert(QStringLiteral("key"), QStringLiteral("manual"));
    manual.insert(QStringLiteral("title"), QStringLiteral("직접 선택"));
    manual.insert(QStringLiteral("count"), m_graphSelectedKeys.size());
    m_graphPresetCache.push_back(manual);
    for (const auto& preset : m_graphPresetSpecs) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), preset.key);
        row.insert(QStringLiteral("title"), preset.title);
        row.insert(QStringLiteral("count"), preset.seriesKeys.size());
        m_graphPresetCache.push_back(row);
    }

    emit graphCatalogChanged();
    emit graphSelectionChanged();
}

void AppController::clearGraphHistory(const QString& source) {
    const QString normalized = source.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("live")) {
        m_liveGraphHistory.clear();
        m_liveGraphBucketCache.clear();
    }
    if (normalized.isEmpty() || normalized == QStringLiteral("replay")) {
        m_replayGraphHistory.clear();
    m_replayGraphBucketCache.clear();
        m_replayGraphBucketCache.clear();
        m_replayGraphBuiltStartUs = 0;
        m_replayGraphBuiltEndUs = 0;
        m_replayGraphBuiltSelectionKey.clear();
        m_replayGraphWindowValid = false;
    }
    resetGraphDetailZoomLock();
}

void AppController::invalidateGraphBucketCache(const QString& source, const QString& historyKey) {
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    if (historyKey.trimmed().isEmpty()) cache->clear();
    else cache->remove(historyKey);
}

void AppController::rebuildGraphBucketCacheForSeries(const QString& source, const QString& historyKey, quint64 bucketUs) {
    const QString normalizedKey = historyKey.trimmed();
    if (normalizedKey.isEmpty() || bucketUs == 0) return;
    const auto* history = source == QStringLiteral("replay") ? &m_replayGraphHistory : &m_liveGraphHistory;
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    const auto histIt = history->constFind(normalizedKey);
    if (histIt == history->cend()) {
        cache->remove(normalizedKey);
        return;
    }
    (*cache)[normalizedKey].insert(bucketUs, graphBuildBucketCachePoints(histIt.value(), bucketUs));
}

void AppController::rebuildGraphBucketCachesForSource(const QString& source) {
    auto* history = source == QStringLiteral("replay") ? &m_replayGraphHistory : &m_liveGraphHistory;
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    cache->clear();
    static const QVector<quint64> commonLevels = { 1000ULL, 2000ULL, 4000ULL, 8000ULL, 16000ULL, 32000ULL, 64000ULL, 128000ULL, 256000ULL, 512000ULL, 1024000ULL };
    for (auto it = history->cbegin(); it != history->cend(); ++it) {
        auto& levels = (*cache)[it.key()];
        for (const quint64 bucketUs : commonLevels) {
            levels.insert(bucketUs, graphBuildBucketCachePoints(it.value(), bucketUs));
        }
    }
}

void AppController::appendGraphBucketCaches(const QString& source, const QString& historyKey, const GraphPoint& point) {
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    auto mapIt = cache->find(historyKey);
    if (mapIt == cache->end()) return;
    for (auto levelIt = mapIt->begin(); levelIt != mapIt->end(); ++levelIt) {
        const quint64 bucketUs = levelIt.key();
        auto& buckets = levelIt.value();
        const quint64 bucketIndex = point.frameUs / bucketUs;
        if (buckets.isEmpty() || buckets.back().bucketIndex != bucketIndex) {
            buckets.push_back(GraphBucketCachePoint{bucketIndex, point.frameUs, point.value, point.value, point.value});
        } else {
            auto& back = buckets.back();
            back.closeUs = point.frameUs;
            back.minV = std::min(back.minV, point.value);
            back.maxV = std::max(back.maxV, point.value);
            back.closeV = point.value;
        }
    }
}

void AppController::trimGraphBucketCaches(const QString& source, const QString& historyKey, quint64 minUs) {
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    auto mapIt = cache->find(historyKey);
    if (mapIt == cache->end()) return;
    for (auto levelIt = mapIt->begin(); levelIt != mapIt->end(); ++levelIt) {
        const quint64 bucketUs = levelIt.key();
        auto& buckets = levelIt.value();
        const quint64 minBucketIndex = minUs / bucketUs;
        auto firstKeep = std::lower_bound(buckets.begin(), buckets.end(), minBucketIndex,
            [](const GraphBucketCachePoint& pt, quint64 idx) { return pt.bucketIndex < idx; });
        if (firstKeep != buckets.begin()) buckets.erase(buckets.begin(), firstKeep);
    }
}

QVector<GraphBucketPoint> AppController::sliceGraphBucketCache(const QString& source, const QString& historyKey, quint64 startUs, quint64 endUs, quint64 bucketUs, int reserveCount) {
    QVector<GraphBucketPoint> out;
    out.reserve(std::max(1, reserveCount));
    const QString normalizedKey = historyKey.trimmed();
    if (normalizedKey.isEmpty() || bucketUs == 0 || endUs < startUs) return out;

    const auto* history = source == QStringLiteral("replay") ? &m_replayGraphHistory : &m_liveGraphHistory;
    auto* cache = source == QStringLiteral("replay") ? &m_replayGraphBucketCache : &m_liveGraphBucketCache;
    const auto histIt = history->constFind(normalizedKey);
    if (histIt == history->cend() || histIt.value().isEmpty()) return out;

    auto cacheSeriesIt = cache->find(normalizedKey);
    if (cacheSeriesIt == cache->end() || !cacheSeriesIt->contains(bucketUs)) {
        rebuildGraphBucketCacheForSeries(source, normalizedKey, bucketUs);
        cacheSeriesIt = cache->find(normalizedKey);
        if (cacheSeriesIt == cache->end() || !cacheSeriesIt->contains(bucketUs)) return out;
    }

    const auto& buckets = cacheSeriesIt->value(bucketUs);
    const quint64 startBucket = startUs / bucketUs;
    const quint64 endBucket = endUs / bucketUs;

    auto appendEdgeBucket = [&](quint64 edgeStartUs, quint64 edgeEndUs) {
        const auto& hist = histIt.value();
        auto beginIt = std::lower_bound(hist.begin(), hist.end(), edgeStartUs,
            [](const GraphPoint& pt, quint64 valueUs) { return pt.frameUs < valueUs; });
        auto endIt = std::upper_bound(beginIt, hist.end(), edgeEndUs,
            [](quint64 valueUs, const GraphPoint& pt) { return valueUs < pt.frameUs; });
        if (beginIt == endIt) return;
        GraphBucketPoint edge;
        edge.tMs = double(beginIt->frameUs - startUs) / 1000.0;
        edge.minV = beginIt->value;
        edge.maxV = beginIt->value;
        edge.closeV = beginIt->value;
        for (auto it = beginIt; it != endIt; ++it) {
            edge.tMs = double(it->frameUs - startUs) / 1000.0;
            edge.minV = std::min(edge.minV, it->value);
            edge.maxV = std::max(edge.maxV, it->value);
            edge.closeV = it->value;
        }
        out.push_back(edge);
    };

    if (startBucket == endBucket) {
        appendEdgeBucket(startUs, endUs);
        return out;
    }

    appendEdgeBucket(startUs, std::min(endUs, ((startBucket + 1) * bucketUs) - 1ULL));

    auto firstMid = std::lower_bound(buckets.begin(), buckets.end(), startBucket + 1,
        [](const GraphBucketCachePoint& pt, quint64 idx) { return pt.bucketIndex < idx; });
    auto lastMid = std::lower_bound(firstMid, buckets.end(), endBucket,
        [](const GraphBucketCachePoint& pt, quint64 idx) { return pt.bucketIndex < idx; });
    for (auto it = firstMid; it != lastMid; ++it) {
        out.push_back(GraphBucketPoint{double(it->closeUs - startUs) / 1000.0, it->minV, it->maxV, it->closeV});
    }

    appendEdgeBucket(std::max(startUs, endBucket * bucketUs), endUs);
    return out;
}

void AppController::resetGraphDetailZoomLock() {
    m_graphDetailZoomLockValid = false;
    m_graphDetailZoomLockKey.clear();
}

void AppController::appendGraphSamples(const FrameRecord& fr, const QString& source) {
    if (m_graphSelectedKeys.isEmpty()) return;
    const auto keys = m_graphKeysById.values(fr.canId);
    if (keys.isEmpty()) return;

    auto* history = source == QStringLiteral("replay") ? &m_replayGraphHistory : &m_liveGraphHistory;
    bool changed = false;
    const quint64 retentionUs = quint64(graphHistoryRetentionMs(m_graphWindowMs)) * 1000ULL;
    QSet<QString> updatedHistoryKeys;
    for (const QString& key : keys) {
        if (!m_graphSelectedKeys.contains(key)) continue;
        const auto it = m_graphSignals.constFind(key);
        if (it == m_graphSignals.cend()) continue;
        const QString historyKey = it.value().historyKey.isEmpty() ? key : it.value().historyKey;
        if (updatedHistoryKeys.contains(historyKey)) continue;
        double value = 0.0;
        if (!graphDecodeDescriptorValue(it.value(), fr, m_signalMessages, &value)) continue;
        auto& series = (*history)[historyKey];
        bool replacedLast = false;
        if (!series.isEmpty() && series.back().frameUs == fr.tExtUs) {
            series.back().value = value;
            replacedLast = true;
        } else {
            series.push_back(GraphPoint{fr.tExtUs, value});
            appendGraphBucketCaches(source, historyKey, series.back());
        }
        if (replacedLast) {
            invalidateGraphBucketCache(source, historyKey);
        }
        if (series.size() > 16) {
            const quint64 minUs = (fr.tExtUs > retentionUs) ? (fr.tExtUs - retentionUs) : 0;
            const auto firstKeep = std::lower_bound(series.begin(), series.end(), minUs,
                [](const GraphPoint& pt, quint64 valueUs) { return pt.frameUs < valueUs; });
            if (firstKeep != series.begin()) {
                series.erase(series.begin(), firstKeep);
                trimGraphBucketCaches(source, historyKey, minUs);
            }
        }
        updatedHistoryKeys.insert(historyKey);
        changed = true;
    }
    if (source == QStringLiteral("replay") && changed) {
        m_replayGraphBuiltStartUs = 0;
        m_replayGraphBuiltEndUs = 0;
        m_replayGraphBuiltSelectionKey.clear();
        m_replayGraphWindowValid = false;
        invalidateGraphBucketCache(QStringLiteral("replay"));
    }
    if (changed && m_graphPageActive) requestGraphRefresh(false);
}

void AppController::rebuildReplayGraphHistoryWindow() {
    if (!m_replayLoaded || m_graphSelectedKeys.isEmpty() || m_graphSignals.isEmpty()) {
        clearGraphHistory(QStringLiteral("replay"));
        return;
    }

    const auto& frames = m_replay.frames();
    const int frameCount = int(frames.size());
    if (frameCount <= 0) {
        clearGraphHistory(QStringLiteral("replay"));
        return;
    }

    quint64 endUs = replaySnapshotDisplayedUs();
    if (endUs == 0) endUs = (m_replayDisplayedUs > 0) ? m_replayDisplayedUs : m_replay.currentUs();
    if (endUs == 0) {
        const int idx = std::clamp(m_replayCurrentIndex, 0, frameCount - 1);
        endUs = frames[size_t(idx)].tExtUs;
    }
    const quint64 windowUs = quint64(std::max(1000, m_graphWindowMs)) * 1000ULL;
    const quint64 startUs = endUs > windowUs ? (endUs - windowUs) : 0ULL;
    const QString selectionKey = m_graphSelectedKeys.join(QStringLiteral("|"));
    if (m_replayGraphWindowValid
        && m_replayGraphBuiltStartUs == startUs
        && m_replayGraphBuiltEndUs == endUs
        && m_replayGraphBuiltSelectionKey == selectionKey) {
        return;
    }

    m_replayGraphHistory.clear();
    m_replayGraphBucketCache.clear();

    QSet<quint32> ids;
    for (const QString& key : m_graphSelectedKeys) {
        const auto it = m_graphSignals.constFind(key);
        if (it != m_graphSignals.cend()) ids.insert(it.value().id);
    }
    if (ids.isEmpty()) {
        m_replayGraphBuiltStartUs = startUs;
        m_replayGraphBuiltEndUs = endUs;
        m_replayGraphBuiltSelectionKey = selectionKey;
        m_replayGraphWindowValid = true;
        return;
    }

    int lo = 0;
    int hi = frameCount;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (frames[size_t(mid)].tExtUs < startUs) lo = mid + 1;
        else hi = mid;
    }
    for (int i = lo; i < frameCount; ++i) {
        const FrameRecord& fr = frames[size_t(i)];
        if (fr.tExtUs > endUs) break;
        if (!ids.contains(fr.canId)) continue;
        const auto keys = m_graphKeysById.values(fr.canId);
        QSet<QString> updatedHistoryKeys;
        for (const QString& key : keys) {
            if (!m_graphSelectedKeys.contains(key)) continue;
            const auto it = m_graphSignals.constFind(key);
            if (it == m_graphSignals.cend()) continue;
            const QString historyKey = it.value().historyKey.isEmpty() ? key : it.value().historyKey;
            if (updatedHistoryKeys.contains(historyKey)) continue;
            double value = 0.0;
            if (!graphDecodeDescriptorValue(it.value(), fr, m_signalMessages, &value)) continue;
            auto& series = m_replayGraphHistory[historyKey];
            if (!series.isEmpty() && series.back().frameUs == fr.tExtUs) series.back().value = value;
            else series.push_back(GraphPoint{fr.tExtUs, value});
            updatedHistoryKeys.insert(historyKey);
        }
    }

    rebuildGraphBucketCachesForSource(QStringLiteral("replay"));
    m_replayGraphBuiltStartUs = startUs;
    m_replayGraphBuiltEndUs = endUs;
    m_replayGraphBuiltSelectionKey = selectionKey;
    m_replayGraphWindowValid = true;
}

void AppController::requestGraphRefresh(bool immediate) {
    if (!m_graphPageActive) {
        if (immediate) m_lastGraphRefreshWallMs = -1;
        return;
    }
    if (immediate) {
        m_graphRefreshTimer.stop();
        flushGraphRefresh();
        return;
    }
    const int intervalMs = graphRefreshIntervalMs(m_graphSelectedKeys.size(), m_graphWindowMs);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastGraphRefreshWallMs > 0 && (now - m_lastGraphRefreshWallMs) >= intervalMs + 40) {
        flushGraphRefresh();
        return;
    }
    if (!m_graphRefreshTimer.isActive() || m_graphRefreshTimer.interval() != intervalMs) {
        m_graphRefreshTimer.start(intervalMs);
    }
}

void AppController::flushGraphRefresh() {
    m_lastGraphRefreshWallMs = QDateTime::currentMSecsSinceEpoch();
    QVariantList seriesOut;
    QString rangeSummary = QStringLiteral("-");

    if (!m_graphPageActive || !m_modelEnabled || m_graphSelectedKeys.isEmpty() || m_graphSignals.isEmpty()) {
        m_graphSeriesCache = seriesOut;
        m_graphSourceSummaryCache = !m_modelEnabled ? QStringLiteral("모델 해제 · 그래프 신호 없음") : QStringLiteral("선택 그래프 없음");
        m_graphRangeSummaryCache = QStringLiteral("-");
        m_graphDisplayRangeValid = false;
        m_graphDisplayRangeKey.clear();
        emit graphSeriesChanged();
        return;
    }

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    bool havePoint = false;
    QStringList unitSet;
    QVector<GraphSignalDescriptor> activeDescs;

    const QString source = activeAnalysisSourceKey();
    const auto* history = &m_liveGraphHistory;
    quint64 endUs = 0;
    if (source == QStringLiteral("replay")) {
        history = &m_replayGraphHistory;
        if (history->isEmpty()) rebuildReplayGraphHistoryWindow();
        endUs = replaySnapshotDisplayedUs();
        if (endUs == 0) endUs = (m_replayDisplayedUs > 0) ? m_replayDisplayedUs : m_replay.currentUs();
    } else {
        endUs = m_liveLatestUs;
        if (endUs == 0) {
            for (const QString& key : m_graphSelectedKeys) {
                const auto descIt = m_graphSignals.constFind(key);
                if (descIt == m_graphSignals.cend()) continue;
                const QString historyKey = descIt.value().historyKey.isEmpty() ? key : descIt.value().historyKey;
                const auto& hist = history->value(historyKey);
                if (!hist.isEmpty()) endUs = std::max(endUs, hist.back().frameUs);
            }
        }
    }

    const quint64 windowUs = quint64(std::max(1000, m_graphWindowMs)) * 1000ULL;
    const quint64 startUs = endUs > windowUs ? (endUs - windowUs) : 0ULL;
    const double windowMs = double(std::max(1000, m_graphWindowMs));
    const int renderLimit = graphRenderPointLimit(m_graphWindowMs);
    const int exactRawLimit = graphExactRawPointLimit(m_graphWindowMs);
    const quint64 bucketUs = std::max<quint64>(1000ULL, std::max<quint64>(1ULL, windowUs / quint64(std::max(1, renderLimit))));

    for (const QString& key : m_graphSelectedKeys) {
        const auto descIt = m_graphSignals.constFind(key);
        if (descIt == m_graphSignals.cend()) continue;
        const auto& desc = descIt.value();
        const QString historyKey = desc.historyKey.isEmpty() ? key : desc.historyKey;
        const auto& hist = history->value(historyKey);
        auto beginIt = std::lower_bound(hist.begin(), hist.end(), startUs,
            [](const GraphPoint& pt, quint64 valueUs) { return pt.frameUs < valueUs; });
        const auto endIt = std::upper_bound(beginIt, hist.end(), endUs,
            [](quint64 valueUs, const GraphPoint& pt) { return valueUs < pt.frameUs; });

        const int visibleRawCount = int(std::distance(beginIt, endIt));
        QVector<GraphPoint> working;
        if (desc.mode == QStringLiteral("raw")) {
            if (visibleRawCount > 0) {
                working.reserve(visibleRawCount);
                for (auto itPt = beginIt; itPt != endIt; ++itPt) working.push_back(*itPt);
            }
        } else {
            auto firstIt = beginIt;
            if (firstIt != hist.begin()) --firstIt;
            if (firstIt != endIt) {
                auto prevIt = firstIt;
                auto curIt = firstIt;
                ++curIt;
                for (; curIt != endIt; ++curIt) {
                    if (curIt->frameUs < startUs) {
                        prevIt = curIt;
                        continue;
                    }
                    const double delta = graphWrapAwareDelta(prevIt->value, curIt->value, desc);
                    const quint64 dtUs = std::max<quint64>(1ULL, curIt->frameUs - prevIt->frameUs);
                    double derivedValue = graphModeIsRate(desc.mode)
                        ? (delta * 1000000.0 / double(dtUs))
                        : delta;
                    if (graphModeUsesAbsoluteMagnitude(desc.mode)) derivedValue = std::abs(derivedValue);
                    working.push_back(GraphPoint{curIt->frameUs, derivedValue});
                    prevIt = curIt;
                }
            }
        }

        int rawPointCount = (desc.mode == QStringLiteral("raw")) ? visibleRawCount : working.size();
        double localMin = std::numeric_limits<double>::max();
        double localMax = std::numeric_limits<double>::lowest();
        double latestValue = 0.0;
        bool hasLocalPoint = false;
        for (const auto& pt : working) {
            localMin = std::min(localMin, pt.value);
            localMax = std::max(localMax, pt.value);
            latestValue = pt.value;
            hasLocalPoint = true;
        }

        const bool preferExactForGroup = desc.group == QStringLiteral("ANGLE")
            || desc.group == QStringLiteral("RPM")
            || desc.group == QStringLiteral("ENCD");
        const bool useRawPolyline = rawPointCount > 0 && (preferExactForGroup || (exactRawLimit > 0 && rawPointCount <= exactRawLimit));

        QVariantList rawFlat;
        QVariantList bucketFlat;
        int drawPointCount = 0;
        QString renderMode = QStringLiteral("bucket");

        if (desc.mode == QStringLiteral("raw") && !useRawPolyline && rawPointCount > 0) {
            const quint64 localBucketUs = graphQuantizeBucketUs(desc.group == QStringLiteral("ENCR")
                ? std::max<quint64>(2000ULL, bucketUs)
                : bucketUs);
            const QVector<GraphBucketPoint> buckets = sliceGraphBucketCache(source, historyKey, startUs, endUs, localBucketUs, renderLimit);
            bucketFlat.reserve(buckets.size() * 4);
            for (const auto& bucket : buckets) {
                bucketFlat.push_back(bucket.tMs);
                bucketFlat.push_back(bucket.minV);
                bucketFlat.push_back(bucket.maxV);
                bucketFlat.push_back(bucket.closeV);
                localMin = hasLocalPoint ? std::min(localMin, bucket.minV) : bucket.minV;
                localMax = hasLocalPoint ? std::max(localMax, bucket.maxV) : bucket.maxV;
                latestValue = bucket.closeV;
                hasLocalPoint = true;
            }
            drawPointCount = buckets.size();
            rawPointCount = visibleRawCount;
        } else if (useRawPolyline) {
            renderMode = QStringLiteral("raw");
            rawFlat.reserve(rawPointCount * 2);
            for (const auto& pt : working) {
                rawFlat.push_back(double(pt.frameUs - startUs) / 1000.0);
                rawFlat.push_back(pt.value);
            }
            drawPointCount = rawPointCount;
        } else {
            const quint64 localBucketUs = graphQuantizeBucketUs(desc.group == QStringLiteral("ENCR")
                ? std::max<quint64>(2000ULL, bucketUs)
                : bucketUs);
            const QVector<GraphBucketPoint> buckets = graphBuildStableBuckets(working.begin(), working.end(), startUs, localBucketUs, renderLimit);
            bucketFlat.reserve(buckets.size() * 4);
            for (const auto& bucket : buckets) {
                bucketFlat.push_back(bucket.tMs);
                bucketFlat.push_back(bucket.minV);
                bucketFlat.push_back(bucket.maxV);
                bucketFlat.push_back(bucket.closeV);
            }
            drawPointCount = buckets.size();
        }

        if (hasLocalPoint) {
            havePoint = true;
            globalMin = std::min(globalMin, localMin);
            globalMax = std::max(globalMax, localMax);
            activeDescs.push_back(desc);
        }

        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), desc.label);
        row.insert(QStringLiteral("unit"), desc.unit);
        row.insert(QStringLiteral("color"), graphColorForIndex(desc.colorIndex));
        row.insert(QStringLiteral("group"), desc.group);
        row.insert(QStringLiteral("mode"), desc.mode);
        row.insert(QStringLiteral("renderMode"), renderMode);
        row.insert(QStringLiteral("renderModeLabel"), renderMode == QStringLiteral("raw") ? QStringLiteral("원시선") : QStringLiteral("피크보존"));
        row.insert(QStringLiteral("rawFlat"), rawFlat);
        row.insert(QStringLiteral("bucketFlat"), bucketFlat);
        row.insert(QStringLiteral("latestText"), hasLocalPoint ? QString::number(latestValue, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("minText"), hasLocalPoint ? QString::number(localMin, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("maxText"), hasLocalPoint ? QString::number(localMax, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("rawPointCount"), rawPointCount);
        row.insert(QStringLiteral("drawPointCount"), drawPointCount);
        row.insert(QStringLiteral("bucketUs"), double(bucketUs));
        seriesOut.push_back(row);
        if (!desc.unit.trimmed().isEmpty() && !unitSet.contains(desc.unit)) unitSet << desc.unit;
    }

    if (havePoint) {
        double yMin = 0.0;
        double yMax = 1.0;
        QString rangePrefix;
        const GraphStaticRange staticRange = graphStaticRangeForSelection(activeDescs);
        if (staticRange.valid) {
            if (m_graphDetailZoom) {
                const QString zoomKey = source + QStringLiteral("|") + m_graphSelectedKeys.join(QStringLiteral("|")) + QStringLiteral("|") + QString::number(m_graphWindowMs);
                if (!m_graphDetailZoomLockValid || m_graphDetailZoomLockKey != zoomKey) {
                    const GraphStaticRange detailRange = graphDetailZoomRange(globalMin, globalMax, activeDescs);
                    m_graphDetailZoomLockKey = zoomKey;
                    m_graphDetailZoomLockYMin = detailRange.valid ? detailRange.yMin : staticRange.yMin;
                    m_graphDetailZoomLockYMax = detailRange.valid ? detailRange.yMax : staticRange.yMax;
                    m_graphDetailZoomLockValid = true;
                }
                yMin = m_graphDetailZoomLockYMin;
                yMax = m_graphDetailZoomLockYMax;
                rangePrefix = unitSet.size() == 1
                    ? QStringLiteral("고정축 %1 ~ %2 %3 · 미세확대 잠금 %4 ~ %5 %3")
                        .arg(QString::number(staticRange.yMin, 'f', 2),
                             QString::number(staticRange.yMax, 'f', 2),
                             unitSet.front(),
                             QString::number(yMin, 'f', 2),
                             QString::number(yMax, 'f', 2))
                    : QStringLiteral("고정축 %1 ~ %2 · 미세확대 잠금 %3 ~ %4")
                        .arg(QString::number(staticRange.yMin, 'f', 2),
                             QString::number(staticRange.yMax, 'f', 2),
                             QString::number(yMin, 'f', 2),
                             QString::number(yMax, 'f', 2));
                m_graphDetailZoomSummaryCache = QStringLiteral("미세확대 ON · 잠금폭 고정 유지");
            } else {
                yMin = staticRange.yMin;
                yMax = staticRange.yMax;
                rangePrefix = unitSet.size() == 1
                    ? QStringLiteral("고정축 %1 ~ %2 %3 · 현재 %4 ~ %5 %3")
                        .arg(QString::number(staticRange.yMin, 'f', 2),
                             QString::number(staticRange.yMax, 'f', 2),
                             unitSet.front(),
                             QString::number(globalMin, 'f', 2),
                             QString::number(globalMax, 'f', 2))
                    : QStringLiteral("고정축 %1 ~ %2 · 현재 %3 ~ %4")
                        .arg(QString::number(staticRange.yMin, 'f', 2),
                             QString::number(staticRange.yMax, 'f', 2),
                             QString::number(globalMin, 'f', 2),
                             QString::number(globalMax, 'f', 2));
                m_graphDetailZoomSummaryCache = QStringLiteral("고정축");
            }
            m_graphDisplayRangeValid = false;
            m_graphDisplayRangeKey.clear();
        } else {
            const double pad = std::max(1e-6, (globalMax - globalMin) * 0.12);
            const double proposedMin = globalMin - pad;
            const double proposedMax = globalMax + pad;
            const QString rangeKey = source + QStringLiteral("|") + m_graphSelectedKeys.join(QStringLiteral("|")) + QStringLiteral("|") + QString::number(m_graphWindowMs);
            if (!m_graphDisplayRangeValid || m_graphDisplayRangeKey != rangeKey) {
                m_graphDisplayRangeKey = rangeKey;
                m_graphDisplayYMin = proposedMin;
                m_graphDisplayYMax = proposedMax;
                m_graphDisplayRangeValid = true;
            } else {
                m_graphDisplayYMin = std::min(m_graphDisplayYMin, proposedMin);
                m_graphDisplayYMax = std::max(m_graphDisplayYMax, proposedMax);
            }
            if (m_graphDetailZoom) {
                const QString zoomKey = source + QStringLiteral("|") + m_graphSelectedKeys.join(QStringLiteral("|")) + QStringLiteral("|") + QString::number(m_graphWindowMs);
                if (!m_graphDetailZoomLockValid || m_graphDetailZoomLockKey != zoomKey) {
                    const GraphStaticRange detailRange = graphDetailZoomRange(globalMin, globalMax, activeDescs);
                    m_graphDetailZoomLockKey = zoomKey;
                    m_graphDetailZoomLockYMin = detailRange.valid ? detailRange.yMin : proposedMin;
                    m_graphDetailZoomLockYMax = detailRange.valid ? detailRange.yMax : proposedMax;
                    m_graphDetailZoomLockValid = true;
                }
                yMin = m_graphDetailZoomLockYMin;
                yMax = m_graphDetailZoomLockYMax;
                m_graphDetailZoomSummaryCache = QStringLiteral("미세확대 ON · 잠금폭 고정 유지");
            } else {
                yMin = m_graphDisplayYMin;
                yMax = m_graphDisplayYMax;
                m_graphDetailZoomSummaryCache = QStringLiteral("자동축");
            }
            rangePrefix = unitSet.size() == 1
                ? QStringLiteral("값 %1 ~ %2 %3").arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2), unitSet.front())
                : QStringLiteral("값 %1 ~ %2").arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2));
        }

        rangeSummary = rangePrefix;

        for (int i = 0; i < seriesOut.size(); ++i) {
            QVariantMap row = seriesOut.at(i).toMap();
            row.insert(QStringLiteral("windowMs"), windowMs);
            row.insert(QStringLiteral("yMin"), yMin);
            row.insert(QStringLiteral("yMax"), yMax);
            row.insert(QStringLiteral("detailZoom"), m_graphDetailZoom);
            seriesOut[i] = row;
        }
    } else {
        rangeSummary = QStringLiteral("표시 가능한 수치 샘플 없음");
        m_graphDetailZoomSummaryCache = m_graphDetailZoom ? QStringLiteral("미세확대 ON") : QStringLiteral("고정축");
        m_graphDisplayRangeValid = false;
        m_graphDisplayRangeKey.clear();
        resetGraphDetailZoomLock();
    }

    m_graphSeriesCache = seriesOut;
    m_graphSourceSummaryCache = QStringLiteral("%1 · 최근 %2초 · %3선").arg(source == QStringLiteral("replay") ? QStringLiteral("재생") : QStringLiteral("라이브")).arg(QString::number(double(m_graphWindowMs) / 1000.0, 'f', m_graphWindowMs >= 10000 ? 0 : 1)).arg(seriesOut.size());
    m_graphRangeSummaryCache = rangeSummary;
    emit graphSeriesChanged();
}



void AppController::clearGraphOverviewState() {
    m_graphOverviewBuildTimer.stop();
    m_graphOverviewBuildActive = false;
    m_graphOverviewBuildProgress = 0.0;
    m_graphOverviewBuildNextIndex = 0;
    m_graphOverviewBuildSelectionKey.clear();
    m_graphOverviewBuildSelectedKeys.clear();
    m_graphOverviewBuildIds.clear();
    m_graphOverviewBuiltStartUs = 0;
    m_graphOverviewBuiltEndUs = 0;
    m_graphOverviewBuiltSelectionKey.clear();
    m_replayOverviewGraphHistory.clear();
    m_graphOverviewSeriesCache.clear();
    m_graphOverviewRangeSummaryCache = QStringLiteral("-");
    if (!m_replayLoaded) {
        m_graphOverviewSourceSummaryCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
        m_graphOverviewBuildTextCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
    } else if (!m_modelEnabled || m_graphSignals.isEmpty()) {
        m_graphOverviewSourceSummaryCache = QStringLiteral("모델이 없어서 전체 그래프를 만들 수 없습니다");
        m_graphOverviewBuildTextCache = QStringLiteral("모델 적용 후 다시 시도하세요");
    } else if (m_graphSelectedKeys.isEmpty()) {
        m_graphOverviewSourceSummaryCache = QStringLiteral("전체 그래프 신호를 선택하세요");
        m_graphOverviewBuildTextCache = QStringLiteral("그래프/전체그래프 탭에서 신호를 선택하면 자동 준비됩니다");
    } else {
        m_graphOverviewSourceSummaryCache = QStringLiteral("재생 전체 그래프 준비 대기");
        m_graphOverviewBuildTextCache = QStringLiteral("선택 신호 기준 전체 그래프를 준비합니다");
    }
    emit graphOverviewChanged();
}

QStringList AppController::normalizedGraphOverviewKeys(const QStringList& keys) const {
    QStringList selected;
    for (const QString& key : keys) {
        if (!m_graphSignals.contains(key)) continue;
        if (selected.contains(key)) continue;
        selected << key;
        if (selected.size() >= 4) break;
    }
    return selected;
}

bool AppController::graphOverviewCacheCoversSelection(const QStringList& selected, quint64 startUs, quint64 endUs) const {
    if (selected.isEmpty() || m_graphOverviewBuildActive) return false;
    if (m_replayOverviewGraphHistory.isEmpty()) return false;
    if (m_graphOverviewBuiltStartUs != startUs || m_graphOverviewBuiltEndUs != endUs) return false;

    for (const QString& key : selected) {
        const auto it = m_graphSignals.constFind(key);
        if (it == m_graphSignals.cend()) return false;
        const QString historyKey = it.value().historyKey.isEmpty() ? key : it.value().historyKey;
        const auto historyIt = m_replayOverviewGraphHistory.constFind(historyKey);
        if (historyIt == m_replayOverviewGraphHistory.cend() || historyIt.value().isEmpty()) return false;
    }
    return true;
}

void AppController::reuseGraphOverviewCacheForSelection(const QStringList& selected, quint64 startUs, quint64 endUs) {
    m_graphOverviewBuildTimer.stop();
    m_graphOverviewBuildActive = false;
    m_graphOverviewBuildProgress = 1.0;
    m_graphOverviewBuildNextIndex = 0;
    m_graphOverviewBuildSelectionKey.clear();
    m_graphOverviewBuildSelectedKeys.clear();
    m_graphOverviewBuildIds.clear();
    m_graphOverviewBuiltStartUs = startUs;
    m_graphOverviewBuiltEndUs = endUs;
    m_graphOverviewBuiltSelectionKey = selected.join(QStringLiteral("|"));
    m_graphOverviewBuildTextCache = QStringLiteral("전체 그래프 선택 변경 · 기존 캐시 재사용");
    refreshGraphOverviewSeries();
}

void AppController::restartGraphOverviewBuild(bool clearSeries) {
    m_graphOverviewBuildTimer.stop();

    const QStringList selected = normalizedGraphOverviewKeys(m_graphSelectedKeys);

    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty() || !m_modelEnabled || m_graphSignals.isEmpty() || selected.isEmpty()) {
        clearGraphOverviewState();
        return;
    }

    const QString selectionKey = selected.join(QStringLiteral("|"));
    const quint64 startUs = frames.front().tExtUs;
    const quint64 endUs = frames.back().tExtUs;
    if (!clearSeries && graphOverviewCacheCoversSelection(selected, startUs, endUs)) {
        reuseGraphOverviewCacheForSelection(selected, startUs, endUs);
        return;
    }
    if (!clearSeries
        && !m_graphOverviewBuildActive
        && m_graphOverviewBuiltSelectionKey == selectionKey
        && m_graphOverviewBuiltStartUs == startUs
        && m_graphOverviewBuiltEndUs == endUs) {
        refreshGraphOverviewSeries();
        return;
    }

    m_graphOverviewBuildActive = true;
    m_graphOverviewBuildProgress = 0.0;
    m_graphOverviewBuildNextIndex = 0;
    m_graphOverviewBuildSelectionKey = selectionKey;
    m_graphOverviewBuildSelectedKeys = selected;
    m_graphOverviewBuildIds.clear();
    for (const QString& key : selected) {
        const auto it = m_graphSignals.constFind(key);
        if (it != m_graphSignals.cend()) m_graphOverviewBuildIds.insert(it.value().id);
    }
    m_graphOverviewBuiltStartUs = startUs;
    m_graphOverviewBuiltEndUs = endUs;
    m_graphOverviewBuiltSelectionKey.clear();
    m_replayOverviewGraphHistory.clear();
    if (clearSeries) m_graphOverviewSeriesCache.clear();
    m_graphOverviewSourceSummaryCache = QStringLiteral("재생 전체범위 · 준비 중 · %1선").arg(selected.size());
    m_graphOverviewRangeSummaryCache = QStringLiteral("-");
    m_graphOverviewBuildTextCache = QStringLiteral("전체 그래프 준비 중 0% · 0 / %1 프레임").arg(frames.size());
    emit graphOverviewChanged();
    m_graphOverviewBuildTimer.start(0);
}

void AppController::processGraphOverviewBuildStep() {
    if (!m_graphOverviewBuildActive) return;
    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty() || m_graphOverviewBuildSelectedKeys.isEmpty()) {
        clearGraphOverviewState();
        return;
    }

    QElapsedTimer budget;
    budget.start();
    const int frameCount = int(frames.size());
    int processed = 0;
    const int maxFramesPerSlice = 4096;
    while (m_graphOverviewBuildNextIndex < frameCount && budget.elapsed() < 8 && processed < maxFramesPerSlice) {
        const FrameRecord& fr = frames[size_t(m_graphOverviewBuildNextIndex++)];
        ++processed;
        if (!m_graphOverviewBuildIds.contains(fr.canId)) continue;
        const auto keys = m_graphKeysById.values(fr.canId);
        QSet<QString> updatedHistoryKeys;
        for (const QString& key : keys) {
            if (!m_graphOverviewBuildSelectedKeys.contains(key)) continue;
            const auto it = m_graphSignals.constFind(key);
            if (it == m_graphSignals.cend()) continue;
            const QString historyKey = it.value().historyKey.isEmpty() ? key : it.value().historyKey;
            if (updatedHistoryKeys.contains(historyKey)) continue;
            double value = 0.0;
            if (!graphDecodeDescriptorValue(it.value(), fr, m_signalMessages, &value)) continue;
            auto& series = m_replayOverviewGraphHistory[historyKey];
            if (!series.isEmpty() && series.back().frameUs == fr.tExtUs) series.back().value = value;
            else series.push_back(GraphPoint{fr.tExtUs, value});
            updatedHistoryKeys.insert(historyKey);
        }
    }

    m_graphOverviewBuildProgress = frameCount > 0 ? (double(m_graphOverviewBuildNextIndex) / double(frameCount)) : 0.0;
    m_graphOverviewBuildTextCache = QStringLiteral("전체 그래프 준비 중 %1% · %2 / %3 프레임")
        .arg(qRound(m_graphOverviewBuildProgress * 100.0))
        .arg(m_graphOverviewBuildNextIndex)
        .arg(frameCount);
    emit graphOverviewChanged();

    if (m_graphOverviewBuildNextIndex >= frameCount) {
        m_graphOverviewBuildActive = false;
        m_graphOverviewBuiltSelectionKey = m_graphOverviewBuildSelectionKey;
        m_graphOverviewBuiltStartUs = frames.front().tExtUs;
        m_graphOverviewBuiltEndUs = frames.back().tExtUs;
        refreshGraphOverviewSeries();
        m_graphOverviewBuildTextCache = QStringLiteral("전체 그래프 준비 완료 · %1선 · 전체 %2")
            .arg(m_graphOverviewSeriesCache.size())
            .arg(graphOverviewDurationText());
        emit graphOverviewChanged();
        return;
    }

    m_graphOverviewBuildTimer.start(0);
}

void AppController::refreshGraphOverviewSeries() {
    QVariantList seriesOut;
    QString rangeSummary = QStringLiteral("-");

    const auto& frames = m_replay.frames();
    QStringList selected = m_graphSelectedKeys;
    if (!m_replayLoaded || frames.empty() || !m_modelEnabled || selected.isEmpty() || m_replayOverviewGraphHistory.isEmpty()) {
        m_graphOverviewSeriesCache = seriesOut;
        if (!m_replayLoaded || frames.empty()) {
            m_graphOverviewSourceSummaryCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
            m_graphOverviewBuildTextCache = QStringLiteral("재생 파일을 열면 전체 그래프가 준비됩니다");
        } else if (selected.isEmpty()) {
            m_graphOverviewSourceSummaryCache = QStringLiteral("전체 그래프 신호를 선택하세요");
            m_graphOverviewBuildTextCache = QStringLiteral("그래프/전체그래프 탭에서 최대 4선까지 선택 가능합니다");
        } else {
            m_graphOverviewSourceSummaryCache = QStringLiteral("전체 그래프 데이터 준비 전");
        }
        m_graphOverviewRangeSummaryCache = QStringLiteral("-");
        emit graphOverviewChanged();
        return;
    }

    const quint64 startUs = frames.front().tExtUs;
    const quint64 endUs = frames.back().tExtUs;
    const quint64 durationUs = std::max<quint64>(1000ULL, endUs > startUs ? (endUs - startUs) : 1000ULL);
    const double durationMs = double(durationUs) / 1000.0;
    const quint64 baseBucketUs = std::max<quint64>(1000ULL, durationUs / 1400ULL);

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    bool havePoint = false;
    QStringList unitSet;
    QVector<GraphSignalDescriptor> activeDescs;

    for (const QString& key : selected) {
        const auto descIt = m_graphSignals.constFind(key);
        if (descIt == m_graphSignals.cend()) continue;
        const auto& desc = descIt.value();
        const QString historyKey = desc.historyKey.isEmpty() ? key : desc.historyKey;
        const auto hist = m_replayOverviewGraphHistory.value(historyKey);
        if (hist.isEmpty()) continue;

        QVector<GraphPoint> working;
        if (desc.mode == QStringLiteral("raw")) {
            working = hist;
        } else if (hist.size() >= 2) {
            working.reserve(hist.size() - 1);
            for (int i = 1; i < hist.size(); ++i) {
                const auto& prev = hist.at(i - 1);
                const auto& cur = hist.at(i);
                const double delta = graphWrapAwareDelta(prev.value, cur.value, desc);
                const quint64 dtUs = std::max<quint64>(1ULL, cur.frameUs - prev.frameUs);
                double derivedValue = graphModeIsRate(desc.mode)
                    ? (delta * 1000000.0 / double(dtUs))
                    : delta;
                if (graphModeUsesAbsoluteMagnitude(desc.mode)) derivedValue = std::abs(derivedValue);
                working.push_back(GraphPoint{cur.frameUs, derivedValue});
            }
        }
        if (working.isEmpty()) continue;

        double localMin = std::numeric_limits<double>::max();
        double localMax = std::numeric_limits<double>::lowest();
        double latestValue = 0.0;
        for (const auto& pt : working) {
            localMin = std::min(localMin, pt.value);
            localMax = std::max(localMax, pt.value);
            latestValue = pt.value;
        }

        const quint64 localBucketUs = graphQuantizeBucketUs(desc.group == QStringLiteral("ENCR")
            ? std::max<quint64>(2000ULL, baseBucketUs)
            : baseBucketUs);
        const QVector<GraphBucketPoint> buckets = graphBuildStableBuckets(working.begin(), working.end(), startUs, localBucketUs, 1400);
        QVariantList bucketFlat;
        bucketFlat.reserve(buckets.size() * 4);
        for (const auto& bucket : buckets) {
            bucketFlat.push_back(bucket.tMs);
            bucketFlat.push_back(bucket.minV);
            bucketFlat.push_back(bucket.maxV);
            bucketFlat.push_back(bucket.closeV);
        }

        havePoint = true;
        globalMin = std::min(globalMin, localMin);
        globalMax = std::max(globalMax, localMax);
        activeDescs.push_back(desc);
        if (!desc.unit.trimmed().isEmpty() && !unitSet.contains(desc.unit)) unitSet << desc.unit;

        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), desc.label);
        row.insert(QStringLiteral("unit"), desc.unit);
        row.insert(QStringLiteral("color"), graphColorForIndex(desc.colorIndex));
        row.insert(QStringLiteral("group"), desc.group);
        row.insert(QStringLiteral("mode"), desc.mode);
        row.insert(QStringLiteral("renderMode"), QStringLiteral("bucket"));
        row.insert(QStringLiteral("renderModeLabel"), QStringLiteral("전체요약"));
        row.insert(QStringLiteral("rawFlat"), QVariantList{});
        row.insert(QStringLiteral("bucketFlat"), bucketFlat);
        row.insert(QStringLiteral("latestText"), QString::number(latestValue, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2));
        row.insert(QStringLiteral("minText"), QString::number(localMin, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2));
        row.insert(QStringLiteral("maxText"), QString::number(localMax, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2));
        row.insert(QStringLiteral("rawPointCount"), working.size());
        row.insert(QStringLiteral("drawPointCount"), buckets.size());
        row.insert(QStringLiteral("bucketUs"), double(localBucketUs));
        seriesOut.push_back(row);
    }

    if (havePoint) {
        double yMin = 0.0;
        double yMax = 1.0;
        const GraphStaticRange staticRange = graphStaticRangeForSelection(activeDescs);
        if (staticRange.valid) {
            yMin = staticRange.yMin;
            yMax = staticRange.yMax;
            rangeSummary = unitSet.size() == 1
                ? QStringLiteral("고정축 %1 ~ %2 %3 · 전체 %4 ~ %5 %3")
                    .arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2), unitSet.front(), QString::number(globalMin, 'f', 2), QString::number(globalMax, 'f', 2))
                : QStringLiteral("고정축 %1 ~ %2 · 전체 %3 ~ %4")
                    .arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2), QString::number(globalMin, 'f', 2), QString::number(globalMax, 'f', 2));
        } else {
            const double pad = std::max(1e-6, (globalMax - globalMin) * 0.12);
            yMin = globalMin - pad;
            yMax = globalMax + pad;
            rangeSummary = unitSet.size() == 1
                ? QStringLiteral("전체 %1 ~ %2 %3")
                    .arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2), unitSet.front())
                : QStringLiteral("전체 %1 ~ %2")
                    .arg(QString::number(yMin, 'f', 2), QString::number(yMax, 'f', 2));
        }

        for (int i = 0; i < seriesOut.size(); ++i) {
            QVariantMap row = seriesOut.at(i).toMap();
            row.insert(QStringLiteral("windowMs"), durationMs);
            row.insert(QStringLiteral("yMin"), yMin);
            row.insert(QStringLiteral("yMax"), yMax);
            row.insert(QStringLiteral("detailZoom"), false);
            seriesOut[i] = row;
        }

        m_graphOverviewSourceSummaryCache = QStringLiteral("재생 전체범위 · %1선 · %2").arg(seriesOut.size()).arg(graphOverviewDurationText());
    } else {
        rangeSummary = QStringLiteral("표시 가능한 수치 샘플 없음");
        m_graphOverviewSourceSummaryCache = QStringLiteral("재생 전체범위 · 데이터 없음");
    }

    m_graphOverviewSeriesCache = seriesOut;
    m_graphOverviewRangeSummaryCache = rangeSummary;
    emit graphOverviewChanged();
}

QVariantList AppController::buildGraphOverviewDetailSeries(double startMs, double endMs) const {
    QVariantList seriesOut;

    const auto& frames = m_replay.frames();
    if (!m_replayLoaded || frames.empty() || !m_modelEnabled || m_graphSelectedKeys.isEmpty() || m_replayOverviewGraphHistory.isEmpty()) {
        return seriesOut;
    }

    QStringList selected;
    for (const QString& key : m_graphSelectedKeys) {
        if (!m_graphSignals.contains(key)) continue;
        if (selected.contains(key)) continue;
        selected << key;
        if (selected.size() >= 4) break;
    }
    if (selected.isEmpty()) return seriesOut;

    const quint64 baseUs = frames.front().tExtUs;
    const quint64 totalUs = std::max<quint64>(1000ULL, frames.back().tExtUs > baseUs ? (frames.back().tExtUs - baseUs) : 1000ULL);
    const double totalMs = double(totalUs) / 1000.0;

    const double clampedStartMs = std::clamp(std::min(startMs, endMs), 0.0, totalMs);
    double clampedEndMs = std::clamp(std::max(startMs, endMs), 0.0, totalMs);
    if (clampedEndMs <= clampedStartMs) {
        clampedEndMs = std::min(totalMs, clampedStartMs + 1.0);
    }
    if (clampedEndMs <= clampedStartMs) {
        return seriesOut;
    }

    const quint64 startUs = baseUs + quint64(std::llround(clampedStartMs * 1000.0));
    const quint64 endUs = baseUs + quint64(std::llround(clampedEndMs * 1000.0));
    if (endUs <= startUs) return seriesOut;

    const quint64 windowUs = std::max<quint64>(1000ULL, endUs - startUs);
    const double windowMs = double(windowUs) / 1000.0;
    const int renderLimit = windowMs <= 5000.0 ? 1400 : (windowMs <= 15000.0 ? 1800 : (windowMs <= 60000.0 ? 2000 : 2200));
    const int exactRawLimit = windowMs <= 5000.0 ? 4200 : (windowMs <= 15000.0 ? 3400 : (windowMs <= 60000.0 ? 2600 : 2200));
    const quint64 bucketUs = std::max<quint64>(1000ULL, std::max<quint64>(1ULL, windowUs / quint64(std::max(1, renderLimit))));

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    bool havePoint = false;
    QVector<GraphSignalDescriptor> activeDescs;

    for (const QString& key : selected) {
        const auto descIt = m_graphSignals.constFind(key);
        if (descIt == m_graphSignals.cend()) continue;
        const auto& desc = descIt.value();
        const QString historyKey = desc.historyKey.isEmpty() ? key : desc.historyKey;
        const auto histIt = m_replayOverviewGraphHistory.constFind(historyKey);
        if (histIt == m_replayOverviewGraphHistory.cend() || histIt.value().isEmpty()) continue;
        const auto& hist = histIt.value();

        auto beginIt = std::lower_bound(hist.begin(), hist.end(), startUs,
            [](const GraphPoint& pt, quint64 valueUs) { return pt.frameUs < valueUs; });
        const auto endIt = std::upper_bound(beginIt, hist.end(), endUs,
            [](quint64 valueUs, const GraphPoint& pt) { return valueUs < pt.frameUs; });

        const int visibleRawCount = int(std::distance(beginIt, endIt));
        QVector<GraphPoint> working;
        if (desc.mode == QStringLiteral("raw")) {
            if (visibleRawCount > 0) {
                working.reserve(visibleRawCount);
                for (auto itPt = beginIt; itPt != endIt; ++itPt) working.push_back(*itPt);
            }
        } else {
            auto firstIt = beginIt;
            if (firstIt != hist.begin()) --firstIt;
            if (firstIt != endIt) {
                auto prevIt = firstIt;
                auto curIt = firstIt;
                ++curIt;
                for (; curIt != endIt; ++curIt) {
                    if (curIt->frameUs < startUs) {
                        prevIt = curIt;
                        continue;
                    }
                    const double delta = graphWrapAwareDelta(prevIt->value, curIt->value, desc);
                    const quint64 dtUs = std::max<quint64>(1ULL, curIt->frameUs - prevIt->frameUs);
                    double derivedValue = graphModeIsRate(desc.mode)
                        ? (delta * 1000000.0 / double(dtUs))
                        : delta;
                    if (graphModeUsesAbsoluteMagnitude(desc.mode)) derivedValue = std::abs(derivedValue);
                    working.push_back(GraphPoint{curIt->frameUs, derivedValue});
                    prevIt = curIt;
                }
            }
        }

        int rawPointCount = (desc.mode == QStringLiteral("raw")) ? visibleRawCount : working.size();
        if (rawPointCount <= 0) continue;

        double localMin = std::numeric_limits<double>::max();
        double localMax = std::numeric_limits<double>::lowest();
        double latestValue = 0.0;
        bool hasLocalPoint = false;
        for (const auto& pt : working) {
            localMin = std::min(localMin, pt.value);
            localMax = std::max(localMax, pt.value);
            latestValue = pt.value;
            hasLocalPoint = true;
        }

        const bool preferExactForGroup = desc.group == QStringLiteral("ANGLE")
            || desc.group == QStringLiteral("RPM")
            || desc.group == QStringLiteral("ENCD")
            || desc.group == QStringLiteral("ENCR");
        const bool useRawPolyline = rawPointCount > 0
            && (rawPointCount <= exactRawLimit || (preferExactForGroup && rawPointCount <= 8000));

        QVariantList rawFlat;
        QVariantList bucketFlat;
        int drawPointCount = 0;
        QString renderMode = QStringLiteral("bucket");

        if (useRawPolyline) {
            renderMode = QStringLiteral("raw");
            rawFlat.reserve(rawPointCount * 2);
            for (const auto& pt : working) {
                rawFlat.push_back(double(pt.frameUs - startUs) / 1000.0);
                rawFlat.push_back(pt.value);
            }
            drawPointCount = rawPointCount;
        } else {
            const quint64 localBucketUs = graphQuantizeBucketUs(desc.group == QStringLiteral("ENCR")
                ? std::max<quint64>(2000ULL, bucketUs)
                : bucketUs);
            const QVector<GraphBucketPoint> buckets = graphBuildStableBuckets(working.begin(), working.end(), startUs, localBucketUs, renderLimit);
            bucketFlat.reserve(buckets.size() * 4);
            for (const auto& bucket : buckets) {
                bucketFlat.push_back(bucket.tMs);
                bucketFlat.push_back(bucket.minV);
                bucketFlat.push_back(bucket.maxV);
                bucketFlat.push_back(bucket.closeV);
            }
            drawPointCount = buckets.size();
        }

        if (hasLocalPoint) {
            havePoint = true;
            globalMin = std::min(globalMin, localMin);
            globalMax = std::max(globalMax, localMax);
            activeDescs.push_back(desc);
        }

        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), desc.label);
        row.insert(QStringLiteral("unit"), desc.unit);
        row.insert(QStringLiteral("color"), graphColorForIndex(desc.colorIndex));
        row.insert(QStringLiteral("group"), desc.group);
        row.insert(QStringLiteral("mode"), desc.mode);
        row.insert(QStringLiteral("renderMode"), renderMode);
        row.insert(QStringLiteral("renderModeLabel"), renderMode == QStringLiteral("raw") ? QStringLiteral("원시선") : QStringLiteral("피크보존"));
        row.insert(QStringLiteral("rawFlat"), rawFlat);
        row.insert(QStringLiteral("bucketFlat"), bucketFlat);
        row.insert(QStringLiteral("latestText"), hasLocalPoint ? QString::number(latestValue, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("minText"), hasLocalPoint ? QString::number(localMin, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("maxText"), hasLocalPoint ? QString::number(localMax, 'f', desc.group == QStringLiteral("ENCR") ? 1 : 2) : QStringLiteral("-"));
        row.insert(QStringLiteral("rawPointCount"), rawPointCount);
        row.insert(QStringLiteral("drawPointCount"), drawPointCount);
        row.insert(QStringLiteral("bucketUs"), double(bucketUs));
        seriesOut.push_back(row);
    }

    if (!havePoint) return QVariantList{};

    double yMin = 0.0;
    double yMax = 1.0;
    const GraphStaticRange staticRange = graphStaticRangeForSelection(activeDescs);
    if (staticRange.valid) {
        yMin = staticRange.yMin;
        yMax = staticRange.yMax;
    } else {
        const double pad = std::max(1e-6, (globalMax - globalMin) * 0.12);
        yMin = globalMin - pad;
        yMax = globalMax + pad;
    }

    for (int i = 0; i < seriesOut.size(); ++i) {
        QVariantMap row = seriesOut.at(i).toMap();
        row.insert(QStringLiteral("windowMs"), windowMs);
        row.insert(QStringLiteral("yMin"), yMin);
        row.insert(QStringLiteral("yMax"), yMax);
        row.insert(QStringLiteral("detailZoom"), true);
        seriesOut[i] = row;
    }

    return seriesOut;
}

QVariantList AppController::graphOverviewDetailSeries(double startMs, double endMs) const {
    return buildGraphOverviewDetailSeries(startMs, endMs);
}

void AppController::toggleGraphSignal(const QString& key) {
    QStringList next = m_graphSelectedKeys;
    if (next.contains(key)) {
        next.removeAll(key);
    } else {
        if (!m_graphSignals.contains(key)) return;
        while (next.size() >= 4) next.removeFirst();
        next << key;
    }
    setGraphSelectedKeys(next);
}

void AppController::clearGraphSelection() {
    setGraphSelectedKeys(QStringList{});
}

void AppController::setGraphSelectedKeys(const QStringList& keys) {
    QStringList filtered;
    for (const QString& key : keys) {
        if (!m_graphSignals.contains(key)) continue;
        if (filtered.contains(key)) continue;
        filtered << key;
        if (filtered.size() >= 4) break;
    }
    if (filtered == m_graphSelectedKeys && m_graphPresetKey == QStringLiteral("manual")) return;
    m_graphSelectedKeys = filtered;
    m_graphPresetKey = QStringLiteral("manual");
    resetGraphDetailZoomLock();
    rebuildGraphCatalog();
    if (activeAnalysisSourceKey() == QStringLiteral("replay")) rebuildReplayGraphHistoryWindow();
    restartGraphOverviewBuild(false);
    requestGraphRefresh(true);
}

void AppController::setGraphPresetKey(const QString& key) {
    const QString normalized = key.trimmed();
    if (normalized.isEmpty() || normalized == QStringLiteral("manual")) {
        if (m_graphPresetKey == QStringLiteral("manual")) return;
        m_graphPresetKey = QStringLiteral("manual");
        resetGraphDetailZoomLock();
        rebuildGraphCatalog();
        if (activeAnalysisSourceKey() == QStringLiteral("replay")) rebuildReplayGraphHistoryWindow();
        restartGraphOverviewBuild(true);
        requestGraphRefresh(true);
        return;
    }
    for (const auto& preset : m_graphPresetSpecs) {
        if (preset.key != normalized) continue;
        m_graphPresetKey = preset.key;
        m_graphSelectedKeys = preset.seriesKeys;
        resetGraphDetailZoomLock();
        rebuildGraphCatalog();
        if (activeAnalysisSourceKey() == QStringLiteral("replay")) rebuildReplayGraphHistoryWindow();
        restartGraphOverviewBuild(true);
        requestGraphRefresh(true);
        return;
    }
}


void AppController::setGraphDetailZoom(bool enabled) {
    if (m_graphDetailZoom == enabled) return;
    m_graphDetailZoom = enabled;
    m_graphDisplayRangeValid = false;
    m_graphDisplayRangeKey.clear();
    resetGraphDetailZoomLock();
    emit graphSelectionChanged();
    requestGraphRefresh(true);
}

void AppController::setGraphWindowMs(int ms) {
    const int clamped = std::clamp(ms, 3000, 120000);
    if (m_graphWindowMs == clamped) return;
    m_graphWindowMs = clamped;
    resetGraphDetailZoomLock();
    emit graphSelectionChanged();
    if (activeAnalysisSourceKey() == QStringLiteral("replay")) rebuildReplayGraphHistoryWindow();
    requestGraphRefresh(true);
}

void AppController::refreshPorts() {
    QStringList list;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto& p : ports) list << p.portName();
    m_availablePorts = list;
    emit availablePortsChanged();
}

void AppController::connectPort(const QString& portName) {
    const QString trimmed = portName.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_transportModeKey == QStringLiteral("typed")) resetTypedEvidenceState();
    QString error;
    if (!m_transportRuntime.startSerial(trimmed, &error)) {
        setStatus(error);
    }
}

void AppController::disconnectPort() {
    const bool delayedStop = prepareControlSafeStopForDisconnect(QStringLiteral("operator disconnect safety stop"));
    if (delayedStop) {
        QTimer::singleShot(150, this, [this]() {
            QString error;
            if (!m_transportRuntime.stopSerial(&error)) {
                setStatus(error);
            }
        });
        return;
    }
    QString error;
    if (!m_transportRuntime.stopSerial(&error)) {
        setStatus(error);
    }
}

void AppController::startLog() {
    if (!m_connected) {
        setStatus(QStringLiteral("포트 연결 후 로그를 시작하세요"));
        return;
    }
    if (false && m_transportModeKey == QStringLiteral("typed")) {
        setStatus(QStringLiteral("Typed evidence 저장은 다음 단계입니다. 현재는 수신/파싱 카운터로 packet 확인만 지원합니다."));
        return;
    }
    if (m_logRecordingActive) {
        setStatus(QStringLiteral("이미 로그 기록 중"));
        return;
    }
    if (m_logPendingSave) {
        setStatus(QStringLiteral("이전 로그를 먼저 저장하거나 폐기하세요"));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    if (m_transportModeKey == QStringLiteral("typed")) {
        const StorageRuntime::LogSessionPaths paths = StorageRuntime::makeTypedCapturePaths(stamp, logTargetDirectory(), m_logTargetName);
        const QString sessionDir = paths.recordPath;
        QJsonObject metadata;
        metadata.insert(QStringLiteral("source"), QStringLiteral("live-serial"));
        metadata.insert(QStringLiteral("transport_mode"), QStringLiteral("typed-evidence"));
        metadata.insert(QStringLiteral("operator_target_directory"), logTargetDirectory());
        metadata.insert(QStringLiteral("operator_target_name"), m_logTargetName);
        metadata.insert(QStringLiteral("model_active"), m_modelEnabled);
        metadata.insert(QStringLiteral("model_path"), m_rulesActivePath);
        metadata.insert(QStringLiteral("model_summary"), modelSourceSummary());
        metadata.insert(QStringLiteral("build"), QJsonObject::fromVariantMap(BuildMetadata::toVariantMap(BuildMetadata::current())));

        m_logTypedSession = true;
        m_logTempPath = paths.recordPath;
        m_logTempMetaPath.clear();
        m_logTempModelPath.clear();
        m_logSuggestedSavePath = paths.suggestedSavePath;
        m_logRecordedBytes = 0;
        m_logRecordedFrameCount = 0;
        m_logStopping = false;
        m_logSaving = false;
        m_logPendingSave = false;
        m_logPath = paths.recordPath;
        emit logPathChanged();
        requestLogStateRefresh(true);

        QString error;
        if (!m_transportRuntime.setTypedStorage(true, sessionDir, metadata, &error)) {
            setStatus(error);
            return;
        }
        setStatus(QStringLiteral("Typed capture start requested: %1").arg(sessionDir));
        return;
    }

    const StorageRuntime::LogSessionPaths paths = StorageRuntime::makeLegacyLogPaths(stamp, m_modelEnabled, logTargetDirectory(), m_logTargetName);
    m_logTypedSession = paths.typedSession;
    m_logTempPath = paths.recordPath;
    m_logTempMetaPath = paths.metaPath;
    m_logTempModelPath = paths.modelPath;
    m_logSuggestedSavePath = paths.suggestedSavePath;
    m_logRecordedBytes = 0;
    m_logRecordedFrameCount = 0;
    m_logStopping = false;
    m_logSaving = false;
    m_logPath = m_logTempPath;
    emit logPathChanged();
    requestLogStateRefresh(true);

    const QString rulesPath = m_modelEnabled ? m_rulesActivePath : QString();
    QString error;
    if (!m_transportRuntime.setLegacyLogging(true, m_logTempPath, m_logTempMetaPath, m_logTempModelPath, rulesPath, &error)) {
        setStatus(error);
        return;
    }
    setStatus(QStringLiteral("로그 기록 시작 · 임시 저장 중"));
}

void AppController::resetTypedEvidenceState() {
    m_typedRecordCount = 0;
    m_typedBytesDropped = 0;
    m_typedCrcFailures = 0;
    m_typedLengthFailures = 0;
    m_typedVersionWarnings = 0;
    m_typedSeqGaps = 0;
    m_typedLastMonoUs = 0;
    m_typedLastRecordType.clear();
    m_typedLastCanRxSummary.clear();
    m_typedLastCanTxSummary.clear();
    m_lastTypedEvidenceNotifyWallMs = 0;
    m_lastTypedHealthMonoUs = 0;
    m_lastTypedHealthCanRxTotal = 0;
    m_lastTypedHealthSerialTxTotal = 0;
    m_typedRxHealthParityAnchored = false;
    m_typedRxHealthAnchorBoardTotal = 0;
    m_typedRxHealthAnchorStreamCount = 0;
    m_typedRxHealthBoardDelta = 0;
    m_typedRxHealthStreamDelta = 0;
    m_typedRxHealthMissing = 0;
    m_typedCanRxByBus.clear();
    m_typedCanTxByBus.clear();
    m_typedTypeCounts.clear();
    m_liveProjectionObservedFrames = 0;
    m_liveProjectionProjectedFrames = 0;
    m_liveProjectionWorkerSampledFrames = 0;
    m_liveProjectionWorkerDroppedFrames = 0;
    m_liveProjectionObservedControlEvidenceRecords = 0;
    m_liveProjectionProjectedControlEvidenceRecords = 0;
    m_liveProjectionSampledControlEvidenceRecords = 0;
    m_liveProjectionDroppedFrames = 0;
    m_liveProjectionFlushBudgetHits = 0;
    m_liveProjectionMaxBacklog = 0;
    m_liveProjectionLastFlushMs = 0;
    m_lastRoutineControlWriteNotifyWallMs = 0;
    m_lastHostTxQueueNotifyWallMs = 0;
    m_transportSession.reset();
    m_transportSession.setConnected(m_connected);
    updateTransportDiagnostics();
    m_systemFingerprintHitsByBus.clear();
    m_busRoleResolver.clear();
    seedBusRoleResolver();
    m_evidenceRuntime.reset(m_connected);
    m_controlCapabilityHasBusDescriptors = false;
    m_controlTxAllowedBuses.clear();
    m_controlBusSummary = QStringLiteral("waiting for CAPABILITY bus descriptors");
    m_controlRuntime.clearBurstWallMs();
    m_controlAudit.reset();
    emit transportDiagnosticsChanged();
}

void AppController::stopLog() {
    if (m_logSaving || m_logStopping) {
        setStatus(QStringLiteral("로그 종료/저장 처리 중"));
        return;
    }
    if (!m_logRecordingActive) {
        if (m_logPendingSave) setStatus(QStringLiteral("로그 저장 경로 선택 대기"));
        return;
    }
    m_logStopping = true;
    requestLogStateRefresh(true);
    if (m_logTypedSession) {
        QString error;
        if (!m_transportRuntime.setTypedStorage(false, m_logTempPath, QJsonObject{}, &error)) {
            setStatus(error);
            return;
        }
        setStatus(QStringLiteral("Typed capture finalize requested"));
        return;
    }
    QString error;
    if (!m_transportRuntime.setLegacyLogging(false, QString(), QString(), QString(), QString(), &error)) {
        setStatus(error);
        return;
    }
    setStatus(QStringLiteral("로그 종료 중 · 임시 버퍼 정리"));
}

void AppController::finalizePendingLogSave(const QString& filePath) {
    const StorageRuntime::PendingSavePaths finalPaths = StorageRuntime::makePendingSavePaths(filePath);
    if (!finalPaths.valid) {
        setStatus(QStringLiteral("로그 저장이 취소되었습니다"));
        return;
    }
    const QString finalBin = finalPaths.finalBin;
    const QString finalMeta = finalPaths.finalMeta;
    const QString finalModel = finalPaths.finalModel;

    m_logSaving = true;
    m_logSuggestedSavePath = finalBin;
    requestLogStateRefresh(true);
    setStatus(QStringLiteral("로그 저장 중: %1").arg(finalBin));

    QString failureDetail;
    bool ok = FilePersistence::copyFileAtomically(m_logTempPath, finalBin, &failureDetail);
    ok = ok && FilePersistence::copyFileAtomically(m_logTempMetaPath, finalMeta, &failureDetail);
    if (ok && !m_logTempModelPath.isEmpty() && QFileInfo::exists(m_logTempModelPath)) {
        ok = FilePersistence::copyFileAtomically(m_logTempModelPath, finalModel, &failureDetail);
    }

    m_logSaving = false;
    if (!ok) {
        qCWarning(logDeploy).noquote() << "Pending log finalize failed for" << finalBin << ":" << failureDetail;
        setStatus(QStringLiteral("로그 저장 실패: %1").arg(finalBin));
        requestLogStateRefresh(true);
        return;
    }

    QString cleanupWarning;
    QString cleanupError;
    if (!FilePersistence::removeFileIfExists(m_logTempPath, &cleanupError) && cleanupWarning.isEmpty()) cleanupWarning = cleanupError;
    if (!FilePersistence::removeFileIfExists(m_logTempMetaPath, &cleanupError) && cleanupWarning.isEmpty()) cleanupWarning = cleanupError;
    if (!m_logTempModelPath.isEmpty() && QFileInfo::exists(m_logTempModelPath) &&
        !FilePersistence::removeFileIfExists(m_logTempModelPath, &cleanupError) && cleanupWarning.isEmpty()) {
        cleanupWarning = cleanupError;
    }
    if (!cleanupWarning.isEmpty()) {
        qCWarning(logDeploy).noquote() << "Pending log temp cleanup warning:" << cleanupWarning;
    }

    m_logPath = finalBin;
    m_logLastSavedPath = finalBin;
    m_logSuggestedSavePath = finalBin;
    m_logPendingSave = false;
    m_logStopping = false;
    m_logTempPath.clear();
    m_logTempMetaPath.clear();
    m_logTempModelPath.clear();
    emit logPathChanged();
    requestLogStateRefresh(true);
    setStatus(QStringLiteral("로그 저장 완료: %1").arg(finalBin));
}

void AppController::discardPendingLog() {
    if (m_logTypedSession && !m_logTempPath.isEmpty()) {
        QDir(m_logTempPath).removeRecursively();
    } else {
        removeIfExists(m_logTempPath);
        removeIfExists(m_logTempMetaPath);
        removeIfExists(m_logTempModelPath);
    }
    m_logPendingSave = false;
    m_logRecordingActive = false;
    m_logStopping = false;
    m_logSaving = false;
    m_logTypedSession = false;
    m_logRecordedBytes = 0;
    m_logRecordedFrameCount = 0;
    m_logTempPath.clear();
    m_logTempMetaPath.clear();
    m_logTempModelPath.clear();
    if (!m_logPath.startsWith(defaultTempLogDirectory())) m_logSuggestedSavePath = m_logPath;
    else m_logPath.clear();
    emit logPathChanged();
    requestLogStateRefresh(true);
    setStatus(QStringLiteral("임시 로그 폐기"));
}

void AppController::loadReplay(const QString& filePath) {
    const ReplayRuntime::LoadRequest request = m_replayRuntime.prepareLoadRequest(filePath, m_session);
    const QString normalized = request.normalizedPath;
    if (normalized.isEmpty()) return;

    clearReplaySeekState();
    cancelReplayRebuild(false);
    m_replayCheckpoints.clear();
    clearReplayIssueMarkers();
    clearReplayTypedDiagnostics();
    clearReplaySnapshotState();
    setReplayAnalysisHeld(false);
    const bool previousReplayActive = replayAnalysisActive();
    m_replayFrames.clear();
    m_replayBaseDateTime = {};
    m_replayBaseFrameUs = 0;
    m_replayStates.clear();
    m_replayTimingEvalIds.clear();
    m_replayTimingEvalCursor = 0;
    m_lastReplayTimingEvalCacheWallMs = -1;
    m_replayAlarmGroups.clear();
    m_replayAlarmSequence = 0;
    m_replayBusAlarmEventCount = 0;
    m_replayDisplayedUs = 0;
    m_replayPlayAnchorUs = 0;
    m_replayAnalyzedIndex = -1;
    markAllAnalysisDirty(true);
    updateReplayCursor(0, 0, 0, 0, 0.0);
    requestGraphRefresh(true);
    clearGraphOverviewState();
    m_replayPath = normalized;
    emit replayPathChanged();

    if (!request.exists) {
        setReplayLoaded(false);
        setReplayPlaying(false);
        setStatus(QStringLiteral("재생 파일 없음: %1").arg(normalized));
        return;
    }

    if (request.typedContainer) {
        TypedReplayReader reader;
        QString error;
        if (!reader.loadPath(normalized, &error)) {
            setReplayLoaded(false);
            setReplayPlaying(false);
            setStatus(QStringLiteral("Typed replay load failed: %1").arg(error));
            return;
        }

        const QString streamPath = reader.summary().streamPath;
        loadReplayTimeMeta(streamPath);

        std::vector<FrameRecord> canRxFrames;
        canRxFrames.reserve(size_t(reader.summary().typeCounts.value(static_cast<quint8>(TypedRecordType::CanRxRaw))));
        for (const TypedReplayReader::RecordEntry& entry : reader.records()) {
            const auto can = decodeTypedCanRaw(entry.record);
            if (!can || can->txAudit) continue;

            FrameRecord frame;
            frame.tExtUs = can->monoUs;
            frame.canId = can->canId;
            frame.ext = can->extended;
            frame.rtr = can->rtr;
            frame.dlc = std::min<quint8>(can->dlc, 8);
            std::memcpy(frame.data, can->data, sizeof(frame.data));
            frame.bus = can->bus;
            frame.seq = quint8(entry.record.header.seq & 0xFF);
            canRxFrames.push_back(frame);
        }

        setReplayTypedDiagnosticsFromReader(reader, int(canRxFrames.size()));
        const bool ok = m_replay.loadFrames(QFileInfo(streamPath).absolutePath(), canRxFrames);
        if (!ok) {
            setStatus(QStringLiteral("Typed replay has %1 records but no CAN_RX_RAW frames: %2")
                .arg(reader.summary().recordCount)
                .arg(streamPath));
        }
        return;
    }

    loadReplayTimeMeta(normalized);
    m_replay.loadFile(normalized);
}

void AppController::playReplay(double speed) {
    if (!m_replayLoaded) {
        setStatus(QStringLiteral("재생 파일을 먼저 로드하세요"));
        return;
    }
    if (m_replayRebuildActive) {
        setStatus(QStringLiteral("재생 분석 재구성 중입니다. 완료 후 재생하세요"));
        return;
    }
    clearReplaySeekState();
    m_replaySpeed = (!std::isfinite(speed) || speed <= 0.0) ? 1.0 : std::clamp(speed, 0.1, 8.0);
    setReplayAnalysisHeld(true);
    const int frameCount = m_replay.frameCount();
    if (frameCount > 0) {
        int nextIndex = std::clamp(m_replayAnalyzedIndex + 1, 0, frameCount - 1);
        if (m_replayAnalyzedIndex >= 0 && nextIndex != m_replay.currentIndex()) {
            m_replay.setCurrentIndex(nextIndex);
        }
    }
    m_replayPlayAnchorUs = replayAnalysisUs();
    m_replayPlayClock.restart();
    setReplayPlaying(true);
    m_replay.play(m_replaySpeed);
    emit replayStateChanged();
    if (!m_restoringSession) saveSessionState();
    setStatus(QStringLiteral("재생 시작 x%1").arg(m_replaySpeed, 0, 'f', 1));
}

void AppController::pauseReplay() {
    if (!m_replayLoaded) return;
    clearReplaySeekState();
    setReplayAnalysisHeld(true);
    m_replay.pause();
    setReplayPlaying(false);
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    setStatus(QStringLiteral("재생 일시정지 · 현재 분석 고정"));
}

void AppController::stopReplay() {
    if (!m_replayLoaded) return;
    clearReplaySeekState();
    setReplayAnalysisHeld(true);
    m_replay.stop();
    setReplayPlaying(false);
    if (m_replay.frameCount() > 0) {
        jumpReplayToIndex(0, QStringLiteral("재생 정지 · 시작 프레임으로 복귀"));
    } else {
        refreshTimingRows();
        refreshValueRows();
        refreshAlarmRows();
        setStatus(QStringLiteral("재생 정지"));
    }
}

void AppController::useLiveAnalysis() {
    clearReplaySeekState();
    if (!m_connected) {
        setStatus(QStringLiteral("라이브 연결이 없어 복귀할 수 없습니다"));
        return;
    }
    setReplayPlaying(false);
    setReplayAnalysisHeld(false);
    refreshTimingRows();
    refreshValueRows();
    refreshAlarmRows();
    setStatus(QStringLiteral("라이브 분석 복귀"));
}

void AppController::setReplayLoop(bool enabled) {
    m_replayLoop = enabled;
    m_replay.setLoop(enabled);
    emit replayStateChanged();
}

void AppController::seekReplay(double progress) {
    if (!m_replayLoaded) return;
    const int target = replayIndexForProgress(progress);
    if (target < 0) return;

    setReplayAnalysisHeld(true);
    if (m_replayPlaying) {
        m_replay.pause();
        setReplayPlaying(false);
    }
    m_pendingReplaySeekIndex = target;
    m_replaySeekTimer.start();
}

void AppController::commitSeekReplay(double progress) {
    if (!m_replayLoaded) return;
    const int target = replayIndexForProgress(progress);
    if (target < 0) return;
    clearReplaySeekState();
    jumpReplayToIndex(target, QStringLiteral("재생 지점 이동: %1 / %2").arg(target + 1).arg(m_replay.frameCount()));
}

double AppController::replayTargetProgress() const {
    if (!m_replayLoaded) return 0.0;
    if (!m_replayRebuildActive || m_replayFrameCount <= 1) return m_replayProgress;
    return double(std::clamp(m_replayRebuildTargetIndex, 0, m_replayFrameCount - 1)) / double(m_replayFrameCount - 1);
}

QString AppController::replayTargetTimeText() const {
    if (!m_replayRebuildActive) return replayCurrentTimeText();
    return replayTimeTextForProgress(replayTargetProgress());
}

QString AppController::replayTimeTextForProgress(double progress) const {
    const int target = replayIndexForProgress(progress);
    const auto& frames = m_replay.frames();
    if (target < 0 || frames.empty()) return QStringLiteral("-");
    return timeTextForSourceUs(QStringLiteral("replay"), frames[size_t(target)].tExtUs);
}

void AppController::stepReplay(int delta) {
    if (!m_replayLoaded) return;
    const int frameCount = m_replay.frameCount();
    if (frameCount <= 0) return;
    const int target = std::clamp(m_replay.currentIndex() + delta, 0, frameCount - 1);
    jumpReplayToIndex(target, QStringLiteral("재생 스텝 이동: %1 / %2").arg(target + 1).arg(frameCount));
}

void AppController::exportAnalysisSnapshot(const QString& filePath) {
    const QString normalized = RuntimePaths::normalizeLocalPath(filePath);
    if (normalized.isEmpty()) return;

    auto modelToArray = [](StableMapListModel* model) {
        QJsonArray arr;
        for (int i = 0; i < model->rowCount(); ++i) arr.append(QJsonObject::fromVariantMap(model->get(i)));
        return arr;
    };
    auto detailModelToArray = [](DetailListModel* model) {
        QJsonArray arr;
        for (int i = 0; i < model->count(); ++i) {
            const QModelIndex idx = model->index(i, 0);
            QJsonObject row;
            row.insert(QStringLiteral("key"), model->data(idx, DetailListModel::KeyRole).toString());
            row.insert(QStringLiteral("value"), model->data(idx, DetailListModel::ValueRole).toString());
            row.insert(QStringLiteral("note"), model->data(idx, DetailListModel::NoteRole).toString());
            arr.append(row);
        }
        return arr;
    };
    auto alarmGroupArray = [](const QVector<CanMonitorAnalysis::AlarmGroup>& groups) {
        QJsonArray arr;
        for (const auto& group : groups) {
            QJsonObject row;
            row.insert(QStringLiteral("sequence"), int(group.sequence));
            row.insert(QStringLiteral("key"), group.key);
            row.insert(QStringLiteral("id"), int(group.id));
            row.insert(QStringLiteral("time_text"), group.timeText);
            row.insert(QStringLiteral("severity"), group.severity);
            row.insert(QStringLiteral("name"), group.name);
            row.insert(QStringLiteral("source"), group.source);
            row.insert(QStringLiteral("message"), group.message);
            row.insert(QStringLiteral("active"), group.active);
            row.insert(QStringLiteral("update_count"), group.updateCount);
            row.insert(QStringLiteral("category"), group.category);
            row.insert(QStringLiteral("metric_text"), group.metricText);
            row.insert(QStringLiteral("gauge_pct"), group.gaugePct);
            row.insert(QStringLiteral("history"), stringListToJson(group.history));
            arr.append(row);
        }
        return arr;
    };
    auto variantArrayToMarkdown = [](const QJsonArray& arr, const QString& title, int limit = 40) {
        QStringList lines;
        lines << QStringLiteral("## %1").arg(title);
        if (arr.isEmpty()) {
            lines << QStringLiteral("- 없음");
            return lines.join('\n');
        }
        const int count = std::min(limit, int(arr.size()));
        for (int i = 0; i < count; ++i) {
            const QJsonObject obj = arr.at(i).toObject();
            QStringList parts;
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                const QString value = it.value().isArray()
                                          ? QString::fromUtf8(QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact))
                                          : it.value().isObject()
                                                ? QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact))
                                                : it.value().toVariant().toString();
                if (!value.isEmpty()) parts << QStringLiteral("%1=%2").arg(it.key(), value);
            }
            lines << QStringLiteral("- %1").arg(parts.join(QStringLiteral(" | ")));
        }
        if (arr.size() > count) lines << QStringLiteral("- ... %1개 추가 항목 생략").arg(arr.size() - count);
        return lines.join('\n');
    };

    QJsonObject root;
    root.insert(QStringLiteral("saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("model_name"), modelName());
    root.insert(QStringLiteral("model_key"), modelKey());
    root.insert(QStringLiteral("model_version"), modelVersion());
    root.insert(QStringLiteral("model_vendor"), modelVendor());
    root.insert(QStringLiteral("model_schema"), modelSchema());
    root.insert(QStringLiteral("model_mode"), modelModeText());
    root.insert(QStringLiteral("model_notes"), modelNotes());
    root.insert(QStringLiteral("model_source"), modelSourceSummary());
    root.insert(QStringLiteral("model_path"), modelPath());
    root.insert(QStringLiteral("model_diagnostics_level"), modelDiagnosticsLevel());
    root.insert(QStringLiteral("model_diagnostics_summary"), modelDiagnosticsSummary());
    root.insert(QStringLiteral("status_text"), statusText());
    root.insert(QStringLiteral("analysis_source"), analysisSourceText());
    root.insert(QStringLiteral("analysis_context_text"), analysisContextText());
    root.insert(QStringLiteral("active_view_state_summary"), activeViewStateSummary());
    root.insert(QStringLiteral("replay_cursor_summary"), replayCursorSummary());
    root.insert(QStringLiteral("session_summary"), sessionSummary());
    root.insert(QStringLiteral("live_stats_summary"), liveStatsSummary());
    root.insert(QStringLiteral("root_cause_summary"), rootCauseSummary());
    root.insert(QStringLiteral("operator_headline"), operatorHeadline());
    root.insert(QStringLiteral("operator_action_level"), operatorActionLevel());
    root.insert(QStringLiteral("operator_action_text"), operatorActionText());
    root.insert(QStringLiteral("primary_issue_kind"), primaryIssueKind());
    root.insert(QStringLiteral("primary_issue_id"), primaryIssueId());
    root.insert(QStringLiteral("primary_issue_summary"), primaryIssueSummary());
    root.insert(QStringLiteral("primary_issue_target_tab"), primaryIssueTargetTab());
    root.insert(QStringLiteral("primary_issue_marker_kind"), primaryIssueMarkerKind());
    root.insert(QStringLiteral("operator_focus_text"), operatorFocusText());
    root.insert(QStringLiteral("analysis_reliability_text"), analysisReliabilityText());
    root.insert(QStringLiteral("log_path"), logPath());
    root.insert(QStringLiteral("replay_path"), replayPath());
    root.insert(QStringLiteral("session_file_path"), sessionFilePath());
    root.insert(QStringLiteral("default_log_directory"), defaultLogDirectory());
    root.insert(QStringLiteral("default_snapshot_directory"), defaultSnapshotDirectory());

    QJsonObject liveStats;
    liveStats.insert(QStringLiteral("rx_fps"), liveRxFps());
    liveStats.insert(QStringLiteral("tx_fps"), liveTxFps());
    liveStats.insert(QStringLiteral("dropped_total"), int(droppedTotal()));
    liveStats.insert(QStringLiteral("fifo_overflow_total"), int(fifoOverflowTotal()));
    liveStats.insert(QStringLiteral("err_passive_1s"), errPassiveCount());
    liveStats.insert(QStringLiteral("bus_off_1s"), busOffCount());
    liveStats.insert(QStringLiteral("projection_pending_frames"), int(pendingLiveFrameCount()));
    liveStats.insert(QStringLiteral("projection_observed_frames"), QString::number(m_liveProjectionObservedFrames));
    liveStats.insert(QStringLiteral("projection_projected_frames"), QString::number(m_liveProjectionProjectedFrames));
    liveStats.insert(QStringLiteral("projection_sampled_frames"), QString::number(m_liveProjectionWorkerSampledFrames + m_liveSampledViewDrops));
    liveStats.insert(QStringLiteral("projection_dropped_frames"), QString::number(m_liveProjectionWorkerDroppedFrames + m_liveProjectionDroppedFrames));
    liveStats.insert(QStringLiteral("projection_observed_control_evidence"), QString::number(m_liveProjectionObservedControlEvidenceRecords));
    liveStats.insert(QStringLiteral("projection_projected_control_evidence"), QString::number(m_liveProjectionProjectedControlEvidenceRecords));
    liveStats.insert(QStringLiteral("projection_sampled_control_evidence"), QString::number(m_liveProjectionSampledControlEvidenceRecords));
    liveStats.insert(QStringLiteral("projection_max_backlog"), m_liveProjectionMaxBacklog);
    liveStats.insert(QStringLiteral("projection_flush_budget_hits"), QString::number(m_liveProjectionFlushBudgetHits));
    root.insert(QStringLiteral("live_stats"), liveStats);

    QJsonObject context;
    context.insert(QStringLiteral("connected"), connected());
    context.insert(QStringLiteral("replay_loaded"), replayLoaded());
    context.insert(QStringLiteral("replay_playing"), replayPlaying());
    context.insert(QStringLiteral("replay_analysis_active"), replayAnalysisActive());
    context.insert(QStringLiteral("replay_analysis_held"), replayAnalysisHeld());
    context.insert(QStringLiteral("live_ui_paused"), liveUiPaused());
    context.insert(QStringLiteral("analysis_source_key"), activeAnalysisSourceKey());
    context.insert(QStringLiteral("analysis_source_text"), analysisSourceText());
    context.insert(QStringLiteral("selected_value_id"), selectedValueId());
    context.insert(QStringLiteral("replay_current_index"), replayCurrentIndex());
    context.insert(QStringLiteral("replay_frame_count"), replayFrameCount());
    context.insert(QStringLiteral("replay_progress"), replayProgress());
    context.insert(QStringLiteral("replay_current_time"), replayCurrentTimeText());
    context.insert(QStringLiteral("replay_duration"), replayDurationText());
    context.insert(QStringLiteral("replay_speed"), replaySpeed());
    context.insert(QStringLiteral("replay_loop"), replayLoop());
    root.insert(QStringLiteral("context"), context);

    QJsonObject counts;
    counts.insert(QStringLiteral("timing_rows"), m_timingModel.count());
    counts.insert(QStringLiteral("value_rows"), m_valueModel.count());
    counts.insert(QStringLiteral("alarm_rows"), m_alarmModel.count());
    counts.insert(QStringLiteral("timing_issue_count"), timingIssueCount());
    counts.insert(QStringLiteral("value_issue_count"), valueIssueCount());
    counts.insert(QStringLiteral("active_alarm_count"), activeAlarmCount());
    counts.insert(QStringLiteral("live_observed_ids"), liveObservedIdCount());
    counts.insert(QStringLiteral("replay_observed_ids"), replayObservedIdCount());
    counts.insert(QStringLiteral("live_frame_rows"), m_liveFrames.count());
    counts.insert(QStringLiteral("replay_frame_rows"), m_replayFrames.count());
    counts.insert(QStringLiteral("recent_frame_rows"), m_recentFrames.count());
    counts.insert(QStringLiteral("replay_timing_markers"), replayTimingMarkerCount());
    counts.insert(QStringLiteral("replay_value_markers"), replayValueMarkerCount());
    counts.insert(QStringLiteral("replay_alarm_markers"), replayAlarmMarkerCount());
    root.insert(QStringLiteral("counts"), counts);
    root.insert(QStringLiteral("replay_issue_markers"), QJsonArray::fromVariantList(replayIssueMarkers()));

    QJsonObject modelDiagnostics;
    modelDiagnostics.insert(QStringLiteral("level"), modelDiagnosticsLevel());
    modelDiagnostics.insert(QStringLiteral("summary"), modelDiagnosticsSummary());
    modelDiagnostics.insert(QStringLiteral("rule_count"), rulesCount());
    modelDiagnostics.insert(QStringLiteral("timing_rule_count"), modelTimingRuleCount());
    modelDiagnostics.insert(QStringLiteral("message_count"), signalDbMessageCount());
    modelDiagnostics.insert(QStringLiteral("signal_count"), modelSignalCount());
    modelDiagnostics.insert(QStringLiteral("alarm_signal_count"), modelAlarmSignalCount());
    modelDiagnostics.insert(QStringLiteral("monitor_only_signal_count"), modelMonitorOnlySignalCount());
    root.insert(QStringLiteral("model_diagnostics"), modelDiagnostics);

    QJsonObject storagePaths;
    storagePaths.insert(QStringLiteral("session_file_path"), sessionFilePath());
    storagePaths.insert(QStringLiteral("default_log_directory"), defaultLogDirectory());
    storagePaths.insert(QStringLiteral("default_snapshot_directory"), defaultSnapshotDirectory());
    storagePaths.insert(QStringLiteral("current_log_path"), logPath());
    storagePaths.insert(QStringLiteral("current_replay_path"), replayPath());
    root.insert(QStringLiteral("storage_paths"), storagePaths);

    QJsonObject filters;
    filters.insert(QStringLiteral("live_frame_id_filter"), m_liveFrameView.idFilter());
    filters.insert(QStringLiteral("replay_frame_id_filter"), m_replayFrameView.idFilter());
    filters.insert(QStringLiteral("live_frame_bus_filter"), m_liveFrameView.busFilter());
    filters.insert(QStringLiteral("replay_frame_bus_filter"), m_replayFrameView.busFilter());
    filters.insert(QStringLiteral("live_view"), viewStateToJson(m_liveViewState));
    filters.insert(QStringLiteral("replay_view"), viewStateToJson(m_replayViewState));
    filters.insert(QStringLiteral("active_view"), viewStateToJson(viewStateForSource(activeAnalysisSourceKey())));
    root.insert(QStringLiteral("filters"), filters);

    QJsonObject sorts;
    sorts.insert(QStringLiteral("timing_mode"), m_timingSortMode);
    sorts.insert(QStringLiteral("timing_descending"), m_timingSortDescending);
    sorts.insert(QStringLiteral("value_mode"), m_valueSortMode);
    sorts.insert(QStringLiteral("value_descending"), m_valueSortDescending);
    sorts.insert(QStringLiteral("alarm_mode"), m_alarmSortMode);
    sorts.insert(QStringLiteral("alarm_descending"), m_alarmSortDescending);
    root.insert(QStringLiteral("sorts"), sorts);

    const QJsonArray timingRows = modelToArray(&m_timingModel);
    const QJsonArray valueRows = modelToArray(&m_valueModel);
    const QJsonArray alarmRows = modelToArray(&m_alarmModel);
    const QJsonArray valueDetailRows = detailModelToArray(&m_valueDetailModel);
    const QJsonArray liveAlarmGroups = alarmGroupArray(m_liveAlarmGroups);
    const QJsonArray replayAlarmGroups = alarmGroupArray(m_replayAlarmGroups);

    root.insert(QStringLiteral("timing_rows"), timingRows);
    root.insert(QStringLiteral("value_rows"), valueRows);
    root.insert(QStringLiteral("alarm_rows"), alarmRows);
    root.insert(QStringLiteral("value_detail_rows"), valueDetailRows);
    root.insert(QStringLiteral("live_alarm_groups"), liveAlarmGroups);
    root.insert(QStringLiteral("replay_alarm_groups"), replayAlarmGroups);

    QFile outFile(normalized);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(QStringLiteral("스냅샷 저장 실패: %1").arg(outFile.errorString()));
        return;
    }

    const QString lower = normalized.toLower();
    if (lower.endsWith(QStringLiteral(".md")) || lower.endsWith(QStringLiteral(".markdown"))) {
        QStringList markdown;
        markdown << QStringLiteral("# CAN Monitor Analysis Snapshot");
        markdown << QStringLiteral("");
        markdown << QStringLiteral("- saved_at: %1").arg(root.value(QStringLiteral("saved_at")).toString());
        markdown << QStringLiteral("- model_name: %1").arg(modelName());
        markdown << QStringLiteral("- model_key: %1").arg(modelKey());
        markdown << QStringLiteral("- model_version: %1").arg(modelVersion());
        markdown << QStringLiteral("- model_mode: %1").arg(modelModeText());
        markdown << QStringLiteral("- model_diagnostics: %1 / %2").arg(modelDiagnosticsLevel(), modelDiagnosticsSummary());
        markdown << QStringLiteral("- analysis_source: %1").arg(analysisSourceText());
        markdown << QStringLiteral("- analysis_context: %1").arg(analysisContextText());
        markdown << QStringLiteral("- root_cause_summary: %1").arg(rootCauseSummary());
        markdown << QStringLiteral("- operator_headline: %1").arg(operatorHeadline());
        markdown << QStringLiteral("- operator_action: %1 / %2").arg(operatorActionLevel(), operatorActionText());
        markdown << QStringLiteral("- primary_issue: %1 / %2 / %3").arg(primaryIssueKind(), primaryIssueId(), primaryIssueSummary());
        markdown << QStringLiteral("- focus: %1 / %2").arg(primaryIssueTargetTab(), operatorFocusText());
        markdown << QStringLiteral("- analysis_reliability: %1").arg(analysisReliabilityText());
        markdown << QStringLiteral("- session_summary: %1").arg(sessionSummary());
        markdown << QStringLiteral("- replay_cursor_summary: %1").arg(replayCursorSummary());
        markdown << QStringLiteral("- live_stats_summary: %1").arg(liveStatsSummary());
        markdown << QStringLiteral("- session_file_path: %1").arg(sessionFilePath());
        markdown << QStringLiteral("- default_log_directory: %1").arg(defaultLogDirectory());
        markdown << QStringLiteral("- default_snapshot_directory: %1").arg(defaultSnapshotDirectory());
        markdown << QStringLiteral("");
        markdown << QStringLiteral("## Counts");
        markdown << QStringLiteral("- timing_rows: %1").arg(m_timingModel.count());
        markdown << QStringLiteral("- value_rows: %1").arg(m_valueModel.count());
        markdown << QStringLiteral("- alarm_rows: %1").arg(m_alarmModel.count());
        markdown << QStringLiteral("- timing_issue_count: %1").arg(timingIssueCount());
        markdown << QStringLiteral("- value_issue_count: %1").arg(valueIssueCount());
        markdown << QStringLiteral("- active_alarm_count: %1").arg(activeAlarmCount());
        markdown << QStringLiteral("- live_observed_ids: %1").arg(liveObservedIdCount());
        markdown << QStringLiteral("- replay_observed_ids: %1").arg(replayObservedIdCount());
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(timingRows, QStringLiteral("Timing Rows"));
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(valueRows, QStringLiteral("Value Rows"));
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(alarmRows, QStringLiteral("Alarm Rows"));
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(valueDetailRows, QStringLiteral("Value Detail Rows"), 80);
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(liveAlarmGroups, QStringLiteral("Live Alarm Groups"), 80);
        markdown << QStringLiteral("");
        markdown << variantArrayToMarkdown(replayAlarmGroups, QStringLiteral("Replay Alarm Groups"), 80);
        outFile.write(markdown.join('\n').toUtf8());
    } else {
        outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    outFile.close();

    if (lower.endsWith(QStringLiteral(".json"))) {
        const QFileInfo info(normalized);
        const QString mdPath = info.absolutePath() + QDir::separator() + info.completeBaseName() + QStringLiteral(".summary.md");
        QFile mdFile(mdPath);
        if (mdFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QStringList markdown;
            markdown << QStringLiteral("# CAN Monitor Analysis Snapshot");
            markdown << QStringLiteral("");
            markdown << QStringLiteral("- saved_at: %1").arg(root.value(QStringLiteral("saved_at")).toString());
            markdown << QStringLiteral("- model_name: %1").arg(modelName());
            markdown << QStringLiteral("- model_key: %1").arg(modelKey());
            markdown << QStringLiteral("- model_version: %1").arg(modelVersion());
            markdown << QStringLiteral("- model_mode: %1").arg(modelModeText());
            markdown << QStringLiteral("- model_diagnostics: %1 / %2").arg(modelDiagnosticsLevel(), modelDiagnosticsSummary());
            markdown << QStringLiteral("- analysis_source: %1").arg(analysisSourceText());
            markdown << QStringLiteral("- analysis_context: %1").arg(analysisContextText());
            markdown << QStringLiteral("- root_cause_summary: %1").arg(rootCauseSummary());
            markdown << QStringLiteral("- operator_headline: %1").arg(operatorHeadline());
            markdown << QStringLiteral("- operator_action: %1 / %2").arg(operatorActionLevel(), operatorActionText());
            markdown << QStringLiteral("- primary_issue: %1 / %2 / %3").arg(primaryIssueKind(), primaryIssueId(), primaryIssueSummary());
            markdown << QStringLiteral("- focus: %1 / %2").arg(primaryIssueTargetTab(), operatorFocusText());
            markdown << QStringLiteral("- analysis_reliability: %1").arg(analysisReliabilityText());
            markdown << QStringLiteral("- session_summary: %1").arg(sessionSummary());
            markdown << QStringLiteral("- replay_cursor_summary: %1").arg(replayCursorSummary());
            markdown << QStringLiteral("- live_stats_summary: %1").arg(liveStatsSummary());
            markdown << QStringLiteral("- session_file_path: %1").arg(sessionFilePath());
            markdown << QStringLiteral("- default_log_directory: %1").arg(defaultLogDirectory());
            markdown << QStringLiteral("- default_snapshot_directory: %1").arg(defaultSnapshotDirectory());
            markdown << QStringLiteral("");
            markdown << QStringLiteral("## Counts");
            markdown << QStringLiteral("- timing_rows: %1").arg(m_timingModel.count());
            markdown << QStringLiteral("- value_rows: %1").arg(m_valueModel.count());
            markdown << QStringLiteral("- alarm_rows: %1").arg(m_alarmModel.count());
            markdown << QStringLiteral("- timing_issue_count: %1").arg(timingIssueCount());
            markdown << QStringLiteral("- value_issue_count: %1").arg(valueIssueCount());
            markdown << QStringLiteral("- active_alarm_count: %1").arg(activeAlarmCount());
            markdown << QStringLiteral("- live_observed_ids: %1").arg(liveObservedIdCount());
            markdown << QStringLiteral("- replay_observed_ids: %1").arg(replayObservedIdCount());
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(timingRows, QStringLiteral("Timing Rows"));
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(valueRows, QStringLiteral("Value Rows"));
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(alarmRows, QStringLiteral("Alarm Rows"));
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(valueDetailRows, QStringLiteral("Value Detail Rows"), 80);
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(liveAlarmGroups, QStringLiteral("Live Alarm Groups"), 80);
            markdown << QStringLiteral("");
            markdown << variantArrayToMarkdown(replayAlarmGroups, QStringLiteral("Replay Alarm Groups"), 80);
            mdFile.write(markdown.join('\n').toUtf8());
            mdFile.close();
        }
    }

    setStatus(QStringLiteral("분석 스냅샷 저장: %1").arg(normalized));
}

void AppController::clearFrames() {
    clearReplaySeekState();
    cancelReplayRebuild(false);
    m_replayCheckpoints.clear();
    clearReplaySnapshotState();
    m_pendingLiveFrames.clear();
    m_pendingLiveFrameOffset = 0;
    m_liveFlushTimer.stop();
    m_liveSampledViewDrops = 0;
    m_liveProjectionObservedFrames = 0;
    m_liveProjectionProjectedFrames = 0;
    m_liveProjectionWorkerSampledFrames = 0;
    m_liveProjectionWorkerDroppedFrames = 0;
    m_liveProjectionObservedControlEvidenceRecords = 0;
    m_liveProjectionProjectedControlEvidenceRecords = 0;
    m_liveProjectionSampledControlEvidenceRecords = 0;
    m_liveProjectionDroppedFrames = 0;
    m_liveProjectionFlushBudgetHits = 0;
    m_liveProjectionMaxBacklog = 0;
    m_liveProjectionLastFlushMs = 0;
    m_lastRoutineControlWriteNotifyWallMs = 0;
    m_lastHostTxQueueNotifyWallMs = 0;
    setReplayAnalysisHeld(false);
    m_recentFrames.clear();
    m_liveFrames.clear();
    m_replayFrames.clear();
    clearGraphHistory();
    requestGraphRefresh(true);
    m_liveBaseDateTime = {};
    m_liveBaseFrameUs = 0;
    m_liveLatestUs = 0;
    m_lastLiveFrameWallMs = -1;
    m_lastLiveStatsWallMs = -1;
    m_replayBaseDateTime = {};
    m_replayBaseFrameUs = 0;
    m_liveStates.clear();
    m_replayStates.clear();
    m_liveTimingEvalIds.clear();
    m_replayTimingEvalIds.clear();
    m_liveTimingEvalCursor = 0;
    m_replayTimingEvalCursor = 0;
    m_lastLiveTimingEvalCacheWallMs = -1;
    m_lastReplayTimingEvalCacheWallMs = -1;
    m_liveAlarmGroups.clear();
    m_replayAlarmGroups.clear();
    m_lastDroppedTotalObserved = 0;
    m_lastFifoOverflowTotalObserved = 0;
    m_lastDropBumpMs = -1;
    m_lastFifoBumpMs = -1;
    m_dropAlarmActive = false;
    m_fifoAlarmActive = false;
    m_errPassiveAlarmActive = false;
    m_busOffAlarmActive = false;
    m_liveAlarmSequence = 0;
    m_replayAlarmSequence = 0;
    m_liveBusAlarmEventCount = 0;
    m_replayBusAlarmEventCount = 0;
    m_alarmModel.clear();
    m_hasSelectedValueId = false;
    m_selectedValueCanId = 0;
    m_liveViewState.hasSelectedValueId = false;
    m_liveViewState.selectedValueCanId = 0;
    m_replayViewState.hasSelectedValueId = false;
    m_replayViewState.selectedValueCanId = 0;
    emit selectedValueIdChanged();
    clearDerivedRows();
    m_timingRowsDirty = false;
    m_valueRowsDirty = false;
    m_valueDetailsDirty = false;
    m_alarmRowsDirty = false;
    m_timingReorderRequested = true;
    m_valueReorderRequested = true;
    m_alarmReorderRequested = true;
    setStatus(QStringLiteral("프레임/분석 목록 지움"));
    requestDerivedSummaryRefresh(true);
}

void AppController::setTimingViewHeld(bool held) {
    if (m_timingViewHeld == held) return;
    m_timingViewHeld = held;
    emit viewHoldChanged();
    if (!held && m_timingRowsDirty && !analysisPaused()) refreshTimingRows();
}

void AppController::setValueViewHeld(bool held) {
    if (m_valueViewHeld == held) return;
    m_valueViewHeld = held;
    emit viewHoldChanged();
    if (!held && !analysisPaused()) {
        if (m_valueRowsDirty) refreshValueRows();
        else if (m_valueDetailsDirty) maybeRefreshValueDetails(false);
    }
}

void AppController::setAlarmViewHeld(bool held) {
    if (m_alarmViewHeld == held) return;
    m_alarmViewHeld = held;
    emit viewHoldChanged();
    if (!held && m_alarmRowsDirty && !analysisPaused()) refreshAlarmRows();
}

void AppController::setLiveUiPaused(bool paused) {
    if (m_liveUiPaused == paused) return;
    const bool previousReplayActive = replayAnalysisActive();
    m_liveUiPaused = paused;
    emit liveUiPausedChanged();
    handleAnalysisSourceMaybeChanged(previousReplayActive);
    emit derivedSummaryChanged();
    if (analysisPaused()) {
        setStatus(QStringLiteral("라이브 화면 정지 · 라이브 분석 고정"));
        return;
    }
    setStatus(replayAnalysisActive() ? QStringLiteral("재생 분석 활성") : QStringLiteral("라이브 화면 재개"));
    refreshTimingRows();
    refreshValueRows();
    if (m_alarmRowsDirty) refreshAlarmRows();
}

void AppController::toggleLiveUiPaused() {
    setLiveUiPaused(!m_liveUiPaused);
}

void AppController::selectValueId(const QString& idTextValue) {
    quint32 parsed = 0;
    if (!parseCanIdText(idTextValue, &parsed)) return;
    if (m_hasSelectedValueId && m_selectedValueCanId == parsed) return;
    m_selectedValueCanId = parsed;
    m_hasSelectedValueId = true;
    m_lastValueDetailSignature.clear();
    m_lastValueDetailProjectionWallMs = -1;
    syncActiveViewSelection();
    emit selectedValueIdChanged();
    invalidateValueDetailSignalCache();
    maybeRefreshValueDetails(true);
}

void AppController::setTimingSortMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("name") ||
                          normalized == QStringLiteral("expected") || normalized == QStringLiteral("gap") ||
                          normalized == QStringLiteral("age") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("reason"))
                             ? normalized
                             : QStringLiteral("id");
    if (m_timingSortMode == next) return;
    m_timingSortMode = next;
    m_timingReorderRequested = true;
    emit sortOptionsChanged();
    refreshTimingRows();
}

void AppController::setTimingSortDescending(bool descending) {
    if (m_timingSortDescending == descending) return;
    m_timingSortDescending = descending;
    m_timingReorderRequested = true;
    emit sortOptionsChanged();
    refreshTimingRows();
}

void AppController::setValueSortMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("name") ||
                          normalized == QStringLiteral("raw") || normalized == QStringLiteral("gap") ||
                          normalized == QStringLiteral("age") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("reason"))
                             ? normalized
                             : QStringLiteral("id");
    if (m_valueSortMode == next) return;
    m_valueSortMode = next;
    m_valueReorderRequested = true;
    emit sortOptionsChanged();
    refreshValueRows();
}

void AppController::setValueSortDescending(bool descending) {
    if (m_valueSortDescending == descending) return;
    m_valueSortDescending = descending;
    m_valueReorderRequested = true;
    emit sortOptionsChanged();
    refreshValueRows();
}

void AppController::setAlarmSortMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("id") ||
                          normalized == QStringLiteral("name") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("message"))
                             ? normalized
                             : QStringLiteral("time");
    if (m_alarmSortMode == next) return;
    m_alarmSortMode = next;
    m_alarmReorderRequested = true;
    emit sortOptionsChanged();
    refreshAlarmRows();
}

void AppController::setAlarmSortDescending(bool descending) {
    if (m_alarmSortDescending == descending) return;
    m_alarmSortDescending = descending;
    m_alarmReorderRequested = true;
    emit sortOptionsChanged();
    refreshAlarmRows();
}

void AppController::toggleTimingSort(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("name") ||
                          normalized == QStringLiteral("expected") || normalized == QStringLiteral("gap") ||
                          normalized == QStringLiteral("age") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("reason"))
                             ? normalized
                             : QStringLiteral("id");
    if (m_timingSortMode == next) {
        setTimingSortDescending(!m_timingSortDescending);
    } else {
        m_timingSortMode = next;
        m_timingSortDescending = false;
        m_timingReorderRequested = true;
        emit sortOptionsChanged();
        refreshTimingRows();
    }
}

void AppController::toggleValueSort(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("name") ||
                          normalized == QStringLiteral("raw") || normalized == QStringLiteral("gap") ||
                          normalized == QStringLiteral("age") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("reason"))
                             ? normalized
                             : QStringLiteral("id");
    if (m_valueSortMode == next) {
        setValueSortDescending(!m_valueSortDescending);
    } else {
        m_valueSortMode = next;
        m_valueSortDescending = false;
        m_valueReorderRequested = true;
        emit sortOptionsChanged();
        refreshValueRows();
    }
}

void AppController::toggleAlarmSort(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    const QString next = (normalized == QStringLiteral("severity") || normalized == QStringLiteral("id") ||
                          normalized == QStringLiteral("name") || normalized == QStringLiteral("source") ||
                          normalized == QStringLiteral("message"))
                             ? normalized
                             : QStringLiteral("time");
    if (m_alarmSortMode == next) {
        setAlarmSortDescending(!m_alarmSortDescending);
    } else {
        m_alarmSortMode = next;
        m_alarmSortDescending = false;
        m_alarmReorderRequested = true;
        emit sortOptionsChanged();
        refreshAlarmRows();
    }
}

void AppController::setTimingFilterId(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterId == next) return;
    m_timingFilterId = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterId = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterSeverity(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterSeverity == next) return;
    m_timingFilterSeverity = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterSeverity = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterName(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterName == next) return;
    m_timingFilterName = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterName = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterReason(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterReason == next) return;
    m_timingFilterReason = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterReason = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterExpected(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterExpected == next) return;
    m_timingFilterExpected = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterExpected = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterGap(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterGap == next) return;
    m_timingFilterGap = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterGap = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterAge(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterAge == next) return;
    m_timingFilterAge = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterAge = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setTimingFilterSource(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_timingFilterSource == next) return;
    m_timingFilterSource = next;
    viewStateForSource(activeAnalysisSourceKey()).timingFilterSource = next;
    m_timingReorderRequested = true;
    emit filtersChanged();
    refreshTimingRows();
}

void AppController::setValueFilterId(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterId == next) return;
    m_valueFilterId = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterId = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterSeverity(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterSeverity == next) return;
    m_valueFilterSeverity = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterSeverity = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterName(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterName == next) return;
    m_valueFilterName = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterName = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterSource(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterSource == next) return;
    m_valueFilterSource = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterSource = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterRaw(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterRaw == next) return;
    m_valueFilterRaw = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterRaw = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterGap(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterGap == next) return;
    m_valueFilterGap = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterGap = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setValueFilterReason(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_valueFilterReason == next) return;
    m_valueFilterReason = next;
    viewStateForSource(activeAnalysisSourceKey()).valueFilterReason = next;
    m_valueReorderRequested = true;
    emit filtersChanged();
    refreshValueRows();
}

void AppController::setAlarmFilterId(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterId == next) return;
    m_alarmFilterId = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterId = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterSeverity(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterSeverity == next) return;
    m_alarmFilterSeverity = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterSeverity = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterTime(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterTime == next) return;
    m_alarmFilterTime = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterTime = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterName(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterName == next) return;
    m_alarmFilterName = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterName = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterSource(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterSource == next) return;
    m_alarmFilterSource = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterSource = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterMessage(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterMessage == next) return;
    m_alarmFilterMessage = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterMessage = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setAlarmFilterText(const QString& text) {
    const QString next = normalizedFilterText(text);
    if (m_alarmFilterText == next) return;
    m_alarmFilterText = next;
    viewStateForSource(activeAnalysisSourceKey()).alarmFilterText = next;
    m_alarmReorderRequested = true;
    emit filtersChanged();
    refreshAlarmRows();
}

void AppController::setStatus(const QString& text) {
    if (m_statusText == text) return;
    m_statusText = text;
    emit statusTextChanged();
}

void AppController::setReplayLoaded(bool loaded) {
    if (m_replayLoaded == loaded) return;
    const bool previousReplayActive = replayAnalysisActive();
    m_replayLoaded = loaded;
    if (!loaded) {
        m_replayAnalysisHeld = false;
        m_replayStates.clear();
        m_replayAlarmGroups.clear();
        m_replayAlarmSequence = 0;
        clearReplaySnapshotState();
    }
    emit replayStateChanged();
    handleAnalysisSourceMaybeChanged(previousReplayActive);
    requestGraphRefresh(true);
    emit derivedSummaryChanged();
}

void AppController::setReplayPlaying(bool playing) {
    if (m_replayPlaying == playing) return;
    const bool previousReplayActive = replayAnalysisActive();
    m_replayPlaying = playing;
    if (m_replayPlaying) {
        m_replayPlayAnchorUs = replayAnalysisUs();
        m_replayPlayClock.restart();
    } else {
        m_replayPlayAnchorUs = m_replayDisplayedUs > 0 ? m_replayDisplayedUs : m_replayCurrentUs;
    }
    emit replayStateChanged();
    handleAnalysisSourceMaybeChanged(previousReplayActive);
    requestGraphRefresh(true);
    emit derivedSummaryChanged();
}

void AppController::updateReplayCursor(int index, int frameCount, quint64 currentUs, quint64 durationUs, double progress) {
    bool changed = false;
    const bool keepSnapshot = m_replayRebuildActive && m_replaySnapshotValid;
    const int displayIndex = keepSnapshot
        ? ((m_replaySnapshotAnalyzedIndex >= 0) ? m_replaySnapshotAnalyzedIndex : index)
        : ((m_replayAnalyzedIndex >= 0) ? m_replayAnalyzedIndex : index);
    const quint64 displayUs = keepSnapshot ? m_replaySnapshotDisplayedUs : currentUs;
    const double displayProgress = (frameCount > 1)
        ? (double(std::clamp(displayIndex, 0, frameCount - 1)) / double(frameCount - 1))
        : progress;
    if (m_replayCurrentIndex != displayIndex) { m_replayCurrentIndex = displayIndex; changed = true; }
    if (m_replayFrameCount != frameCount) { m_replayFrameCount = frameCount; changed = true; }
    if (m_replayCurrentUs != displayUs) { m_replayCurrentUs = displayUs; changed = true; }
    if (m_replayDurationUs != durationUs) { m_replayDurationUs = durationUs; changed = true; }
    if (!qFuzzyCompare(m_replayProgress + 1.0, displayProgress + 1.0)) { m_replayProgress = displayProgress; changed = true; }
    if (changed) {
        emit replayStateChanged();
        requestGraphRefresh(false);
    }
}

bool AppController::loadModelFile(const QString& path) {
    CanModel::ModelPack pack;
    QString errorText;
    if (!CanModel::ModelPackLoader::loadFile(path, &pack, &errorText)) {
        setStatus(errorText.isEmpty() ? QStringLiteral("모델 로드 실패") : errorText);
        return false;
    }

    m_rules = pack.rules;
    m_signalMessages = pack.messages;
    m_controlPolicy = pack.controlPolicy;
    invalidateValueDetailSignalCache();
    rebuildGraphCatalog();
    clearGraphHistory();
    m_modelMeta = pack.meta;
    m_modelEnabled = true;
    m_liveAlarmGroups.clear();
    m_replayAlarmGroups.clear();
    m_liveAlarmSequence = 0;
    m_replayAlarmSequence = 0;
    m_liveBusAlarmEventCount = 0;
    m_replayBusAlarmEventCount = 0;
    m_dropAlarmActive = false;
    m_fifoAlarmActive = false;
    m_errPassiveAlarmActive = false;
    m_busOffAlarmActive = false;
    auto resetStateMap = [](QHash<quint32, IdState>& states) {
        for (auto it = states.begin(); it != states.end(); ++it) {
            it.value().cachedTimingRow.clear();
            it.value().cachedPreviewInfo.clear();
            it.value().cachedValueAlarmInfo.clear();
            it.value().cachedValueRow.clear();
            it.value().timingDerivedDirty = true;
            it.value().valueDerivedDirty = true;
            it.value().lastTimingAgeBucket = -999;
            it.value().nextTimingEvalMs = std::numeric_limits<qint64>::max();
            it.value().activeValueAlarmKey.clear();
            it.value().lastValueAlarmMessage.clear();
            it.value().lastValueAlarmSeenMs = -1;
            it.value().valueAlarmEventCount = 0;
            it.value().lastValueFingerprint = 0;
            it.value().lastValueRenderedSeverity.clear();
            it.value().lastValueRenderedReason.clear();
            it.value().activeTimingAlarmKey.clear();
            it.value().lastTimingAlarmSeenMs = -1;
            it.value().timingEvents.clear();
            it.value().timingEventCount = 0;
            it.value().lastTimingIssueKey.clear();
            it.value().timingIssueLatched = false;
        }
    };
    resetStateMap(m_liveStates);
    resetStateMap(m_replayStates);
    m_alarmCapableSignalIds.clear();
    for (auto it = m_signalMessages.cbegin(); it != m_signalMessages.cend(); ++it) {
        for (const auto& sig : it.value().signalSpecs) {
            if (signalSpecHasAlarmDefinition(sig)) {
                m_alarmCapableSignalIds.insert(it.key());
                break;
            }
        }
    }
    markAllAnalysisDirty(true);

    const QString normalized = path.startsWith(QStringLiteral(":")) ? path : RuntimePaths::normalizeLocalPath(path);
    m_rulesUsingBundled = normalized.startsWith(QStringLiteral(":"));
    m_rulesActivePath = normalized;

    QString nameText = m_modelMeta.modelName.trimmed();
    if (nameText.isEmpty()) nameText = QStringLiteral("모델 팩");
    if (m_rulesUsingBundled) {
        m_rulesActiveSource = QStringLiteral("기본 모델 팩: %1").arg(nameText);
    } else {
        const QFileInfo fi(normalized);
        m_rulesActiveSource = QStringLiteral("외부 모델 팩: %1").arg(fi.fileName());
    }

    emit rulesChanged();
    emit signalDbChanged();
    QString loadModeText;
    if (m_rules.isEmpty() && m_signalMessages.isEmpty()) loadModeText = QStringLiteral("빈 모델 팩");
    else if (m_rules.isEmpty()) loadModeText = QStringLiteral("값 해석 전용");
    else if (m_signalMessages.isEmpty()) loadModeText = QStringLiteral("주기 기준 전용");
    else loadModeText = QStringLiteral("주기+값 해석 활성");
    QStringList metaSummary;
    if (!m_modelMeta.vendor.trimmed().isEmpty()) metaSummary << m_modelMeta.vendor.trimmed();
    if (!m_modelMeta.modelVersion.trimmed().isEmpty()) metaSummary << m_modelMeta.modelVersion.trimmed();
    const QString metaText = metaSummary.isEmpty() ? QString() : QStringLiteral(" · %1").arg(metaSummary.join(QStringLiteral(" / ")));
    setStatus(QStringLiteral("모델 로드: %1%2 · 기준 %3건 / 해석 ID %4건 · %5").arg(nameText, metaText).arg(m_rules.size()).arg(m_signalMessages.size()).arg(loadModeText));
    refreshTimingRows();
    refreshValueRows();
    syncLiveBusHealthAlarms();
    refreshAlarmRows();
    seedBusRoleResolver();
    emit controlStateChanged();
    if (m_replayLoaded) restartGraphOverviewBuild(true);
    requestGraphRefresh(true);
    return pack.hasContent();
}

bool AppController::loadRulesFile(const QString& path) {
    return loadModelFile(path);
}

bool AppController::loadSignalDbFile(const QString& path) {
    return loadModelFile(path);
}

void AppController::ingestFrame(const FrameRecord& fr, const QString& source) {
    QHash<quint32, IdState>& states = stateMapForSource(source);
    IdState& st = states[fr.canId];
    const qint64 nowMs = qint64(fr.tExtUs / 1000ULL);
    if (st.seen && fr.tExtUs >= st.lastBoardSeenUs) {
        st.lastGapMs = double(fr.tExtUs - st.lastBoardSeenUs) / 1000.0;
    }
    st.seen = true;
    st.lastFrame = fr;
    st.lastSource = source;
    st.lastLocalSeenMs = nowMs;
    st.lastBoardSeenUs = fr.tExtUs;
    appendGraphSamples(fr, source);
    const auto ruleIt = m_rules.constFind(fr.canId);
    const RuleSpec* rule = (m_modelEnabled && ruleIt != m_rules.cend()) ? &ruleIt.value() : nullptr;
    st.lastTimingAgeBucket = CanMonitorAnalysis::TimingEvaluator::timingAgeBucket(rule, 0.0);
    updateNextTimingEvalMs(fr.canId, st, nowMs);
    st.timingDerivedDirty = true;
    st.valueDerivedDirty = true;
    if (timingScopeActive()) m_timingRowsDirty = true;
    if (valueScopeActive() || (m_hasSelectedValueId && m_selectedValueCanId == fr.canId)) m_valueRowsDirty = true;
    if (m_hasSelectedValueId && m_selectedValueCanId == fr.canId) m_valueDetailsDirty = true;

    if (source == QStringLiteral("live") && !projectionBackpressureActive()) {
        syncValueAlarmState(fr.canId, st, source, false);
    }
}

AppController::EvalResult AppController::evaluateId(quint32 id, const IdState* state, qint64 nowMs) const {
    CanMonitorAnalysis::TimingInput input;
    input.id = id;
    input.displayName = displayNameForId(id);
    input.source = (state && state->seen) ? state->lastSource : QStringLiteral("-");
    input.modelEnabled = m_modelEnabled;
    input.seen = state && state->seen;
    input.nowMs = nowMs;
    input.lastLocalSeenMs = (state && state->seen) ? state->lastLocalSeenMs : -1;
    input.gapMs = (state && state->seen) ? state->lastGapMs : -1.0;
    const auto ruleIt = m_rules.constFind(id);
    input.rule = (m_modelEnabled && ruleIt != m_rules.cend()) ? &ruleIt.value() : nullptr;
    return CanMonitorAnalysis::TimingEvaluator::evaluate(input);
}


QVector<QVariantMap> AppController::stabilizeRows(const QVector<QVariantMap>& sortedRows, const StableMapListModel& model, bool forceReorder) const {
    if (forceReorder || model.rowCount() == 0) return sortedRows;

    QHash<QString, QVariantMap> byKey;
    byKey.reserve(sortedRows.size());
    for (const auto& row : sortedRows) byKey.insert(row.value(QStringLiteral("key")).toString(), row);

    QVector<QVariantMap> out;
    out.reserve(sortedRows.size());
    for (const QString& key : model.orderedKeys()) {
        auto it = byKey.find(key);
        if (it == byKey.end()) continue;
        out.push_back(it.value());
        byKey.erase(it);
    }
    for (const auto& row : sortedRows) {
        const QString key = row.value(QStringLiteral("key")).toString();
        if (byKey.contains(key)) {
            out.push_back(row);
            byKey.remove(key);
        }
    }
    return out;
}

void AppController::updateTimingHistory(IdState& state, quint32 id, const EvalResult& eval, const QString& source, qint64 nowMs) {
    const bool activeIssue = (eval.severity == QStringLiteral("WARN") || eval.severity == QStringLiteral("ERR"));
    if (!activeIssue) {
        state.timingIssueLatched = false;
        state.lastTimingIssueKey.clear();
        return;
    }

    const QString normalizedReasonKey = normalizeAlarmMessageKey(eval.reason);
    const QString issueKey = eval.alarmKey.isEmpty()
        ? QStringLiteral("%1|%2").arg(eval.severity, normalizedReasonKey)
        : QStringLiteral("%1|%2").arg(eval.severity, eval.alarmKey);
    if (state.timingIssueLatched && state.lastTimingIssueKey == issueKey) return;

    const QString nowText = timeTextForSourceMs(source, nowMs);
    state.timingEvents.prepend(QStringLiteral("[%1] %2 %3 · %4").arg(nowText, idText(id), eval.severity, eval.reason));
    while (state.timingEvents.size() > 24) state.timingEvents.removeLast();
    state.timingIssueLatched = true;
    state.lastTimingIssueKey = issueKey;
    state.timingEventCount += 1;
    if (source == QStringLiteral("replay")) appendReplayIssueMarker(QStringLiteral("timing"), id, eval.severity, eval.reason);
}

void AppController::refreshDerivedModels() {
    refreshTimingRows();
    refreshValueRows();
}

void AppController::refreshTimingRows() {
    if (m_timingViewHeld) return;

    struct TimingRowWrap {
        QVariantMap row;
        int rank = 0;
        quint32 id = 0;
        QString name;
        QString source;
        QString expectedText;
        QString reason;
        double gapMs = -1.0;
        double ageMs = -1.0;
    };

    const qint64 nowMs = analysisNowMsForSource(activeAnalysisSourceKey());
    QVector<CanMonitorAnalysis::AlarmGroup>& alarmGroups = activeAlarmGroups();
    QSet<quint32> ids;
    for (auto it = m_rules.cbegin(); it != m_rules.cend(); ++it) ids.insert(it.key());
    auto& states = activeStateMap();
    for (auto it = states.cbegin(); it != states.cend(); ++it) ids.insert(it.key());

    std::vector<TimingRowWrap> timingWrapped;
    timingWrapped.reserve(size_t(ids.size()));

    for (quint32 id : ids) {
        IdState* state = states.contains(id) ? &states[id] : nullptr;
        const bool canCache = state && state->seen;
        const bool needRebuild = !canCache || state->timingDerivedDirty || state->cachedTimingRow.isEmpty();

        QVariantMap row;
        if (needRebuild) {
            const EvalResult eval = evaluateId(id, state, nowMs);
            if (canCache) {
                state->lastSeverity = eval.severity;
                state->lastReason = eval.reason;
                if (!state->activeTimingAlarmKey.isEmpty()) {
                    resolveAlarmGroup(state->activeTimingAlarmKey, QString(), QStringLiteral("timing"), alarmGroups, m_alarmRowsDirty);
                    state->activeTimingAlarmKey.clear();
                    state->lastTimingAlarmSeenMs = -1;
                }
            }

            row.insert(QStringLiteral("key"), idText(id));
            row.insert(QStringLiteral("idText"), idText(id));
            row.insert(QStringLiteral("name"), eval.name);
            row.insert(QStringLiteral("severity"), eval.severity);
            row.insert(QStringLiteral("severityColor"), severityColor(eval.severity));
            row.insert(QStringLiteral("expectedMsText"), m_rules.contains(id) ? fmtMs(m_rules.value(id).expectedPeriodMs) : QStringLiteral("-"));
            row.insert(QStringLiteral("lastGapMsText"), fmtMs(eval.gapMs));
            row.insert(QStringLiteral("ageMsText"), fmtMs(eval.ageMs));
            row.insert(QStringLiteral("source"), eval.source);
            row.insert(QStringLiteral("reason"), eval.reason);
            row.insert(QStringLiteral("metricText"), eval.deviationPct >= 0.0 ? fmtPct(eval.deviationPct) : QStringLiteral("-"));
            row.insert(QStringLiteral("gaugePct"), eval.deviationPct >= 0.0 ? eval.gaugePct : 0.0);
            row.insert(QStringLiteral("eventCount"), state ? state->timingEventCount : 0);
            row.insert(QStringLiteral("history"), state ? QVariant(state->timingEvents) : QVariant(QStringList{}));
            row.insert(QStringLiteral("sortRank"), eval.severityRank);
            row.insert(QStringLiteral("sortGapMs"), eval.gapMs);
            row.insert(QStringLiteral("sortAgeMs"), eval.ageMs);
            row.insert(QStringLiteral("sortId"), uint(id));

            if (canCache) {
                state->cachedTimingRow = row;
                state->timingDerivedDirty = false;
            }
        } else {
            row = state->cachedTimingRow;
            row.insert(QStringLiteral("eventCount"), state->timingEventCount);
            row.insert(QStringLiteral("history"), QVariant(state->timingEvents));
            state->cachedTimingRow = row;
        }

        const bool timingMatch =
            containsFilterText(row.value(QStringLiteral("idText")).toString(), m_timingFilterId) &&
            containsFilterText(row.value(QStringLiteral("severity")).toString(), m_timingFilterSeverity) &&
            containsFilterText(row.value(QStringLiteral("name")).toString(), m_timingFilterName) &&
            containsFilterText(row.value(QStringLiteral("expectedMsText")).toString(), m_timingFilterExpected) &&
            containsFilterText(row.value(QStringLiteral("lastGapMsText")).toString(), m_timingFilterGap) &&
            containsFilterText(row.value(QStringLiteral("ageMsText")).toString(), m_timingFilterAge) &&
            containsFilterText(row.value(QStringLiteral("source")).toString(), m_timingFilterSource) &&
            containsFilterText(row.value(QStringLiteral("reason")).toString(), m_timingFilterReason);

        if (timingMatch) {
            timingWrapped.push_back({row,
                                     row.value(QStringLiteral("sortRank")).toInt(),
                                     row.value(QStringLiteral("sortId")).toUInt(),
                                     row.value(QStringLiteral("name")).toString(),
                                     row.value(QStringLiteral("source")).toString(),
                                     row.value(QStringLiteral("expectedMsText")).toString(),
                                     row.value(QStringLiteral("reason")).toString(),
                                     row.value(QStringLiteral("sortGapMs")).toDouble(),
                                     row.value(QStringLiteral("sortAgeMs")).toDouble()});
        }
    }

    std::sort(timingWrapped.begin(), timingWrapped.end(), [this](const TimingRowWrap& a, const TimingRowWrap& b) {
        int cmp = 0;
        if (m_timingSortMode == QStringLiteral("severity")) cmp = compareInt64(a.rank, b.rank);
        else if (m_timingSortMode == QStringLiteral("name")) cmp = compareQString(a.name, b.name);
        else if (m_timingSortMode == QStringLiteral("expected")) cmp = compareQString(a.expectedText, b.expectedText);
        else if (m_timingSortMode == QStringLiteral("gap")) cmp = compareOptionalDouble(a.gapMs, b.gapMs);
        else if (m_timingSortMode == QStringLiteral("age")) cmp = compareOptionalDouble(a.ageMs, b.ageMs);
        else if (m_timingSortMode == QStringLiteral("source")) cmp = compareQString(a.source, b.source);
        else if (m_timingSortMode == QStringLiteral("reason")) cmp = compareQString(a.reason, b.reason);
        else cmp = compareUInt32(a.id, b.id);
        if (cmp == 0) cmp = compareUInt32(a.id, b.id);
        return lessFromCompare(cmp, m_timingSortDescending);
    });

    QVector<QVariantMap> newTimingRows;
    newTimingRows.reserve(int(timingWrapped.size()));
    for (const TimingRowWrap& wrap : timingWrapped) newTimingRows.push_back(wrap.row);

    newTimingRows = stabilizeRows(newTimingRows, m_timingModel, m_timingReorderRequested);
    const bool allowStructureSync = timingStructureSyncAllowed() || m_timingModel.rowCount() == 0;
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    m_timingModel.setRowsRelaxed(newTimingRows, allowStructureSync);
    m_lastTimingProjectionWallMs = nowWallMs;
    if (allowStructureSync) m_lastTimingStructureSyncWallMs = nowWallMs;
    m_timingRowsDirty = !allowStructureSync;
    if (allowStructureSync) m_timingReorderRequested = false;
    requestDerivedSummaryRefresh(false);
}

void AppController::refreshValueRows() {
    if (m_valueViewHeld) return;

    struct ValueRowWrap {
        QVariantMap row;
        int rank = 0;
        quint32 id = 0;
        QString name;
        QString source;
        QString raw;
        QString summary;
        double gapMs = -1.0;
        double ageMs = -1.0;
    };
    std::vector<ValueRowWrap> valueWrapped;
    auto& states = activeStateMap();
    valueWrapped.reserve(size_t(states.size()));

    const qint64 nowMs = analysisNowMsForSource(activeAnalysisSourceKey());

    for (auto it = states.begin(); it != states.end(); ++it) {
        if (!it.value().seen) continue;
        const quint32 id = it.key();
        IdState& state = it.value();
        const bool needRebuild = state.valueDerivedDirty || state.cachedValueRow.isEmpty();

        QVariantMap row;
        if (needRebuild) {
            const EvalResult eval = evaluateId(id, &state, nowMs);
            const quint64 currentFingerprint = framePayloadFingerprint(state.lastFrame);
            QVariantMap previewInfo = (state.cachedPreviewFingerprint == currentFingerprint) ? state.cachedPreviewInfo : QVariantMap();
            if (previewInfo.isEmpty()) {
                const auto preview = CanMonitorAnalysis::SignalDecoder::makePreview(id, state.lastFrame, m_signalMessages, m_modelEnabled);
                previewInfo = QVariantMap{{QStringLiteral("plain"), preview.plain}, {QStringLiteral("rich"), preview.rich}};
            }
            QVariantMap alarmInfo = (state.cachedValueAlarmFingerprint == currentFingerprint) ? state.cachedValueAlarmInfo : QVariantMap();
            if (hasAlarmCapableSignals(id) && alarmInfo.isEmpty()) {
                alarmInfo = CanMonitorAnalysis::SignalDecoder::makeValueAlarm(id, state.lastFrame, m_signalMessages, m_modelEnabled).toVariantMap();
            }

            const QString previewText = previewInfo.value(QStringLiteral("plain")).toString();
            const QString previewRich = previewInfo.value(QStringLiteral("rich")).toString();
            const QString valueSeverity = alarmInfo.value(QStringLiteral("severity")).toString();
            const bool valueAlarmActive = alarmInfo.value(QStringLiteral("active")).toBool();
            const QString effectiveSeverity = valueAlarmActive ? valueSeverity : eval.severity;

            row.insert(QStringLiteral("key"), idText(id));
            row.insert(QStringLiteral("idText"), idText(id));
            row.insert(QStringLiteral("name"), eval.name);
            row.insert(QStringLiteral("severity"), effectiveSeverity);
            row.insert(QStringLiteral("severityColor"), severityColor(effectiveSeverity));
            row.insert(QStringLiteral("source"), state.lastSource);
            row.insert(QStringLiteral("bus"), QStringLiteral("BUS %1").arg(state.lastFrame.bus));
            row.insert(QStringLiteral("dataHex"), hexBytes(state.lastFrame.data, state.lastFrame.dlc));
            row.insert(QStringLiteral("gapText"), fmtMs(eval.gapMs));
            row.insert(QStringLiteral("ageText"), fmtMs(eval.ageMs));
            row.insert(QStringLiteral("reason"), valueAlarmActive ? alarmInfo.value(QStringLiteral("message")).toString() : eval.reason);
            row.insert(QStringLiteral("previewText"), previewText);
            row.insert(QStringLiteral("summaryText"), previewText.isEmpty() ? (m_modelEnabled ? QStringLiteral("해석값 없음") : QStringLiteral("주기 관찰 + RAW 표시")) : previewText);
            row.insert(QStringLiteral("summaryRich"), previewRich);
            row.insert(QStringLiteral("valueMetricText"), alarmInfo.value(QStringLiteral("metricText")).toString());
            row.insert(QStringLiteral("valueGaugePct"), alarmInfo.value(QStringLiteral("gaugePct")).toDouble());
            row.insert(QStringLiteral("sortRank"), severityRank(effectiveSeverity));
            row.insert(QStringLiteral("sortGapMs"), eval.gapMs);
            row.insert(QStringLiteral("sortAgeMs"), eval.ageMs);
            row.insert(QStringLiteral("sortId"), uint(id));

            state.cachedPreviewInfo = previewInfo;
            state.cachedPreviewFingerprint = currentFingerprint;
            state.cachedValueAlarmInfo = alarmInfo;
            state.cachedValueAlarmFingerprint = currentFingerprint;
            state.cachedValueRow = row;
            state.valueDerivedDirty = false;
        } else {
            row = state.cachedValueRow;
        }

        const QString effectiveSeverity = row.value(QStringLiteral("severity")).toString();
        const bool valueMatch =
            containsFilterText(row.value(QStringLiteral("idText")).toString(), m_valueFilterId) &&
            containsFilterText(effectiveSeverity, m_valueFilterSeverity) &&
            containsFilterText(row.value(QStringLiteral("name")).toString(), m_valueFilterName) &&
            containsFilterText(row.value(QStringLiteral("source")).toString(), m_valueFilterSource) &&
            containsFilterText(row.value(QStringLiteral("dataHex")).toString(), m_valueFilterRaw) &&
            containsFilterText(row.value(QStringLiteral("gapText")).toString(), m_valueFilterGap) &&
            (containsFilterText(row.value(QStringLiteral("summaryText")).toString(), m_valueFilterReason) ||
             containsFilterText(row.value(QStringLiteral("reason")).toString(), m_valueFilterReason));

        if (valueMatch) {
            valueWrapped.push_back({row,
                                    row.value(QStringLiteral("sortRank")).toInt(),
                                    row.value(QStringLiteral("sortId")).toUInt(),
                                    row.value(QStringLiteral("name")).toString(),
                                    row.value(QStringLiteral("source")).toString(),
                                    row.value(QStringLiteral("dataHex")).toString(),
                                    row.value(QStringLiteral("summaryText")).toString(),
                                    row.value(QStringLiteral("sortGapMs")).toDouble(),
                                    row.value(QStringLiteral("sortAgeMs")).toDouble()});
        }
    }

    std::sort(valueWrapped.begin(), valueWrapped.end(), [this](const ValueRowWrap& a, const ValueRowWrap& b) {
        int cmp = 0;
        if (m_valueSortMode == QStringLiteral("severity")) cmp = compareInt64(a.rank, b.rank);
        else if (m_valueSortMode == QStringLiteral("name")) cmp = compareQString(a.name, b.name);
        else if (m_valueSortMode == QStringLiteral("raw")) cmp = compareQString(a.raw, b.raw);
        else if (m_valueSortMode == QStringLiteral("gap")) cmp = compareOptionalDouble(a.gapMs, b.gapMs);
        else if (m_valueSortMode == QStringLiteral("age")) cmp = compareOptionalDouble(a.ageMs, b.ageMs);
        else if (m_valueSortMode == QStringLiteral("source")) cmp = compareQString(a.source, b.source);
        else if (m_valueSortMode == QStringLiteral("reason")) cmp = compareQString(a.summary, b.summary);
        else cmp = compareUInt32(a.id, b.id);
        if (cmp == 0) cmp = compareUInt32(a.id, b.id);
        return lessFromCompare(cmp, m_valueSortDescending);
    });

    if (!m_hasSelectedValueId || !states.contains(m_selectedValueCanId) || !states[m_selectedValueCanId].seen) {
        if (!valueWrapped.empty()) {
            const quint32 nextId = valueWrapped.front().id;
            if (!m_hasSelectedValueId || m_selectedValueCanId != nextId) {
                m_selectedValueCanId = nextId;
                m_hasSelectedValueId = true;
                syncActiveViewSelection();
                emit selectedValueIdChanged();
                m_lastValueDetailSignature.clear();
                m_lastValueDetailProjectionWallMs = -1;
                invalidateValueDetailSignalCache();
                m_valueDetailsDirty = true;
            }
        } else if (m_hasSelectedValueId) {
            m_hasSelectedValueId = false;
            m_selectedValueCanId = 0;
            syncActiveViewSelection();
            emit selectedValueIdChanged();
            m_lastValueDetailSignature.clear();
            m_lastValueDetailProjectionWallMs = -1;
            invalidateValueDetailSignalCache();
            m_valueDetailsDirty = true;
        }
    }

    QVector<QVariantMap> valueRowsVec;
    valueRowsVec.reserve(int(valueWrapped.size()));
    for (const ValueRowWrap& wrap : valueWrapped) valueRowsVec.push_back(wrap.row);
    valueRowsVec = stabilizeRows(valueRowsVec, m_valueModel, m_valueReorderRequested);
    const bool allowStructureSync = valueStructureSyncAllowed() || m_valueModel.rowCount() == 0;
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    m_valueModel.setRowsRelaxed(valueRowsVec, allowStructureSync);
    m_lastValueProjectionWallMs = nowWallMs;
    if (allowStructureSync) m_lastValueStructureSyncWallMs = nowWallMs;
    m_valueRowsDirty = !allowStructureSync;
    if (allowStructureSync) m_valueReorderRequested = false;
    requestDerivedSummaryRefresh(false);
    maybeRefreshValueDetails(false);
}

void AppController::invalidateValueDetailSignalCache() {
    m_cachedValueDetailCanId = 0;
    m_cachedValueDetailSource.clear();
    m_cachedValueDetailFingerprint = 0;
    m_cachedValueDetailModelEnabled = m_modelEnabled;
    m_cachedValueDetailSignalRows.clear();
}

void AppController::maybeRefreshValueDetails(bool immediate) {
    m_valueDetailsDirty = true;
    if (!m_valuePanelActive || m_valueViewHeld || analysisPaused()) return;
    if (!immediate && !projectionDue(m_lastValueDetailProjectionWallMs, valueDetailProjectionIntervalMs())) return;
    refreshValueDetails();
}

void AppController::refreshValueDetails() {
    if (!m_valuePanelActive) {
        m_valueDetailsDirty = true;
        return;
    }
    QVector<DetailRow> rows;
    QString renderedDetailSignature;

    auto& states = activeStateMap();
    if (m_hasSelectedValueId && states.contains(m_selectedValueCanId) && states[m_selectedValueCanId].seen) {
        const IdState& state = states[m_selectedValueCanId];
        const EvalResult eval = evaluateId(m_selectedValueCanId, &state, analysisNowMsForSource(activeAnalysisSourceKey()));
        const auto ruleIt = m_rules.constFind(m_selectedValueCanId);
        const RuleSpec* rule = (ruleIt != m_rules.cend()) ? &ruleIt.value() : nullptr;
        const quint64 fingerprint = framePayloadFingerprint(state.lastFrame);
        QVariantMap alarmInfo = (state.cachedValueAlarmFingerprint == fingerprint) ? state.cachedValueAlarmInfo : QVariantMap();
        if (hasAlarmCapableSignals(m_selectedValueCanId) && alarmInfo.isEmpty()) {
            alarmInfo = CanMonitorAnalysis::SignalDecoder::makeValueAlarm(m_selectedValueCanId, state.lastFrame, m_signalMessages, m_modelEnabled).toVariantMap();
        }
        renderedDetailSignature = valueDetailSignatureForState(m_selectedValueCanId, state.lastSource, fingerprint, eval, alarmInfo);
        if (m_valueDetailModel.rowCount() > 0 && m_lastValueDetailSignature == renderedDetailSignature) {
            m_valueDetailsDirty = false;
            m_lastValueDetailProjectionWallMs = QDateTime::currentMSecsSinceEpoch();
            return;
        }

        QVariantMap previewInfo = (state.cachedPreviewFingerprint == fingerprint) ? state.cachedPreviewInfo : QVariantMap();
        if (previewInfo.isEmpty()) {
            const auto preview = CanMonitorAnalysis::SignalDecoder::makePreview(m_selectedValueCanId, state.lastFrame, m_signalMessages, m_modelEnabled);
            previewInfo = QVariantMap{{QStringLiteral("plain"), preview.plain}, {QStringLiteral("rich"), preview.rich}};
        }
        const QString shortPreview = previewInfo.value(QStringLiteral("plain")).toString();
        const QString verbosePreview = CanMonitorAnalysis::SignalDecoder::makeVerbosePreview(m_selectedValueCanId, state.lastFrame, m_signalMessages, m_modelEnabled);
        rows.push_back(makeModelDetailRow(QStringLiteral("해석 전체"),
                                          verbosePreview.isEmpty() ? shortPreview : verbosePreview,
                                          m_modelEnabled
                                              ? (shortPreview == verbosePreview || shortPreview.isEmpty()
                                                     ? QStringLiteral("상단 축약 없이 전체 해석을 표시합니다")
                                                     : QStringLiteral("상단 축약본: %1").arg(shortPreview))
                                              : QStringLiteral("모델 해제 상태라 RAW 위주입니다")));
        rows.push_back(makeModelDetailRow(QStringLiteral("현재 판정 / RAW"),
                                          QStringLiteral("%1 · %2 · %3").arg(eval.severity, eval.reason, hexBytes(state.lastFrame.data, state.lastFrame.dlc)),
                                          QStringLiteral("선택 ID %1 · %2 / BUS %3 / DLC %4").arg(idText(m_selectedValueCanId), state.lastSource).arg(state.lastFrame.bus).arg(state.lastFrame.dlc)));

        if (alarmInfo.value(QStringLiteral("active")).toBool()) {
            rows.push_back(makeModelDetailRow(QStringLiteral("값 경보 근거"),
                                              QStringLiteral("%1 · %2").arg(alarmInfo.value(QStringLiteral("severity")).toString(), alarmInfo.value(QStringLiteral("message")).toString()),
                                              QStringLiteral("지표 %1 / 게이지 %2 %")
                                                  .arg(alarmInfo.value(QStringLiteral("metricText")).toString())
                                                  .arg(QString::number(alarmInfo.value(QStringLiteral("gaugePct")).toDouble(), 'f', 1))));
        } else {
            rows.push_back(makeModelDetailRow(QStringLiteral("값 경보 근거"), QStringLiteral("활성 값 경보 없음"), QStringLiteral("주기 경보와 값 경보는 분리 관리")));
        }
        rows.push_back(makeModelDetailRow(QStringLiteral("적용 모델"), m_modelEnabled ? (m_rulesActiveSource.isEmpty() ? QStringLiteral("기본 모델") : m_rulesActiveSource) : QStringLiteral("모델 해제"),
                                          QStringLiteral("%1 · %2")
                                              .arg(rule ? QStringLiteral("주기 기준 %1").arg(rule->name) : QStringLiteral("해당 ID 주기 기준 미정의"),
                                                   modelSourceSummary())));

        QVector<DetailRow> signalRows;
        const bool canReuseSignalRows =
            (m_cachedValueDetailCanId == m_selectedValueCanId) &&
            (m_cachedValueDetailSource == state.lastSource) &&
            (m_cachedValueDetailFingerprint == fingerprint) &&
            (m_cachedValueDetailModelEnabled == m_modelEnabled) &&
            !m_cachedValueDetailSignalRows.isEmpty();
        if (canReuseSignalRows) {
            signalRows = m_cachedValueDetailSignalRows;
        } else {
            signalRows = CanMonitorAnalysis::SignalDecoder::makeDetailRows(m_selectedValueCanId, state.lastFrame, m_signalMessages, m_modelEnabled);
            m_cachedValueDetailCanId = m_selectedValueCanId;
            m_cachedValueDetailSource = state.lastSource;
            m_cachedValueDetailFingerprint = fingerprint;
            m_cachedValueDetailModelEnabled = m_modelEnabled;
            m_cachedValueDetailSignalRows = signalRows;
        }
        if (!signalRows.isEmpty()) {
            signalRows.erase(std::remove_if(signalRows.begin(), signalRows.end(), [](const DetailRow& row) {
                                 return row.key == QStringLiteral("메시지");
                             }),
                             signalRows.end());
            rows += signalRows;
        }
    } else {
        invalidateValueDetailSignalCache();
        rows.push_back(makeModelDetailRow(QStringLiteral("선택 없음"), QStringLiteral("상단 ID 목록에서 항목을 고르면 상세가 표시됩니다."), QStringLiteral("값 탭 상단 목록 사용")));
    }

    m_valueDetailModel.setRows(rows);
    m_valueDetailsDirty = false;
    m_lastValueDetailSignature = renderedDetailSignature;
    m_lastValueDetailProjectionWallMs = QDateTime::currentMSecsSinceEpoch();
}

void AppController::refreshAlarmRows() {
    if (m_alarmViewHeld) return;

    auto& sourceAlarmGroups = activeAlarmGroups();
    const auto keepAlarmGroup = [](const CanMonitorAnalysis::AlarmGroup& group) {
        if (!group.active) return false;
        if (group.category == QStringLiteral("timing")) return false;
        if (group.severity == QStringLiteral("복구") || group.severity == QStringLiteral("해제")) return false;
        return true;
    };
    const qsizetype beforeCount = sourceAlarmGroups.size();
    sourceAlarmGroups.erase(std::remove_if(sourceAlarmGroups.begin(), sourceAlarmGroups.end(),
                                           [&keepAlarmGroup](const CanMonitorAnalysis::AlarmGroup& group) { return !keepAlarmGroup(group); }),
                            sourceAlarmGroups.end());
    if (sourceAlarmGroups.size() != beforeCount) m_alarmRowsDirty = true;

    QVector<CanMonitorAnalysis::AlarmGroup> entries = sourceAlarmGroups;
    std::sort(entries.begin(), entries.end(), [this](const CanMonitorAnalysis::AlarmGroup& a, const CanMonitorAnalysis::AlarmGroup& b) {
        int cmp = 0;
        if (m_alarmSortMode == QStringLiteral("severity")) cmp = compareInt64(a.severityRank, b.severityRank);
        else if (m_alarmSortMode == QStringLiteral("id")) cmp = compareUInt32(a.id, b.id);
        else if (m_alarmSortMode == QStringLiteral("name")) cmp = compareQString(a.name, b.name);
        else if (m_alarmSortMode == QStringLiteral("source")) cmp = compareQString(a.source, b.source);
        else if (m_alarmSortMode == QStringLiteral("message")) cmp = compareQString(a.message, b.message);
        else cmp = compareInt64(a.sequence, b.sequence);
        if (cmp == 0) cmp = compareQString(a.key, b.key);
        if (cmp == 0) cmp = compareInt64(a.sequence, b.sequence);
        return lessFromCompare(cmp, m_alarmSortDescending);
    });

    QVector<QVariantMap> rows;
    rows.reserve(entries.size());
    for (const CanMonitorAnalysis::AlarmGroup& entry : entries) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), entry.key);
        row.insert(QStringLiteral("timeText"), entry.timeText);
        row.insert(QStringLiteral("severity"), entry.severity);
        row.insert(QStringLiteral("severityColor"), entry.severityColor);
        row.insert(QStringLiteral("idText"), entry.id == 0 ? QStringLiteral("BUS") : idText(entry.id));
        row.insert(QStringLiteral("name"), entry.name);
        row.insert(QStringLiteral("source"), entry.source);
        row.insert(QStringLiteral("message"), entry.message);
        row.insert(QStringLiteral("active"), entry.active);
        row.insert(QStringLiteral("count"), entry.updateCount);
        row.insert(QStringLiteral("metricText"), entry.metricText);
        row.insert(QStringLiteral("gaugePct"), entry.gaugePct);
        row.insert(QStringLiteral("category"), entry.category);
        row.insert(QStringLiteral("categoryLabel"), categoryLabel(entry.category));
        row.insert(QStringLiteral("history"), QVariant(entry.history));

        const bool alarmMatch =
            containsFilterText(row.value(QStringLiteral("timeText")).toString(), m_alarmFilterTime) &&
            containsFilterText(row.value(QStringLiteral("idText")).toString(), m_alarmFilterId) &&
            containsFilterText(entry.severity, m_alarmFilterSeverity) &&
            containsFilterText(entry.name, m_alarmFilterName) &&
            containsFilterText(entry.source, m_alarmFilterSource) &&
            containsFilterText(entry.message, m_alarmFilterMessage) &&
            (m_alarmFilterText.isEmpty() ||
             containsFilterText(entry.name, m_alarmFilterText) ||
             containsFilterText(entry.message, m_alarmFilterText) ||
             containsFilterText(entry.source, m_alarmFilterText) ||
             containsFilterText(row.value(QStringLiteral("timeText")).toString(), m_alarmFilterText) ||
             containsFilterText(entry.metricText, m_alarmFilterText) ||
             containsFilterText(categoryLabel(entry.category), m_alarmFilterText));
        if (alarmMatch) rows.push_back(row);
    }

    rows = stabilizeRows(rows, m_alarmModel, m_alarmReorderRequested);
    const bool allowStructureSync = alarmStructureSyncAllowed() || m_alarmModel.rowCount() == 0;
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    m_alarmModel.setRowsRelaxed(rows, allowStructureSync);
    m_lastAlarmProjectionWallMs = nowWallMs;
    if (allowStructureSync) m_lastAlarmStructureSyncWallMs = nowWallMs;
    m_alarmRowsDirty = !allowStructureSync;
    if (allowStructureSync) m_alarmReorderRequested = false;
    requestDerivedSummaryRefresh(false);
}

void AppController::clearDerivedRows() {
    m_timingModel.clear();
    m_valueModel.clear();
    m_alarmModel.clear();
    m_lastTimingProjectionWallMs = -1;
    m_lastValueProjectionWallMs = -1;
    m_lastAlarmProjectionWallMs = -1;
    m_lastTimingStructureSyncWallMs = -1;
    m_lastValueStructureSyncWallMs = -1;
    m_lastAlarmStructureSyncWallMs = -1;
    m_valueDetailModel.clear();
    requestDerivedSummaryRefresh(true);
}
