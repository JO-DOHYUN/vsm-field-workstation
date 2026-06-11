import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Label {
    id: root
    property real uiScale: 1.0
    property int basePixelSize: Math.round(11 * uiScale)
    property int minPixelSize: Math.max(8, Math.round(9 * uiScale))
    property int maxLines: 1
    property bool fitToBox: true
    property bool showOverflowTip: true
    property bool layoutProbe: true

    clip: true
    Layout.minimumWidth: 0
    Layout.minimumHeight: 0
    font.pixelSize: basePixelSize
    minimumPixelSize: minPixelSize
    fontSizeMode: fitToBox ? Text.Fit : Text.FixedSize
    maximumLineCount: maxLines
    wrapMode: maxLines > 1 ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter

    readonly property bool paintedOverflowed: paintedWidth > width + 1 || paintedHeight > height + 1
    readonly property bool overflowed: paintedOverflowed || truncated

    HoverHandler { id: safeTextHover }
    ToolTip.visible: showOverflowTip && safeTextHover.hovered && overflowed
    ToolTip.text: root.text
}
