import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 22
    radius: 5
    color: mainArea.containsMouse ? "#e7eef7" : "transparent"
    border.color: hasActiveFilter ? "#7ea5ff" : "transparent"
    border.width: hasActiveFilter ? 1 : 0

    property string title: ""
    property string sortIndicator: ""
    property string filterText: ""
    property string filterPlaceholder: title + " 필터"
    property bool resizable: true
    property int minimumWidth: 70
    property int labelPixelSize: 11
    property color textColor: "#243447"
    property color accentColor: "#2563eb"
    readonly property bool hasActiveFilter: filterText.trim().length > 0

    signal sortRequested()
    signal sortAscendingRequested()
    signal sortDescendingRequested()
    signal filterApplied(string text)
    signal filterCleared()
    signal resizeRequested(real delta)

    MouseArea {
        id: mainArea
        anchors.fill: parent
        anchors.rightMargin: root.resizable ? 6 : 0
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                menuPopup.open()
            else
                root.sortRequested()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: resizable ? 7 : 4
        spacing: 2

        Label {
            Layout.fillWidth: true
            text: root.title + root.sortIndicator
            color: root.textColor
            font.bold: true
            font.pixelSize: root.labelPixelSize
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            visible: root.hasActiveFilter
            width: 6
            height: 6
            radius: 3
            color: root.accentColor
        }

        ToolButton {
            id: menuButton
            text: "▾"
            padding: 0
            implicitWidth: 14
            implicitHeight: 14
            font.pixelSize: 9
            onClicked: menuPopup.open()
            background: Rectangle {
                radius: 4
                color: menuButton.hovered ? "#dce7f5" : "transparent"
            }
        }
    }

    MouseArea {
        id: resizeArea
        visible: root.resizable
        width: 6
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real lastX: 0
        onPressed: function(mouse) { lastX = mouse.x }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const delta = mouse.x - lastX
            if (delta !== 0) {
                root.resizeRequested(delta)
                lastX = mouse.x
            }
        }
    }

    Popup {
        id: menuPopup
        y: root.height + 4
        x: Math.max(0, root.width - width)
        width: 228
        modal: false
        focus: true
        padding: 12
        background: Rectangle {
            radius: 10
            color: "#ffffff"
            border.color: "#cfd8e3"
        }

        contentItem: ColumnLayout {
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                height: 34
                radius: 8
                color: "#eef3f8"
                Label {
                    anchors.centerIn: parent
                    text: root.title
                    color: root.textColor
                    font.bold: true
                    font.pixelSize: 13
                }
            }

            TextField {
                id: filterField
                Layout.fillWidth: true
                placeholderText: root.filterPlaceholder
                selectByMouse: true
                onAccepted: {
                    root.filterApplied(text)
                    menuPopup.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    Layout.fillWidth: true
                    text: "오름차순"
                    onClicked: {
                        root.sortAscendingRequested()
                        menuPopup.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: "내림차순"
                    onClicked: {
                        root.sortDescendingRequested()
                        menuPopup.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    Layout.fillWidth: true
                    text: "필터 적용"
                    highlighted: true
                    onClicked: {
                        root.filterApplied(filterField.text)
                        menuPopup.close()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: "필터 지우기"
                    onClicked: {
                        filterField.text = ""
                        root.filterCleared()
                        menuPopup.close()
                    }
                }
            }
        }

        onOpened: {
            filterField.text = root.filterText
            filterField.forceActiveFocus()
            filterField.selectAll()
        }
    }
}
