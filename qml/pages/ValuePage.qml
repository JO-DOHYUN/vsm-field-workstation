import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    Component.onCompleted: Qt.callLater(scrollToFocusedRow)
    property real uiScale: 1.0
    property string uiFontFamily: Qt.platform.os === "windows" ? "Malgun Gothic" : ""
    property string monoFontFamily: Qt.platform.os === "windows" ? "Cascadia Mono" : "Monospace"
    property real savedDetailScrollY: 0

    property real stateColWidth: 56
    property real idColWidth: 74
    property real nameColWidth: 126
    property real rawColWidth: 110
    property real summaryColWidth: 0
    property real listPaneHeight: 220
    property real detailKeyWidth: 96

    Timer {
        id: holdReleaseTimer
        interval: 260
        repeat: false
        onTriggered: appController.setValueViewHeld(false)
    }

    function sortMark(mode) {
        if (appController.valueSortMode !== mode)
            return ""
        return appController.valueSortDescending ? " ▼" : " ▲"
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
        const focusId = (appController.valueFilterId !== "") ? appController.valueFilterId : appController.selectedValueId
        if (focusId === "") return
        const rowIndex = appController.valueModel.findIndex("idText", focusId)
        if (rowIndex >= 0) idList.positionViewAtIndex(rowIndex, ListView.Visible)
    }


    function selectedRowIndex() {
        return appController.valueModel.indexOfKey(appController.selectedValueId)
    }

    function clearFilters() {
        appController.setValueFilterSeverity("")
        appController.setValueFilterId("")
        appController.setValueFilterName("")
        appController.setValueFilterSource("")
        appController.setValueFilterRaw("")
        appController.setValueFilterGap("")
        appController.setValueFilterReason("")
    }


    Connections {
        target: appController
        function onSelectedValueIdChanged() {
            const keep = savedDetailScrollY
            Qt.callLater(function() {
                detailList.contentY = Math.min(keep, Math.max(0, detailList.contentHeight - detailList.height))
                scrollToFocusedRow()
            })
        }
        function onFiltersChanged() { Qt.callLater(scrollToFocusedRow) }
    }


    Connections {
        target: appController.valueDetailModel
        function onCountChanged() {
            const keep = savedDetailScrollY
            Qt.callLater(function() {
                detailList.contentY = Math.min(keep, Math.max(0, detailList.contentHeight - detailList.height))
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        Frame {
            Layout.fillWidth: true
            implicitHeight: valueHeaderFlow.implicitHeight + Math.round(10 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            Components.FlowToolbar {
                id: valueHeaderFlow
                anchors.fill: parent
                anchors.margins: 5
                spacing: 6
                uiScale: uiScale
                Components.SafeText { text: appController.selectedValueId !== "" ? ("선택 " + appController.selectedValueId) : (appController.modelActive ? "값 해석" : "값 / RAW"); width: Math.round(112 * uiScale); uiScale: uiScale; basePixelSize: Math.round(11.0 * uiScale); font.bold: true; color: "#243447" }
                Components.StatusBadge { text: "행 " + idList.count; kind: "info"; uiScale: uiScale; maxWidth: Math.round(78 * uiScale) }
                Components.StatusBadge { text: "입력 " + appController.sourceStateLevel; kind: kindFromLevel(appController.sourceStateLevel); uiScale: uiScale }
                Components.StatusBadge { text: "분석 " + appController.analysisModeLevel; kind: kindFromLevel(appController.analysisModeLevel); uiScale: uiScale }
                Components.SafeText { text: "활성 " + appController.valueIssueCount + " / 누적 " + appController.valueCumulativeCount; width: Math.round(112 * uiScale); uiScale: uiScale; basePixelSize: Math.round(10.6 * uiScale); color: "#5b6673" }
                Components.SafeButton { text: "필터 초기화"; uiScale: uiScale; maxButtonWidth: Math.round(96 * uiScale); onClicked: clearFilters() }
                Components.SafeButton { text: "프레임 지우기"; uiScale: uiScale; maxButtonWidth: Math.round(104 * uiScale); onClicked: appController.clearFrames() }
                Components.SafeButton {
                    text: "선택 위치"
                    uiScale: uiScale
                    maxButtonWidth: Math.round(88 * uiScale)
                    enabled: appController.selectedValueId !== ""
                    onClicked: {
                        const idx = selectedRowIndex()
                        if (idx >= 0)
                            idList.positionViewAtIndex(idx, ListView.Center)
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            implicitHeight: valueStatusColumn.implicitHeight + Math.round(12 * uiScale)
            background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }
            ColumnLayout {
                id: valueStatusColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Components.FlowToolbar {
                    Layout.fillWidth: true
                    uiScale: uiScale
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: kindFromLevel(appController.systemLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "값 " + appController.valueLevel; kind: kindFromLevel(appController.valueLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "조치 " + appController.operatorActionLevel; kind: kindFromLevel(appController.operatorActionLevel); uiScale: uiScale }
                    Components.SafeButton { text: "최상위 값"; uiScale: uiScale; maxButtonWidth: Math.round(92 * uiScale); enabled: appController.topValueId !== ""; onClicked: appController.focusValueId(appController.topValueId) }
                    Components.SafeText {
                        text: appController.topValueId !== "" ? (appController.topValueId + " · " + appController.primaryIssueSummary) : appController.rootCauseSummary
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

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            Frame {
                SplitView.preferredHeight: listPaneHeight
                SplitView.minimumHeight: 112
                SplitView.fillWidth: true
                background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 3

                    Rectangle {
                        Layout.fillWidth: true
                        height: Math.round(24 * uiScale)
                        radius: 8
                        color: "#eef3f8"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 1

                            Components.ColumnHeaderCell {
                                Layout.preferredWidth: stateColWidth
                                title: "상태"
                                sortIndicator: sortMark("severity")
                                filterText: appController.valueFilterSeverity
                                filterPlaceholder: "상태 필터"
                                onSortRequested: appController.toggleValueSort("severity")
                                onSortAscendingRequested: { appController.setValueSortMode("severity"); appController.setValueSortDescending(false) }
                                onSortDescendingRequested: { appController.setValueSortMode("severity"); appController.setValueSortDescending(true) }
                                onFilterApplied: function(text) { appController.setValueFilterSeverity(text) }
                                onFilterCleared: appController.setValueFilterSeverity("")
                                onResizeRequested: function(delta) { stateColWidth = clampWidth(stateColWidth + delta, 56, 120) }
                            }
                            Components.ColumnHeaderCell {
                                Layout.preferredWidth: idColWidth
                                title: "ID"
                                sortIndicator: sortMark("id")
                                filterText: appController.valueFilterId
                                filterPlaceholder: "ID 필터"
                                onSortRequested: appController.toggleValueSort("id")
                                onSortAscendingRequested: { appController.setValueSortMode("id"); appController.setValueSortDescending(false) }
                                onSortDescendingRequested: { appController.setValueSortMode("id"); appController.setValueSortDescending(true) }
                                onFilterApplied: function(text) { appController.setValueFilterId(text) }
                                onFilterCleared: appController.setValueFilterId("")
                                onResizeRequested: function(delta) { idColWidth = clampWidth(idColWidth + delta, 70, 160) }
                            }
                            Components.ColumnHeaderCell {
                                Layout.preferredWidth: nameColWidth
                                title: "이름"
                                sortIndicator: sortMark("name")
                                filterText: appController.valueFilterName
                                filterPlaceholder: "이름 필터"
                                onSortRequested: appController.toggleValueSort("name")
                                onSortAscendingRequested: { appController.setValueSortMode("name"); appController.setValueSortDescending(false) }
                                onSortDescendingRequested: { appController.setValueSortMode("name"); appController.setValueSortDescending(true) }
                                onFilterApplied: function(text) { appController.setValueFilterName(text) }
                                onFilterCleared: appController.setValueFilterName("")
                                onResizeRequested: function(delta) { nameColWidth = clampWidth(nameColWidth + delta, 110, 280) }
                            }
                            Components.ColumnHeaderCell {
                                Layout.preferredWidth: rawColWidth
                                title: "RAW"
                                sortIndicator: sortMark("raw")
                                filterText: appController.valueFilterRaw
                                filterPlaceholder: "RAW 필터"
                                onSortRequested: appController.toggleValueSort("raw")
                                onSortAscendingRequested: { appController.setValueSortMode("raw"); appController.setValueSortDescending(false) }
                                onSortDescendingRequested: { appController.setValueSortMode("raw"); appController.setValueSortDescending(true) }
                                onFilterApplied: function(text) { appController.setValueFilterRaw(text) }
                                onFilterCleared: appController.setValueFilterRaw("")
                                onResizeRequested: function(delta) { rawColWidth = clampWidth(rawColWidth + delta, 96, 240) }
                            }
                            Components.ColumnHeaderCell {
                                Layout.fillWidth: true
                                title: "해석값"
                                sortIndicator: sortMark("reason")
                                filterText: appController.valueFilterReason
                                filterPlaceholder: "해석값 필터"
                                resizable: false
                                onSortRequested: appController.toggleValueSort("reason")
                                onSortAscendingRequested: { appController.setValueSortMode("reason"); appController.setValueSortDescending(false) }
                                onSortDescendingRequested: { appController.setValueSortMode("reason"); appController.setValueSortDescending(true) }
                                onFilterApplied: function(text) { appController.setValueFilterReason(text) }
                                onFilterCleared: appController.setValueFilterReason("")
                            }
                        }
                    }

                    ListView {
                        id: idList
                    onMovementStarted: { appController.setValueViewHeld(true); holdReleaseTimer.stop() }
                    onMovementEnded: holdReleaseTimer.restart()
                    onFlickStarted: { appController.setValueViewHeld(true); holdReleaseTimer.stop() }
                    onFlickEnded: holdReleaseTimer.restart()
                    onDraggingChanged: { if (dragging) { appController.setValueViewHeld(true); holdReleaseTimer.stop() } else holdReleaseTimer.restart() }
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 1
                        model: appController.valueModel
                        reuseItems: true
                        cacheBuffer: 24
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                    boundsBehavior: Flickable.StopAtBounds
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { applyWheel(idList, event, appController.setValueViewHeld, holdReleaseTimer) }
                    }

                        Components.EmptyState {
                            parent: idList
                            anchors.centerIn: parent
                            width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                            visible: idList.count === 0
                            z: 10
                            title: "표시할 값 항목 없음"
                            message: appController.modelActive ? "현재 필터 조건에 맞는 값 항목이 없습니다." : "포트를 연결하거나 재생 파일을 열면 CAN ID별 RAW/해석값이 표시됩니다."
                            badgeText: appController.modelActive ? "필터/정상" : "대기"
                            kind: appController.modelActive ? "ok" : "info"
                            uiScale: uiScale
                        }

                        delegate: MouseArea {
                            required property string key
                            required property string idText
                            required property string name
                            required property string severity
                            required property string severityColor
                            required property string dataHex
                            required property string summaryRich
                            required property string summaryText
                            required property string previewText
                            width: idList.width
                            height: Math.max(Math.round(30 * uiScale), rowSummary.implicitHeight + Math.round(6 * uiScale))
                            onClicked: appController.selectValueId(idText)

                            Rectangle {
                                anchors.fill: parent
                                radius: 6
                                color: (appController.selectedValueId === idText || (appController.valueFilterId !== "" && appController.valueFilterId === idText)) ? "#eaf4ff" : "#fbfdff"
                                border.color: (appController.selectedValueId === idText || (appController.valueFilterId !== "" && appController.valueFilterId === idText)) ? "#7ea5ff" : "#d7e0ea"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    spacing: 2

                                    Rectangle {
                                        Layout.preferredWidth: stateColWidth
                                        Layout.preferredHeight: Math.round(20 * uiScale)
                                        radius: 6
                                        color: severityColor
                                        Text {
                                            anchors.centerIn: parent
                                            text: severity
                                            color: "white"
                                            font.bold: true
                                            font.pixelSize: Math.round(9.8 * uiScale)
                                            font.family: uiFontFamily
                                            renderType: Text.NativeRendering
                                        }
                                    }
                                    Text {
                                        Layout.preferredWidth: idColWidth
                                        text: idText
                                        color: "#243447"
                                        font.bold: true
                                        font.pixelSize: Math.round(10.0 * uiScale)
                                        font.family: monoFontFamily
                                        renderType: Text.NativeRendering
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.preferredWidth: nameColWidth
                                        text: name
                                        color: "#243447"
                                        elide: Text.ElideRight
                                        font.pixelSize: Math.round(9.5 * uiScale)
                                        font.family: uiFontFamily
                                        renderType: Text.NativeRendering
                                    }
                                    Text {
                                        Layout.preferredWidth: rawColWidth
                                        text: dataHex
                                        color: "#243447"
                                        elide: Text.ElideRight
                                        font.family: monoFontFamily
                                        font.pixelSize: Math.round(9.0 * uiScale)
                                        renderType: Text.NativeRendering
                                    }
                                    Text {
                                        id: rowSummary
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        text: summaryText !== undefined ? summaryText : previewText
                                        textFormat: Text.PlainText
                                        color: "#1f3448"
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        elide: Text.ElideNone
                                        clip: true
                                        font.pixelSize: Math.round(9.8 * uiScale)
                                        font.family: uiFontFamily
                                        renderType: Text.NativeRendering
                                        lineHeight: 1.08
                                    }
                                }
                            }

                            ToolTip.visible: containsMouse && (rowSummary.contentHeight > rowSummary.height + 1)
                            ToolTip.text: summaryText !== undefined ? summaryText : previewText
                        }
                    }
                }
            }

            Frame {
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                background: Rectangle { color: "#ffffff"; radius: 10; border.color: "#d7e0ea" }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: appController.selectedValueId !== "" ? (appController.selectedValueId + " 상세") : "선택 상세"; font.bold: true; color: "#243447"; font.pixelSize: Math.round(10.4 * uiScale) }
                        Item { Layout.fillWidth: true }
                        Label { text: "상세행 " + appController.valueDetailModel.count; color: "#5b6673"; font.pixelSize: Math.round(10.2 * uiScale) }
                    }

                    ListView {
                        id: detailList
                        onContentYChanged: savedDetailScrollY = contentY
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        reuseItems: true
                        cacheBuffer: 24
                        model: appController.valueDetailModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                    boundsBehavior: Flickable.StopAtBounds
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) { applyWheel(detailList, event, appController.setValueViewHeld, holdReleaseTimer) }
                    }

                        Components.EmptyState {
                            parent: detailList
                            anchors.centerIn: parent
                            width: Math.max(0, Math.min(parent.width - Math.round(28 * uiScale), Math.round(430 * uiScale)))
                            visible: detailList.count === 0
                            z: 10
                            title: "선택 상세 없음"
                            message: appController.selectedValueId !== "" ? "선택된 ID에 표시할 해석 상세가 없습니다." : "왼쪽 목록에서 CAN ID를 선택하면 RAW, DLC, 모델 해석 상세가 표시됩니다."
                            badgeText: appController.selectedValueId !== "" ? appController.selectedValueId : "선택 대기"
                            kind: appController.selectedValueId !== "" ? "info" : "warn"
                            uiScale: uiScale
                        }

                        delegate: Rectangle {
                            required property string key
                            required property string value
                            required property string note
                            width: detailList.width
                            height: Math.max(Math.round(24 * uiScale), detailValue.implicitHeight + detailNote.implicitHeight + Math.round(10 * uiScale))
                            radius: 7
                            color: "#fbfdff"
                            border.color: "#d7e0ea"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Math.round(4 * uiScale)
                                spacing: Math.round(5 * uiScale)
                                Text {
                                    Layout.preferredWidth: detailKeyWidth
                                    Layout.alignment: Qt.AlignTop
                                    text: key
                                    color: "#243447"
                                    font.bold: true
                                    font.pixelSize: Math.round(10.2 * uiScale)
                                    font.family: uiFontFamily
                                    renderType: Text.NativeRendering
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    id: detailValue
                                    Layout.preferredWidth: Math.round(220 * uiScale)
                                    Layout.alignment: Qt.AlignTop
                                    text: value
                                    color: "#102a43"
                                    wrapMode: (key.indexOf("RAW") >= 0 || key.indexOf("BYTE") >= 0 || key.indexOf("CAN ID") >= 0) ? Text.WrapAnywhere : Text.WordWrap
                                    font.pixelSize: Math.round(10.8 * uiScale)
                                    font.family: (key.indexOf("RAW") >= 0 || key.indexOf("BYTE") >= 0 || key.indexOf("CAN ID") >= 0) ? monoFontFamily : uiFontFamily
                                    renderType: Text.NativeRendering
                                }
                                Text {
                                    id: detailNote
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    text: note
                                    color: "#52606d"
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Math.round(9.2 * uiScale)
                                    font.family: uiFontFamily
                                    renderType: Text.NativeRendering
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
