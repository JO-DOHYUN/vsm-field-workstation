import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Button {
    id: root
    property real uiScale: 1.0
    property bool layoutProbe: true
    property string tipText: text
    Components.UiMetrics { id: metrics; uiScale: root.uiScale }
    property int minButtonWidth: metrics.buttonMinWidth
    property int maxButtonWidth: metrics.buttonMaxWidth
    property int buttonHeight: metrics.buttonHeight

    implicitWidth: Math.min(maxButtonWidth, Math.max(minButtonWidth, safeLabel.implicitWidth + Math.round(22 * uiScale)))
    implicitHeight: buttonHeight
    Layout.minimumWidth: 0
    Layout.minimumHeight: buttonHeight
    padding: 0

    contentItem: Components.SafeText {
        id: safeLabel
        anchors.fill: parent
        anchors.leftMargin: Math.round(8 * root.uiScale)
        anchors.rightMargin: Math.round(8 * root.uiScale)
        text: root.text
        uiScale: root.uiScale
        basePixelSize: Math.round(10.8 * root.uiScale)
        minPixelSize: Math.max(8, Math.round(8.5 * root.uiScale))
        horizontalAlignment: Text.AlignHCenter
        color: root.enabled ? "#172033" : "#8a94a3"
        font.bold: root.checked
        showOverflowTip: false
    }

    ToolTip.visible: hovered && safeLabel.overflowed
    ToolTip.text: tipText
}
