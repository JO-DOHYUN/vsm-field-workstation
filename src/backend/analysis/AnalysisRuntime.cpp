#include "analysis/AnalysisRuntime.h"

#include "../SignalDecoder.h"
#include "../TimingEvaluator.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr quint64 kRuleOnlyBit = quint64(1) << 53;
constexpr int kTimingHistoryLimit = 6;

bool lessBySeverityThenId(const QVariantMap& a, const QVariantMap& b) {
    const int rankA = a.value(QStringLiteral("sortRank")).toInt();
    const int rankB = b.value(QStringLiteral("sortRank")).toInt();
    if (rankA != rankB) return rankA > rankB;
    const quint64 idA = a.value(QStringLiteral("sortId")).toULongLong();
    const quint64 idB = b.value(QStringLiteral("sortId")).toULongLong();
    return idA < idB;
}

QString normalizedDlcText(const int histogram[16]) {
    QStringList parts;
    for (int i = 0; i < 16; ++i) {
        if (histogram[i] > 0) parts << QStringLiteral("%1:%2").arg(i).arg(histogram[i]);
    }
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" "));
}

} // namespace

namespace CanMonitorAnalysis {

void AnalysisRuntime::reset() {
    m_states.clear();
    m_status = {};
    m_status.maxStateKeys = m_config.maxStateKeys;
    m_status.maxRowsPerSnapshot = m_config.maxRowsPerSnapshot;
    m_nextSnapshotSeq = 1;
    m_hasMaxCaptureSeq = false;
    m_maxCaptureSeq = 0;
    m_transportGapEpoch = 0;
}

void AnalysisRuntime::setConfig(const Config& config) {
    m_config = config;
    if (m_config.maxStateKeys <= 0) m_config.maxStateKeys = 4096;
    if (m_config.maxRowsPerSnapshot <= 0) m_config.maxRowsPerSnapshot = 1200;

    m_alarmCapableIds.clear();
    for (auto it = m_config.signalMessages.cbegin(); it != m_config.signalMessages.cend(); ++it) {
        bool capable = false;
        for (const CanModel::SignalSpec& sig : it.value().signalSpecs) {
            if (sig.hasWarnMin || sig.hasWarnMax || sig.hasErrMin || sig.hasErrMax ||
                !sig.alarmMode.trimmed().isEmpty() || !sig.alarmSeverity.trimmed().isEmpty() ||
                !sig.alarmMessage.trimmed().isEmpty()) {
                capable = true;
                break;
            }
        }
        if (capable) m_alarmCapableIds.insert(it.key(), true);
    }

    m_status.maxStateKeys = m_config.maxStateKeys;
    m_status.maxRowsPerSnapshot = m_config.maxRowsPerSnapshot;
}

void AnalysisRuntime::noteTruthLoss(quint64 frames) {
    if (frames == 0) return;
    m_status.truthLoss += frames;
    m_status.analysisOverrun += frames;
}

void AnalysisRuntime::ingestFrame(const FrameRecord& frame, const QString& source) {
    ++m_status.acceptedCanRxFrames;
    ++m_status.decodedCanRxFrames;

    bool transportGapBeforeFrame = false;
    if (frame.hasCaptureSeq) {
        if (!m_hasMaxCaptureSeq) {
            m_hasMaxCaptureSeq = true;
            m_maxCaptureSeq = frame.captureSeq;
        } else if (frame.captureSeq > m_maxCaptureSeq) {
            if (frame.captureSeq != m_maxCaptureSeq + 1) {
                transportGapBeforeFrame = true;
                ++m_status.captureSeqGapEvents;
                ++m_transportGapEpoch;
            }
            m_maxCaptureSeq = frame.captureSeq;
        } else {
            ++m_status.captureSeqReorderEvents;
        }
    }

    const Key key = keyForFrame(frame);
    auto it = m_states.find(key);
    if (it == m_states.end()) {
        if (m_states.size() >= m_config.maxStateKeys) {
            ++m_status.truthLoss;
            ++m_status.analysisOverrun;
            return;
        }
        it = m_states.insert(key, State{});
    }

    State& state = it.value();
    const qint64 nowMs = qint64(frame.tExtUs / 1000ULL);
    double gapMs = -1.0;
    if (frame.hasObservedGap) {
        gapMs = double(frame.observedGapUs) / 1000.0;
    } else if (state.seen && frame.tExtUs >= state.lastBoardSeenUs) {
        gapMs = double(frame.tExtUs - state.lastBoardSeenUs) / 1000.0;
    }
    const bool intervalCrossedTransportGap = state.seen
        && gapMs >= 0.0
        && (transportGapBeforeFrame || m_transportGapEpoch != state.lastTransportGapEpoch);
    if (gapMs >= 0.0) {
        if (intervalCrossedTransportGap) {
            state.lastTransportGapMs = gapMs;
            state.maxTransportGapMs = state.maxTransportGapMs < 0.0 ? gapMs : std::max(state.maxTransportGapMs, gapMs);
            ++state.transportContaminatedGapCount;
            ++m_status.transportContaminatedIntervals;
        } else {
            state.lastGapMs = gapMs;
            state.minGapMs = state.minGapMs < 0.0 ? gapMs : std::min(state.minGapMs, gapMs);
            state.maxGapMs = state.maxGapMs < 0.0 ? gapMs : std::max(state.maxGapMs, gapMs);
        }
    }

    state.seen = true;
    state.lastFrame = frame;
    state.lastSource = source;
    state.lastLocalSeenMs = nowMs;
    state.lastBoardSeenUs = frame.tExtUs;
    state.lastTransportGapEpoch = m_transportGapEpoch;
    ++state.frameCount;
    if (frame.dlc < 16) ++state.dlcHistogram[frame.dlc];
    state.payloadFingerprint = framePayloadFingerprint(frame);

    const auto ruleIt = m_config.rules.constFind(frame.canId);
    const CanModel::RuleSpec* rule = (m_config.modelEnabled && ruleIt != m_config.rules.cend()) ? &ruleIt.value() : nullptr;
    TimingInput input;
    input.id = frame.canId;
    input.displayName = displayNameForId(frame.canId);
    input.source = source;
    input.modelEnabled = m_config.modelEnabled;
    input.seen = true;
    input.nowMs = nowMs;
    input.lastLocalSeenMs = state.lastLocalSeenMs;
    input.gapMs = state.lastGapMs;
    input.rule = rule;
    const TimingEvalResult timing = TimingEvaluator::evaluate(input);

    if (timing.activeAlarm && timing.alarmKey != state.activeTimingAlarmKey) {
        state.activeTimingAlarmKey = timing.alarmKey;
        ++state.timingEventCount;
        state.timingEvents.push_front(timing.reason);
        while (state.timingEvents.size() > kTimingHistoryLimit) state.timingEvents.pop_back();
    } else if (!timing.activeAlarm && !state.activeTimingAlarmKey.isEmpty()) {
        state.activeTimingAlarmKey.clear();
    }

    if (hasAlarmCapableSignals(frame.canId)) {
        const ValueAlarmResult alarm = SignalDecoder::makeValueAlarm(frame.canId, frame, m_config.signalMessages, m_config.modelEnabled);
        if (alarm.active && alarm.alarmKey != state.activeValueAlarmKey) {
            state.activeValueAlarmKey = alarm.alarmKey;
            ++state.valueAlarmEventCount;
        } else if (!alarm.active && !state.activeValueAlarmKey.isEmpty()) {
            state.activeValueAlarmKey.clear();
        }
    }

    state.lastSeverity = timing.severity;
    state.lastReason = timing.reason;
    state.renderedFingerprint = 0;
    m_status.stateKeyCount = m_states.size();
    m_status.maxStateKeyCount = std::max(m_status.maxStateKeyCount, m_status.stateKeyCount);
}

void AnalysisRuntime::ingestFrames(const FrameRecordList& frames, const QString& source) {
    for (const FrameRecord& frame : frames) ingestFrame(frame, source);
}

AnalysisRuntime::Snapshot AnalysisRuntime::makeSnapshot(qint64 nowMs, const QString& source) {
    Snapshot snapshot;
    snapshot.seq = m_nextSnapshotSeq++;
    snapshot.nowMs = nowMs;
    snapshot.source = source;

    QSet<quint32> observedIds;
    QVector<Key> keys;
    keys.reserve(m_states.size() + m_config.rules.size());
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        keys.push_back(it.key());
        if (it.value().seen) observedIds.insert(it.value().lastFrame.canId);
    }
    for (auto it = m_config.rules.cbegin(); it != m_config.rules.cend(); ++it) {
        if (!observedIds.contains(it.key())) keys.push_back(ruleOnlyKey(it.key()));
    }

    snapshot.timingRows.reserve(std::min<int>(int(keys.size()), m_config.maxRowsPerSnapshot));
    for (Key key : keys) {
        const State* state = m_states.contains(key) ? &m_states[key] : nullptr;
        snapshot.timingRows.push_back(makeTimingRow(key, state, nowMs));
    }

    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (!it.value().seen) continue;
        snapshot.valueRows.push_back(makeValueRow(it.key(), it.value(), nowMs));
    }

    std::sort(snapshot.timingRows.begin(), snapshot.timingRows.end(), lessBySeverityThenId);
    std::sort(snapshot.valueRows.begin(), snapshot.valueRows.end(), lessBySeverityThenId);
    if (snapshot.timingRows.size() > m_config.maxRowsPerSnapshot) {
        snapshot.timingRows.resize(m_config.maxRowsPerSnapshot);
        ++m_status.analysisOverrun;
    }
    if (snapshot.valueRows.size() > m_config.maxRowsPerSnapshot) {
        snapshot.valueRows.resize(m_config.maxRowsPerSnapshot);
        ++m_status.analysisOverrun;
    }

    for (const QVariantMap& row : snapshot.timingRows) {
        const QString severity = row.value(QStringLiteral("severity")).toString();
        if (severity != QStringLiteral("WARN") && severity != QStringLiteral("ERR")) continue;
        snapshot.alarmRows.push_back(makeAlarmRow(QStringLiteral("timing|%1").arg(row.value(QStringLiteral("key")).toString()),
                                                  row.value(QStringLiteral("canId")).toUInt(),
                                                  row.value(QStringLiteral("source")).toString(),
                                                  severity,
                                                  row.value(QStringLiteral("name")).toString(),
                                                  row.value(QStringLiteral("reason")).toString(),
                                                  QStringLiteral("timing"),
                                                  row.value(QStringLiteral("eventCount")).toULongLong(),
                                                  row.value(QStringLiteral("metricText")).toString(),
                                                  row.value(QStringLiteral("gaugePct")).toDouble()));
    }
    for (const QVariantMap& row : snapshot.valueRows) {
        const QString category = row.value(QStringLiteral("alarmCategory")).toString();
        if (category != QStringLiteral("value")) continue;
        snapshot.alarmRows.push_back(makeAlarmRow(QStringLiteral("value|%1").arg(row.value(QStringLiteral("key")).toString()),
                                                  row.value(QStringLiteral("canId")).toUInt(),
                                                  row.value(QStringLiteral("source")).toString(),
                                                  row.value(QStringLiteral("severity")).toString(),
                                                  row.value(QStringLiteral("name")).toString(),
                                                  row.value(QStringLiteral("reason")).toString(),
                                                  QStringLiteral("value"),
                                                  row.value(QStringLiteral("valueAlarmEventCount")).toULongLong(),
                                                  row.value(QStringLiteral("valueMetricText")).toString(),
                                                  row.value(QStringLiteral("valueGaugePct")).toDouble()));
    }
    std::sort(snapshot.alarmRows.begin(), snapshot.alarmRows.end(), lessBySeverityThenId);
    if (snapshot.alarmRows.size() > m_config.maxRowsPerSnapshot) {
        snapshot.alarmRows.resize(m_config.maxRowsPerSnapshot);
        ++m_status.analysisOverrun;
    }

    m_status.stateKeyCount = m_states.size();
    m_status.maxStateKeyCount = std::max(m_status.maxStateKeyCount, m_status.stateKeyCount);
    m_status.snapshotCount = snapshot.seq;
    m_status.timingRows = snapshot.timingRows.size();
    m_status.valueRows = snapshot.valueRows.size();
    m_status.alarmRows = snapshot.alarmRows.size();
    m_status.maxStateKeys = m_config.maxStateKeys;
    m_status.maxRowsPerSnapshot = m_config.maxRowsPerSnapshot;
    snapshot.status = m_status;
    snapshot.summary = makeSummary(snapshot);
    snapshot.diagnostics = makeDiagnostics(snapshot);
    return snapshot;
}

AnalysisRuntime::Status AnalysisRuntime::status() const {
    Status out = m_status;
    out.stateKeyCount = m_states.size();
    out.maxStateKeys = m_config.maxStateKeys;
    out.maxRowsPerSnapshot = m_config.maxRowsPerSnapshot;
    return out;
}

AnalysisRuntime::Diff AnalysisRuntime::diff(const Snapshot& before, const Snapshot& after) {
    Diff out;
    out.fromSeq = before.seq;
    out.toSeq = after.seq;
    out.fullRefresh = before.seq == 0 || after.seq == 0;

    QHash<QString, quint64> beforeRows;
    QHash<QString, quint64> afterRows;
    const auto collect = [](const QVector<QVariantMap>& rows, QHash<QString, quint64>& outRows) {
        for (const QVariantMap& row : rows) {
            const QString key = row.value(QStringLiteral("key")).toString();
            if (!key.isEmpty()) outRows.insert(key, rowFingerprint(row));
        }
    };
    collect(before.timingRows, beforeRows);
    collect(after.timingRows, afterRows);

    for (auto it = afterRows.cbegin(); it != afterRows.cend(); ++it) {
        const auto beforeIt = beforeRows.constFind(it.key());
        if (beforeIt == beforeRows.cend()) out.insertedKeys << it.key();
        else if (beforeIt.value() != it.value()) out.changedKeys << it.key();
    }
    for (auto it = beforeRows.cbegin(); it != beforeRows.cend(); ++it) {
        if (!afterRows.contains(it.key())) out.removedKeys << it.key();
    }
    out.summaryChanged = rowFingerprint(before.summary) != rowFingerprint(after.summary);
    return out;
}

AnalysisRuntime::Key AnalysisRuntime::keyForFrame(const FrameRecord& frame) {
    Key key = (Key(frame.bus) << 56);
    if (frame.ext) key |= (Key(1) << 55);
    if (frame.rtr) key |= (Key(1) << 54);
    key |= Key(frame.canId & 0x1FFFFFFFU);
    return key;
}

AnalysisRuntime::Key AnalysisRuntime::ruleOnlyKey(quint32 canId) {
    return kRuleOnlyBit | Key(canId & 0x1FFFFFFFU);
}

QString AnalysisRuntime::keyText(Key key) {
    if (key & kRuleOnlyBit) return QStringLiteral("rule|0x%1").arg(canIdFromKey(key), 3, 16, QLatin1Char('0')).toUpper();
    return QStringLiteral("bus%1|%2|%3|0x%4")
        .arg(busFromKey(key))
        .arg(extFromKey(key) ? QStringLiteral("ext") : QStringLiteral("std"))
        .arg(rtrFromKey(key) ? QStringLiteral("rtr") : QStringLiteral("data"))
        .arg(canIdFromKey(key), extFromKey(key) ? 8 : 3, 16, QLatin1Char('0')).toUpper();
}

quint8 AnalysisRuntime::busFromKey(Key key) {
    return quint8((key >> 56) & 0xFFU);
}

quint32 AnalysisRuntime::canIdFromKey(Key key) {
    return quint32(key & 0x1FFFFFFFU);
}

bool AnalysisRuntime::extFromKey(Key key) {
    return (key & (Key(1) << 55)) != 0;
}

bool AnalysisRuntime::rtrFromKey(Key key) {
    return (key & (Key(1) << 54)) != 0;
}

QVariantMap AnalysisRuntime::makeTimingRow(Key key, const State* state, qint64 nowMs) const {
    const quint32 id = state && state->seen ? state->lastFrame.canId : canIdFromKey(key);
    const auto ruleIt = m_config.rules.constFind(id);
    const CanModel::RuleSpec* rule = (m_config.modelEnabled && ruleIt != m_config.rules.cend()) ? &ruleIt.value() : nullptr;

    TimingInput input;
    input.id = id;
    input.displayName = displayNameForId(id);
    input.source = state && state->seen ? state->lastSource : QStringLiteral("-");
    input.modelEnabled = m_config.modelEnabled;
    input.seen = state && state->seen;
    input.nowMs = nowMs;
    input.lastLocalSeenMs = state && state->seen ? state->lastLocalSeenMs : -1;
    input.gapMs = state && state->seen ? state->lastGapMs : -1.0;
    input.rule = rule;
    const TimingEvalResult eval = TimingEvaluator::evaluate(input);

    QVariantMap row;
    row.insert(QStringLiteral("key"), keyText(key));
    row.insert(QStringLiteral("canId"), id);
    row.insert(QStringLiteral("idText"), idTextForKey(key, state && state->seen ? &state->lastFrame : nullptr));
    row.insert(QStringLiteral("name"), eval.name);
    row.insert(QStringLiteral("severity"), eval.severity);
    row.insert(QStringLiteral("severityColor"), severityColor(eval.severity));
    row.insert(QStringLiteral("expectedMsText"), rule ? fmtMs(rule->expectedPeriodMs) : QStringLiteral("-"));
    row.insert(QStringLiteral("lastGapMsText"), fmtMs(eval.gapMs));
    row.insert(QStringLiteral("minGapMsText"), state ? fmtMs(state->minGapMs) : QStringLiteral("-"));
    row.insert(QStringLiteral("maxGapMsText"), state ? fmtMs(state->maxGapMs) : QStringLiteral("-"));
    row.insert(QStringLiteral("ageMsText"), fmtMs(eval.ageMs));
    row.insert(QStringLiteral("source"), eval.source);
    QString reason = eval.reason;
    if (state && state->transportContaminatedGapCount > 0) {
        const QString transportNote = QStringLiteral("전송/capture_seq gap %1회로 일부 수신 간격은 실제 CAN 주기 판정에서 제외됨 (최대 오염 gap %2)")
            .arg(state->transportContaminatedGapCount)
            .arg(fmtMs(state->maxTransportGapMs));
        reason = reason.isEmpty() ? transportNote : reason + QStringLiteral(" · ") + transportNote;
    }
    row.insert(QStringLiteral("reason"), reason);
    row.insert(QStringLiteral("metricText"), eval.deviationPct >= 0.0 ? fmtPct(eval.deviationPct) : QStringLiteral("-"));
    row.insert(QStringLiteral("gaugePct"), eval.deviationPct >= 0.0 ? eval.gaugePct : 0.0);
    row.insert(QStringLiteral("eventCount"), state ? qulonglong(state->timingEventCount) : qulonglong(0));
    row.insert(QStringLiteral("frameCount"), state ? qulonglong(state->frameCount) : qulonglong(0));
    row.insert(QStringLiteral("transportContaminated"), state ? state->transportContaminatedGapCount > 0 : false);
    row.insert(QStringLiteral("transportGapCount"), state ? qulonglong(state->transportContaminatedGapCount) : qulonglong(0));
    row.insert(QStringLiteral("transportGapText"), state ? fmtMs(state->maxTransportGapMs) : QStringLiteral("-"));
    row.insert(QStringLiteral("history"), state ? QVariant(state->timingEvents) : QVariant(QStringList{}));
    row.insert(QStringLiteral("sortRank"), eval.severityRank);
    row.insert(QStringLiteral("sortGapMs"), eval.gapMs);
    row.insert(QStringLiteral("sortAgeMs"), eval.ageMs);
    row.insert(QStringLiteral("sortId"), qulonglong(key));
    if (state) appendDlcHistogram(row, *state);
    return row;
}

QVariantMap AnalysisRuntime::makeValueRow(Key key, const State& state, qint64 nowMs) const {
    const quint32 id = state.lastFrame.canId;
    const auto ruleIt = m_config.rules.constFind(id);
    const CanModel::RuleSpec* rule = (m_config.modelEnabled && ruleIt != m_config.rules.cend()) ? &ruleIt.value() : nullptr;

    TimingInput input;
    input.id = id;
    input.displayName = displayNameForId(id);
    input.source = state.lastSource;
    input.modelEnabled = m_config.modelEnabled;
    input.seen = true;
    input.nowMs = nowMs;
    input.lastLocalSeenMs = state.lastLocalSeenMs;
    input.gapMs = state.lastGapMs;
    input.rule = rule;
    const TimingEvalResult timing = TimingEvaluator::evaluate(input);

    const SignalPreviewResult preview = SignalDecoder::makePreview(id, state.lastFrame, m_config.signalMessages, m_config.modelEnabled);
    ValueAlarmResult alarm;
    if (hasAlarmCapableSignals(id)) {
        alarm = SignalDecoder::makeValueAlarm(id, state.lastFrame, m_config.signalMessages, m_config.modelEnabled);
    }
    const QString effectiveSeverity = alarm.active ? alarm.severity : timing.severity;

    QVariantMap row;
    row.insert(QStringLiteral("key"), keyText(key));
    row.insert(QStringLiteral("canId"), id);
    row.insert(QStringLiteral("idText"), idTextForKey(key, &state.lastFrame));
    row.insert(QStringLiteral("name"), timing.name);
    row.insert(QStringLiteral("severity"), effectiveSeverity);
    row.insert(QStringLiteral("severityColor"), severityColor(effectiveSeverity));
    row.insert(QStringLiteral("source"), state.lastSource);
    row.insert(QStringLiteral("bus"), busTextForFrame(state.lastFrame));
    row.insert(QStringLiteral("dataHex"), hexBytes(state.lastFrame.data, state.lastFrame.dlc));
    row.insert(QStringLiteral("dlc"), int(state.lastFrame.dlc));
    row.insert(QStringLiteral("gapText"), fmtMs(timing.gapMs));
    row.insert(QStringLiteral("ageText"), fmtMs(timing.ageMs));
    QString reason = alarm.active ? alarm.message : timing.reason;
    if (state.transportContaminatedGapCount > 0) {
        const QString transportNote = QStringLiteral("전송/capture_seq gap %1회로 일부 주기 gap은 실제 CAN 주기 판정에서 제외됨")
            .arg(state.transportContaminatedGapCount);
        reason = reason.isEmpty() ? transportNote : reason + QStringLiteral(" · ") + transportNote;
    }
    row.insert(QStringLiteral("reason"), reason);
    row.insert(QStringLiteral("previewText"), preview.plain);
    row.insert(QStringLiteral("summaryText"), preview.plain.isEmpty() ? QStringLiteral("RAW") : preview.plain);
    row.insert(QStringLiteral("summaryRich"), preview.rich);
    row.insert(QStringLiteral("valueMetricText"), alarm.metricText);
    row.insert(QStringLiteral("valueGaugePct"), alarm.gaugePct);
    row.insert(QStringLiteral("alarmCategory"), alarm.active ? QStringLiteral("value") : QString());
    row.insert(QStringLiteral("valueAlarmEventCount"), qulonglong(state.valueAlarmEventCount));
    row.insert(QStringLiteral("frameCount"), qulonglong(state.frameCount));
    row.insert(QStringLiteral("transportContaminated"), state.transportContaminatedGapCount > 0);
    row.insert(QStringLiteral("transportGapCount"), qulonglong(state.transportContaminatedGapCount));
    row.insert(QStringLiteral("transportGapText"), fmtMs(state.maxTransportGapMs));
    row.insert(QStringLiteral("sortRank"), severityRank(effectiveSeverity));
    row.insert(QStringLiteral("sortGapMs"), timing.gapMs);
    row.insert(QStringLiteral("sortAgeMs"), timing.ageMs);
    row.insert(QStringLiteral("sortId"), qulonglong(key));
    appendDlcHistogram(row, state);
    return row;
}

QVariantMap AnalysisRuntime::makeAlarmRow(const QString& key,
                                          quint32 canId,
                                          const QString& source,
                                          const QString& severity,
                                          const QString& name,
                                          const QString& message,
                                          const QString& category,
                                          quint64 eventCount,
                                          const QString& metricText,
                                          double gaugePct) const {
    QVariantMap row;
    row.insert(QStringLiteral("key"), key);
    row.insert(QStringLiteral("timeText"), QStringLiteral("-"));
    row.insert(QStringLiteral("severity"), severity);
    row.insert(QStringLiteral("severityColor"), severityColor(severity));
    row.insert(QStringLiteral("idText"), idText(canId));
    row.insert(QStringLiteral("canId"), canId);
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("source"), source);
    row.insert(QStringLiteral("message"), message);
    row.insert(QStringLiteral("active"), true);
    row.insert(QStringLiteral("count"), qulonglong(eventCount));
    row.insert(QStringLiteral("metricText"), metricText);
    row.insert(QStringLiteral("gaugePct"), gaugePct);
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("categoryLabel"), category);
    row.insert(QStringLiteral("sortRank"), severityRank(severity));
    row.insert(QStringLiteral("sortId"), qulonglong(canId));
    return row;
}

QVariantMap AnalysisRuntime::makeSummary(const Snapshot& snapshot) const {
    int warn = 0;
    int err = 0;
    for (const QVariantMap& row : snapshot.timingRows) {
        const QString severity = row.value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("WARN")) ++warn;
        else if (severity == QStringLiteral("ERR")) ++err;
    }
    for (const QVariantMap& row : snapshot.valueRows) {
        const QString severity = row.value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("WARN")) ++warn;
        else if (severity == QStringLiteral("ERR")) ++err;
    }

    const bool transportContaminated = snapshot.status.captureSeqGapEvents > 0
        || snapshot.status.transportContaminatedIntervals > 0;
    QVariantMap summary;
    summary.insert(QStringLiteral("source"), snapshot.source);
    summary.insert(QStringLiteral("level"), snapshot.status.truthLoss > 0 ? QStringLiteral("ERR") : (err > 0 ? QStringLiteral("ERR") : ((warn > 0 || transportContaminated) ? QStringLiteral("WARN") : QStringLiteral("OK"))));
    summary.insert(QStringLiteral("truthFrames"), qulonglong(snapshot.status.acceptedCanRxFrames));
    summary.insert(QStringLiteral("stateKeys"), snapshot.status.stateKeyCount);
    summary.insert(QStringLiteral("timingRows"), snapshot.timingRows.size());
    summary.insert(QStringLiteral("valueRows"), snapshot.valueRows.size());
    summary.insert(QStringLiteral("alarmRows"), snapshot.alarmRows.size());
    summary.insert(QStringLiteral("warningRows"), warn);
    summary.insert(QStringLiteral("errorRows"), err);
    summary.insert(QStringLiteral("truthLoss"), qulonglong(snapshot.status.truthLoss));
    summary.insert(QStringLiteral("analysisOverrun"), qulonglong(snapshot.status.analysisOverrun));
    summary.insert(QStringLiteral("captureSeqGapEvents"), qulonglong(snapshot.status.captureSeqGapEvents));
    summary.insert(QStringLiteral("transportContaminatedIntervals"), qulonglong(snapshot.status.transportContaminatedIntervals));
    summary.insert(QStringLiteral("text"),
                   QStringLiteral("%1 truth frames · %2 state keys · T/V/A %3/%4/%5 · loss %6 · overrun %7")
                       .arg(snapshot.status.acceptedCanRxFrames)
                       .arg(snapshot.status.stateKeyCount)
                       .arg(snapshot.timingRows.size())
                       .arg(snapshot.valueRows.size())
                       .arg(snapshot.alarmRows.size())
                       .arg(snapshot.status.truthLoss)
                       .arg(snapshot.status.analysisOverrun));
    if (transportContaminated) {
        summary.insert(QStringLiteral("text"),
                       summary.value(QStringLiteral("text")).toString()
                           + QStringLiteral(" · transport-gap %1/%2")
                                 .arg(snapshot.status.captureSeqGapEvents)
                                 .arg(snapshot.status.transportContaminatedIntervals));
    }
    return summary;
}

QVariantList AnalysisRuntime::makeDiagnostics(const Snapshot& snapshot) const {
    QVariantList rows;
    auto add = [&rows](const QString& key, const QString& level, const QString& title, const QString& value, const QString& detail) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("level"), level);
        row.insert(QStringLiteral("title"), title);
        row.insert(QStringLiteral("value"), value);
        row.insert(QStringLiteral("detail"), detail);
        rows.push_back(row);
    };
    add(QStringLiteral("truth_frames"), QStringLiteral("OK"), QStringLiteral("truth frames"),
        QString::number(snapshot.status.acceptedCanRxFrames), QStringLiteral("accepted CAN_RX_RAW frames consumed by analysis runtime"));
    add(QStringLiteral("state_keys"),
        snapshot.status.stateKeyCount >= snapshot.status.maxStateKeys ? QStringLiteral("WARN") : QStringLiteral("OK"),
        QStringLiteral("state keys"),
        QStringLiteral("%1 / %2").arg(snapshot.status.stateKeyCount).arg(snapshot.status.maxStateKeys),
        QStringLiteral("bounded bus+id+ext+rtr state cache; no raw frame list is retained"));
    add(QStringLiteral("truth_loss"),
        snapshot.status.truthLoss > 0 ? QStringLiteral("ERR") : QStringLiteral("OK"),
        QStringLiteral("truth loss"),
        QString::number(snapshot.status.truthLoss),
        QStringLiteral("non-zero means analysis could not accept a factual input"));
    add(QStringLiteral("analysis_overrun"),
        snapshot.status.analysisOverrun > 0 ? QStringLiteral("WARN") : QStringLiteral("OK"),
        QStringLiteral("analysis overrun"),
        QString::number(snapshot.status.analysisOverrun),
        QStringLiteral("row/state limits hit; display may be bounded but truth is never silently sampled"));
    add(QStringLiteral("capture_seq_gap"),
        snapshot.status.captureSeqGapEvents > 0 ? QStringLiteral("WARN") : QStringLiteral("OK"),
        QStringLiteral("capture_seq gap"),
        QString::number(snapshot.status.captureSeqGapEvents),
        QStringLiteral("transport/capture continuity gaps; affected timing intervals are marked contaminated instead of real CAN period errors"));
    add(QStringLiteral("capture_seq_reorder"),
        snapshot.status.captureSeqReorderEvents > 0 ? QStringLiteral("WARN") : QStringLiteral("OK"),
        QStringLiteral("capture_seq reorder"),
        QString::number(snapshot.status.captureSeqReorderEvents),
        QStringLiteral("global ordering diagnostic only; gap/duplicate counters remain the truth-loss continuity signals"));
    add(QStringLiteral("transport_contaminated_intervals"),
        snapshot.status.transportContaminatedIntervals > 0 ? QStringLiteral("WARN") : QStringLiteral("OK"),
        QStringLiteral("transport-contaminated timing"),
        QString::number(snapshot.status.transportContaminatedIntervals),
        QStringLiteral("per-ID gaps crossing capture_seq discontinuity; preserved as evidence but excluded from model period severity"));
    return rows;
}

QString AnalysisRuntime::displayNameForId(quint32 id) const {
    return SignalDecoder::displayNameForId(id, m_config.rules, m_config.signalMessages);
}

QString AnalysisRuntime::idTextForKey(Key key, const FrameRecord* frame) const {
    const quint32 id = frame ? frame->canId : canIdFromKey(key);
    if (!frame && (key & kRuleOnlyBit)) return idText(id);
    const bool ext = frame ? frame->ext : extFromKey(key);
    const bool rtr = frame ? frame->rtr : rtrFromKey(key);
    return QStringLiteral("BUS %1 %2%3 %4")
        .arg(frame ? frame->bus : busFromKey(key))
        .arg(ext ? QStringLiteral("EXT ") : QString())
        .arg(rtr ? QStringLiteral("RTR ") : QString())
        .arg(idText(id));
}

bool AnalysisRuntime::hasAlarmCapableSignals(quint32 id) const {
    return m_alarmCapableIds.contains(id);
}

QString AnalysisRuntime::severityColor(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return QStringLiteral("#c0392b");
    if (severity == QStringLiteral("WARN")) return QStringLiteral("#d97706");
    if (severity == QStringLiteral("OK")) return QStringLiteral("#118a42");
    if (severity == QStringLiteral("관찰")) return QStringLiteral("#0f4c81");
    if (severity == QStringLiteral("미수신")) return QStringLiteral("#8a6d3b");
    return QStringLiteral("#607080");
}

int AnalysisRuntime::severityRank(const QString& severity) {
    if (severity == QStringLiteral("ERR")) return 4;
    if (severity == QStringLiteral("WARN")) return 3;
    if (severity == QStringLiteral("OK")) return 2;
    if (severity == QStringLiteral("관찰") || severity == QStringLiteral("미수신")) return 1;
    return 0;
}

QString AnalysisRuntime::fmtMs(double ms) {
    if (ms < 0.0) return QStringLiteral("-");
    return QString::number(ms, 'f', 1) + QStringLiteral(" ms");
}

QString AnalysisRuntime::fmtPct(double pct) {
    if (pct < 0.0) return QStringLiteral("-");
    return QString::number(pct, 'f', 1) + QStringLiteral(" %");
}

quint64 AnalysisRuntime::framePayloadFingerprint(const FrameRecord& frame) {
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(frame.tExtUs);
    mix(frame.canId);
    mix(frame.bus);
    mix(frame.ext ? 1 : 0);
    mix(frame.rtr ? 1 : 0);
    mix(frame.dlc);
    for (int i = 0; i < std::min<int>(frame.dlc, 8); ++i) mix(frame.data[i]);
    return hash;
}

quint64 AnalysisRuntime::rowFingerprint(const QVariantMap& row) {
    quint64 hash = 1469598103934665603ULL;
    const QStringList keys = row.keys();
    for (const QString& key : keys) {
        const QString value = row.value(key).toString();
        for (const QChar ch : key + QLatin1Char('=') + value + QLatin1Char(';')) {
            hash ^= quint64(ch.unicode());
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

QString AnalysisRuntime::busTextForFrame(const FrameRecord& frame) {
    return QStringLiteral("BUS %1").arg(frame.bus);
}

void AnalysisRuntime::appendDlcHistogram(QVariantMap& row, const State& state) {
    row.insert(QStringLiteral("dlc"), int(state.lastFrame.dlc));
    row.insert(QStringLiteral("dlcHistogram"), normalizedDlcText(state.dlcHistogram));
}

} // namespace CanMonitorAnalysis
