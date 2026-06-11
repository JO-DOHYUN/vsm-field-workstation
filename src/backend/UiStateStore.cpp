#include "UiStateStore.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace {

QString readTrimmed(const QJsonObject& obj, const QString& key) {
    return obj.value(key).toString().trimmed();
}

QString readTrimmedSetting(const SessionManager& session, const QString& key) {
    return session.value(key).toString().trimmed();
}

double normalizeReplaySpeed(double speed) {
    if (!std::isfinite(speed) || speed <= 0.0) return 1.0;
    return std::clamp(speed, 0.1, 8.0);
}

QJsonObject toJson(const AppUiState::AnalysisViewStateSnapshot& state) {
    QJsonObject obj;
    obj.insert(QStringLiteral("timing_id"), state.timingFilterId);
    obj.insert(QStringLiteral("timing_severity"), state.timingFilterSeverity);
    obj.insert(QStringLiteral("timing_name"), state.timingFilterName);
    obj.insert(QStringLiteral("timing_reason"), state.timingFilterReason);
    obj.insert(QStringLiteral("timing_expected"), state.timingFilterExpected);
    obj.insert(QStringLiteral("timing_gap"), state.timingFilterGap);
    obj.insert(QStringLiteral("timing_age"), state.timingFilterAge);
    obj.insert(QStringLiteral("timing_source"), state.timingFilterSource);
    obj.insert(QStringLiteral("value_id"), state.valueFilterId);
    obj.insert(QStringLiteral("value_severity"), state.valueFilterSeverity);
    obj.insert(QStringLiteral("value_name"), state.valueFilterName);
    obj.insert(QStringLiteral("value_source"), state.valueFilterSource);
    obj.insert(QStringLiteral("value_raw"), state.valueFilterRaw);
    obj.insert(QStringLiteral("value_gap"), state.valueFilterGap);
    obj.insert(QStringLiteral("value_reason"), state.valueFilterReason);
    obj.insert(QStringLiteral("alarm_id"), state.alarmFilterId);
    obj.insert(QStringLiteral("alarm_severity"), state.alarmFilterSeverity);
    obj.insert(QStringLiteral("alarm_time"), state.alarmFilterTime);
    obj.insert(QStringLiteral("alarm_name"), state.alarmFilterName);
    obj.insert(QStringLiteral("alarm_source"), state.alarmFilterSource);
    obj.insert(QStringLiteral("alarm_message"), state.alarmFilterMessage);
    obj.insert(QStringLiteral("alarm_text"), state.alarmFilterText);
    obj.insert(QStringLiteral("selected_value_id"), state.selectedValueId);
    return obj;
}

AppUiState::AnalysisViewStateSnapshot fromJson(const QJsonObject& obj) {
    AppUiState::AnalysisViewStateSnapshot state;
    state.timingFilterId = readTrimmed(obj, QStringLiteral("timing_id"));
    state.timingFilterSeverity = readTrimmed(obj, QStringLiteral("timing_severity"));
    state.timingFilterName = readTrimmed(obj, QStringLiteral("timing_name"));
    state.timingFilterReason = readTrimmed(obj, QStringLiteral("timing_reason"));
    state.timingFilterExpected = readTrimmed(obj, QStringLiteral("timing_expected"));
    state.timingFilterGap = readTrimmed(obj, QStringLiteral("timing_gap"));
    state.timingFilterAge = readTrimmed(obj, QStringLiteral("timing_age"));
    state.timingFilterSource = readTrimmed(obj, QStringLiteral("timing_source"));
    state.valueFilterId = readTrimmed(obj, QStringLiteral("value_id"));
    state.valueFilterSeverity = readTrimmed(obj, QStringLiteral("value_severity"));
    state.valueFilterName = readTrimmed(obj, QStringLiteral("value_name"));
    state.valueFilterSource = readTrimmed(obj, QStringLiteral("value_source"));
    state.valueFilterRaw = readTrimmed(obj, QStringLiteral("value_raw"));
    state.valueFilterGap = readTrimmed(obj, QStringLiteral("value_gap"));
    state.valueFilterReason = readTrimmed(obj, QStringLiteral("value_reason"));
    state.alarmFilterId = readTrimmed(obj, QStringLiteral("alarm_id"));
    state.alarmFilterSeverity = readTrimmed(obj, QStringLiteral("alarm_severity"));
    state.alarmFilterTime = readTrimmed(obj, QStringLiteral("alarm_time"));
    state.alarmFilterName = readTrimmed(obj, QStringLiteral("alarm_name"));
    state.alarmFilterSource = readTrimmed(obj, QStringLiteral("alarm_source"));
    state.alarmFilterMessage = readTrimmed(obj, QStringLiteral("alarm_message"));
    state.alarmFilterText = readTrimmed(obj, QStringLiteral("alarm_text"));
    state.selectedValueId = readTrimmed(obj, QStringLiteral("selected_value_id"));
    return state;
}

} // namespace

AppUiState::Snapshot UiStateStore::load(const SessionManager& session) const {
    const int version = session.value(QStringLiteral("ui_state/version"), 0).toInt();
    const QString encoded = session.value(QStringLiteral("ui_state/blob")).toString().trimmed();
    if (version == kSchemaVersion && !encoded.isEmpty()) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            AppUiState::Snapshot snapshot;

            const QJsonObject model = root.value(QStringLiteral("model")).toObject();
            snapshot.modelEnabled = model.value(QStringLiteral("enabled")).toBool(snapshot.modelEnabled);
            snapshot.modelBundled = model.value(QStringLiteral("bundled")).toBool(snapshot.modelBundled);
            snapshot.modelPath = readTrimmed(model, QStringLiteral("path"));

            const QJsonObject sort = root.value(QStringLiteral("sort")).toObject();
            snapshot.timingSortMode = readTrimmed(sort, QStringLiteral("timing_mode"));
            if (snapshot.timingSortMode.isEmpty()) snapshot.timingSortMode = QStringLiteral("id");
            snapshot.timingSortDescending = sort.value(QStringLiteral("timing_desc")).toBool(snapshot.timingSortDescending);
            snapshot.valueSortMode = readTrimmed(sort, QStringLiteral("value_mode"));
            if (snapshot.valueSortMode.isEmpty()) snapshot.valueSortMode = QStringLiteral("id");
            snapshot.valueSortDescending = sort.value(QStringLiteral("value_desc")).toBool(snapshot.valueSortDescending);
            snapshot.alarmSortMode = readTrimmed(sort, QStringLiteral("alarm_mode"));
            if (snapshot.alarmSortMode.isEmpty()) snapshot.alarmSortMode = QStringLiteral("id");
            snapshot.alarmSortDescending = sort.value(QStringLiteral("alarm_desc")).toBool(snapshot.alarmSortDescending);

            const QJsonObject views = root.value(QStringLiteral("views")).toObject();
            snapshot.liveViewState = fromJson(views.value(QStringLiteral("live")).toObject());
            snapshot.replayViewState = fromJson(views.value(QStringLiteral("replay")).toObject());

            const QJsonObject frames = root.value(QStringLiteral("frames")).toObject();
            snapshot.liveFrameIdFilter = readTrimmed(frames, QStringLiteral("live_id_filter"));
            snapshot.replayFrameIdFilter = readTrimmed(frames, QStringLiteral("replay_id_filter"));
            snapshot.liveFrameBusFilter = frames.value(QStringLiteral("live_bus_filter")).toInt(snapshot.liveFrameBusFilter);
            snapshot.replayFrameBusFilter = frames.value(QStringLiteral("replay_bus_filter")).toInt(snapshot.replayFrameBusFilter);

            const QJsonObject replay = root.value(QStringLiteral("replay")).toObject();
            snapshot.replaySpeed = normalizeReplaySpeed(replay.value(QStringLiteral("speed")).toDouble(snapshot.replaySpeed));
            snapshot.replayLoop = replay.value(QStringLiteral("loop")).toBool(snapshot.replayLoop);

            const QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
            snapshot.liveUiPaused = ui.value(QStringLiteral("live_paused")).toBool(snapshot.liveUiPaused);

            const QJsonObject log = root.value(QStringLiteral("log")).toObject();
            snapshot.logTargetDirectory = readTrimmed(log, QStringLiteral("target_directory"));
            snapshot.logTargetName = readTrimmed(log, QStringLiteral("target_name"));
            return snapshot;
        }
    }

    return loadLegacy(session);
}

void UiStateStore::save(SessionManager& session, const AppUiState::Snapshot& snapshot) const {
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kSchemaVersion);
    root.insert(QStringLiteral("saved_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QJsonObject model;
    model.insert(QStringLiteral("enabled"), snapshot.modelEnabled);
    model.insert(QStringLiteral("bundled"), snapshot.modelBundled);
    model.insert(QStringLiteral("path"), snapshot.modelPath.trimmed());
    root.insert(QStringLiteral("model"), model);

    QJsonObject sort;
    sort.insert(QStringLiteral("timing_mode"), snapshot.timingSortMode.trimmed());
    sort.insert(QStringLiteral("timing_desc"), snapshot.timingSortDescending);
    sort.insert(QStringLiteral("value_mode"), snapshot.valueSortMode.trimmed());
    sort.insert(QStringLiteral("value_desc"), snapshot.valueSortDescending);
    sort.insert(QStringLiteral("alarm_mode"), snapshot.alarmSortMode.trimmed());
    sort.insert(QStringLiteral("alarm_desc"), snapshot.alarmSortDescending);
    root.insert(QStringLiteral("sort"), sort);

    QJsonObject views;
    views.insert(QStringLiteral("live"), toJson(snapshot.liveViewState));
    views.insert(QStringLiteral("replay"), toJson(snapshot.replayViewState));
    root.insert(QStringLiteral("views"), views);

    QJsonObject frames;
    frames.insert(QStringLiteral("live_id_filter"), snapshot.liveFrameIdFilter.trimmed());
    frames.insert(QStringLiteral("replay_id_filter"), snapshot.replayFrameIdFilter.trimmed());
    frames.insert(QStringLiteral("live_bus_filter"), snapshot.liveFrameBusFilter);
    frames.insert(QStringLiteral("replay_bus_filter"), snapshot.replayFrameBusFilter);
    root.insert(QStringLiteral("frames"), frames);

    QJsonObject replay;
    replay.insert(QStringLiteral("speed"), normalizeReplaySpeed(snapshot.replaySpeed));
    replay.insert(QStringLiteral("loop"), snapshot.replayLoop);
    root.insert(QStringLiteral("replay"), replay);

    QJsonObject ui;
    ui.insert(QStringLiteral("live_paused"), snapshot.liveUiPaused);
    root.insert(QStringLiteral("ui"), ui);

    QJsonObject log;
    log.insert(QStringLiteral("target_directory"), snapshot.logTargetDirectory.trimmed());
    log.insert(QStringLiteral("target_name"), snapshot.logTargetName.trimmed());
    root.insert(QStringLiteral("log"), log);

    session.setValue(QStringLiteral("ui_state/version"), kSchemaVersion);
    session.setValue(QStringLiteral("ui_state/blob"),
                     QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    session.sync();
}

AppUiState::Snapshot UiStateStore::loadLegacy(const SessionManager& session) const {
    AppUiState::Snapshot snapshot;

    snapshot.modelEnabled = session.value(QStringLiteral("model/enabled"), snapshot.modelEnabled).toBool();
    snapshot.modelBundled = session.value(QStringLiteral("model/useBundled"), snapshot.modelBundled).toBool();
    snapshot.modelPath = readTrimmedSetting(session, QStringLiteral("model/path"));

    snapshot.timingSortMode = readTrimmedSetting(session, QStringLiteral("sort/timingMode"));
    if (snapshot.timingSortMode.isEmpty()) snapshot.timingSortMode = QStringLiteral("id");
    snapshot.timingSortDescending = session.value(QStringLiteral("sort/timingDesc"), snapshot.timingSortDescending).toBool();
    snapshot.valueSortMode = readTrimmedSetting(session, QStringLiteral("sort/valueMode"));
    if (snapshot.valueSortMode.isEmpty()) snapshot.valueSortMode = QStringLiteral("id");
    snapshot.valueSortDescending = session.value(QStringLiteral("sort/valueDesc"), snapshot.valueSortDescending).toBool();
    snapshot.alarmSortMode = readTrimmedSetting(session, QStringLiteral("sort/alarmMode"));
    if (snapshot.alarmSortMode.isEmpty()) snapshot.alarmSortMode = QStringLiteral("id");
    snapshot.alarmSortDescending = session.value(QStringLiteral("sort/alarmDesc"), snapshot.alarmSortDescending).toBool();

    auto loadLegacyView = [&session](const QString& prefix) {
        AppUiState::AnalysisViewStateSnapshot state;
        state.timingFilterId = readTrimmedSetting(session, prefix + QStringLiteral("/timingId"));
        state.timingFilterSeverity = readTrimmedSetting(session, prefix + QStringLiteral("/timingSeverity"));
        state.timingFilterName = readTrimmedSetting(session, prefix + QStringLiteral("/timingName"));
        state.timingFilterReason = readTrimmedSetting(session, prefix + QStringLiteral("/timingReason"));
        state.timingFilterExpected = readTrimmedSetting(session, prefix + QStringLiteral("/timingExpected"));
        state.timingFilterGap = readTrimmedSetting(session, prefix + QStringLiteral("/timingGap"));
        state.timingFilterAge = readTrimmedSetting(session, prefix + QStringLiteral("/timingAge"));
        state.timingFilterSource = readTrimmedSetting(session, prefix + QStringLiteral("/timingSource"));
        state.valueFilterId = readTrimmedSetting(session, prefix + QStringLiteral("/valueId"));
        state.valueFilterSeverity = readTrimmedSetting(session, prefix + QStringLiteral("/valueSeverity"));
        state.valueFilterName = readTrimmedSetting(session, prefix + QStringLiteral("/valueName"));
        state.valueFilterSource = readTrimmedSetting(session, prefix + QStringLiteral("/valueSource"));
        state.valueFilterRaw = readTrimmedSetting(session, prefix + QStringLiteral("/valueRaw"));
        state.valueFilterGap = readTrimmedSetting(session, prefix + QStringLiteral("/valueGap"));
        state.valueFilterReason = readTrimmedSetting(session, prefix + QStringLiteral("/valueReason"));
        state.alarmFilterId = readTrimmedSetting(session, prefix + QStringLiteral("/alarmId"));
        state.alarmFilterSeverity = readTrimmedSetting(session, prefix + QStringLiteral("/alarmSeverity"));
        state.alarmFilterTime = readTrimmedSetting(session, prefix + QStringLiteral("/alarmTime"));
        state.alarmFilterName = readTrimmedSetting(session, prefix + QStringLiteral("/alarmName"));
        state.alarmFilterSource = readTrimmedSetting(session, prefix + QStringLiteral("/alarmSource"));
        state.alarmFilterMessage = readTrimmedSetting(session, prefix + QStringLiteral("/alarmMessage"));
        state.alarmFilterText = readTrimmedSetting(session, prefix + QStringLiteral("/alarmText"));
        state.selectedValueId = readTrimmedSetting(session, prefix + QStringLiteral("/selectedValueId"));
        return state;
    };

    snapshot.liveViewState = loadLegacyView(QStringLiteral("filter/live"));
    snapshot.replayViewState = loadLegacyView(QStringLiteral("filter/replay"));

    const bool missingLiveFilters =
        snapshot.liveViewState.timingFilterId.isEmpty() &&
        snapshot.liveViewState.valueFilterId.isEmpty() &&
        snapshot.liveViewState.alarmFilterId.isEmpty() &&
        snapshot.liveViewState.timingFilterSeverity.isEmpty() &&
        snapshot.liveViewState.valueFilterSeverity.isEmpty() &&
        snapshot.liveViewState.alarmFilterSeverity.isEmpty() &&
        snapshot.liveViewState.timingFilterName.isEmpty() &&
        snapshot.liveViewState.valueFilterName.isEmpty() &&
        snapshot.liveViewState.alarmFilterName.isEmpty() &&
        snapshot.liveViewState.timingFilterReason.isEmpty() &&
        snapshot.liveViewState.valueFilterReason.isEmpty() &&
        snapshot.liveViewState.alarmFilterMessage.isEmpty() &&
        snapshot.liveViewState.timingFilterExpected.isEmpty() &&
        snapshot.liveViewState.timingFilterGap.isEmpty() &&
        snapshot.liveViewState.timingFilterAge.isEmpty() &&
        snapshot.liveViewState.timingFilterSource.isEmpty() &&
        snapshot.liveViewState.valueFilterSource.isEmpty() &&
        snapshot.liveViewState.valueFilterRaw.isEmpty() &&
        snapshot.liveViewState.valueFilterGap.isEmpty() &&
        snapshot.liveViewState.alarmFilterTime.isEmpty() &&
        snapshot.liveViewState.alarmFilterSource.isEmpty() &&
        snapshot.liveViewState.alarmFilterText.isEmpty();

    if (missingLiveFilters) {
        snapshot.liveViewState = loadLegacyView(QStringLiteral("filter"));
    }

    snapshot.liveFrameIdFilter = readTrimmedSetting(session, QStringLiteral("frame/liveIdFilter"));
    snapshot.replayFrameIdFilter = readTrimmedSetting(session, QStringLiteral("frame/replayIdFilter"));
    snapshot.liveFrameBusFilter = session.value(QStringLiteral("frame/liveBusFilter"), snapshot.liveFrameBusFilter).toInt();
    snapshot.replayFrameBusFilter = session.value(QStringLiteral("frame/replayBusFilter"), snapshot.replayFrameBusFilter).toInt();
    snapshot.replaySpeed = normalizeReplaySpeed(session.value(QStringLiteral("replay/speed"), snapshot.replaySpeed).toDouble());
    snapshot.replayLoop = session.value(QStringLiteral("replay/loop"), snapshot.replayLoop).toBool();
    snapshot.liveUiPaused = session.value(QStringLiteral("ui/livePaused"), snapshot.liveUiPaused).toBool();
    snapshot.logTargetDirectory = readTrimmedSetting(session, QStringLiteral("log/targetDirectory"));
    snapshot.logTargetName = readTrimmedSetting(session, QStringLiteral("log/targetName"));
    return snapshot;
}
