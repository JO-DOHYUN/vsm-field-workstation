import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root
    objectName: "replayPage"

    signal openReplay()
    signal openTypedReplay()
    signal playReplay(real speed)
    signal pauseReplay()
    signal stopReplay()
    signal setReplayLoop(bool enabled)
    signal seekReplay(real progress)
    signal stepReplay(int delta)

    property bool autoFollow: true
    property real uiScale: 1.0
    property bool internalSeekUpdate: false
    property real pendingSeekValue: 0.0
    property bool pendingSeekInitialized: false
    property bool seekCommitPending: false
    property bool showTimingMarkers: true
    property bool showValueMarkers: true
    property bool showAlarmMarkers: true

    function kindFromLevel(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function applyWheel(view, event) {
        let delta = 0
        if (event.pixelDelta && event.pixelDelta.y)
            delta = -event.pixelDelta.y
        else if (event.angleDelta && event.angleDelta.y)
            delta = -(event.angleDelta.y / 120.0) * (44 * uiScale)
        if (delta !== 0) {
            const maxY = Math.max(0, view.contentHeight - view.height)
            view.contentY = Math.max(0, Math.min(maxY, view.contentY + delta))
            if (event.accepted !== undefined)
                event.accepted = true
        }
    }

    function markerVisible(kind) {
        if (kind === "timing") return showTimingMarkers
        if (kind === "value") return showValueMarkers
        if (kind === "alarm") return showAlarmMarkers
        return true
    }

    function markerColor(kind, severity) {
        if (severity === "ERR") return "#c0392b"
        if (kind === "timing") return "#2563eb"
        if (kind === "value") return "#d97706"
        if (kind === "alarm") return "#b45309"
        return "#52606d"
    }

    function markerTip(modelData) {
        return (modelData.kind === "timing" ? "주기" : (modelData.kind === "value" ? "값" : "경보"))
               + " · " + modelData.idText + " · " + modelData.severity
               + " · " + modelData.timeText
               + (modelData.note !== "" ? ("\n" + modelData.note) : "")
    }

    function testReplayTypedDiagnosticCount() {
        return appController.replayTypedDiagnostics.length
    }

    function testReplayBusFilter() {
        return appController.replayFrameView.busFilter
    }

    function testSetReplayBusFilter(bus) {
        appController.replayFrameView.busFilter = bus
        return appController.replayFrameView.busFilter
    }

    function testReplayMaxSpeedButton() {
        return 8.0
    }

    function testReplayTypedDiagnosticValue(key) {
        for (let i = 0; i < appController.replayTypedDiagnostics.length; ++i) {
            const row = appController.replayTypedDiagnostics[i]
            if (row.key === key)
                return row.value
        }
        return ""
    }

    function testReplayTypedDiagnosticLabel(key) {
        for (let i = 0; i < appController.replayTypedDiagnostics.length; ++i) {
            const row = appController.replayTypedDiagnostics[i]
            if (row.key === key)
                return row.label || row.key
        }
        return ""
    }

    function testReplayTypedDiagnosticNote(key) {
        for (let i = 0; i < appController.replayTypedDiagnostics.length; ++i) {
            const row = appController.replayTypedDiagnostics[i]
            if (row.key === key)
                return row.note || ""
        }
        return ""
    }

    function testReplayTypedDiagnosticLevel(key) {
        for (let i = 0; i < appController.replayTypedDiagnostics.length; ++i) {
            const row = appController.replayTypedDiagnostics[i]
            if (row.key === key)
                return row.level
        }
        return ""
    }

    function typedDiagnosticValue(key) {
        return testReplayTypedDiagnosticValue(key)
    }

    function typedDiagnosticLevel(key) {
        return testReplayTypedDiagnosticLevel(key)
    }

    function typedDiagnosticKind(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function typedDiagnosticColor(level) {
        if (level === "ERR") return "#c0392b"
        if (level === "WARN") return "#92400e"
        if (level === "OK") return "#166534"
        return "#52606d"
    }

    Connections {
        target: appController.replayFrameView
        function onCountChanged() {
            if (autoFollow && replayList.count > 0)
                replayList.positionViewAtBeginning()
        }
    }

    Connections {
        target: appController
        function onReplayStateChanged() {
            if (replaySlider.pressed)
                return
            if (appController.replayRebuilding)
                pendingSeekValue = appController.replayTargetProgress
            else
                pendingSeekValue = appController.replayProgress
            pendingSeekInitialized = true
            seekCommitPending = false
        }
    }

    Component.onCompleted: {
        pendingSeekValue = appController.replayProgress
        pendingSeekInitialized = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 2

                Components.FlowToolbar {
                    id: replayActionFlow
                    Layout.fillWidth: true
                    uiScale: root.uiScale

                    Components.SafeButton { text: "BIN 열기"; uiScale: root.uiScale; maxButtonWidth: Math.round(78 * uiScale); onClicked: openReplay() }
                    Components.SafeButton { text: "Typed 열기"; uiScale: root.uiScale; maxButtonWidth: Math.round(92 * uiScale); onClicked: openTypedReplay() }
                    Repeater {
                        model: [0.5, 1.0, 2.0, 4.0, 8.0]
                        delegate: Components.SafeButton {
                            required property var modelData
                            text: modelData + "x"
                            uiScale: root.uiScale
                            minButtonWidth: Math.round(44 * uiScale)
                            maxButtonWidth: Math.round(48 * uiScale)
                            enabled: appController.replayLoaded
                            checkable: true
                            checked: Math.abs(appController.replaySpeed - modelData) < 0.01
                            onClicked: playReplay(modelData)
                        }
                    }
                    Components.SafeButton { text: "◀주기"; uiScale: root.uiScale; maxButtonWidth: Math.round(70 * uiScale); enabled: appController.replayLoaded && appController.replayTimingMarkerCount > 0; onClicked: appController.seekReplayIssue("timing", -1) }
                    Components.SafeButton { text: "주기▶"; uiScale: root.uiScale; maxButtonWidth: Math.round(70 * uiScale); enabled: appController.replayLoaded && appController.replayTimingMarkerCount > 0; onClicked: appController.seekReplayIssue("timing", 1) }
                    Components.SafeButton { text: "◀값"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * uiScale); enabled: appController.replayLoaded && appController.replayValueMarkerCount > 0; onClicked: appController.seekReplayIssue("value", -1) }
                    Components.SafeButton { text: "값▶"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * uiScale); enabled: appController.replayLoaded && appController.replayValueMarkerCount > 0; onClicked: appController.seekReplayIssue("value", 1) }
                    Components.SafeButton { text: "◀경보"; uiScale: root.uiScale; maxButtonWidth: Math.round(70 * uiScale); enabled: appController.replayLoaded && appController.replayAlarmMarkerCount > 0; onClicked: appController.seekReplayIssue("alarm", -1) }
                    Components.SafeButton { text: "경보▶"; uiScale: root.uiScale; maxButtonWidth: Math.round(70 * uiScale); enabled: appController.replayLoaded && appController.replayAlarmMarkerCount > 0; onClicked: appController.seekReplayIssue("alarm", 1) }
                    TextField {
                        id: replaySeekIdField
                        width: Math.round(92 * uiScale)
                        placeholderText: "ID"
                        selectByMouse: true
                        onAccepted: if (appController.replayLoaded && text !== "") appController.seekReplayId(text, 1)
                    }
                    Components.SafeButton { text: "◀ID"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * uiScale); enabled: appController.replayLoaded && replaySeekIdField.text !== ""; onClicked: appController.seekReplayId(replaySeekIdField.text, -1) }
                    Components.SafeButton { text: "ID▶"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * uiScale); enabled: appController.replayLoaded && replaySeekIdField.text !== ""; onClicked: appController.seekReplayId(replaySeekIdField.text, 1) }
                    Components.SafeButton { text: "전체"; uiScale: root.uiScale; maxButtonWidth: Math.round(64 * uiScale); checkable: true; checked: appController.replayFrameView.busFilter < 0; onClicked: appController.replayFrameView.busFilter = -1 }
                    Components.SafeButton { text: "버스 0"; uiScale: root.uiScale; maxButtonWidth: Math.round(74 * uiScale); checkable: true; checked: appController.replayFrameView.busFilter === 0; onClicked: appController.replayFrameView.busFilter = 0 }
                    Components.SafeButton { text: "버스 1"; uiScale: root.uiScale; maxButtonWidth: Math.round(74 * uiScale); checkable: true; checked: appController.replayFrameView.busFilter === 1; onClicked: appController.replayFrameView.busFilter = 1 }
                    Components.SafeCheckBox { text: "주기"; uiScale: root.uiScale; maxControlWidth: Math.round(64 * uiScale); checked: showTimingMarkers; onToggled: showTimingMarkers = checked }
                    Components.SafeCheckBox { text: "값"; uiScale: root.uiScale; maxControlWidth: Math.round(58 * uiScale); checked: showValueMarkers; onToggled: showValueMarkers = checked }
                    Components.SafeCheckBox { text: "경보"; uiScale: root.uiScale; maxControlWidth: Math.round(64 * uiScale); checked: showAlarmMarkers; onToggled: showAlarmMarkers = checked }
                    Components.SafeCheckBox { text: "자동"; uiScale: root.uiScale; maxControlWidth: Math.round(64 * uiScale); checked: autoFollow; onToggled: autoFollow = checked }
                    TextField {
                        width: Math.round(115 * uiScale)
                        placeholderText: "필터 ID"
                        text: appController.replayFrameView.idFilter
                        selectByMouse: true
                        onTextEdited: appController.replayFrameView.idFilter = text
                    }
                    Components.SafeText {
                        width: Math.max(Math.round(220 * uiScale), Math.min(Math.round(540 * uiScale), root.width - Math.round(900 * uiScale)))
                        text: appController.replayPath === "" ? "재생 파일 미선택" : appController.replayPath
                        color: "#607080"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.6 * uiScale)
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(20 * uiScale)
                    visible: appController.replayLoaded

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: Math.max(5, Math.round(5 * uiScale))
                        radius: height / 2
                        color: "#edf2f7"
                        border.color: "#dbe5f0"
                    }

                    Repeater {
                        model: appController.replayIssueMarkers
                        delegate: Rectangle {
                            required property var modelData
                            visible: markerVisible(modelData.kind)
                            width: modelData.severity === "ERR" ? Math.max(5, Math.round(5 * uiScale)) : Math.max(3, Math.round(3 * uiScale))
                            height: Math.round(16 * uiScale)
                            radius: width / 2
                            color: markerColor(modelData.kind, modelData.severity)
                            border.color: "#ffffff"
                            x: Math.max(0, Math.min(parent.width - width, modelData.progress * Math.max(1, parent.width - width)))
                            y: Math.round(2 * uiScale)
                            opacity: modelData.severity === "ERR" ? 0.98 : 0.78
                            TapHandler {
                                onTapped: appController.jumpReplayToFrameIndex(modelData.index,
                                    (modelData.kind === "timing" ? "주기" : (modelData.kind === "value" ? "값" : "경보"))
                                    + " 마커 이동 · " + modelData.idText + " · " + modelData.timeText)
                            }
                            HoverHandler { id: markerHover }
                            ToolTip.visible: markerHover.hovered
                            ToolTip.text: markerTip(modelData)
                        }
                    }
                }

                Components.SafeText {
                    Layout.fillWidth: true
                    text: appController.replayCursorSummary
                    color: appController.replayPlaying ? "#15803d" : (appController.replayRebuilding ? "#0f4c81" : "#52606d")
                    uiScale: root.uiScale
                    basePixelSize: Math.round(10.8 * uiScale)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: appController.replayLoaded
                    Components.SafeText {
                        text: appController.replayCurrentTimeText + " / " + appController.replayDurationText
                        color: "#35506b"
                        Layout.preferredWidth: Math.round(170 * uiScale)
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.4 * uiScale)
                    }
                    Components.PreciseSlider {
                        id: replaySlider
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        enabled: appController.replayLoaded
                        liveUpdate: false
                        releaseHoldMs: 220
                        value: (replaySlider.visualHoldActive || seekCommitPending || appController.replayRebuilding || !pendingSeekInitialized)
                               ? pendingSeekValue
                               : appController.replayProgress
                        onCommitted: function(nextValue) {
                            if (!appController.replayLoaded)
                                return
                            pendingSeekValue = nextValue
                            seekCommitPending = true
                            appController.commitSeekReplay(nextValue)
                        }
                    }
                    Components.SafeText {
                        Layout.preferredWidth: Math.round(48 * uiScale)
                        horizontalAlignment: Text.AlignRight
                        color: (replaySlider.pressed || seekCommitPending || appController.replayRebuilding) ? "#0f4c81" : "#52606d"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.4 * uiScale)
                        font.bold: true
                        text: Math.round((replaySlider.visualHoldActive
                                          ? replaySlider.visualValue
                                          : ((seekCommitPending || appController.replayRebuilding)
                                                ? (seekCommitPending ? pendingSeekValue : appController.replayTargetProgress)
                                                : appController.replayProgress)) * 100) + "%"
                    }
                }

                Components.DiagnosticStrip {
                    Layout.fillWidth: true
                    visible: appController.replayTypedDiagnosticsSummary !== ""
                    title: "Typed"
                    level: typedDiagnosticLevel("operator_verdict") !== "" ? typedDiagnosticLevel("operator_verdict") : (appController.replayTypedCaptureState === "FINALIZED" ? "OK" : "WARN")
                    summary: (typedDiagnosticValue("operator_verdict") !== "" ? (typedDiagnosticValue("operator_verdict") + " · ") : "") + appController.replayTypedDiagnosticsSummary
                    detailsModel: appController.replayTypedDiagnostics
                    uiScale: root.uiScale
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: appController.systemLevel === "ERR" ? "bad" : (appController.systemLevel === "WARN" ? "warn" : "ok"); uiScale: uiScale }
                    Components.StatusBadge { text: "재생 " + (appController.replayPlaying ? "실행" : "대기"); kind: appController.replayPlaying ? "warn" : "info"; uiScale: uiScale }
                    Components.StatusBadge { text: "조치 " + appController.operatorActionLevel; kind: kindFromLevel(appController.operatorActionLevel); uiScale: uiScale }
                    Item { Layout.fillWidth: true }
                    Components.SafeText { text: appController.primaryIssueId !== "" ? (appController.primaryIssueId + " · " + appController.primaryIssueSummary) : appController.primaryIssueSummary; color: "#607080"; uiScale: root.uiScale; basePixelSize: Math.round(10.6 * uiScale); Layout.preferredWidth: Math.min(Math.round(420 * uiScale), Math.round(root.width * 0.42)) }
                }
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe5f0"
                    implicitHeight: Math.round(34 * uiScale)
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.operatorHeadline + " · " + appController.operatorActionText
                        color: "#35506b"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.8 * uiScale)
                        maxLines: 2
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Components.SafeText { text: "재생 프레임"; uiScale: root.uiScale; basePixelSize: Math.round(11.0 * uiScale); font.bold: true; color: "#243447"; Layout.preferredWidth: Math.round(92 * uiScale) }
                    Item { Layout.fillWidth: true }
                    Components.SafeText { text: (appController.replayPlaying ? ("x" + appController.replaySpeed.toFixed(2)) : "정지") + " · " + appController.replayIssueSummary; color: appController.replayPlaying ? "#b45309" : "#15803d"; uiScale: root.uiScale; basePixelSize: Math.round(10.6 * uiScale); Layout.preferredWidth: Math.min(Math.round(420 * uiScale), Math.round(root.width * 0.45)); horizontalAlignment: Text.AlignRight }
                }

                ListView {
                    id: replayList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    reuseItems: true
                    cacheBuffer: 220
                    boundsBehavior: Flickable.StopAtBounds
                    model: appController.replayFrameView
                    onMovementStarted: autoFollow = false
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn; active: true }
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { autoFollow = false; applyWheel(replayList, event) }
                    }

                    Components.EmptyState {
                        parent: replayList
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                        visible: replayList.count === 0
                        z: 10
                        title: "재생 프레임 없음"
                        message: appController.replayLoaded ? "재생 파일은 열렸지만 현재 필터 조건에 맞는 프레임이 없습니다." : "BIN 또는 Typed capture를 열면 DLC 보존 프레임이 여기에 표시됩니다."
                        badgeText: appController.replayLoaded ? "필터 확인" : "파일 대기"
                        kind: appController.replayLoaded ? "warn" : "info"
                        uiScale: root.uiScale
                    }

                    delegate: Rectangle {
                        required property int index
                        required property string idText
                        required property int bus
                        required property int dlc
                        required property string dataHex
                        required property string timeText
                        required property string source
                        width: ListView.view.width
                        height: Math.round(30 * uiScale)
                        radius: 8
                        color: index % 2 === 0 ? "#fffdf8" : "#fff8ef"
                        border.color: "#ead9b5"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 70
                                Layout.preferredHeight: Math.round(20 * uiScale)
                                radius: 6
                                color: "#fff0cc"
                                Components.SafeText { anchors.fill: parent; anchors.margins: 3; text: idText; color: "#9a5b00"; uiScale: root.uiScale; basePixelSize: Math.round(11 * uiScale); font.bold: true; horizontalAlignment: Text.AlignHCenter }
                            }
                            Components.SafeText { text: "버스 " + bus; color: "#6b7280"; uiScale: root.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 54 }
                            Components.SafeText { text: "DLC " + dlc; color: "#6b7280"; uiScale: root.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 48 }
                            Components.SafeText { text: dataHex; color: "#3b2f1d"; Layout.fillWidth: true; uiScale: root.uiScale; basePixelSize: Math.round(11 * uiScale); font.family: "Consolas" }
                            Components.SafeText { text: timeText; color: "#6b7280"; uiScale: root.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 92; horizontalAlignment: Text.AlignRight }
                            Rectangle {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: Math.round(20 * uiScale)
                                radius: 6
                                color: "#fff0cc"
                                Components.SafeText { anchors.fill: parent; anchors.margins: 3; text: source; color: "#9a5b00"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * uiScale); horizontalAlignment: Text.AlignHCenter }
                            }
                        }
                    }
                }
            }
        }
    }
}
