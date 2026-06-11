import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property real uiScale: 1.0
    property int spacing: Math.round(6 * uiScale)
    property bool layoutProbe: true
    default property alias content: toolbarFlow.data

    implicitHeight: toolbarFlow.implicitHeight
    Layout.minimumWidth: 0
    Layout.minimumHeight: implicitHeight
    Layout.fillWidth: true

    Flow {
        id: toolbarFlow
        width: parent.width
        spacing: root.spacing
    }
}
