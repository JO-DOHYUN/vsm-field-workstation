import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root
    objectName: "settingsPage"
    property real uiScale: 1.0
    property bool showRawDetails: false
    property bool showPerformanceRows: false
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

    function selectedVerifyScenario() {
        var catalog = appController.verificationScenarioCatalog
        if (!catalog || verifyScenario.currentIndex < 0 || verifyScenario.currentIndex >= catalog.length)
            return {}
        return catalog[verifyScenario.currentIndex] || {}
    }

    function verifyScenarioField(name) {
        var scenario = selectedVerifyScenario()
        return scenario[name] || ""
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
                            text: "?ㅼ젙"
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
                        Components.StatusBadge { text: "議곗튂 " + appController.operatorActionLevel; kind: root.levelKind(appController.operatorActionLevel); uiScale: root.uiScale }
                        Components.SafeText {
                            width: Math.max(Math.round(280 * root.uiScale), root.width - Math.round(170 * root.uiScale))
                            text: appController.operatorHeadline + " 쨌 " + appController.operatorActionText
                            color: "#243447"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(10.8 * root.uiScale)
                        }
                    }

                    Components.SafeText {
                        Layout.fillWidth: true
                        text: appController.primaryIssueId !== "" ? (appController.primaryIssueId + " 쨌 " + appController.primaryIssueSummary) : appController.primaryIssueSummary
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
                    title: "紐⑤뜽"
                    value: appController.modelActive ? appController.modelName : "紐⑤뜽 ?댁젣"
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
                    title: "????꾩튂"
                    value: "project replay_data"
                    note: appController.defaultLogDirectory
                    badgeText: "PATH"
                    kind: "info"
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "?쇱씠釉?/ ?ъ깮"
                    value: appController.analysisSourceText
                    note: appController.activeViewStateSummary
                    badgeText: appController.replayAnalysisActive ? "REPLAY" : (appController.connected ? "LIVE" : "OFF")
                    kind: appController.replayAnalysisActive ? "warn" : (appController.connected ? "ok" : "info")
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "踰꾩뒪"
                    value: appController.busHealthLevel
                    note: appController.busHealthText
                    badgeText: "BUS"
                    kind: root.levelKind(appController.busHealthLevel)
                    preferredHeight: Math.round(92 * root.uiScale)
                    uiScale: root.uiScale
                }
                Components.InfoCard {
                    Layout.fillWidth: true
                    title: "?꾪꽣"
                    value: appController.activeViewStateSummary
                    note: "?쇱씠釉?" + (appController.liveFrameView.idFilter === "" ? "?꾩껜" : appController.liveFrameView.idFilter) + " 쨌 ?ъ깮 " + (appController.replayFrameView.idFilter === "" ? "?꾩껜" : appController.replayFrameView.idFilter)
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
                        Components.SafeText { text: "寃쎈줈 / ?몄뀡"; uiScale: root.uiScale; basePixelSize: Math.round(13 * root.uiScale); font.bold: true; color: "#243447"; width: Math.round(120 * root.uiScale) }
                        Components.StatusBadge { text: "?낇???" + appController.sessionUptimeText; kind: "info"; uiScale: root.uiScale }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 900 ? 2 : 1
                        rowSpacing: Math.round(6 * root.uiScale)
                        columnSpacing: Math.round(12 * root.uiScale)

                        Components.SafeText { text: "紐⑤뜽 ?뚯씪"; color: "#52606d"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.8 * root.uiScale) }
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
                Layout.fillWidth: true
                padding: 0
                implicitHeight: debugColumn.implicitHeight + Math.round(20 * root.uiScale)
                background: Rectangle { color: "#ffffff"; radius: 8; border.color: appController.debugProfilerEnabled ? "#8ec5ff" : "#d7e0ea" }

                ColumnLayout {
                    id: debugColumn
                    anchors.fill: parent
                    anchors.margins: Math.round(10 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeText { text: "디버그 / 검증"; uiScale: root.uiScale; basePixelSize: Math.round(13 * root.uiScale); font.bold: true; color: "#243447"; width: Math.round(126 * root.uiScale) }
                        Components.StatusBadge { text: appController.debugProfilerEnabled ? "계측 ON" : "계측 OFF"; kind: appController.debugProfilerEnabled ? "warn" : "info"; uiScale: root.uiScale }
                        Components.StatusBadge { text: appController.verificationRunnerActive ? "검증 실행 중" : "검증 대기"; kind: appController.verificationRunnerActive ? "warn" : "info"; uiScale: root.uiScale }
                    }

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        Components.SafeButton {
                            text: appController.debugProfilerEnabled ? "계측 끄기" : "계측 켜기"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(96 * root.uiScale)
                            onClicked: appController.toggleDebugProfiler()
                        }
                        Components.SafeButton {
                            text: "계측 초기화"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(96 * root.uiScale)
                            onClicked: appController.resetPerformanceMetrics()
                        }
                        Components.SafeCheckBox {
                            text: "상위 경로 보기"
                            uiScale: root.uiScale
                            maxControlWidth: Math.round(120 * root.uiScale)
                            checked: root.showPerformanceRows
                            onToggled: root.showPerformanceRows = checked
                        }
                        Components.SafeText {
                            text: appController.performanceSummary
                            color: "#35506b"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(10.6 * root.uiScale)
                            width: Math.max(Math.round(280 * root.uiScale), Math.min(Math.round(740 * root.uiScale), root.width - Math.round(360 * root.uiScale)))
                        }
                    }

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        ComboBox {
                            id: verifyScenario
                            width: Math.round(260 * root.uiScale)
                            model: appController.verificationScenarioCatalog
                            textRole: "title"
                            valueRole: "key"
                        }
                        ComboBox {
                            id: verifyDuration
                            width: Math.round(104 * root.uiScale)
                            textRole: "title"
                            valueRole: "key"
                            model: ListModel {
                                ListElement { title: "30s"; key: "30s" }
                                ListElement { title: "5m"; key: "5m" }
                                ListElement { title: "10m"; key: "10m" }
                                ListElement { title: "1h"; key: "1h" }
                                ListElement { title: "manual"; key: "manual" }
                            }
                        }
                        ComboBox {
                            id: verifyPort
                            width: Math.round(110 * root.uiScale)
                            editable: true
                            model: appController.availablePorts
                            Component.onCompleted: editText = appController.availablePorts.length > 0 ? appController.availablePorts[0] : "COM7"
                        }
                        Components.SafeButton {
                            text: "포트 새로고침"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(104 * root.uiScale)
                            onClicked: appController.refreshPorts()
                        }
                        Components.SafeButton {
                            text: appController.verificationRunnerActive ? "실행 중" : "검증 실행"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(92 * root.uiScale)
                            enabled: !appController.verificationRunnerActive
                            onClicked: appController.runVerificationScenario(verifyScenario.currentValue, verifyPort.editText, verifyDuration.currentValue)
                        }
                        Components.SafeButton {
                            text: "검증 중지"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(88 * root.uiScale)
                            enabled: appController.verificationRunnerActive
                            onClicked: appController.stopVerificationRunner()
                        }
                    }

                    Components.SafeText {
                        Layout.fillWidth: true
                        text: appController.verificationRunnerStatus + (appController.verificationRunnerArtifactPath !== "" ? (" | " + appController.verificationRunnerArtifactPath) : "")
                        color: appController.verificationRunnerActive ? "#9a5b00" : "#52606d"
                        uiScale: root.uiScale
                        basePixelSize: Math.round(10.6 * root.uiScale)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 7
                        color: "#f8fafc"
                        border.color: "#dbe5f0"
                        implicitHeight: verifyScenarioDetails.implicitHeight + Math.round(14 * root.uiScale)
                        ColumnLayout {
                            id: verifyScenarioDetails
                            anchors.fill: parent
                            anchors.margins: Math.round(7 * root.uiScale)
                            spacing: Math.round(3 * root.uiScale)
                            Components.SafeText { Layout.fillWidth: true; text: "검증 경로: " + root.verifyScenarioField("route"); color: "#243447"; uiScale: root.uiScale; basePixelSize: Math.round(10.4 * root.uiScale) }
                            Components.SafeText { Layout.fillWidth: true; text: "부하/모델: " + root.verifyScenarioField("load") + " | " + root.verifyScenarioField("model"); color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale) }
                            Components.SafeText { Layout.fillWidth: true; text: "검증 항목: " + root.verifyScenarioField("stress"); color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale) }
                            Components.SafeText { Layout.fillWidth: true; text: "판정 기준: " + root.verifyScenarioField("acceptance"); color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale) }
                            Components.SafeText { Layout.fillWidth: true; text: "주의: " + root.verifyScenarioField("safety") + " | 산출물 " + root.verifyScenarioField("artifactRoot"); color: "#9a3412"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale) }
                            Components.SafeText { Layout.fillWidth: true; text: "명령: " + root.verifyScenarioField("python") + " | 기간 " + verifyDuration.currentText; color: "#607080"; uiScale: root.uiScale; basePixelSize: Math.round(10.0 * root.uiScale) }
                        }
                    }

                    Repeater {
                        model: root.showPerformanceRows ? appController.performanceDiagnostics.slice(0, 12) : []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            radius: 7
                            color: "#f8fafc"
                            border.color: "#dbe5f0"
                            implicitHeight: Math.round(28 * root.uiScale)
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Math.round(5 * root.uiScale)
                                spacing: Math.round(8 * root.uiScale)
                                Components.SafeText { text: modelData.name || "-"; color: "#243447"; font.bold: true; uiScale: root.uiScale; basePixelSize: Math.round(10.4 * root.uiScale); Layout.fillWidth: true }
                                Components.SafeText { text: "calls " + (modelData.calls || "0"); color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale); Layout.preferredWidth: Math.round(96 * root.uiScale); horizontalAlignment: Text.AlignRight }
                                Components.SafeText { text: "p95 " + Math.round(modelData.p95Us || 0) + "us"; color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale); Layout.preferredWidth: Math.round(90 * root.uiScale); horizontalAlignment: Text.AlignRight }
                                Components.SafeText { text: "max " + Math.round(modelData.maxUs || 0) + "us"; color: "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale); Layout.preferredWidth: Math.round(90 * root.uiScale); horizontalAlignment: Text.AlignRight }
                                Components.SafeText { text: "over " + (modelData.overBudget || "0"); color: Number(modelData.overBudget || 0) > 0 ? "#b45309" : "#52606d"; uiScale: root.uiScale; basePixelSize: Math.round(10.2 * root.uiScale); Layout.preferredWidth: Math.round(76 * root.uiScale); horizontalAlignment: Text.AlignRight }
                            }
                        }
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

                    Label { text: "?곸꽭 ?곹깭"; color: "#243447"; font.bold: true }
                    Label { Layout.fillWidth: true; text: "紐⑤뜽: " + appController.modelVersion + " / " + appController.modelSchema + " 쨌 " + appController.modelVendor; color: "#52606d"; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: "遺꾩꽍: " + appController.analysisContextText; color: "#52606d"; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: "理쒓렐: " + appController.operatorRecentSummary; color: "#52606d"; wrapMode: Text.WordWrap }

                    Repeater {
                        model: appController.operatorRecentEvents
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Math.round(6 * root.uiScale)
                            Components.StatusBadge { text: modelData.category; kind: modelData.level === "ERR" ? "bad" : (modelData.level === "WARN" ? "warn" : "info"); uiScale: root.uiScale }
                            Label { text: modelData.summary; color: "#243447"; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.ageText + " ms"; color: "#607080"; font.pixelSize: Math.round(10.6 * root.uiScale) }
                        }
                    }
                }
            }
        }
    }
}
