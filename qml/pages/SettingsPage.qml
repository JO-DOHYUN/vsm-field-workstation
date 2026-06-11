import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root
    objectName: "settingsPage"
    property real uiScale: 1.0
    property bool showRawDetails: false
    signal exportSnapshot()

    function levelKind(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function cardColumns(widthValue) {
        return widthValue >= 1320 ? 3 : (widthValue >= 860 ? 2 : 1)
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: Math.max(0, root.width - Math.round(18 * root.uiScale))
            spacing: Math.round(10 * root.uiScale)

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: settingsHeroColumn.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    id: settingsHeroColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeText {
                            text: "설정"
                            color: "#243447"
                            font.bold: true
                            uiScale: root.uiScale
                            basePixelSize: Math.round(18 * root.uiScale)
                            width: Math.max(Math.round(110 * root.uiScale), Math.min(Math.round(320 * root.uiScale), root.width - Math.round(260 * root.uiScale)))
                        }
                        Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: root.levelKind(appController.systemLevel); uiScale: root.uiScale }
                        Components.StatusBadge { text: appController.modelActive ? "모델 적용" : "모델 해제"; kind: appController.modelActive ? "ok" : "warn"; uiScale: root.uiScale }
                    }

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeButton { text: "스냅샷"; uiScale: root.uiScale; onClicked: root.exportSnapshot() }
                        Components.SafeButton { text: "분석 초기화"; uiScale: root.uiScale; maxButtonWidth: Math.round(98 * root.uiScale); onClicked: appController.resetAnalysisContext() }
                        Components.SafeButton { text: "필터 초기화"; uiScale: root.uiScale; maxButtonWidth: Math.round(98 * root.uiScale); onClicked: appController.resetAllAnalysisFilters() }
                        Components.SafeButton { text: "세션 초기화"; uiScale: root.uiScale; maxButtonWidth: Math.round(98 * root.uiScale); onClicked: appController.clearSavedSession() }
                        Components.SafeCheckBox { text: "상세 표시"; uiScale: root.uiScale; maxControlWidth: Math.round(100 * root.uiScale); checked: root.showRawDetails; onToggled: root.showRawDetails = checked }
                    }

                    Components.SafeText {
                        Layout.fillWidth: true
                        text: appController.sessionSummary
                        color: "#35506b"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11.0 * root.uiScale)
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: settingsActionColumn.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    id: settingsActionColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.StatusBadge { text: "조치 " + appController.operatorActionLevel; kind: root.levelKind(appController.operatorActionLevel); uiScale: root.uiScale }
                        Components.SafeText {
                            width: Math.max(Math.round(280 * root.uiScale), root.width - Math.round(170 * root.uiScale))
                            text: appController.operatorHeadline + " · " + appController.operatorActionText
                            color: "#243447"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(10.8 * root.uiScale)
                        }
                    }

                    Components.SafeText {
                        Layout.fillWidth: true
                        text: appController.primaryIssueId !== "" ? (appController.primaryIssueId + " · " + appController.primaryIssueSummary) : appController.primaryIssueSummary
                        color: "#607080"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.6 * root.uiScale)
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.cardColumns(root.width)
                rowSpacing: Math.round(8 * root.uiScale)
                columnSpacing: Math.round(8 * root.uiScale)

                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "모델"
                    value: appController.modelActive ? appController.modelName : "모델 해제"
                    note: appController.modelDiagnosticsSummary
                    badgeText: appController.modelActive ? "MODEL" : "OFF"
                    kind: appController.modelActive ? root.levelKind(appController.modelDiagnosticsLevel) : "warn"
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "시그널 해석"
                    value: appController.signalDbLoaded ? (String(appController.signalDbMessageCount) + " ID") : "미적용"
                    note: appController.signalDbSummary
                    badgeText: appController.signalDbLoaded ? "READY" : "NONE"
                    kind: appController.signalDbLoaded ? "ok" : "warn"
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "저장 위치"
                    value: "project replay_data"
                    note: appController.defaultLogDirectory
                    badgeText: "PATH"
                    kind: "info"
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "라이브 / 재생"
                    value: appController.analysisSourceText
                    note: appController.activeViewStateSummary
                    badgeText: appController.replayAnalysisActive ? "REPLAY" : (appController.connected ? "LIVE" : "OFF")
                    kind: appController.replayAnalysisActive ? "warn" : (appController.connected ? "ok" : "info")
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "버스"
                    value: appController.busHealthLevel
                    note: appController.busHealthText
                    badgeText: "BUS"
                    kind: root.levelKind(appController.busHealthLevel)
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "필터"
                    value: appController.activeViewStateSummary
                    note: "라이브 " + (appController.liveFrameView.idFilter === "" ? "전체" : appController.liveFrameView.idFilter) + " · 재생 " + (appController.replayFrameView.idFilter === "" ? "전체" : appController.replayFrameView.idFilter)
                    badgeText: "FILTER"
                    kind: "info"
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: settingsPathColumn.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    id: settingsPathColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeText { text: "경로 / 세션"; uiScale: root.uiScale; basePixelSize: Math.round(13 * root.uiScale); font.bold: true; color: "#243447"; width: Math.round(120 * root.uiScale) }
                        Components.StatusBadge { text: "업타임 " + appController.sessionUptimeText; kind: "info"; uiScale: root.uiScale }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 900 ? 2 : 1
                        rowSpacing: Math.round(6 * root.uiScale)
                        columnSpacing: Math.round(12 * root.uiScale)

                        Components.SafeText { text: "모델 파일"; color: "#52606d"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale) }
                        Components.SafeText { text: appController.modelPath !== "" ? appController.modelPath : appController.modelSourceSummary; color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale); Layout.fillWidth: true }

                        Components.SafeText { text: "로그 폴더"; color: "#52606d"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale) }
                        Components.SafeText { text: appController.defaultLogDirectory; color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale); Layout.fillWidth: true }

                        Components.SafeText { text: "스냅샷 폴더"; color: "#52606d"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale) }
                        Components.SafeText { text: appController.defaultSnapshotDirectory; color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale); Layout.fillWidth: true }

                        Components.SafeText { text: "재생 파일"; color: "#52606d"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale) }
                        Components.SafeText { text: appController.replayPath !== "" ? appController.replayPath : "재생 파일 미선택"; color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale); Layout.fillWidth: true }
                    }
                }
            }

            Frame {
                visible: root.showRawDetails
                Layout.fillWidth: true
                padding: 0
                implicitHeight: settingsRawColumn.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#d7e0ea" }

                ColumnLayout {
                    id: settingsRawColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Label { text: "상세 상태"; color: "#243447"; font.bold: true }
                    Label { Layout.fillWidth: true; text: "모델: " + appController.modelVersion + " / " + appController.modelSchema + " · " + appController.modelVendor; color: "#52606d"; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: "분석: " + appController.analysisContextText; color: "#52606d"; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: "최근: " + appController.operatorRecentSummary; color: "#52606d"; wrapMode: Text.WordWrap }

                    Repeater {
                        model: appController.operatorRecentEvents
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Math.round(6 * root.uiScale)
                            Components.StatusBadge { text: modelData.category; kind: modelData.level === "ERR" ? "bad" : (modelData.level === "WARN" ? "warn" : "info"); uiScale: root.uiScale }
                            Label { text: modelData.summary; color: "#243447"; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.ageText + " 전"; color: "#607080"; font.pixelSize: Math.round(10.6 * root.uiScale) }
                        }
                    }
                }
            }
        }
    }
}
