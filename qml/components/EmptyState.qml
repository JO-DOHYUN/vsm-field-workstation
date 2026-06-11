import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root
    property real uiScale: 1.0
    property string title: "표시할 항목 없음"
    property string message: ""
    property string badgeText: ""
    property string kind: "info"
    property bool layoutProbe: true

    radius: Math.round(10 * uiScale)
    color: "#f8fafc"
    border.color: "#dbe5f0"
    implicitWidth: Math.round(420 * uiScale)
    implicitHeight: emptyColumn.implicitHeight + Math.round(24 * uiScale)

    function badgeKind() {
        if (kind === "ok" || kind === "warn" || kind === "bad" || kind === "info")
            return kind
        if (kind === "ERR") return "bad"
        if (kind === "WARN") return "warn"
        if (kind === "OK") return "ok"
        return "info"
    }

    ColumnLayout {
        id: emptyColumn
        anchors.fill: parent
        anchors.margins: Math.round(12 * root.uiScale)
        spacing: Math.round(6 * root.uiScale)

        Components.FlowToolbar {
            Layout.fillWidth: true
            uiScale: root.uiScale
            spacing: Math.round(6 * root.uiScale)

            Components.SafeText {
                text: root.title
                width: Math.max(Math.round(180 * root.uiScale), parent.width - (root.badgeText !== "" ? Math.round(120 * root.uiScale) : 0))
                color: "#243447"
                font.bold: true
                uiScale: root.uiScale
                basePixelSize: Math.round(13 * root.uiScale)
                maxLines: 2
            }
            Components.StatusBadge {
                visible: root.badgeText !== ""
                text: root.badgeText
                kind: root.badgeKind()
                uiScale: root.uiScale
                maxWidth: Math.round(120 * root.uiScale)
            }
        }

        Components.SafeText {
            Layout.fillWidth: true
            text: root.message
            color: "#64748b"
            uiScale: root.uiScale
            basePixelSize: Math.round(10.8 * root.uiScale)
            maxLines: 3
        }
    }
}
