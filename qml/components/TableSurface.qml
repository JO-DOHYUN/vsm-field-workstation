import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    property real uiScale: 1.0
    property real minimumContentWidth: width
    property alias surfaceData: surface.data
    default property alias contentData: surface.data

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: Math.max(width, minimumContentWidth)
    contentHeight: surface.implicitHeight
    flickableDirection: Flickable.HorizontalAndVerticalFlick

    ColumnLayout {
        id: surface
        width: root.contentWidth
        spacing: Math.round(4 * root.uiScale)
    }

    ScrollBar.vertical: ScrollBar {
        policy: root.contentHeight > root.height + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }
    ScrollBar.horizontal: ScrollBar {
        policy: root.contentWidth > root.width + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }
}
