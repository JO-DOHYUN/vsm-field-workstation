import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Item {
    id: root
    property real uiScale: 1.0
    property real minReadableWidth: Math.round(640 * uiScale)
    property alias contentItem: body
    property bool horizontalScrollAllowed: false
    default property alias contentData: body.data

    Components.UiMetrics {
        id: metrics
        uiScale: root.uiScale
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: root.horizontalScrollAllowed ? Math.max(width, root.minReadableWidth) : width
        contentHeight: body.implicitHeight
        flickableDirection: root.horizontalScrollAllowed ? Flickable.HorizontalAndVerticalFlick : Flickable.VerticalFlick

        ColumnLayout {
            id: body
            width: flick.contentWidth
            spacing: metrics.gapMd
        }

        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
        ScrollBar.horizontal: ScrollBar {
            policy: root.horizontalScrollAllowed && flick.contentWidth > flick.width + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
    }
}
