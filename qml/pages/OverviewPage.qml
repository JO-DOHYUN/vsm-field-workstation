import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root
    objectName: "overviewPage"
    property real uiScale: 1.0
    signal openPanel(string key)
    signal focusIssue(string key, string idText)
    signal openReplay()
    signal exportSnapshot()

    function kindFromLevel(level) {
        if (level === "ERR") return "bad"
        if (level === "WARN") return "warn"
        if (level === "OK") return "ok"
        return "info"
    }

    function issueKindLabel(kind) {
        if (kind === "bus") return "버스"
        if (kind === "timing") return "주기"
        if (kind === "value") return "값"
        if (kind === "alarm") return "경보"
        return "상태"
    }

    function openPrimaryIssue() {
        if (appController.primaryIssueTargetTab === "timing")
            root.focusIssue("timing", appController.primaryIssueId)
        else if (appController.primaryIssueTargetTab === "value")
            root.focusIssue("value", appController.primaryIssueId)
        else if (appController.primaryIssueTargetTab === "alarm")
            root.focusIssue("alarm", appController.primaryIssueId)
        else
            root.openPanel(appController.primaryIssueTargetTab)
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: Math.max(0, root.width - Math.round(18 * root.uiScale))
            spacing: Math.round(9 * root.uiScale)

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: overviewHeroColumn.implicitHeight + Math.round(24 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }

                ColumnLayout {
                    id: overviewHeroColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeText {
                            text: "현장 개요"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(18 * root.uiScale)
                            font.bold: true
                            color: "#243447"
                            width: Math.max(Math.round(150 * root.uiScale), Math.min(Math.round(420 * root.uiScale), root.width - Math.round(320 * root.uiScale)))
                        }
                        Components.StatusBadge { text: "입력 " + appController.sourceStateLevel; kind: root.kindFromLevel(appController.sourceStateLevel); uiScale: root.uiScale }
                        Components.StatusBadge { text: "분석 " + appController.analysisModeLevel; kind: root.kindFromLevel(appController.analysisModeLevel); uiScale: root.uiScale }
                        Components.StatusBadge { text: "로그 " + appController.loggingStateLevel; kind: root.kindFromLevel(appController.loggingStateLevel); uiScale: root.uiScale }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: appController.operatorActionLevel === "ERR" ? "#fff1f2" : (appController.operatorActionLevel === "WARN" ? "#fff7ed" : "#f8fafc")
                        border.color: appController.operatorActionLevel === "ERR" ? "#fecdd3" : (appController.operatorActionLevel === "WARN" ? "#fdba74" : "#dbe5f0")
                        implicitHeight: headlineLabel.implicitHeight + Math.round(16 * root.uiScale)
                        Components.SafeText {
                            id: headlineLabel
                            anchors.fill: parent
                            anchors.margins: Math.round(8 * root.uiScale)
                            text: appController.operatorHeadline + " · " + appController.operatorActionText
                            color: appController.operatorActionLevel === "ERR" ? "#9f1239" : (appController.operatorActionLevel === "WARN" ? "#9a3412" : "#35506b")
                            uiScale: root.uiScale
                            basePixelSize: Math.round(11.5 * root.uiScale)
                            maxLines: 2
                        }
                    }

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale

                        Components.SafeButton { text: "라이브"; uiScale: root.uiScale; onClicked: root.openPanel("live") }
                        Components.SafeButton { text: "재생"; uiScale: root.uiScale; onClicked: root.openPanel("replay") }
                        Components.SafeButton { text: "재생 열기"; uiScale: root.uiScale; onClicked: root.openReplay() }
                        Components.SafeButton {
                            text: appController.replayPlaying ? "재생 정지" : "재생 시작"
                            uiScale: root.uiScale
                            enabled: appController.replayLoaded
                            onClicked: {
                                root.openPanel("replay")
                                if (appController.replayPlaying)
                                    appController.pauseReplay()
                                else
                                    appController.playReplay(appController.replaySpeed > 0 ? appController.replaySpeed : 1.0)
                            }
                        }
                        Components.SafeButton {
                            text: appController.logRecordingActive ? "로그 기록 중" : "로그 시작"
                            uiScale: root.uiScale
                            enabled: appController.connected && !appController.logRecordingActive && !appController.logPendingSave && !appController.logStopping && !appController.logSaving
                            onClicked: {
                                root.openPanel("live")
                                appController.startLog()
                            }
                        }
                        Components.SafeButton {
                            text: appController.logRecordingActive ? "로그 중지" : "저장/확정"
                            uiScale: root.uiScale
                            enabled: (appController.logRecordingActive || appController.logPendingSave) && !appController.logStopping && !appController.logSaving
                            onClicked: {
                                root.openPanel("live")
                                appController.stopLog()
                            }
                        }
                        Components.SafeButton { text: "스냅샷"; uiScale: root.uiScale; onClicked: root.exportSnapshot() }
                        Components.SafeButton { text: "최우선 이슈"; uiScale: root.uiScale; enabled: appController.primaryIssueTargetTab !== ""; onClicked: root.openPrimaryIssue() }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width > 1320 ? 4 : (root.width > 840 ? 2 : 1)
                rowSpacing: Math.round(8 * root.uiScale)
                columnSpacing: Math.round(8 * root.uiScale)

                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "입력"
                    value: appController.sourceStateText
                    note: appController.liveStatsSummary
                    badgeText: appController.connected ? "LIVE" : (appController.replayLoaded ? "REPLAY" : "OFF")
                    kind: root.kindFromLevel(appController.sourceStateLevel)
                    preferredHeight: Math.round(104 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel(appController.connected ? "live" : "replay")
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "버스"
                    value: appController.busHealthLevel
                    note: appController.busHealthText
                    badgeText: "BUS"
                    kind: root.kindFromLevel(appController.busHealthLevel)
                    preferredHeight: Math.round(104 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel("settings")
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "로그"
                    value: appController.loggingStateText
                    note: appController.logStatusSummary
                    badgeText: "LOG"
                    kind: root.kindFromLevel(appController.loggingStateLevel)
                    preferredHeight: Math.round(104 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel("live")
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "제어"
                    value: appController.controlOperatorSummary
                    note: appController.controlActionVerdict
                    badgeText: appController.controlReady ? "READY" : "BLOCK"
                    kind: appController.controlReady ? "ok" : "warn"
                    preferredHeight: Math.round(104 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel("control")
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: overviewIssueColumn.implicitHeight + Math.round(24 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }
                ColumnLayout {
                    id: overviewIssueColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    RowLayout {
                        Layout.fillWidth: true
                        Components.SafeText { text: "이슈 요약"; uiScale: root.uiScale; basePixelSize: Math.round(15 * root.uiScale); font.bold: true; color: "#243447"; Layout.fillWidth: true }
                        Components.StatusBadge { text: root.issueKindLabel(appController.primaryIssueKind); kind: root.kindFromLevel(appController.operatorActionLevel); uiScale: root.uiScale }
                    }
                    Components.SafeText {
                        Layout.fillWidth: true
                        text: (appController.primaryIssueId !== "" ? (appController.primaryIssueId + " · ") : "") + appController.primaryIssueSummary
                        color: "#52606d"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(11.0 * root.uiScale)
                        maxLines: 2
                    }
                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeButton { text: "이슈 위치"; uiScale: root.uiScale; onClicked: root.openPrimaryIssue() }
                        Components.SafeButton { text: "이전 이슈"; uiScale: root.uiScale; enabled: appController.primaryIssueSeekAvailable; onClicked: appController.seekReplayPrimaryIssue(-1) }
                        Components.SafeButton { text: "다음 이슈"; uiScale: root.uiScale; enabled: appController.primaryIssueSeekAvailable; onClicked: appController.seekReplayPrimaryIssue(1) }
                        Components.SafeButton { text: "주기"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * root.uiScale); onClicked: root.openPanel("timing") }
                        Components.SafeButton { text: "값"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * root.uiScale); onClicked: root.openPanel("value") }
                        Components.SafeButton { text: "경보"; uiScale: root.uiScale; maxButtonWidth: Math.round(62 * root.uiScale); onClicked: root.openPanel("alarm") }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width > 1180 ? 3 : 1
                rowSpacing: Math.round(8 * root.uiScale)
                columnSpacing: Math.round(8 * root.uiScale)

                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "분석 소스"
                    value: appController.analysisSourceText
                    note: appController.analysisContextText
                    badgeText: appController.replayAnalysisActive ? "REPLAY" : "LIVE"
                    kind: appController.replayAnalysisActive ? "warn" : "ok"
                    preferredHeight: Math.round(98 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel(appController.replayLoaded ? "replay" : "live")
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "재생"
                    value: appController.replayLoaded ? appController.replayCursorSummary : "재생 파일 없음"
                    note: appController.replayIssueSummary
                    badgeText: appController.replayLoaded ? (appController.replayPlaying ? "RUN" : "HOLD") : "NONE"
                    kind: appController.replayLoaded ? (appController.replayPlaying ? "warn" : "info") : "info"
                    preferredHeight: Math.round(98 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel("replay")
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "모델"
                    value: appController.modelActive ? appController.modelName : "모델 해제"
                    note: appController.modelDiagnosticsSummary
                    badgeText: appController.modelActive ? "MODEL" : "RAW"
                    kind: root.kindFromLevel(appController.modelDiagnosticsLevel)
                    preferredHeight: Math.round(98 * root.uiScale)
                    uiScale: root.uiScale
                    clickable: true
                    onClicked: root.openPanel("settings")
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: overviewStatusFlow.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: "#dbe5f0" }
                Components.FlowToolbar {
                    id: overviewStatusFlow
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(6 * root.uiScale)
                    uiScale: root.uiScale
                    Components.StatusBadge { text: "시스템 " + appController.systemLevel; kind: root.kindFromLevel(appController.systemLevel); uiScale: root.uiScale }
                    Components.StatusBadge { text: "주기 " + appController.timingLevel; kind: root.kindFromLevel(appController.timingLevel); uiScale: root.uiScale }
                    Components.StatusBadge { text: "값 " + appController.valueLevel; kind: root.kindFromLevel(appController.valueLevel); uiScale: root.uiScale }
                    Components.StatusBadge { text: "경보 " + appController.alarmLevel; kind: root.kindFromLevel(appController.alarmLevel); uiScale: root.uiScale }
                    Components.SafeText {
                        width: Math.max(Math.round(240 * root.uiScale), root.width - Math.round(460 * root.uiScale))
                        text: appController.operatorRecentSummary
                        color: "#607080"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.6 * root.uiScale)
                    }
                }
            }
        }
    }
}
