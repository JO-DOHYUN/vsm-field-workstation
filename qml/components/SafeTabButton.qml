import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

TabButton {
    id: root
    property real uiScale: 1.0
    property bool layoutProbe: true
    property int baseWidth: Math.round(92 * uiScale)
    property int minTabWidth: Math.round(58 * uiScale)
    property string tipText: text

    width: Math.max(minTabWidth, baseWidth)
    implicitHeight: Math.round(28 * uiScale)
    Layout.minimumWidth: 0
    Layout.minimumHeight: implicitHeight
    padding: 0

    contentItem: Components.SafeText {
        id: safeLabel
        anchors.fill: parent
        anchors.leftMargin: Math.round(6 * root.uiScale)
        anchors.rightMargin: Math.round(6 * root.uiScale)
        text: root.text
        uiScale: root.uiScale
        basePixelSize: Math.round(11.0 * root.uiScale)
        minPixelSize: Math.max(8, Math.round(8.5 * root.uiScale))
        horizontalAlignment: Text.AlignHCenter
        color: root.checked ? "#123a66" : "#52606d"
        font.bold: root.checked
        showOverflowTip: false
    }

    ToolTip.visible: hovered && safeLabel.overflowed
    ToolTip.text: tipText
}
