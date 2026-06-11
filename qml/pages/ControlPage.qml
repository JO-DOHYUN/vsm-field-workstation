import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as Components

Item {
    id: root
    objectName: "controlPage"
    property real uiScale: 1.0
    property string monoFontFamily: "Monospace"
    readonly property int driveButtonWidth: Math.round(110 * uiScale)
    readonly property int driveButtonHeight: Math.round(30 * uiScale)

    focus: true
    onActiveFocusChanged: {
        if (!activeFocus) root.releaseAllDriveKeys()
    }
    onVisibleChanged: {
        if (!visible) root.releaseAllDriveKeys()
    }
    Component.onDestruction: root.releaseAllDriveKeys()

    function keyboardDriveKey(event) {
        const keypad = (event.modifiers & Qt.KeypadModifier) !== 0
        if (event.key === Qt.Key_Space) return "space"
        if (!keypad && event.key === Qt.Key_1) return "1"
        if (!keypad && event.key === Qt.Key_2) return "2"
        if (!keypad && event.key === Qt.Key_3) return "3"
        if (event.key === Qt.Key_W || event.key === Qt.Key_Up) return "w"
        if (event.key === Qt.Key_S || event.key === Qt.Key_Down) return "s"
        if (event.key === Qt.Key_A || event.key === Qt.Key_Left) return "a"
        if (event.key === Qt.Key_D || event.key === Qt.Key_Right) return "d"
        if (event.key === Qt.Key_X || event.key === Qt.Key_Escape) return "x"
        return ""
    }

    function releaseAllDriveKeys() {
        appController.controlKeyboardReleaseAll()
    }

    Keys.onPressed: function(event) {
        var key = root.keyboardDriveKey(event)
        if (key === "") return
        event.accepted = true
        if (event.isAutoRepeat) return
        if (key === "space") {
            appController.controlEmergencyStop()
            return
        }
        if (key === "1" || key === "2" || key === "3") {
            appController.controlKeyboardCommand(key)
            return
        }
        if (key === "x") {
            root.releaseAllDriveKeys()
            appController.controlSendNeutral()
            return
        }
        appController.controlKeyboardPress(key)
    }
    Keys.onReleased: function(event) {
        var key = root.keyboardDriveKey(event)
        if (key === "" || key === "x" || key === "space" || key === "1" || key === "2" || key === "3") return
        event.accepted = true
        if (event.isAutoRepeat) return
        appController.controlKeyboardRelease(key)
    }

    function badgeKindForReady() {
        return appController.controlReady ? "ok" : "warn"
    }

    function txKind() {
        return appController.controlActualTxConfirmed ? "ok" : "warn"
    }

    function faultKind() {
        return appController.controlFaultActive ? "bad" : "ok"
    }

    function ackKind() {
        return appController.controlLastAckSummary.indexOf("REJECTED") >= 0 ? "bad" : "info"
    }

    function stageFill(level) {
        if (level === "ok") return "#ecfdf3"
        if (level === "error") return "#fff1f2"
        if (level === "warn") return "#fff7ed"
        return "#f8fafc"
    }

    function stageBorder(level) {
        if (level === "ok") return "#bbf7d0"
        if (level === "error") return "#fecdd3"
        if (level === "warn") return "#fed7aa"
        return "#dbe5f0"
    }

    function stageTitleColor(level) {
        if (level === "ok") return "#166534"
        if (level === "error") return "#be123c"
        if (level === "warn") return "#9a3412"
        return "#475569"
    }

    function testControlStageCount() {
        return appController.controlEvidenceStages.length
    }

    function testControlStageSummary(index) {
        if (index < 0 || index >= appController.controlEvidenceStages.length) return ""
        return appController.controlEvidenceStages[index].title + " " + appController.controlEvidenceStages[index].summary
    }

    function testControlStageField(index, field) {
        if (index < 0 || index >= appController.controlEvidenceStages.length) return ""
        return appController.controlEvidenceStages[index][field]
    }

    function testControlOperatorSummary() {
        return appController.controlOperatorSummary
    }

    function testControlActionVerdict() {
        return appController.controlActionVerdict
    }

    function testControlChecklistCount() {
        return appController.controlOperatorChecklist.length
    }

    function testControlChecklistField(index, field) {
        if (index < 0 || index >= appController.controlOperatorChecklist.length) return ""
        return appController.controlOperatorChecklist[index][field]
    }

    function testControlPolicySummary() {
        return appController.controlPolicySummary
    }

    function testControlPolicyChecklistCount() {
        return appController.controlPolicyChecklist.length
    }

    function testControlActionEnabled(action) {
        if (action === "arm") return armButton.enabled
        if (action === "apply") return applyTargetButton.enabled
        if (action === "pattern") return patternSweepButton.enabled
        return false
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
                padding: Math.round(12 * root.uiScale)
                implicitHeight: controlHeroColumn.implicitHeight + Math.round(24 * root.uiScale)
                background: Rectangle { color: "#f8fbff"; radius: 14; border.color: "#cbd9ea" }

                ColumnLayout {
                    id: controlHeroColumn
                    anchors.fill: parent
                    spacing: Math.round(8 * root.uiScale)

                    Components.FlowToolbar {
                        Layout.fillWidth: true
                        uiScale: root.uiScale
                        spacing: Math.round(8 * root.uiScale)

                        Components.SafeText {
                            text: "차량 제어"
                            uiScale: root.uiScale
                            basePixelSize: Math.round(22 * root.uiScale)
                            font.bold: true
                            color: "#102033"
                            width: Math.round(132 * root.uiScale)
                        }
                        Components.StatusBadge {
                            text: appController.controlArmed ? "ARM" : "대기"
                            kind: appController.controlArmed ? "warn" : "info"
                            uiScale: root.uiScale
                            maxWidth: Math.round(95 * root.uiScale)
                        }
                        Components.StatusBadge {
                            text: appController.controlReady ? "제어 가능" : "제어 차단"
                            kind: root.badgeKindForReady()
                            uiScale: root.uiScale
                            maxWidth: Math.round(120 * root.uiScale)
                        }
                        Components.StatusBadge {
                            text: appController.controlActualTxConfirmed ? "CAN_TX 확인" : "CAN_TX 미확인"
                            kind: root.txKind()
                            uiScale: root.uiScale
                            maxWidth: Math.round(130 * root.uiScale)
                        }
                        Components.StatusBadge {
                            text: appController.controlFaultActive ? "fault/block" : "fault 없음"
                            kind: root.faultKind()
                            uiScale: root.uiScale
                            maxWidth: Math.round(120 * root.uiScale)
                        }
                        Components.StatusBadge {
                            text: appController.controlTestRunning ? "테스트 중" : "수동"
                            kind: appController.controlTestRunning ? "warn" : "ok"
                            uiScale: root.uiScale
                            maxWidth: Math.round(110 * root.uiScale)
                        }
                        Components.SafeButton {
                            text: "WSAD 포커스"
                            uiScale: root.uiScale
                            maxButtonWidth: Math.round(100 * root.uiScale)
                            onClicked: root.forceActiveFocus()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: appController.controlStatusSummary
                        wrapMode: Text.WordWrap
                        color: "#334155"
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: appController.boardConnectionSummary
                        wrapMode: Text.WordWrap
                        color: appController.boardAlive ? "#166534" : "#9a3412"
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: appController.controlActionVerdict + " | " + appController.controlOperatorSummary
                        wrapMode: Text.WordWrap
                        color: appController.controlReady ? "#166534" : "#9a3412"
                        font.family: root.monoFontFamily
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: appController.controlPolicySummary
                        wrapMode: Text.WordWrap
                        color: "#475569"
                        font.family: root.monoFontFamily
                        font.pixelSize: Math.round(10 * root.uiScale)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Math.round(10 * root.uiScale)

                Frame {
                    id: manualControlFrame
                    implicitHeight: manualControlColumn.implicitHeight + Math.round(24 * root.uiScale)
                    Layout.minimumWidth: Math.round(320 * root.uiScale)
                    Layout.preferredWidth: Math.round(440 * root.uiScale)
                    Layout.fillHeight: true
                    padding: Math.round(12 * root.uiScale)
                    background: Rectangle { color: "#ffffff"; radius: 14; border.color: "#d7e0ea" }

                    ColumnLayout {
                        id: manualControlColumn
                        anchors.fill: parent
                        spacing: Math.round(10 * root.uiScale)

                        Label {
                            text: "수동 주행 입력"
                            font.bold: true
                            font.pixelSize: Math.round(17 * root.uiScale)
                            color: "#172033"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "W/S 또는 방향키 상/하는 누르는 동안만 전후진합니다. 키를 떼면 목표 구동은 0으로 빠르게 감속하고, A/D 또는 좌/우는 최대 45도까지 누르는 동안만 조향합니다. Space는 즉시 정지, 숫자 1/2/3은 조향 0/크랩 +90/크랩 -90입니다."
                            wrapMode: Text.WordWrap
                            color: "#64748b"
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Math.round(8 * root.uiScale)
                            rowSpacing: Math.round(8 * root.uiScale)

                            Label { text: "목표 BUS"; color: "#475569" }
                            SpinBox {
                                from: 0
                                to: 15
                                value: appController.controlTargetBus
                                editable: true
                                Layout.fillWidth: true
                                onValueModified: appController.setControlTargetBus(value)
                            }
                            Label { text: "목표 rpm"; color: "#475569" }
                            SpinBox {
                                from: 0
                                to: 10000
                                stepSize: 100
                                value: appController.controlTargetRpm
                                editable: true
                                Layout.fillWidth: true
                                onValueModified: appController.setControlTargetRpm(value)
                            }
                            Label { text: "목표 조향"; color: "#475569" }
                            RowLayout {
                                Layout.fillWidth: true
                                Slider {
                                    from: -90
                                    to: 90
                                    stepSize: 1
                                    value: appController.controlTargetSteeringDeg
                                    Layout.fillWidth: true
                                    onMoved: appController.setControlTargetSteeringDeg(value)
                                }
                                Label {
                                    text: appController.controlTargetSteeringDeg.toFixed(1) + "°"
                                    Layout.preferredWidth: Math.round(58 * root.uiScale)
                                    horizontalAlignment: Text.AlignRight
                                    font.family: root.monoFontFamily
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 10
                            color: appController.controlReady ? "#ecfdf3" : "#fff7ed"
                            border.color: appController.controlReady ? "#bbf7d0" : "#fed7aa"
                            implicitHeight: verdictLabel.implicitHeight + Math.round(18 * root.uiScale)
                            Label {
                                id: verdictLabel
                                anchors.fill: parent
                                anchors.margins: Math.round(9 * root.uiScale)
                                text: appController.controlActionVerdict
                                wrapMode: Text.WordWrap
                                color: appController.controlReady ? "#166534" : "#9a3412"
                                font.bold: true
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Math.round(6 * root.uiScale)
                            rowSpacing: Math.round(6 * root.uiScale)

                            Repeater {
                                model: appController.controlOperatorChecklist
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.round(66 * root.uiScale)
                                    radius: 8
                                    color: root.stageFill(modelData.level)
                                    border.color: root.stageBorder(modelData.level)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: Math.round(7 * root.uiScale)
                                        spacing: 2
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label {
                                                text: modelData.title
                                                font.bold: true
                                                color: root.stageTitleColor(modelData.level)
                                                font.pixelSize: Math.round(10 * root.uiScale)
                                            }
                                            Item { Layout.fillWidth: true }
                                            Label {
                                                text: modelData.state
                                                color: modelData.blocking ? "#be123c" : "#475569"
                                                font.family: root.monoFontFamily
                                                font.pixelSize: Math.round(9 * root.uiScale)
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.detail
                                            elide: Text.ElideRight
                                            color: "#64748b"
                                            font.pixelSize: Math.round(9 * root.uiScale)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 10
                            color: "#f8fafc"
                            border.color: "#dbe5f0"
                            implicitHeight: busLabel.implicitHeight + Math.round(18 * root.uiScale)
                            Label {
                                id: busLabel
                                anchors.fill: parent
                                anchors.margins: Math.round(9 * root.uiScale)
                                text: appController.controlBusSummary
                                wrapMode: Text.WordWrap
                                color: "#334155"
                                font.pixelSize: Math.round(10 * root.uiScale)
                            }
                        }

                        Flow {
                            id: drivePad
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Math.round(8 * root.uiScale)

                            Item {
                                width: drivePad.width < Math.round(270 * root.uiScale) ? 0 : driveButtonWidth
                                height: width > 0 ? driveButtonHeight : 0
                                visible: width > 0
                            }
                            Components.SafeButton {
                                text: "W 전진"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onPressedChanged: pressed ? appController.controlKeyboardPress("w") : appController.controlKeyboardRelease("w")
                            }
                            Item {
                                width: drivePad.width < Math.round(270 * root.uiScale) ? 0 : driveButtonWidth
                                height: width > 0 ? driveButtonHeight : 0
                                visible: width > 0
                            }
                            Components.SafeButton {
                                text: "A 좌회전"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onPressedChanged: pressed ? appController.controlKeyboardPress("a") : appController.controlKeyboardRelease("a")
                            }
                            Components.SafeButton {
                                text: "X 중립"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlReady || appController.controlArmed
                                onClicked: {
                                    root.releaseAllDriveKeys()
                                    appController.controlSendNeutral()
                                }
                            }
                            Components.SafeButton {
                                text: "D 우회전"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onPressedChanged: pressed ? appController.controlKeyboardPress("d") : appController.controlKeyboardRelease("d")
                            }
                            Item {
                                width: drivePad.width < Math.round(270 * root.uiScale) ? 0 : driveButtonWidth
                                height: width > 0 ? 0 : driveButtonHeight
                                visible: width > 0
                            }
                            Components.SafeButton {
                                text: "S 후진"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onPressedChanged: pressed ? appController.controlKeyboardPress("s") : appController.controlKeyboardRelease("s")
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Math.round(8 * root.uiScale)
                            Components.SafeButton {
                                text: "Space 정지"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlReady || appController.controlArmed
                                onClicked: appController.controlEmergencyStop()
                            }
                            Components.SafeButton {
                                text: "1 조향 0"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlReady || appController.controlArmed
                                onClicked: appController.controlKeyboardCommand("1")
                            }
                            Components.SafeButton {
                                text: "2 크랩 +90"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onClicked: appController.controlKeyboardCommand("2")
                            }
                            Components.SafeButton {
                                text: "3 크랩 -90"
                                width: driveButtonWidth
                                buttonHeight: driveButtonHeight
                                enabled: appController.controlArmed && appController.controlReady
                                onClicked: appController.controlKeyboardCommand("3")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                id: armButton
                                text: appController.controlArmed ? "제어 해제" : "제어 ARM"
                                Layout.fillWidth: true
                                enabled: appController.controlReady || appController.controlArmed
                                highlighted: !appController.controlArmed
                                onClicked: appController.setControlArmed(!appController.controlArmed)
                            }
                            Button {
                                id: applyTargetButton
                                text: "현재 목표 적용"
                                Layout.fillWidth: true
                                enabled: appController.controlArmed && appController.controlReady
                                onClicked: appController.controlSendManual()
                            }
                        }
                    }
                }

                Frame {
                    id: controlEvidenceFrame
                    implicitHeight: controlEvidenceColumn.implicitHeight + Math.round(24 * root.uiScale)
                    Layout.minimumWidth: Math.round(560 * root.uiScale)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: Math.round(12 * root.uiScale)
                    background: Rectangle { color: "#ffffff"; radius: 14; border.color: "#d7e0ea" }

                    ColumnLayout {
                        id: controlEvidenceColumn
                        anchors.fill: parent
                        spacing: Math.round(10 * root.uiScale)

                        Label {
                            text: "시험 패턴"
                            font.bold: true
                            font.pixelSize: Math.round(17 * root.uiScale)
                            color: "#172033"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "패턴은 급격한 ±90도 스텝을 쓰지 않습니다. 저속/완만한 각도 변화로 0x503, 0x510~0x513을 반복 송신하고, 실제 성공은 CAN_TX_RAW audit로만 판단합니다."
                            wrapMode: Text.WordWrap
                            color: "#64748b"
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Math.round(8 * root.uiScale)
                            Button { id: patternSweepButton; text: "완만 조향 -30/+30"; enabled: appController.controlArmed && appController.controlReady; onClicked: appController.controlRunPattern("sweep") }
                            Button { text: "가변 rpm 조향"; enabled: appController.controlArmed && appController.controlReady; onClicked: appController.controlRunPattern("variable") }
                            Button { text: "저속 피벗"; enabled: appController.controlArmed && appController.controlReady; onClicked: appController.controlRunPattern("spin") }
                            Button { text: "패턴 정지"; enabled: appController.controlTestRunning; onClicked: appController.controlStopPattern() }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: Math.round(8 * root.uiScale)
                            rowSpacing: Math.round(8 * root.uiScale)

                            Repeater {
                                model: appController.controlEvidenceStages
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.round(112 * root.uiScale)
                                    radius: 8
                                    color: root.stageFill(modelData.level)
                                    border.color: root.stageBorder(modelData.level)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: Math.round(9 * root.uiScale)
                                        spacing: 3
                                        Label {
                                            text: modelData.title
                                            font.bold: true
                                            color: root.stageTitleColor(modelData.level)
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.summary
                                            wrapMode: Text.WordWrap
                                            elide: Text.ElideRight
                                            maximumLineCount: 2
                                            color: "#334155"
                                            font.family: root.monoFontFamily
                                            font.pixelSize: Math.round(10 * root.uiScale)
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.evidence
                                            color: "#64748b"
                                            elide: Text.ElideRight
                                            font.pixelSize: Math.round(9 * root.uiScale)
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.operatorText
                                            color: modelData.successAuthority ? "#166534" : "#64748b"
                                            elide: Text.ElideRight
                                            font.pixelSize: Math.round(9 * root.uiScale)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 10
                            color: "#fff7ed"
                            border.color: "#fed7aa"
                            implicitHeight: statsLabel.implicitHeight + Math.round(18 * root.uiScale)
                            Label {
                                id: statsLabel
                                anchors.fill: parent
                                anchors.margins: Math.round(9 * root.uiScale)
                                text: appController.controlEvidenceStatsSummary
                                wrapMode: Text.WordWrap
                                color: "#9a3412"
                                font.family: root.monoFontFamily
                                font.pixelSize: Math.round(10 * root.uiScale)
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: Math.round(180 * root.uiScale)
                            radius: 10
                            color: "#f8fafc"
                            border.color: "#dbe5f0"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Math.round(10 * root.uiScale)
                                spacing: Math.round(6 * root.uiScale)

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: "증거 타임라인"; font.bold: true; color: "#172033" }
                                    Label {
                                        text: "반복 OK 이벤트는 샘플링 표시"
                                        color: "#64748b"
                                        font.pixelSize: Math.round(11 * root.uiScale)
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: appController.controlEvidenceModel
                                    spacing: Math.round(4 * root.uiScale)
                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        radius: 8
                                        color: level === "ok" ? "#ecfdf3" : (level === "error" ? "#fff1f2" : (level === "warn" ? "#fff7ed" : "#ffffff"))
                                        border.color: level === "ok" ? "#bbf7d0" : (level === "error" ? "#fecdd3" : (level === "warn" ? "#fed7aa" : "#e2e8f0"))
                                        implicitHeight: eventColumn.implicitHeight + Math.round(12 * root.uiScale)

                                        ColumnLayout {
                                            id: eventColumn
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.margins: Math.round(7 * root.uiScale)
                                            spacing: 2

                                            RowLayout {
                                                Layout.fillWidth: true
                                                Label {
                                                    text: timeText + "  " + stage
                                                    color: "#0f172a"
                                                    font.bold: true
                                                    font.family: root.monoFontFamily
                                                    font.pixelSize: Math.round(10 * root.uiScale)
                                                }
                                                Label {
                                                    text: [commandId, bus, canId].filter(function(x) { return x && x.length > 0 }).join(" ")
                                                    color: "#475569"
                                                    font.family: root.monoFontFamily
                                                    font.pixelSize: Math.round(10 * root.uiScale)
                                                }
                                                Item { Layout.fillWidth: true }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: summary
                                                color: "#334155"
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: Math.round(11 * root.uiScale)
                                            }
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: appController.controlEvidenceModel.count === 0
                                        text: "아직 제어 evidence 없음"
                                        color: "#94a3b8"
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
