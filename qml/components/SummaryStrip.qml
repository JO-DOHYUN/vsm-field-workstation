import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root
    property real uiScale: 1.0
    property string title: "상태"
    property string level: "OK"
    property string summary: ""
    property var detailsModel: []
    readonly property bool hasDetails: detailsModel !== undefined && detailsModel !== null && detailsModel.length !== undefined && detailsModel.length > 0

    function kindFromLevel(levelValue) {
        if (levelValue === "ERR") return "bad"
        if (levelValue === "WARN") return "warn"
        if (levelValue === "OK") return "ok"
        return "info"
    }

    Layout.fillWidth: true
    radius: Math.round(8 * uiScale)
    color: level === "ERR" ? "#fff1f2" : (level === "WARN" ? "#fff7ed" : "#f8fafc")
    border.color: level === "ERR" ? "#fecdd3" : (level === "WARN" ? "#fed7aa" : "#dbe5f0")
    implicitHeight: Math.max(Math.round(34 * uiScale), stripFlow.implicitHeight + Math.round(10 * uiScale))

    Components.FlowToolbar {
        id: stripFlow
        anchors.fill: parent
        anchors.margins: Math.round(5 * root.uiScale)
        spacing: Math.round(7 * root.uiScale)
        uiScale: root.uiScale

        Components.StatusBadge {
            text: root.title + " " + root.level
            kind: root.kindFromLevel(root.level)
            uiScale: root.uiScale
            maxWidth: Math.round(142 * root.uiScale)
        }

        Components.SafeText {
            text: root.summary
            width: Math.max(Math.round(260 * root.uiScale), Math.min(Math.round(720 * root.uiScale), root.width - Math.round(190 * root.uiScale)))
            color: root.level === "ERR" ? "#9f1239" : (root.level === "WARN" ? "#9a3412" : "#334155")
            uiScale: root.uiScale
            basePixelSize: Math.round(10.6 * root.uiScale)
        }

        Components.SafeButton {
            text: "상세"
            enabled: root.hasDetails
            visible: root.hasDetails
            uiScale: root.uiScale
            maxButtonWidth: Math.round(72 * root.uiScale)
            onClicked: detailsPopup.open()
        }
    }

    Popup {
        id: detailsPopup
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(Math.max(Math.round(420 * root.uiScale), root.width), Math.round(760 * root.uiScale))
        height: Math.min(Math.round(360 * root.uiScale), detailsFlick.contentHeight + Math.round(24 * root.uiScale))
        x: Math.max(0, root.width - width)
        y: root.height + Math.round(4 * root.uiScale)

        background: Rectangle {
            radius: Math.round(8 * root.uiScale)
            color: "#ffffff"
            border.color: "#cbd5e1"
        }

        contentItem: Flickable {
            id: detailsFlick
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: detailsColumn.implicitHeight

            ColumnLayout {
                id: detailsColumn
                width: detailsFlick.width
                spacing: Math.round(6 * root.uiScale)

                Repeater {
                    model: root.detailsModel
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        radius: Math.round(7 * root.uiScale)
                        color: "#f8fafc"
                        border.color: "#dbe5f0"
                        implicitHeight: detailText.implicitHeight + Math.round(12 * root.uiScale)

                        Components.SafeText {
                            id: detailText
                            anchors.fill: parent
                            anchors.margins: Math.round(6 * root.uiScale)
                            text: (modelData.title || modelData.key || "-")
                                  + ": "
                                  + (modelData.value || "")
                                  + ((modelData.detail || modelData.note) ? (" · " + (modelData.detail || modelData.note)) : "")
                            color: modelData.level === "ERR" ? "#be123c" : (modelData.level === "WARN" ? "#9a3412" : "#334155")
                            uiScale: root.uiScale
                            basePixelSize: Math.round(10.4 * root.uiScale)
                            maxLines: 4
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: detailsFlick.contentHeight > detailsFlick.height + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
        }
    }
}
