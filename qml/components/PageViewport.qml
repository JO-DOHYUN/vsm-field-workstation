import QtQuick
import QtQuick.Controls

Flickable {
    id: root
    objectName: "pageViewport"
    property Component pageComponent
    property real minPageWidth: 0
    property bool active: true
    property real uiScale: 1.0
    readonly property real effectiveMinPageWidth: Math.max(0, minPageWidth * uiScale)
    readonly property bool needsHorizontalScroll: contentWidth > width + 1
    readonly property bool horizontalScrollEnabled: needsHorizontalScroll
    property bool layoutProbe: true

    clip: true
    visible: active
    boundsBehavior: Flickable.StopAtBounds
    boundsMovement: Flickable.StopAtBounds
    contentWidth: Math.max(width, effectiveMinPageWidth)
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    interactive: needsHorizontalScroll
    pixelAligned: true

    function clampValue(v, minV, maxV) {
        return Math.max(minV, Math.min(maxV, v))
    }

    Item {
        id: pageHost
        width: root.contentWidth
        height: root.height

        Loader {
            id: pageLoader
            anchors.fill: parent
            active: root.active
            sourceComponent: root.pageComponent
        }
    }

    ScrollBar.horizontal: ScrollBar {
        id: hbar
        policy: root.needsHorizontalScroll ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        active: root.needsHorizontalScroll
    }
}
