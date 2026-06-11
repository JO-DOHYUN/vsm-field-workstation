import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    Component.onCompleted: Qt.callLater(scrollToFocusedRow)
    property real uiScale: 1.0
    property var timingExpandedState: ({})
    property real stateColWidth: 88
    property real idColWidth: 92
    property real nameColWidth: 190
    property real expectedColWidth: 96
    property real gapColWidth: 96
    property real ageColWidth: 96
    property real sourceColWidth: 84
    property real metricColWidth: 82
    property real gaugeColWidth: 120
    property real reasonColWidth: 260

    Timer {
        id: holdReleaseTimer
        interval: 260
        repeat: false
        onTriggered: appController.setTimingViewHeld(false)
    }

    function sortMark(mode) {
        if (appController.timingSortMode !== mode)
            return ""
        return appController.timingSortDescending ? " ▼" : " ▲"
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
        const focusId = (appController.timingFilterId !== "") ? appController.timingFilterId : ""
        if (focusId === "") return
        const rowIndex = appController.timingModel.findIndex("idText", focusId)
        if (rowIndex >= 0) timingList.positionViewAtIndex(rowIndex, ListView.Visible)
    }


    function clearFilters() {
        appController.setTimingFilterSeverity("")
        appController.setTimingFilterId("")
        appController.setTimingFilterName("")
        appController.setTimingFilterExpected("")
        appController.setTimingFilterGap("")
        appController.setTimingFilterAge("")
        appController.setTimingFilterSource("")
        appController.setTimingFilterReason("")
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
            implicitHeight: timingHeaderFlow.implicitHeight + Math.round(16 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            Components.FlowToolbar {
                id: timingHeaderFlow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                uiScale: uiScale
                Components.SafeText { text: "모델 " + appController.rulesCount + "건"; width: Math.round(76 * uiScale); uiScale: uiScale; basePixelSize: Math.round(11 * uiScale); font.bold: true; color: "#243447" }
                Components.SafeText { text: appController.rulesLoaded ? "모델 기준 비교" : "관찰 모드"; width: Math.round(86 * uiScale); uiScale: uiScale; basePixelSize: Math.round(10.6 * uiScale); color: "#5b6673" }
                Components.StatusBadge { text: "입력 " + appController.sourceStateLevel; kind: kindFromLevel(appController.sourceStateLevel); uiScale: uiScale }
                Components.StatusBadge { text: "분석 " + appController.analysisModeLevel; kind: kindFromLevel(appController.analysisModeLevel); uiScale: uiScale }
                Components.SafeText { text: "활성 " + appController.timingIssueCount + " / 누적 " + appController.timingCumulativeCount; width: Math.round(112 * uiScale); uiScale: uiScale; basePixelSize: Math.round(10.6 * uiScale); color: "#5b6673" }
                Components.SafeButton { text: "필터 초기화"; uiScale: uiScale; maxButtonWidth: Math.round(96 * uiScale); onClicked: clearFilters() }
                Components.SafeButton { text: "프레임 지우기"; uiScale: uiScale; maxButtonWidth: Math.round(104 * uiScale); onClicked: appController.clearFrames() }
                Components.StatusBadge { text: "행 " + timingList.count; kind: "info"; uiScale: uiScale; maxWidth: Math.round(82 * uiScale) }
            }
        }

        Frame {
            Layout.fillWidth: true
            implicitHeight: timingStatusColumn.implicitHeight + Math.round(16 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            ColumnLayout {
                id: timingStatusColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                Components.FlowToolbar {
                    Layout.fillWidth: true
                    uiScale: uiScale
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: kindFromLevel(appController.systemLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "주기 " + appController.timingLevel; kind: kindFromLevel(appController.timingLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "조치 " + appController.operatorActionLevel; kind: kindFromLevel(appController.operatorActionLevel); uiScale: uiScale }
                    Components.SafeButton { text: "최상위 주기"; uiScale: uiScale; maxButtonWidth: Math.round(98 * uiScale); enabled: appController.topTimingId !== ""; onClicked: appController.focusTimingId(appController.topTimingId) }
                    Components.SafeText {
                        text: appController.topTimingId !== "" ? (appController.topTimingId + " · " + appController.primaryIssueSummary) : appController.rootCauseSummary
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
                    height: 34
                    radius: 8
                    color: "#eef3f8"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: stateColWidth
                            title: "상태"
                            sortIndicator: sortMark("severity")
                            filterText: appController.timingFilterSeverity
                            filterPlaceholder: "상태 필터"
                            onSortRequested: appController.toggleTimingSort("severity")
                            onSortAscendingRequested: { appController.setTimingSortMode("severity"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("severity"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterSeverity(text) }
                            onFilterCleared: appController.setTimingFilterSeverity("")
                            onResizeRequested: function(delta) { stateColWidth = clampWidth(stateColWidth + delta, 74, 160) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: idColWidth
                            title: "ID"
                            sortIndicator: sortMark("id")
                            filterText: appController.timingFilterId
                            filterPlaceholder: "ID 필터"
                            onSortRequested: appController.toggleTimingSort("id")
                            onSortAscendingRequested: { appController.setTimingSortMode("id"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("id"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterId(text) }
                            onFilterCleared: appController.setTimingFilterId("")
                            onResizeRequested: function(delta) { idColWidth = clampWidth(idColWidth + delta, 72, 180) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: nameColWidth
                            title: "이름"
                            sortIndicator: sortMark("name")
                            filterText: appController.timingFilterName
                            filterPlaceholder: "이름 필터"
                            onSortRequested: appController.toggleTimingSort("name")
                            onSortAscendingRequested: { appController.setTimingSortMode("name"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("name"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterName(text) }
                            onFilterCleared: appController.setTimingFilterName("")
                            onResizeRequested: function(delta) { nameColWidth = clampWidth(nameColWidth + delta, 120, 360) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: expectedColWidth
                            title: "기대"
                            sortIndicator: sortMark("expected")
                            filterText: appController.timingFilterExpected
                            filterPlaceholder: "기대주기 필터"
                            onSortRequested: appController.toggleTimingSort("expected")
                            onSortAscendingRequested: { appController.setTimingSortMode("expected"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("expected"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterExpected(text) }
                            onFilterCleared: appController.setTimingFilterExpected("")
                            onResizeRequested: function(delta) { expectedColWidth = clampWidth(expectedColWidth + delta, 78, 180) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: gapColWidth
                            title: "실제"
                            sortIndicator: sortMark("gap")
                            filterText: appController.timingFilterGap
                            filterPlaceholder: "실제주기 필터"
                            onSortRequested: appController.toggleTimingSort("gap")
                            onSortAscendingRequested: { appController.setTimingSortMode("gap"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("gap"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterGap(text) }
                            onFilterCleared: appController.setTimingFilterGap("")
                            onResizeRequested: function(delta) { gapColWidth = clampWidth(gapColWidth + delta, 78, 180) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: ageColWidth
                            title: "경과"
                            sortIndicator: sortMark("age")
                            filterText: appController.timingFilterAge
                            filterPlaceholder: "경과시간 필터"
                            onSortRequested: appController.toggleTimingSort("age")
                            onSortAscendingRequested: { appController.setTimingSortMode("age"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("age"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterAge(text) }
                            onFilterCleared: appController.setTimingFilterAge("")
                            onResizeRequested: function(delta) { ageColWidth = clampWidth(ageColWidth + delta, 78, 180) }
                        }
                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: sourceColWidth
                            title: "소스"
                            sortIndicator: sortMark("source")
                            filterText: appController.timingFilterSource
                            filterPlaceholder: "소스 필터"
                            onSortRequested: appController.toggleTimingSort("source")
                            onSortAscendingRequested: { appController.setTimingSortMode("source"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("source"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterSource(text) }
                            onFilterCleared: appController.setTimingFilterSource("")
                            onResizeRequested: function(delta) { sourceColWidth = clampWidth(sourceColWidth + delta, 70, 160) }
                        }

                        Components.ColumnHeaderCell {
                            Layout.preferredWidth: metricColWidth
                            title: "%"
                            sortIndicator: ""
                            filterText: ""
                            filterPlaceholder: ""
                            onSortRequested: function() {}
                            onSortAscendingRequested: function() {}
                            onSortDescendingRequested: function() {}
                            onFilterApplied: function(text) {}
                            onFilterCleared: function() {}
                            onResizeRequested: function(delta) { metricColWidth = clampWidth(metricColWidth + delta, 68, 120) }
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
                            Layout.preferredWidth: reasonColWidth
                            Layout.fillWidth: true
                            title: "이유"
                            sortIndicator: sortMark("reason")
                            filterText: appController.timingFilterReason
                            filterPlaceholder: "이유 필터"
                            resizable: false
                            onSortRequested: appController.toggleTimingSort("reason")
                            onSortAscendingRequested: { appController.setTimingSortMode("reason"); appController.setTimingSortDescending(false) }
                            onSortDescendingRequested: { appController.setTimingSortMode("reason"); appController.setTimingSortDescending(true) }
                            onFilterApplied: function(text) { appController.setTimingFilterReason(text) }
                            onFilterCleared: appController.setTimingFilterReason("")
                        }
                    }
                }

                ListView {
                    id: timingList
                    onMovementStarted: { appController.setTimingViewHeld(true); holdReleaseTimer.stop() }
                    onMovementEnded: holdReleaseTimer.restart()
                    onFlickStarted: { appController.setTimingViewHeld(true); holdReleaseTimer.stop() }
                    onFlickEnded: holdReleaseTimer.restart()
                    onDraggingChanged: { if (dragging) { appController.setTimingViewHeld(true); holdReleaseTimer.stop() } else holdReleaseTimer.restart() }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: appController.timingModel
                    reuseItems: true
                    cacheBuffer: 24
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                    boundsBehavior: Flickable.StopAtBounds
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { applyWheel(timingList, event, appController.setTimingViewHeld, holdReleaseTimer) }
                    }

                    Components.EmptyState {
                        parent: timingList
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                        visible: timingList.count === 0
                        z: 10
                        title: "표시할 주기 항목 없음"
                        message: appController.rulesLoaded ? "현재 필터 조건에 맞는 주기 이슈가 없습니다." : "포트를 연결하거나 BIN/Typed 파일을 열면 주기 기준과 관찰 프레임을 비교합니다."
                        badgeText: appController.rulesLoaded ? "필터/정상" : "대기"
                        kind: appController.rulesLoaded ? "ok" : "info"
                        uiScale: uiScale
                    }

                    delegate: MouseArea {
                        required property string key
                        required property string idText
                        required property string name
                        required property string severity
                        required property string severityColor
                        required property string expectedMsText
                        required property string lastGapMsText
                        required property string ageMsText
                        required property string source
                        required property string metricText
                        required property double gaugePct
                        required property string reason
                        required property int eventCount
                        required property var history
                        property bool expanded: timingExpandedState[key] !== undefined ? timingExpandedState[key] : false
                        width: timingList.width
                        height: rowBox.implicitHeight
                        onClicked: { expanded = !expanded; timingExpandedState[key] = expanded }

                        Rectangle {
                            id: rowBox
                            width: parent.width
                            implicitHeight: bodyColumn.implicitHeight + 8
                            radius: 8
                            color: appController.timingFilterId !== "" && appController.timingFilterId === idText ? "#eef6ff" : "#fbfdff"
                            border.color: appController.timingFilterId !== "" && appController.timingFilterId === idText ? "#93c5fd" : "#d7e0ea"

                            ColumnLayout {
                                id: bodyColumn
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Rectangle {
                                        Layout.preferredWidth: stateColWidth
                                        Layout.preferredHeight: 24
                                        radius: 6
                                        color: severityColor
                                        Label { anchors.centerIn: parent; text: severity; color: "white"; font.bold: true; font.pixelSize: Math.round(12 * uiScale) }
                                    }
                                    Label { Layout.preferredWidth: idColWidth; text: idText; color: "#243447"; font.bold: true; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: nameColWidth; text: name; color: "#243447"; elide: Text.ElideRight; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: expectedColWidth; text: expectedMsText; color: "#243447"; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: gapColWidth; text: lastGapMsText; color: "#243447"; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: ageColWidth; text: ageMsText; color: "#243447"; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: sourceColWidth; text: source; color: "#243447"; font.pixelSize: Math.round(12 * uiScale) }
                                    Label { Layout.preferredWidth: metricColWidth; text: metricText; color: severity === "ERR" ? "#dc2626" : (severity === "WARN" ? "#d97706" : "#475569"); font.bold: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: Math.round(12 * uiScale) }
                                    Item {
                                        Layout.preferredWidth: gaugeColWidth
                                        Layout.preferredHeight: Math.round(18 * uiScale)
                                        Rectangle { anchors.fill: parent; radius: 6; color: "#edf2f7"; border.color: "#d7e0ea" }
                                        Rectangle {
                                            width: metricText === "-" ? 0 : Math.max(4, parent.width * Math.min(1, Math.max(0, gaugePct / 100.0)))
                                            height: parent.height
                                            radius: 6
                                            color: severity === "ERR" ? "#ef4444" : (severity === "WARN" ? "#f59e0b" : "#94a3b8")
                                        }
                                    }
                                    Rectangle {
                                        visible: eventCount > 0
                                        Layout.preferredWidth: 44
                                        Layout.preferredHeight: 20
                                        radius: 6
                                        color: "#e0ecff"
                                        Label { anchors.centerIn: parent; text: String(eventCount); color: "#1d4ed8"; font.bold: true; font.pixelSize: Math.round(10 * uiScale) }
                                    }
                                    Label {
                                        Layout.preferredWidth: reasonColWidth
                                        Layout.fillWidth: true
                                        text: reason
                                        color: "#52606d"
                                        elide: Text.ElideRight
                                        font.pixelSize: Math.round(12 * uiScale)
                                    }
                                    Label { text: expanded ? "▾" : "▸"; color: "#64748b"; font.pixelSize: Math.round(13 * uiScale) }
                                }

                                Loader {
                                    Layout.fillWidth: true
                                    active: expanded && history && history.length > 0
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
                                                spacing: 3
                                                Label { text: "주기 이탈 기록"; color: "#475569"; font.bold: true; font.pixelSize: Math.round(11 * uiScale) }
                                                Repeater {
                                                    model: history
                                                    delegate: Label {
                                                        required property string modelData
                                                        Layout.fillWidth: true
                                                        text: modelData
                                                        wrapMode: Text.WordWrap
                                                        color: "#52606d"
                                                        font.pixelSize: Math.round(10 * uiScale)
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
    }
}
