import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    Component.onCompleted: Qt.callLater(scrollToFocusedRow)
    property real uiScale: 1.0
    property var expandedState: ({})
    property real timeColWidth: 90
    property real stateColWidth: 84
    property real idColWidth: 78
    property real nameColWidth: 150
    property real sourceColWidth: 68
    property real metricColWidth: 86
    property real countColWidth: 54
    property real gaugeColWidth: 120
    property real messageColWidth: 0
    property var dialogHistory: []
    property string dialogTitle: ""

    Timer {
        id: holdReleaseTimer
        interval: 260
        repeat: false
        onTriggered: appController.setAlarmViewHeld(false)
    }

    function sortMark(mode) {
        if (appController.alarmSortMode !== mode)
            return ""
        return appController.alarmSortDescending ? " ▼" : " ▲"
    }

    function clampWidth(v, minV, maxV) {
        return Math.max(minV, Math.min(maxV, v))
    }

    function kindFromLevel(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function applyWheel(view, event, holdSetter, holdTimer) {
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
        if (holdSetter)
            holdSetter(true)
        if (holdTimer)
            holdTimer.restart()
    }

    function scrollToFocusedRow() {
        const focusId = (appController.alarmFilterId !== "") ? appController.alarmFilterId : ""
        if (focusId === "") return
        const rowIndex = appController.alarmModel.findIndex("idText", focusId)
        if (rowIndex >= 0) alarmList.positionViewAtIndex(rowIndex, ListView.Visible)
    }

    function openHistory(title, items) {
        dialogTitle = title
        dialogHistory = items ? items.slice(0) : []
        historyDialog.open()
    }


    function clearFilters() {
        appController.setAlarmFilterTime("")
        appController.setAlarmFilterSeverity("")
        appController.setAlarmFilterId("")
        appController.setAlarmFilterName("")
        appController.setAlarmFilterSource("")
        appController.setAlarmFilterMessage("")
        appController.setAlarmFilterText("")
    }
    Connections {
        target: appController
        function onFiltersChanged() { Qt.callLater(scrollToFocusedRow) }
        function onSelectedValueIdChanged() { Qt.callLater(scrollToFocusedRow) }
    }



    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Frame {
            Layout.fillWidth: true
            implicitHeight: alarmHeaderFlow.implicitHeight + Math.round(16 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            Components.FlowToolbar {
                id: alarmHeaderFlow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                uiScale: uiScale
                Components.SafeText { text: "값/버스 경보"; width: Math.round(92 * uiScale); uiScale: uiScale; basePixelSize: Math.round(11 * uiScale); font.bold: true; color: "#243447" }
                Components.SafeText { text: "주기 이슈는 주기 탭"; width: Math.round(126 * uiScale); uiScale: uiScale; basePixelSize: Math.round(10.6 * uiScale); color: "#5b6673" }
                Components.StatusBadge { text: "입력 " + appController.sourceStateLevel; kind: kindFromLevel(appController.sourceStateLevel); uiScale: uiScale }
                Components.StatusBadge { text: "분석 " + appController.analysisModeLevel; kind: kindFromLevel(appController.analysisModeLevel); uiScale: uiScale }
                Components.SafeText { text: "활성 " + appController.activeAlarmCount + " / 누적 " + appController.alarmCumulativeCount; width: Math.round(112 * uiScale); uiScale: uiScale; basePixelSize: Math.round(10.6 * uiScale); color: "#5b6673" }
                Components.SafeButton { text: "필터 초기화"; uiScale: uiScale; maxButtonWidth: Math.round(96 * uiScale); onClicked: clearFilters() }
                Components.SafeButton { text: "프레임 지우기"; uiScale: uiScale; maxButtonWidth: Math.round(104 * uiScale); onClicked: appController.clearFrames() }
                Components.StatusBadge { text: "건수 " + alarmList.count; kind: "info"; uiScale: uiScale; maxWidth: Math.round(86 * uiScale) }
            }
        }

        Frame {
            Layout.fillWidth: true
            implicitHeight: alarmStatusColumn.implicitHeight + Math.round(12 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            ColumnLayout {
                id: alarmStatusColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Components.FlowToolbar {
                    Layout.fillWidth: true
                    uiScale: uiScale
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: kindFromLevel(appController.systemLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "경보 " + appController.alarmLevel; kind: kindFromLevel(appController.alarmLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "버스 " + appController.busHealthLevel; kind: kindFromLevel(appController.busHealthLevel); uiScale: uiScale }
                    Components.SafeButton { text: "최상위 경보"; uiScale: uiScale; maxButtonWidth: Math.round(98 * uiScale); enabled: appController.topAlarmId !== ""; onClicked: appController.focusAlarmId(appController.topAlarmId) }
                    Components.SafeText {
                        text: appController.topAlarmId !== "" ? (appController.topAlarmId + " · " + appController.primaryIssueSummary) : appController.rootCauseSummary
                        color: "#607080"
                        uiScale: uiScale
                        basePixelSize: Math.round(10.6 * uiScale)
                        width: Math.max(Math.round(260 * uiScale), Math.min(Math.round(520 * uiScale), parent.width - Math.round(380 * uiScale)))
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe5f0"
                    implicitHeight: Math.round(32 * uiScale)
                    Label { anchors.fill: parent; anchors.margins: 6; text: appController.operatorActionText; color: "#35506b"; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Rectangle {
                    Layout.fillWidth: true
                    height: Math.round(34 * uiScale)
                    radius: 8
                    color: "#eef3f8"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: timeColWidth
                            title: "시간"
                            sortIndicator: sortMark("time")
                            filterText: appController.alarmFilterTime
                            filterPlaceholder: "시간 필터"
                            onSortRequested: appController.toggleAlarmSort("time")
                            onSortAscendingRequested: { appController.setAlarmSortMode("time"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("time"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterTime(text) }
                            onFilterCleared: appController.setAlarmFilterTime("")
                            onResizeRequested: function(delta) { timeColWidth = clampWidth(timeColWidth + delta, 76, 160) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: stateColWidth
                            title: "상태"
                            sortIndicator: sortMark("severity")
                            filterText: appController.alarmFilterSeverity
                            filterPlaceholder: "상태 필터"
                            onSortRequested: appController.toggleAlarmSort("severity")
                            onSortAscendingRequested: { appController.setAlarmSortMode("severity"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("severity"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterSeverity(text) }
                            onFilterCleared: appController.setAlarmFilterSeverity("")
                            onResizeRequested: function(delta) { stateColWidth = clampWidth(stateColWidth + delta, 72, 130) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: idColWidth
                            title: "ID"
                            sortIndicator: sortMark("id")
                            filterText: appController.alarmFilterId
                            filterPlaceholder: "ID 필터"
                            onSortRequested: appController.toggleAlarmSort("id")
                            onSortAscendingRequested: { appController.setAlarmSortMode("id"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("id"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterId(text) }
                            onFilterCleared: appController.setAlarmFilterId("")
                            onResizeRequested: function(delta) { idColWidth = clampWidth(idColWidth + delta, 68, 150) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: nameColWidth
                            title: "이름"
                            sortIndicator: sortMark("name")
                            filterText: appController.alarmFilterName
                            filterPlaceholder: "이름 필터"
                            onSortRequested: appController.toggleAlarmSort("name")
                            onSortAscendingRequested: { appController.setAlarmSortMode("name"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("name"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterName(text) }
                            onFilterCleared: appController.setAlarmFilterName("")
                            onResizeRequested: function(delta) { nameColWidth = clampWidth(nameColWidth + delta, 100, 320) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: sourceColWidth
                            title: "소스"
                            sortIndicator: sortMark("source")
                            filterText: appController.alarmFilterSource
                            filterPlaceholder: "소스 필터"
                            onSortRequested: appController.toggleAlarmSort("source")
                            onSortAscendingRequested: { appController.setAlarmSortMode("source"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("source"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterSource(text) }
                            onFilterCleared: appController.setAlarmFilterSource("")
                            onResizeRequested: function(delta) { sourceColWidth = clampWidth(sourceColWidth + delta, 56, 120) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: metricColWidth
                            title: "지표"
                            sortIndicator: sortMark("message")
                            filterText: appController.alarmFilterText
                            filterPlaceholder: "검색"
                            onSortRequested: appController.toggleAlarmSort("message")
                            onSortAscendingRequested: { appController.setAlarmSortMode("message"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("message"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterText(text) }
                            onFilterCleared: appController.setAlarmFilterText("")
                            onResizeRequested: function(delta) { metricColWidth = clampWidth(metricColWidth + delta, 72, 130) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: countColWidth
                            title: "횟수"
                            sortIndicator: ""
                            filterText: ""
                            filterPlaceholder: ""
                            onSortRequested: function() {}
                            onSortAscendingRequested: function() {}
                            onSortDescendingRequested: function() {}
                            onFilterApplied: function(text) {}
                            onFilterCleared: function() {}
                            onResizeRequested: function(delta) { countColWidth = clampWidth(countColWidth + delta, 46, 90) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: gaugeColWidth
                            title: "게이지"
                            sortIndicator: ""
                            filterText: ""
                            filterPlaceholder: ""
                            onSortRequested: function() {}
                            onSortAscendingRequested: function() {}
                            onSortDescendingRequested: function() {}
                            onFilterApplied: function(text) {}
                            onFilterCleared: function() {}
                            onResizeRequested: function(delta) { gaugeColWidth = clampWidth(gaugeColWidth + delta, 90, 180) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.fillWidth: true
                            title: "메시지 / 이력"
                            sortIndicator: sortMark("message")
                            filterText: appController.alarmFilterMessage
                            filterPlaceholder: "메시지 필터"
                            resizable: false
                            onSortRequested: appController.toggleAlarmSort("message")
                            onSortAscendingRequested: { appController.setAlarmSortMode("message"); appController.setAlarmSortDescending(false) }
                            onSortDescendingRequested: { appController.setAlarmSortMode("message"); appController.setAlarmSortDescending(true) }
                            onFilterApplied: function(text) { appController.setAlarmFilterMessage(text) }
                            onFilterCleared: appController.setAlarmFilterMessage("")
                        }
                    }
                }

                ListView {
                    id: alarmList
                    onMovementStarted: { appController.setAlarmViewHeld(true); holdReleaseTimer.stop() }
                    onMovementEnded: holdReleaseTimer.restart()
                    onFlickStarted: { appController.setAlarmViewHeld(true); holdReleaseTimer.stop() }
                    onFlickEnded: holdReleaseTimer.restart()
                    onDraggingChanged: { if (dragging) { appController.setAlarmViewHeld(true); holdReleaseTimer.stop() } else holdReleaseTimer.restart() }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: appController.alarmModel
                    reuseItems: true
                    cacheBuffer: 20
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                    boundsBehavior: Flickable.StopAtBounds
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { applyWheel(alarmList, event, appController.setAlarmViewHeld, holdReleaseTimer) }
                    }

                    Components.EmptyState {
                        parent: alarmList
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                        visible: alarmList.count === 0
                        z: 10
                        title: "표시할 경보 항목 없음"
                        message: appController.modelActive ? "현재 필터 조건에 맞는 값/버스 경보가 없습니다." : "모델을 적용하거나 프레임을 수신하면 값/버스 경보가 이 표면에 표시됩니다."
                        badgeText: appController.modelActive ? "정상/필터" : "대기"
                        kind: appController.modelActive ? "ok" : "info"
                        uiScale: uiScale
                    }

                    delegate: MouseArea {
                        required property string key
                        required property string timeText
                        required property string severity
                        required property string severityColor
                        required property string idText
                        required property string name
                        required property string source
                        required property string categoryLabel
                        required property string metricText
                        required property int count
                        required property double gaugePct
                        required property string message
                        required property bool active
                        required property var history
                        property bool rowActive: active
                        width: alarmList.width
                        height: rowBox.implicitHeight
                        property bool expanded: expandedState[key] !== undefined ? expandedState[key] : false
                        onClicked: {
                            expanded = !expanded
                            expandedState[key] = expanded
                        }

                        Rectangle {
                            id: rowBox
                            width: parent.width
                            implicitHeight: bodyColumn.implicitHeight + 10
                            radius: 8
                            color: appController.alarmFilterId !== "" && appController.alarmFilterId === idText ? "#eef6ff" : (rowActive ? "#fff9f2" : "#fbfdff")
                            border.color: appController.alarmFilterId !== "" && appController.alarmFilterId === idText ? "#93c5fd" : (rowActive ? "#ffd2a8" : "#d7e0ea")

                            ColumnLayout {
                                id: bodyColumn
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 5

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label { Layout.preferredWidth: timeColWidth; text: timeText; color: "#243447"; font.pixelSize: Math.round(12 * uiScale) }
                                    Rectangle {
                                        Layout.preferredWidth: stateColWidth
                                        Layout.preferredHeight: Math.round(22 * uiScale)
                                        radius: 6
                                        color: severityColor
                                        Label { anchors.centerIn: parent; text: severity; color: "white"; font.bold: true; font.pixelSize: Math.round(11 * uiScale) }
                                    }
                                    Label { Layout.preferredWidth: idColWidth; text: idText; color: "#243447"; font.bold: true; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: nameColWidth; text: name; color: "#243447"; elide: Text.ElideRight; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: sourceColWidth; text: categoryLabel + " / " + source; color: "#52606d"; elide: Text.ElideRight; font.pixelSize: Math.round(11 * uiScale) }
                                    Label { Layout.preferredWidth: metricColWidth; text: metricText; color: "#7c3aed"; font.bold: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: Math.round(12 * uiScale) }
                                    Rectangle {
                                        Layout.preferredWidth: countColWidth
                                        Layout.preferredHeight: Math.round(20 * uiScale)
                                        radius: 6
                                        color: rowActive ? "#fee2e2" : "#e5eef8"
                                        Label { anchors.centerIn: parent; text: "×" + count; color: rowActive ? "#b91c1c" : "#475569"; font.bold: true; font.pixelSize: Math.round(11 * uiScale) }
                                    }
                                    Item {
                                        Layout.preferredWidth: gaugeColWidth
                                        Layout.preferredHeight: Math.round(18 * uiScale)
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 6
                                            color: "#edf2f7"
                                            border.color: "#d7e0ea"
                                        }
                                        Rectangle {
                                            width: Math.max(4, parent.width * Math.min(1, Math.max(0, gaugePct / 100.0)))
                                            height: parent.height
                                            radius: 6
                                            color: rowActive ? (severity === "ERR" ? "#ef4444" : "#f59e0b") : "#94a3b8"
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: message
                                        color: rowActive ? "#7c2d12" : "#52606d"
                                        elide: Text.ElideRight
                                        font.pixelSize: Math.round(11 * uiScale)
                                    }
                                    Label {
                                        text: expanded ? "▾" : "▸"
                                        color: "#64748b"
                                        font.pixelSize: Math.round(13 * uiScale)
                                    }
                                }

                                Loader {
                                    Layout.fillWidth: true
                                    active: expanded
                                    visible: active
                                    sourceComponent: Component {
                                        Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: historyColumn.implicitHeight + 8
                                            radius: 6
                                            color: "#f8fafc"
                                            border.color: "#e2e8f0"

                                            ColumnLayout {
                                                id: historyColumn
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 4
                                                Label { text: categoryLabel + " · 현재 유지 이력"; color: "#475569"; font.bold: true; font.pixelSize: Math.round(11 * uiScale) }
                                                Repeater {
                                                    model: (history && history.length > 8) ? history.slice(0, 8) : history
                                                    delegate: Label {
                                                        required property string modelData
                                                        Layout.fillWidth: true
                                                        text: modelData
                                                        wrapMode: Text.WordWrap
                                                        color: "#52606d"
                                                        font.pixelSize: Math.round(10 * uiScale)
                                                    }
                                                }
                                                Label {
                                                    visible: history && history.length > 8
                                                    text: "+" + (history.length - 8) + "건 더 있음"
                                                    color: "#2563eb"
                                                    font.pixelSize: Math.round(10 * uiScale)
                                                    font.bold: true
                                                    MouseArea { anchors.fill: parent; onClicked: openHistory(idText + " · " + name, history) }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: historyDialog
        modal: true
        width: Math.min(parent ? parent.width * 0.78 : 960, 960)
        height: Math.min(parent ? parent.height * 0.78 : 680, 680)
        title: dialogTitle
        standardButtons: Dialog.Close

        contentItem: Frame {
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            ListView {
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                model: dialogHistory
                spacing: 4
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: "#334155"
                    font.pixelSize: Math.round(11 * uiScale)
                }
            }
        }
    }

}
