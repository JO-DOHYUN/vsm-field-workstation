import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: liveRoot
    objectName: "livePage"

    signal startLog()
    signal stopLog()
    signal savePendingLog()
    signal discardPendingLog()
    signal chooseLogFolder()

    property bool autoFollow: true
    property real uiScale: 1.0

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

    function testLiveFrameCountText() {
        return appController.liveFrameView.count + " / " + appController.liveFrames.count
    }

    function testLiveFilterText() {
        return appController.liveFrameView.idFilter
    }

    function testLiveBusFilter() {
        return appController.liveFrameView.busFilter
    }

    function testSetLiveBusFilter(bus) {
        appController.liveFrameView.busFilter = bus
        return appController.liveFrameView.busFilter
    }

    function testLiveLogTargetPreview() {
        return appController.logTargetPreview
    }

    function testLivePauseText() {
        return appController.liveUiPaused ? "화면 정지" : "실시간 반영"
    }

    function testLiveAutoFollow() {
        return autoFollow
    }

    function testTransportDiagnosticCount() {
        return appController.transportDiagnostics.length
    }

    function testTransportDiagnosticField(index, field) {
        if (index < 0 || index >= appController.transportDiagnostics.length) return ""
        return appController.transportDiagnostics[index][field]
    }

    function testTransportDiagnosticSummary() {
        return appController.transportDiagnosticsSummary
    }

    Connections {
        target: appController.liveFrameView
        function onCountChanged() {
            if (autoFollow && liveList.count > 0)
                liveList.positionViewAtBeginning()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 9
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Components.SafeText { text: "라이브 스트림"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(19 * uiScale); font.bold: true; color: "#243447"; Layout.fillWidth: true }
                    Rectangle { width: 10; height: 10; radius: 5; color: appController.liveUiPaused ? "#d97706" : "#16a34a" }
                    Components.SafeText {
                        text: appController.liveUiPaused ? "화면 정지" : "실시간 반영"
                        uiScale: liveRoot.uiScale
                        basePixelSize: Math.round(11 * uiScale)
                        color: appController.liveUiPaused ? "#b45309" : "#15803d"
                        Layout.preferredWidth: Math.round(90 * uiScale)
                    }
                    Components.SafeText { text: "표시: " + appController.liveFrameView.count + " / 원본: " + appController.liveFrames.count; color: "#5b6673"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(12 * uiScale); Layout.preferredWidth: Math.round(160 * uiScale); horizontalAlignment: Text.AlignRight }
                }

                Components.FlowToolbar {
                    id: liveActionFlow
                    Layout.fillWidth: true
                    uiScale: liveRoot.uiScale

                    Components.SafeButton {
                        text: appController.logRecordingActive ? "기록 중" : (appController.logStopping ? "종료 중" : (appController.logSaving ? "저장 중" : (appController.logPendingSave ? "저장 대기" : "로그 시작")))
                        uiScale: liveRoot.uiScale
                        enabled: appController.connected && !appController.logRecordingActive && !appController.logPendingSave && !appController.logStopping && !appController.logSaving
                        onClicked: startLog()
                    }
                    Components.SafeButton { text: appController.liveUiPaused ? "화면 재개" : "화면 정지"; uiScale: liveRoot.uiScale; onClicked: appController.toggleLiveUiPaused() }
                    Components.SafeButton { text: "프레임 지우기"; uiScale: liveRoot.uiScale; maxButtonWidth: Math.round(112 * uiScale); onClicked: appController.clearFrames() }
                    Components.SafeButton {
                        text: appController.logRecordingActive ? "기록 중지·저장" : (appController.logStopping ? "종료 중" : (appController.logPendingSave ? "저장하기" : "저장하기"))
                        uiScale: liveRoot.uiScale
                        maxButtonWidth: Math.round(120 * uiScale)
                        enabled: (appController.logRecordingActive || appController.logPendingSave) && !appController.logStopping && !appController.logSaving
                        onClicked: stopLog()
                    }
                    Components.SafeButton {
                        text: "폐기"
                        uiScale: liveRoot.uiScale
                        maxButtonWidth: Math.round(62 * uiScale)
                        enabled: appController.logPendingSave && !appController.logRecordingActive && !appController.logStopping && !appController.logSaving
                        onClicked: discardPendingLog()
                    }
                    Components.SafeButton {
                        text: "전체"
                        uiScale: liveRoot.uiScale
                        maxButtonWidth: Math.round(64 * uiScale)
                        checkable: true
                        checked: appController.liveFrameView.busFilter < 0
                        onClicked: appController.liveFrameView.busFilter = -1
                    }
                    Components.SafeButton {
                        text: "버스 0"
                        uiScale: liveRoot.uiScale
                        maxButtonWidth: Math.round(74 * uiScale)
                        checkable: true
                        checked: appController.liveFrameView.busFilter === 0
                        onClicked: appController.liveFrameView.busFilter = 0
                    }
                    Components.SafeButton {
                        text: "버스 1"
                        uiScale: liveRoot.uiScale
                        maxButtonWidth: Math.round(74 * uiScale)
                        checkable: true
                        checked: appController.liveFrameView.busFilter === 1
                        onClicked: appController.liveFrameView.busFilter = 1
                    }
                    TextField {
                        width: Math.round(190 * uiScale)
                        placeholderText: "ID 필터 (예: 0x117,118)"
                        text: appController.liveFrameView.idFilter
                        selectByMouse: true
                        onTextEdited: appController.liveFrameView.idFilter = text
                    }
                    Components.SafeCheckBox {
                        text: "자동 따라가기"
                        uiScale: liveRoot.uiScale
                        maxControlWidth: Math.round(118 * uiScale)
                        checked: autoFollow
                        onToggled: autoFollow = checked
                    }
                }

                Components.FlowToolbar {
                    id: logTargetFlow
                    Layout.fillWidth: true
                    uiScale: liveRoot.uiScale

                    Components.SafeButton {
                        text: "로그 폴더"
                        uiScale: liveRoot.uiScale
                        enabled: !appController.logRecordingActive && !appController.logStopping && !appController.logSaving
                        onClicked: chooseLogFolder()
                    }
                    TextField {
                        width: Math.round(230 * uiScale)
                        placeholderText: "로그 이름 (비우면 자동)"
                        text: appController.logTargetName
                        enabled: !appController.logRecordingActive && !appController.logStopping && !appController.logSaving
                        selectByMouse: true
                        onTextEdited: appController.logTargetName = text
                    }
                    Components.SafeText {
                        width: Math.max(Math.round(260 * uiScale), Math.min(Math.round(720 * uiScale), liveRoot.width - Math.round(520 * uiScale)))
                        text: appController.logTargetPreview
                        color: "#52606d"
                        uiScale: liveRoot.uiScale
                        basePixelSize: Math.round(10.6 * uiScale)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Components.SafeText {
                        text: appController.modelSourceSummary
                        Layout.fillWidth: true
                        uiScale: liveRoot.uiScale
                        basePixelSize: Math.round(10.8 * uiScale)
                        color: appController.modelActive ? "#52606d" : "#b45309"
                    }
                    Rectangle {
                        Layout.preferredWidth: Math.round(460 * uiScale)
                        Layout.minimumWidth: Math.round(230 * uiScale)
                        Layout.maximumWidth: Math.round(460 * uiScale)
                        Layout.preferredHeight: Math.round(28 * uiScale)
                        radius: 8
                        color: appController.logRecordingActive ? "#ecfdf5" : (appController.logPendingSave ? "#fff7ed" : "#f8fafc")
                        border.color: appController.logRecordingActive ? "#86efac" : (appController.logPendingSave ? "#fdba74" : "#dbe5f0")
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 10
                                radius: 5
                                color: appController.logRecordingActive ? "#16a34a" : (appController.logPendingSave ? "#d97706" : "#94a3b8")
                            }
                            Components.SafeText {
                                text: appController.logStatusSummary
                                Layout.fillWidth: true
                                uiScale: liveRoot.uiScale
                                basePixelSize: Math.round(10.6 * uiScale)
                                color: appController.logRecordingActive ? "#166534" : (appController.logPendingSave ? "#9a3412" : (appController.logStopping || appController.logSaving ? "#7c2d12" : "#52606d"))
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe5f0"
                    implicitHeight: Math.round(36 * uiScale)
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.logActionText
                        uiScale: liveRoot.uiScale
                        basePixelSize: Math.round(10.6 * uiScale)
                        maxLines: 2
                        color: "#52606d"
                    }
                }

                Components.DiagnosticStrip {
                    title: "전송"
                    level: appController.transportDiagnosticsLevel
                    summary: appController.transportDiagnosticsSummary
                    detailsModel: appController.transportDiagnostics
                    uiScale: liveRoot.uiScale
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
                    Components.StatusBadge { text: "버스 " + appController.busHealthLevel; kind: kindFromLevel(appController.busHealthLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "조치 " + appController.operatorActionLevel; kind: kindFromLevel(appController.operatorActionLevel); uiScale: uiScale }
                    Item { Layout.fillWidth: true }
                    Components.SafeText { text: appController.primaryIssueId !== "" ? (appController.primaryIssueId + " · " + appController.primaryIssueSummary) : appController.primaryIssueSummary; color: "#607080"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(10.6 * uiScale); Layout.preferredWidth: Math.min(Math.round(420 * uiScale), Math.round(liveRoot.width * 0.42)) }
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
                        uiScale: liveRoot.uiScale
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
                anchors.margins: 6
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Components.SafeText { text: "최근 라이브 프레임"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(11.0 * uiScale); font.bold: true; color: "#243447"; Layout.preferredWidth: Math.round(120 * uiScale) }
                    Item { Layout.fillWidth: true }
                    Components.SafeText {
                        text: appController.liveUiPaused ? "스크롤/검토용 정지 상태" : (appController.liveFrameView.idFilter === "" && appController.liveFrameView.busFilter < 0 ? "실시간 수신 중" : "실시간 수신 중 · 필터 적용")
                        color: appController.liveUiPaused ? "#b45309" : "#15803d"
                        uiScale: liveRoot.uiScale
                        basePixelSize: Math.round(10.6 * uiScale)
                        Layout.preferredWidth: Math.min(Math.round(320 * uiScale), Math.round(liveRoot.width * 0.42))
                        horizontalAlignment: Text.AlignRight
                    }
                }

                ListView {
                    id: liveList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    reuseItems: true
                    cacheBuffer: 220
                    boundsBehavior: Flickable.StopAtBounds
                    model: appController.liveFrameView
                    onMovementStarted: autoFollow = false
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn; active: true }
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { autoFollow = false; applyWheel(liveList, event) }
                    }

                    Components.EmptyState {
                        parent: liveList
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                        visible: liveList.count === 0
                        z: 10
                        title: "수신 프레임 없음"
                        message: appController.connected ? "포트는 열렸지만 아직 표시 조건에 맞는 CAN 프레임이 없습니다. 버스/ID 필터를 확인하세요." : "포트를 연결하면 bus0/bus1 수신 프레임이 여기에 표시됩니다."
                        badgeText: appController.connected ? "연결됨" : "미연결"
                        kind: appController.connected ? "warn" : "info"
                        uiScale: liveRoot.uiScale
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
                        color: index % 2 === 0 ? "#ffffff" : "#f7fbff"
                        border.color: "#d7e0ea"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 70
                                Layout.preferredHeight: Math.round(20 * uiScale)
                                radius: 6
                                color: "#e8f0ff"
                                Components.SafeText { anchors.fill: parent; anchors.margins: 3; text: idText; color: "#1e40af"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(11 * uiScale); font.bold: true; horizontalAlignment: Text.AlignHCenter }
                            }
                            Components.SafeText { text: "버스 " + bus; color: "#52606d"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 54 }
                            Components.SafeText { text: "DLC " + dlc; color: "#52606d"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 48 }
                            Components.SafeText { text: dataHex; color: "#0f172a"; Layout.fillWidth: true; uiScale: liveRoot.uiScale; basePixelSize: Math.round(11 * uiScale); font.family: "Consolas" }
                            Components.SafeText { text: timeText; color: "#52606d"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(10.4 * uiScale); Layout.preferredWidth: 92; horizontalAlignment: Text.AlignRight }
                            Rectangle {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: Math.round(20 * uiScale)
                                radius: 6
                                color: "#e8f7ed"
                                Components.SafeText { anchors.fill: parent; anchors.margins: 3; text: source; color: "#15803d"; uiScale: liveRoot.uiScale; basePixelSize: Math.round(10.2 * uiScale); horizontalAlignment: Text.AlignHCenter }
                            }
                        }
                    }
                }
            }
        }
    }
}
