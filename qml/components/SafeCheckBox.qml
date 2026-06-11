import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

CheckBox {
    id: root
    property real uiScale: 1.0
    property bool layoutProbe: true
    property int minControlWidth: Math.round(54 * uiScale)
    property int maxControlWidth: Math.round(130 * uiScale)
    property string tipText: text

    implicitWidth: Math.min(maxControlWidth, Math.max(minControlWidth, indicator.width + safeLabel.implicitWidth + Math.round(14 * uiScale)))
    implicitHeight: Math.round(28 * uiScale)
    Layout.minimumWidth: 0
    Layout.minimumHeight: implicitHeight
    padding: 0
    spacing: Math.round(5 * uiScale)

    contentItem: Components.SafeText {
        id: safeLabel
        anchors.left: parent.left
        anchors.leftMargin: root.indicator.width + Math.round(6 * root.uiScale)
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height
        text: root.text
        uiScale: root.uiScale
        basePixelSize: Math.round(10.5 * root.uiScale)
        minPixelSize: Math.max(8, Math.round(8.2 * root.uiScale))
        color: root.enabled ? "#243447" : "#94a3b8"
        showOverflowTip: false
    }

    ToolTip.visible: hovered && safeLabel.overflowed
    ToolTip.text: tipText
}
