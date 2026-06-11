import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CanMonitor
import "../components" as Components

Item {
    id: pageRoot
    objectName: "graphPage"
    property real uiScale: 1.0

    function kindFromLevel(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function presetIndexFor(key) {
        const list = appController.graphPresets
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === key)
                return i
        }
        return 0
    }

    function syncPresetIndex() {
        const idx = presetIndexFor(appController.graphPresetKey)
        if (presetCombo.currentIndex !== idx)
            presetCombo.currentIndex = idx
    }

    function signalIndexForKey(key) {
        return appController.graphCatalogModel.indexOfKey(key)
    }

    function toggleGraphSignalFromList(key) {
        if (!key)
            return
        appController.toggleGraphSignal(key)
    }

    function clearGraphSelectionFromList() {
        appController.clearGraphSelection()
    }

    function setGraphPresetFromList(key) {
        appController.setGraphPresetKey(key)
    }

    function testGraphCatalogCount() {
        return appController.graphCatalogModel.count
    }

    function testSignalListMaxY() {
        return Math.max(0, signalList.contentHeight - signalList.height)
    }

    function testSetSignalListContentY(value) {
        const numeric = Number(value)
        signalList.contentY = Math.max(0, Math.min(isFinite(numeric) ? numeric : 0, testSignalListMaxY()))
        return signalList.contentY
    }

    function testSignalListContentY() {
        return signalList.contentY
    }

    function testGraphCatalogKeyAt(index) {
        const entry = appController.graphCatalogModel.get(index)
        return entry ? entry.key : ""
    }

    function testGraphCatalogColorAt(index) {
        const entry = appController.graphCatalogModel.get(index)
        return entry ? entry.color : ""
    }

    function testGraphCatalogColorForKey(key) {
        const idx = signalIndexForKey(key)
        return idx >= 0 ? testGraphCatalogColorAt(idx) : ""
    }

    function testGraphSeriesColorForKey(key) {
        const list = appController.graphSeries
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === key)
                return list[i].color
        }
        return ""
    }

    function testToggleGraphSignalAt(index) {
        const entry = appController.graphCatalogModel.get(index)
        if (!entry)
            return ""
        toggleGraphSignalFromList(entry.key)
        return entry.key
    }

    function testUserToggleGraphSignalAt(index) {
        const entry = appController.graphCatalogModel.get(index)
        if (!entry)
            return ""
        toggleGraphSignalFromList(entry.key)
        return entry.key
    }

    Component.onCompleted: Qt.callLater(syncPresetIndex)

    Connections {
        target: appController
        function onGraphCatalogChanged() { syncPresetIndex() }
        function onGraphSelectionChanged() { syncPresetIndex() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Components.SafeText { text: "프리셋"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.6 * pageRoot.uiScale); color: "#5b6673"; font.bold: true; Layout.preferredWidth: Math.round(54 * pageRoot.uiScale) }
                    ComboBox {
                        id: presetCombo
                        Layout.preferredWidth: 220
                        model: appController.graphPresets
                        textRole: "title"
                        onActivated: {
                            const entry = appController.graphPresets[currentIndex]
                            if (entry)
                                setGraphPresetFromList(entry.key)
                        }
                    }
                    Components.SafeText { text: "창"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.6 * pageRoot.uiScale); color: "#5b6673"; font.bold: true; Layout.preferredWidth: Math.round(30 * pageRoot.uiScale) }
                    ComboBox {
                        id: windowCombo
                        model: [
                            { title: "5초", ms: 5000 },
                            { title: "10초", ms: 10000 },
                            { title: "15초", ms: 15000 },
                            { title: "30초", ms: 30000 },
                            { title: "60초", ms: 60000 }
                        ]
                        textRole: "title"
                        Component.onCompleted: {
                            for (let i = 0; i < model.length; ++i) {
                                if (model[i].ms === appController.graphWindowMs) {
                                    currentIndex = i
                                    break
                                }
                            }
                        }
                        onActivated: appController.setGraphWindowMs(model[currentIndex].ms)
                    }
                    Components.SafeButton { text: "프리셋 해제"; uiScale: pageRoot.uiScale; maxButtonWidth: Math.round(104 * pageRoot.uiScale); onClicked: setGraphPresetFromList("manual") }
                    Components.SafeButton { text: "선택 지우기"; uiScale: pageRoot.uiScale; maxButtonWidth: Math.round(104 * pageRoot.uiScale); onClicked: clearGraphSelectionFromList() }
                    Components.SafeCheckBox {
                        text: "상세 확대"
                        uiScale: pageRoot.uiScale
                        maxControlWidth: Math.round(96 * pageRoot.uiScale)
                        checked: appController.graphDetailZoom
                        onToggled: appController.setGraphDetailZoom(checked)
                    }
                    Components.SafeText {
                        text: appController.graphDetailZoomSummary
                        color: appController.graphDetailZoom ? "#2563eb" : "#5b6673"
                        uiScale: pageRoot.uiScale
                        basePixelSize: Math.round(10.4 * pageRoot.uiScale)
                        font.bold: appController.graphDetailZoom
                        Layout.preferredWidth: Math.round(150 * pageRoot.uiScale)
                    }
                    Item { Layout.fillWidth: true }
                    Components.SafeText { text: appController.graphSourceSummary; color: "#5b6673"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.4 * pageRoot.uiScale); Layout.preferredWidth: Math.min(Math.round(220 * pageRoot.uiScale), Math.round(pageRoot.width * 0.2)) }
                    Components.SafeText {
                        text: appController.graphRangeSummary
                        color: "#35506b"
                        uiScale: pageRoot.uiScale
                        basePixelSize: Math.round(10.4 * pageRoot.uiScale)
                        Layout.maximumWidth: Math.round(420 * pageRoot.uiScale)
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: kindFromLevel(appController.systemLevel); uiScale: uiScale }
                    Components.StatusBadge { text: "버스 " + appController.busHealthLevel; kind: kindFromLevel(appController.busHealthLevel); uiScale: uiScale }
                    Components.StatusBadge { text: appController.graphDetailZoom ? "확대축 고정" : "고정축"; kind: appController.graphDetailZoom ? "warn" : "info"; uiScale: uiScale }
                    Item { Layout.fillWidth: true }
                    Components.SafeText { text: appController.graphRangeSummary; color: "#607080"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.6 * pageRoot.uiScale); Layout.preferredWidth: Math.min(Math.round(430 * pageRoot.uiScale), Math.round(pageRoot.width * 0.42)) }
                }
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe5f0"
                    implicitHeight: Math.round(32 * uiScale)
                    Components.SafeText { anchors.fill: parent; anchors.margins: 6; text: appController.operatorHeadline + " · " + appController.graphDetailZoomSummary; color: "#35506b"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.6 * pageRoot.uiScale); maxLines: 2 }
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Frame {
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 260
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Components.SafeText { text: "그래프 신호"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(11.0 * pageRoot.uiScale); font.bold: true; color: "#243447"; Layout.fillWidth: true }
                        Item { Layout.fillWidth: true }
                        Components.SafeText { text: "최대 4선"; uiScale: pageRoot.uiScale; basePixelSize: Math.round(10.2 * pageRoot.uiScale); color: "#5b6673"; Layout.preferredWidth: Math.round(68 * pageRoot.uiScale); horizontalAlignment: Text.AlignRight }
                    }

                    ListView {
                        id: signalList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        reuseItems: true
                        cacheBuffer: 220
                        boundsBehavior: Flickable.StopAtBounds
                        model: appController.graphCatalogModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property string key
                            required property string label
                            required property bool selected
                            width: ListView.view.width
                            radius: 7
                            color: selected ? "#eff6ff" : "#f8fafc"
                            border.color: selected ? "#93c5fd" : "#dbe5f0"
                            implicitHeight: Math.round(26 * uiScale)

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 2
                                spacing: 4

                                Components.SafeCheckBox {
                                    objectName: "graphSignalCheckBox"
                                    property string signalKey: key
                                    text: ""
                                    uiScale: pageRoot.uiScale
                                    minControlWidth: Math.round(24 * pageRoot.uiScale)
                                    maxControlWidth: Math.round(28 * pageRoot.uiScale)
                                    checked: selected
                                    onClicked: toggleGraphSignalFromList(key)
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true

                                    TapHandler {
                                        onTapped: toggleGraphSignalFromList(key)
                                    }

                                    Components.SafeText {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: label
                                        color: "#243447"
                                        uiScale: pageRoot.uiScale
                                        basePixelSize: Math.round(10.6 * pageRoot.uiScale)
                                        font.bold: selected
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Frame {
                SplitView.fillWidth: true
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "시계열 비교"; font.bold: true; color: "#243447"; font.pixelSize: Math.round(11.0 * uiScale) }
                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: appController.graphSeries
                            delegate: RowLayout {
                                property var entry: modelData
                                spacing: 4
                                Rectangle { width: 12; height: 12; radius: 6; color: entry.color }
                                Label {
                                    text: entry.label + "  현재 " + entry.latestText + (entry.unit ? (" " + entry.unit) : "") + "  [" + entry.renderModeLabel + "]"
                                    color: "#4b5563"
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: appController.graphDetailZoom ? "판정은 RAW 기준으로 유지하고, 선택 구간 값축만 고정합니다." : "최근 구간을 고정축으로 표시합니다. 버스별 원본은 라이브/재생 프레임 필터에서 확인합니다."
                        color: "#64748b"
                        font.pixelSize: Math.round(10.5 * uiScale)
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12
                        color: "#f8fbff"
                        border.color: "#dbe5f0"

                        GraphViewport {
                            id: graphViewport
                            anchors.fill: parent
                            series: appController.graphSeries
                            uiScale: pageRoot.uiScale
                        }
                    }
                }
            }
        }
    }
}
