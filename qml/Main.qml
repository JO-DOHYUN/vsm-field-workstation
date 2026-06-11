import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "pages" as Pages
import "components" as Components

ApplicationWindow {
    id: root
    width: 1680
    height: 980
    visible: true
    title: "CAN Monitor Reboot"
    color: "#f1f3f6"
    font.pixelSize: Math.round(13 * uiScale)
    font.family: uiFontFamily
    minimumWidth: 820
    minimumHeight: 600

    property bool workspaceMode: false
    property int uiScaleIndex: 1
    readonly property var uiScaleSteps: [0.9, 1.0, 1.1]
    readonly property real uiScale: uiScaleSteps[Math.max(0, Math.min(uiScaleIndex, uiScaleSteps.length - 1))]
    readonly property string uiFontFamily: Qt.platform.os === "windows" ? "Malgun Gothic" : ""
    readonly property string monoFontFamily: Qt.platform.os === "windows" ? "Cascadia Mono" : "Monospace"
    readonly property bool compactChrome: width < 1180
    readonly property bool narrowChrome: width < 960
    readonly property real tabButtonWidth: Math.max(Math.round(54 * uiScale), Math.min(Math.round(96 * uiScale), Math.floor(Math.max(1, width - Math.round(104 * uiScale)) / 10)))

    Component.onCompleted: Qt.callLater(syncAnalysisVisibility)

    function visiblePanelCount() {
        let count = 0
        for (let i = 0; i < workspacePanels.count; ++i) {
            if (workspacePanels.get(i).opened)
                count++
        }
        return count
    }

    function togglePanel(index) {
        const current = workspacePanels.get(index).opened
        workspacePanels.setProperty(index, "opened", !current)
        syncAnalysisVisibility()
    }

    function enablePanel(index) {
        workspacePanels.setProperty(index, "opened", true)
        syncAnalysisVisibility()
    }

    function setWorkspacePreset(primary, secondary, tertiary) {
        for (let i = 0; i < workspacePanels.count; ++i) {
            const on = i === primary || i === secondary || i === tertiary
            workspacePanels.setProperty(i, "opened", on)
        }
        syncAnalysisVisibility()
    }

    function openPanelByKey(key) {
        const map = { overview: 0, live: 1, replay: 2, timing: 3, value: 4, graph: 5, graph_overview: 6, alarm: 7, settings: 8, control: 9 }
        const index = map[key] !== undefined ? map[key] : 0
        tabs.currentIndex = index
        if (root.workspaceMode)
            root.enablePanel(index)
        syncAnalysisVisibility()
    }

    function panelVisible(index) {
        return root.workspaceMode ? workspacePanels.get(index).opened : (tabs.currentIndex === index)
    }

    function syncAnalysisVisibility() {
        appController.setPanelActive("overview", panelVisible(0))
        appController.setPanelActive("live", panelVisible(1))
        appController.setPanelActive("timing", panelVisible(3))
        appController.setPanelActive("value", panelVisible(4))
        appController.setPanelActive("graph", panelVisible(5))
        appController.setPanelActive("alarm", panelVisible(7))
    }

    function focusIssue(key, idText) {
        root.openPanelByKey(key)
        if (key === "timing")
            appController.focusTimingId(idText)
        else if (key === "value")
            appController.focusValueId(idText)
        else if (key === "alarm")
            appController.focusAlarmId(idText)
    }

    function focusPrimaryIssue() {
        const key = appController.primaryIssueTargetTab
        const idText = appController.primaryIssueId
        if (key === "timing" || key === "value" || key === "alarm")
            root.focusIssue(key, idText)
        else
            root.openPanelByKey(key)
    }

    function seekPrimaryIssue(direction) {
        if (!appController.primaryIssueSeekAvailable)
            return
        root.openPanelByKey("replay")
        appController.seekReplayPrimaryIssue(direction)
    }

    function componentForKey(key) {
        if (key === "overview") return overviewPageComponent
        if (key === "live") return livePageComponent
        if (key === "replay") return replayPageComponent
        if (key === "timing") return timingPageComponent
        if (key === "value") return valuePageComponent
        if (key === "graph") return graphPageComponent
        if (key === "graph_overview") return graphOverviewPageComponent
        if (key === "alarm") return alarmPageComponent
        if (key === "control") return controlPageComponent
        return settingsPageComponent
    }

    function minimumPageWidthForKey(key) {
        if (key === "overview") return 0
        if (key === "live") return 920
        if (key === "replay") return 960
        if (key === "timing") return 1080
        if (key === "value") return 980
        if (key === "graph") return 1040
        if (key === "graph_overview") return 1060
        if (key === "alarm") return 1020
        if (key === "control") return 1060
        if (key === "settings") return 0
        return 960
    }

    footer: ToolBar {
        padding: 0
        implicitHeight: Math.round(30 * root.uiScale)
        background: Rectangle {
            color: "#ffffff"
            border.color: "#d7e0ea"
        }

        Flickable {
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: footerRow.implicitWidth + Math.round(20 * root.uiScale)
            contentHeight: height
            interactive: contentWidth > width + 1

            RowLayout {
                id: footerRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Math.round(10 * root.uiScale)
                spacing: Math.round(8 * root.uiScale)

                Components.StatusBadge { text: appController.analysisSourceText; kind: appController.replayAnalysisActive ? "warn" : "ok"; uiScale: root.uiScale }
                Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: appController.systemLevel === "ERR" ? "bad" : (appController.systemLevel === "WARN" ? "warn" : "ok"); uiScale: root.uiScale }
                Components.StatusBadge {
                    text: "조치 " + appController.operatorActionLevel
                    kind: appController.operatorActionLevel === "ERR" ? "bad" : (appController.operatorActionLevel === "WARN" ? "warn" : "info")
                    uiScale: root.uiScale
                }
                Rectangle {
                    visible: !root.narrowChrome
                    Layout.preferredWidth: Math.round(root.compactChrome ? 300 * root.uiScale : 520 * root.uiScale)
                    Layout.preferredHeight: Math.round(26 * root.uiScale)
                    radius: 8
                    color: "#f7faff"
                    border.color: "#dbe5f0"
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.rootCauseSummary
                        color: "#35506b"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11 * root.uiScale)
                    }
                }
                Rectangle {
                    visible: !root.compactChrome
                    Layout.preferredWidth: Math.round(390 * root.uiScale)
                    Layout.preferredHeight: Math.round(26 * root.uiScale)
                    radius: 8
                    color: appController.operatorActionLevel === "ERR" ? "#fff1f2" : (appController.operatorActionLevel === "WARN" ? "#fffaf0" : "#f8fafc")
                    border.color: appController.operatorActionLevel === "ERR" ? "#fecdd3" : (appController.operatorActionLevel === "WARN" ? "#f3d7a1" : "#dbe5f0")
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.operatorActionText
                        color: appController.operatorActionLevel === "ERR" ? "#9f1239" : (appController.operatorActionLevel === "WARN" ? "#9a5b00" : "#52606d")
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11 * root.uiScale)
                    }
                }
                Components.SafeButton {
                    text: "최상위 보기"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(96 * root.uiScale)
                    onClicked: root.focusPrimaryIssue()
                }
                Components.SafeButton {
                    text: "◀ 이슈"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(80 * root.uiScale)
                    enabled: appController.primaryIssueSeekAvailable
                    onClicked: root.seekPrimaryIssue(-1)
                }
                Components.SafeButton {
                    text: "이슈 ▶"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(80 * root.uiScale)
                    enabled: appController.primaryIssueSeekAvailable
                    onClicked: root.seekPrimaryIssue(1)
                }
                Rectangle {
                    visible: !root.compactChrome
                    Layout.preferredWidth: Math.round(260 * root.uiScale)
                    Layout.preferredHeight: Math.round(26 * root.uiScale)
                    radius: 8
                    color: "#fffaf0"
                    border.color: "#f3d7a1"
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.replayIssueSummary
                        color: "#9a5b00"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11 * root.uiScale)
                    }
                }
                Rectangle {
                    visible: !root.narrowChrome
                    Layout.preferredWidth: Math.round(180 * root.uiScale)
                    Layout.preferredHeight: Math.round(26 * root.uiScale)
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe5f0"
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: "업타임 " + appController.sessionUptimeText
                        color: "#52606d"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11 * root.uiScale)
                    }
                }
                Rectangle {
                    visible: root.width >= 1400
                    Layout.preferredWidth: Math.round(300 * root.uiScale)
                    Layout.preferredHeight: Math.round(26 * root.uiScale)
                    radius: 8
                    color: "#f7faff"
                    border.color: "#dbe5f0"
                    Components.SafeText {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: appController.operatorRecentSummary
                        color: "#35506b"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11 * root.uiScale)
                    }
                }
                Components.SafeButton { text: "분석 초기화"; uiScale: root.uiScale; maxButtonWidth: Math.round(98 * root.uiScale); onClicked: appController.resetAnalysisContext() }
                Components.SafeButton { text: "필터 초기화"; uiScale: root.uiScale; maxButtonWidth: Math.round(98 * root.uiScale); onClicked: appController.resetAllAnalysisFilters() }
                Components.SafeButton {
                    text: "라이브 복귀"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(98 * root.uiScale)
                    enabled: appController.connected && appController.replayAnalysisHeld
                    onClicked: appController.useLiveAnalysis()
                }
            }

            ScrollBar.horizontal: ScrollBar { }
        }
    }

    header: ToolBar {
        padding: 0
        implicitHeight: Math.round(44 * root.uiScale)
        background: Rectangle {
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#ffffff" }
                GradientStop { position: 1.0; color: "#f7faff" }
            }
            border.color: "#d7e0ea"
        }

        Flickable {
            id: toolbarFlick
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: toolbarContent.width
            contentHeight: height
            interactive: contentWidth > width + 1

            Item {
                id: toolbarContent
                width: toolbarRow.implicitWidth + Math.round(16 * root.uiScale)
                height: toolbarFlick.height

                RowLayout {
                    id: toolbarRow
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(8 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    ComboBox {
                        id: portCombo
                        Layout.preferredWidth: root.narrowChrome ? 124 : 170
                        model: appController.availablePorts
                    }
                    Components.SafeButton { text: "포트 새로고침"; uiScale: root.uiScale; maxButtonWidth: Math.round(112 * root.uiScale); onClicked: appController.refreshPorts() }
                    Components.SafeButton { text: "연결"; uiScale: root.uiScale; maxButtonWidth: Math.round(64 * root.uiScale); onClicked: appController.connectPort(portCombo.currentText) }
                    Components.SafeButton { text: "해제"; uiScale: root.uiScale; maxButtonWidth: Math.round(64 * root.uiScale); onClicked: appController.disconnectPort() }
                    Components.StatusBadge {
                        text: appController.transportModeText
                        kind: appController.transportMode === "typed" ? "ok" : "info"
                        uiScale: root.uiScale
                        maxWidth: Math.round(132 * root.uiScale)
                    }
                    Components.StatusBadge {
                        visible: appController.transportMode === "typed"
                        text: appController.typedCanSummary
                        kind: appController.typedTransportFaultCount > 0 ? "warn" : (appController.typedRecordCount > 0 ? "ok" : "info")
                        uiScale: root.uiScale
                        maxWidth: Math.round(root.compactChrome ? 220 * root.uiScale : 420 * root.uiScale)
                    }
                    Components.StatusBadge {
                        visible: appController.transportMode === "typed" && !root.narrowChrome
                        text: appController.typedEvidenceSummary
                        kind: appController.typedTransportFaultCount > 0 ? "warn" : "info"
                        uiScale: root.uiScale
                        maxWidth: Math.round(root.compactChrome ? 220 * root.uiScale : 420 * root.uiScale)
                    }
                    Components.StatusBadge {
                        text: appController.logRecordingActive ? "LOG REC" : (appController.logPendingSave ? "SAVE PENDING" : "LOG IDLE")
                        kind: appController.logRecordingActive ? "ok" : (appController.logPendingSave ? "warn" : "info")
                        uiScale: root.uiScale
                    }

                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#d7e0ea" }

                    Components.SafeButton { text: "모델 선택"; uiScale: root.uiScale; maxButtonWidth: Math.round(90 * root.uiScale); onClicked: modelDialog.open() }
                    Components.SafeButton { text: "기본 모델"; uiScale: root.uiScale; maxButtonWidth: Math.round(90 * root.uiScale); onClicked: appController.useBundledModel() }
                    Components.SafeButton { text: "모델 해제"; uiScale: root.uiScale; maxButtonWidth: Math.round(90 * root.uiScale); onClicked: appController.clearModel() }
                    Rectangle {
                        visible: !root.narrowChrome
                        Layout.preferredWidth: root.compactChrome ? 220 : 310
                        Layout.fillHeight: true
                        radius: 8
                        color: appController.modelActive ? "#eef6ff" : "#fff7ed"
                        border.color: appController.modelActive ? "#bfd6ff" : "#f2c485"
                        Components.SafeText {
                            anchors.fill: parent
                            anchors.margins: 7
                            text: appController.modelSourceSummary
                            color: appController.modelActive ? "#2457a6" : "#a05a00"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(12 * root.uiScale)
                        }
                        ToolTip.visible: modelHover.hovered
                        ToolTip.text: appController.modelSourceSummary
                        HoverHandler { id: modelHover }
                    }

                    Item { Layout.fillWidth: true }

                    Components.StatusBadge {
                        text: appController.connected ? "연결됨" : "미연결"
                        kind: appController.connected ? "ok" : "bad"
                        uiScale: root.uiScale
                    }
                    ComboBox {
                        id: scaleCombo
                        Layout.preferredWidth: 78
                        model: ["90%", "100%", "110%"]
                        currentIndex: root.uiScaleIndex
                        onActivated: root.uiScaleIndex = currentIndex
                    }
                }
            }

            ScrollBar.horizontal: ScrollBar {
                policy: toolbarFlick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
        }

    }

    FileDialog {
        id: modelDialog
        title: "모델 JSON 선택 (1파일 팩)"
        nameFilters: ["JSON files (*.json)"]
        onAccepted: appController.modelPath = selectedFile.toString()
    }

    FileDialog {
        id: replayDialog
        title: "재생 파일 선택"
        currentFolder: appController.replayOpenDirectory
        nameFilters: ["Replay files (*.bin capture.stream)", "Legacy BIN (*.bin)", "Typed stream (capture.stream)", "All files (*)"]
        onAccepted: appController.loadReplay(selectedFile.toString())
    }

    FolderDialog {
        id: typedReplayFolderDialog
        title: "Typed 재생 세션 폴더"
        currentFolder: appController.replayOpenDirectory
        onAccepted: appController.loadReplay(selectedFolder.toString())
    }

    FolderDialog {
        id: logTargetFolderDialog
        title: "로그 저장 폴더 선택"
        currentFolder: appController.logTargetDirectory
        onAccepted: appController.logTargetDirectory = selectedFolder.toString()
    }

    FileDialog {
        id: logSaveDialog
        title: "로그 저장"
        fileMode: FileDialog.SaveFile
        currentFile: appController.suggestedLogSavePath
        nameFilters: ["BIN files (*.bin)"]
        onAccepted: appController.finalizePendingLogSave(selectedFile.toString())
    }

    Connections {
        target: appController
        function onLogStateChanged() {
            if (appController.logPendingSave && !appController.logRecordingActive && !appController.logStopping && !appController.logSaving && !logSaveDialog.visible)
                logSaveDialog.open()
        }
    }

    FileDialog {
        id: snapshotDialog
        title: "분석 스냅샷 저장"
        fileMode: FileDialog.SaveFile
        currentFile: appController.suggestedSnapshotPath
        nameFilters: ["JSON files (*.json)"]
        onAccepted: appController.exportAnalysisSnapshot(selectedFile.toString())
    }

    Shortcut {
        sequences: [StandardKey.Open]
        enabled: !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: {
            if ((!root.workspaceMode && tabs.currentIndex === 2) || (root.workspaceMode && panelVisible(2))) replayDialog.open()
            else modelDialog.open()
        }
    }
    Shortcut {
        sequence: "Ctrl+R"
        enabled: !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: replayDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: !snapshotDialog.visible && !modelDialog.visible && !replayDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: snapshotDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: !logSaveDialog.visible && !modelDialog.visible && !replayDialog.visible && !snapshotDialog.visible && !logTargetFolderDialog.visible
        onActivated: {
            if (appController.logRecordingActive || appController.logPendingSave) appController.stopLog()
            else if (appController.connected && !appController.logStopping && !appController.logSaving) appController.startLog()
        }
    }
    Shortcut {
        sequence: "Space"
        enabled: appController.replayLoaded && !panelVisible(9) && !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: {
            if (appController.replayPlaying) appController.pauseReplay()
            else appController.playReplay(appController.replaySpeed > 0 ? appController.replaySpeed : 1.0)
        }
    }
    Shortcut {
        sequence: "Left"
        enabled: appController.replayLoaded && !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: appController.stepReplay(-1)
    }
    Shortcut {
        sequence: "Right"
        enabled: appController.replayLoaded && !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: appController.stepReplay(1)
    }
    Shortcut {
        sequence: "F6"
        enabled: !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: root.focusPrimaryIssue()
    }
    Shortcut {
        sequence: "F7"
        enabled: appController.primaryIssueSeekAvailable && !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: root.seekPrimaryIssue(-1)
    }
    Shortcut {
        sequence: "F8"
        enabled: appController.primaryIssueSeekAvailable && !replayDialog.visible && !modelDialog.visible && !snapshotDialog.visible && !logSaveDialog.visible && !logTargetFolderDialog.visible
        onActivated: root.seekPrimaryIssue(1)
    }

    ListModel {
        id: workspacePanels
        ListElement { key: "overview"; title: "개요"; opened: true }
        ListElement { key: "live"; title: "라이브"; opened: true }
        ListElement { key: "replay"; title: "재생"; opened: false }
        ListElement { key: "timing"; title: "주기"; opened: false }
        ListElement { key: "value"; title: "값"; opened: false }
        ListElement { key: "graph"; title: "그래프"; opened: false }
        ListElement { key: "graph_overview"; title: "전체그래프"; opened: false }
        ListElement { key: "alarm"; title: "경보"; opened: false }
        ListElement { key: "settings"; title: "설정"; opened: false }
        ListElement { key: "control"; title: "제어"; opened: false }
    }

    Component {
        id: overviewPageComponent
        Pages.OverviewPage {
            uiScale: root.uiScale
            onOpenPanel: function(key) { root.openPanelByKey(key) }
            onFocusIssue: function(key, idText) { root.focusIssue(key, idText) }
            onOpenReplay: replayDialog.open()
            onExportSnapshot: snapshotDialog.open()
        }
    }

    Component {
        id: livePageComponent
        Pages.LivePage {
            uiScale: root.uiScale
            onStartLog: appController.startLog()
            onStopLog: appController.stopLog()
            onSavePendingLog: logSaveDialog.open()
            onDiscardPendingLog: appController.discardPendingLog()
            onChooseLogFolder: logTargetFolderDialog.open()
        }
    }

    Component {
        id: replayPageComponent
        Pages.ReplayPage {
            uiScale: root.uiScale
            onOpenReplay: replayDialog.open()
            onOpenTypedReplay: typedReplayFolderDialog.open()
            onPlayReplay: function(speed) { appController.playReplay(speed) }
            onPauseReplay: appController.pauseReplay()
            onStopReplay: appController.stopReplay()
            onSetReplayLoop: function(enabled) { appController.setReplayLoop(enabled) }
            onSeekReplay: function(progress) { appController.seekReplay(progress) }
            onStepReplay: function(delta) { appController.stepReplay(delta) }
        }
    }

    Component { id: timingPageComponent; Pages.TimingPage { uiScale: root.uiScale } }
    Component { id: valuePageComponent; Pages.ValuePage { uiScale: root.uiScale; uiFontFamily: root.uiFontFamily; monoFontFamily: root.monoFontFamily } }
    Component { id: graphPageComponent; Pages.GraphPage { uiScale: root.uiScale } }
    Component { id: graphOverviewPageComponent; Pages.GraphOverviewPage { uiScale: root.uiScale } }
    Component { id: alarmPageComponent; Pages.AlarmPage { uiScale: root.uiScale } }
    Component { id: controlPageComponent; Pages.ControlPage { uiScale: root.uiScale; monoFontFamily: root.monoFontFamily } }
    Component {
        id: settingsPageComponent
        Pages.SettingsPage {
            uiScale: root.uiScale
            onExportSnapshot: snapshotDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.round(4 * root.uiScale)
        spacing: Math.round(4 * root.uiScale)

        Frame {
            Layout.fillWidth: true
            padding: 0
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Math.round(4 * root.uiScale)
                spacing: Math.round(3 * root.uiScale)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Math.round(6 * root.uiScale)
                    TabBar {
                        id: tabs
                        Layout.fillWidth: true
                        currentIndex: 0
                        implicitHeight: Math.round(28 * root.uiScale)
                        onCurrentIndexChanged: root.syncAnalysisVisibility()
                        Components.SafeTabButton { text: "개요"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(0) }
                        Components.SafeTabButton { text: "라이브"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(1) }
                        Components.SafeTabButton { text: "재생"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(2) }
                        Components.SafeTabButton { text: "주기"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(3) }
                        Components.SafeTabButton { text: "값"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(4) }
                        Components.SafeTabButton { text: "그래프"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(5) }
                        Components.SafeTabButton { text: "전체그래프"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(6) }
                        Components.SafeTabButton { text: "경보"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(7) }
                        Components.SafeTabButton { text: "설정"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(8) }
                        Components.SafeTabButton { text: "제어"; uiScale: root.uiScale; baseWidth: root.tabButtonWidth; onClicked: if (root.workspaceMode) root.enablePanel(9) }
                    }
                    Switch {
                        id: workspaceSwitch
                        checked: root.workspaceMode
                        text: checked ? "분할" : "단일"
                        onToggled: {
                            root.workspaceMode = checked
                            root.syncAnalysisVisibility()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.workspaceMode
                    spacing: Math.round(6 * root.uiScale)
                    Components.SafeText { text: "패널"; color: "#5b6673"; uiScale: root.uiScale; basePixelSize: Math.round(11 * root.uiScale); Layout.preferredWidth: Math.round(40 * root.uiScale) }
                    Repeater {
                        model: workspacePanels
                        delegate: Components.SafeButton {
                            required property int index
                            required property string title
                            required property bool opened
                            text: (opened ? "● " : "○ ") + title
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(126 * root.uiScale)
                            checkable: true
                            checked: opened
                            onClicked: root.togglePanel(index)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Components.SafeButton { text: "개요+라이브+경보"; uiScale: root.uiScale; maxButtonWidth: Math.round(136 * root.uiScale); onClicked: root.setWorkspacePreset(0, 1, 7) }
                    Components.SafeButton { text: "라이브+주기+값"; uiScale: root.uiScale; maxButtonWidth: Math.round(136 * root.uiScale); onClicked: root.setWorkspacePreset(1, 3, 4) }
                    Components.SafeButton { text: "라이브+값+그래프"; uiScale: root.uiScale; maxButtonWidth: Math.round(136 * root.uiScale); onClicked: root.setWorkspacePreset(1, 4, 5) }
                    Components.SafeButton { text: "현재 탭 열기"; uiScale: root.uiScale; maxButtonWidth: Math.round(112 * root.uiScale); onClicked: root.enablePanel(tabs.currentIndex) }
                }
            }
        }

        Frame {
            id: compactReplayBar
            Layout.fillWidth: true
            visible: appController.replayLoaded || (!root.workspaceMode && tabs.currentIndex === 2) || (root.workspaceMode && panelVisible(2))
            padding: 0
            implicitHeight: Math.round(42 * root.uiScale)
            Layout.minimumHeight: implicitHeight
            background: Rectangle { color: "#fffaf3"; radius: 8; border.color: "#ead9b5" }

            property real pendingSeekValue: 0.0
            property bool pendingSeekInitialized: false
            property bool seekCommitPending: false

            Connections {
                target: appController
                function onReplayStateChanged() {
                    if (compactReplaySlider.pressed)
                        return
                    compactReplayBar.pendingSeekValue = appController.replayRebuilding ? appController.replayTargetProgress : appController.replayProgress
                    compactReplayBar.pendingSeekInitialized = true
                    compactReplayBar.seekCommitPending = false
                }
            }

            Component.onCompleted: {
                pendingSeekValue = appController.replayProgress
                pendingSeekInitialized = true
            }

            RowLayout {
                id: compactReplayContent
                anchors.fill: parent
                anchors.margins: Math.round(6 * root.uiScale)
                spacing: Math.round(6 * root.uiScale)

                Components.SafeButton {
                    text: "BIN"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(54 * root.uiScale)
                    onClicked: replayDialog.open()
                }
                Components.SafeButton {
                    text: "Typed"
                    uiScale: root.uiScale
                    maxButtonWidth: Math.round(66 * root.uiScale)
                    onClicked: typedReplayFolderDialog.open()
                }
                Components.StatusBadge {
                    text: appController.replayLoaded ? (appController.replayPlaying ? "재생" : (appController.replayRebuilding ? "준비" : "대기")) : "재생 없음"
                    kind: appController.replayPlaying ? "warn" : (appController.replayLoaded ? "info" : "ok")
                    uiScale: Math.max(0.86, root.uiScale * 0.86)
                    maxWidth: Math.round(88 * root.uiScale)
                }
                Components.SafeButton { text: "◀"; uiScale: root.uiScale; minButtonWidth: Math.round(34 * root.uiScale); maxButtonWidth: Math.round(38 * root.uiScale); enabled: appController.replayLoaded; onClicked: appController.stepReplay(-1) }
                Components.SafeButton { text: appController.replayPlaying ? "Ⅱ" : "▶"; uiScale: root.uiScale; minButtonWidth: Math.round(34 * root.uiScale); maxButtonWidth: Math.round(38 * root.uiScale); enabled: appController.replayLoaded; onClicked: { if (appController.replayPlaying) appController.pauseReplay(); else appController.playReplay(appController.replaySpeed > 0 ? appController.replaySpeed : 1.0) } }
                Components.SafeButton { text: "■"; uiScale: root.uiScale; minButtonWidth: Math.round(34 * root.uiScale); maxButtonWidth: Math.round(38 * root.uiScale); enabled: appController.replayLoaded; onClicked: appController.stopReplay() }
                Components.SafeButton { text: "▶"; uiScale: root.uiScale; minButtonWidth: Math.round(34 * root.uiScale); maxButtonWidth: Math.round(38 * root.uiScale); enabled: appController.replayLoaded; onClicked: appController.stepReplay(1) }
                ComboBox {
                    Layout.preferredWidth: Math.round(76 * root.uiScale)
                    enabled: appController.replayLoaded
                    model: ["0.5x", "1x", "2x", "4x", "8x"]
                    currentIndex: appController.replaySpeed >= 6.0 ? 4 : (appController.replaySpeed >= 3.0 ? 3 : (appController.replaySpeed >= 1.75 ? 2 : (appController.replaySpeed <= 0.75 ? 0 : 1)))
                    onActivated: {
                        if (currentIndex === 0)
                            appController.playReplay(0.5)
                        else if (currentIndex === 1)
                            appController.playReplay(1.0)
                        else if (currentIndex === 2)
                            appController.playReplay(2.0)
                        else if (currentIndex === 3)
                            appController.playReplay(4.0)
                        else
                            appController.playReplay(8.0)
                    }
                }

                Components.SafeText {
                    text: appController.replayLoaded ? (appController.replayCurrentTimeText + " / " + appController.replayDurationText) : "재생 파일 미선택"
                    color: "#35506b"
                    uiScale: root.uiScale
                    basePixelSize: Math.round(10.4 * root.uiScale)
                    Layout.preferredWidth: Math.round(root.compactChrome ? 116 * root.uiScale : 190 * root.uiScale)
                }
                Components.PreciseSlider {
                    id: compactReplaySlider
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    enabled: appController.replayLoaded
                    liveUpdate: false
                    releaseHoldMs: 220
                    value: (compactReplaySlider.visualHoldActive || compactReplayBar.seekCommitPending || appController.replayRebuilding || !compactReplayBar.pendingSeekInitialized)
                           ? compactReplayBar.pendingSeekValue
                           : appController.replayProgress
                    onCommitted: function(nextValue) {
                        if (!appController.replayLoaded)
                            return
                        compactReplayBar.pendingSeekValue = nextValue
                        compactReplayBar.seekCommitPending = true
                        appController.commitSeekReplay(nextValue)
                    }
                }
                Components.SafeText {
                    Layout.preferredWidth: Math.round(50 * root.uiScale)
                    horizontalAlignment: Text.AlignRight
                    color: (compactReplaySlider.pressed || compactReplayBar.seekCommitPending || appController.replayRebuilding) ? "#0f4c81" : "#52606d"
                    uiScale: root.uiScale
                    basePixelSize: Math.round(10.4 * root.uiScale)
                    font.bold: true
                    text: Math.round((compactReplaySlider.visualHoldActive
                                      ? compactReplaySlider.visualValue
                                      : ((compactReplayBar.seekCommitPending || appController.replayRebuilding)
                                            ? (compactReplayBar.seekCommitPending ? compactReplayBar.pendingSeekValue : appController.replayTargetProgress)
                                            : appController.replayProgress)) * 100) + "%"
                }
                Components.SafeButton { text: "최상위"; uiScale: root.uiScale; maxButtonWidth: Math.round(78 * root.uiScale); onClicked: root.focusPrimaryIssue() }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            visible: !root.workspaceMode

            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 0; pageComponent: overviewPageComponent; minPageWidth: root.minimumPageWidthForKey("overview"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 1; pageComponent: livePageComponent; minPageWidth: root.minimumPageWidthForKey("live"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 2; pageComponent: replayPageComponent; minPageWidth: root.minimumPageWidthForKey("replay"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 3; pageComponent: timingPageComponent; minPageWidth: root.minimumPageWidthForKey("timing"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 4; pageComponent: valuePageComponent; minPageWidth: root.minimumPageWidthForKey("value"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 5; pageComponent: graphPageComponent; minPageWidth: root.minimumPageWidthForKey("graph"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 6; pageComponent: graphOverviewPageComponent; minPageWidth: root.minimumPageWidthForKey("graph_overview"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 7; pageComponent: alarmPageComponent; minPageWidth: root.minimumPageWidthForKey("alarm"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 8; pageComponent: settingsPageComponent; minPageWidth: root.minimumPageWidthForKey("settings"); uiScale: root.uiScale }
            Components.PageViewport { active: !root.workspaceMode && tabs.currentIndex === 9; pageComponent: controlPageComponent; minPageWidth: root.minimumPageWidthForKey("control"); uiScale: root.uiScale }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.workspaceMode
            padding: 0
            background: Rectangle {
                radius: 14
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#f6f9fd" }
                    GradientStop { position: 1.0; color: "#edf3fa" }
                }
                border.color: "#d7e0ea"
            }

            Item {
                anchors.fill: parent

                SplitView {
                    anchors.fill: parent
                    anchors.margins: Math.round(8 * root.uiScale)
                    visible: root.visiblePanelCount() > 0
                    orientation: Qt.Horizontal

                    Repeater {
                        model: workspacePanels
                        delegate: Frame {
                            required property int index
                            required property string key
                            required property string title
                            required property bool opened
                            visible: opened
                            enabled: visible
                            implicitWidth: visible ? 320 : 0
                            SplitView.fillWidth: true
                            SplitView.fillHeight: true
                            padding: 0
                            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Math.round(8 * root.uiScale)
                                spacing: Math.round(8 * root.uiScale)
                                RowLayout {
                                    Layout.fillWidth: true
                                    Components.SafeText { text: title; uiScale: root.uiScale; basePixelSize: Math.round(13.5 * root.uiScale); font.bold: true; color: "#243447"; Layout.fillWidth: true }
                                    Components.StatusBadge {
                                        text: tabs.currentIndex === index ? "현재" : "열림"
                                        kind: tabs.currentIndex === index ? "info" : "ok"
                                        uiScale: Math.max(0.9, root.uiScale * 0.95)
                                    }
                                    Components.SafeButton { text: "포커스"; uiScale: root.uiScale; maxButtonWidth: Math.round(76 * root.uiScale); onClicked: tabs.currentIndex = index }
                                    Components.SafeButton { text: "닫기"; uiScale: root.uiScale; maxButtonWidth: Math.round(64 * root.uiScale); onClicked: { workspacePanels.setProperty(index, "opened", false); root.syncAnalysisVisibility() } }
                                }
                                Components.PageViewport {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    active: opened
                                    pageComponent: root.componentForKey(key)
                                    minPageWidth: root.minimumPageWidthForKey(key)
                                    uiScale: root.uiScale
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 18
                    visible: root.visiblePanelCount() === 0
                    radius: 8
                    color: "#ffffff"
                    border.color: "#d7e0ea"
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 10
                        Components.SafeText { text: "열린 패널이 없습니다"; color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(22 * root.uiScale); font.bold: true; Layout.preferredWidth: Math.round(360 * root.uiScale); horizontalAlignment: Text.AlignHCenter }
                        Components.SafeText { text: "상단 패널 버튼이나 프리셋 버튼으로 작업 화면을 다시 열 수 있습니다."; color: "#5b6673"; uiScale: root.uiScale; basePixelSize: Math.round(11 * root.uiScale); Layout.preferredWidth: Math.round(520 * root.uiScale); horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }
        }
    }
}
