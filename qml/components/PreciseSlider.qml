import QtQuick

Item {
    id: root
    implicitWidth: 180
    implicitHeight: 28

    property real from: 0.0
    property real to: 1.0
    property real value: from
    property real stepSize: 0.0
    property bool enabled: true
    property bool pressed: dragArea.pressed
    property bool liveUpdate: true
    property color trackColor: "#dbe5f0"
    property color fillColor: "#2563eb"
    property color handleColor: enabled ? "#ffffff" : "#f1f5f9"
    property color borderColor: enabled ? "#94a3b8" : "#cbd5e1"
    property real handleDiameter: 18
    property bool settling: false
    property int releaseHoldMs: 180
    readonly property bool visualHoldActive: pressed || settling || releaseHoldTimer.running
    readonly property real visualValue: visualHoldActive ? internalValue : value

    signal moved(real value)
    signal committed(real value)

    property real internalValue: value

    function clamp(v) {
        return Math.max(Math.min(v, Math.max(from, to)), Math.min(from, to))
    }

    function snapped(v) {
        const c = clamp(v)
        if (stepSize <= 0)
            return c
        const steps = Math.round((c - from) / stepSize)
        return clamp(from + steps * stepSize)
    }

    function ratioFor(v) {
        if (Math.abs(to - from) < 1e-9)
            return 0
        return (v - from) / (to - from)
    }

    function valueForX(xPos) {
        const left = handleDiameter * 0.5
        const right = width - handleDiameter * 0.5
        const usable = Math.max(1, right - left)
        const px = Math.max(left, Math.min(right, xPos))
        return snapped(from + ((px - left) / usable) * (to - from))
    }

    function updateFromMouse(xPos, emitLive) {
        const next = valueForX(xPos)
        if (Math.abs(next - internalValue) > 1e-9)
            internalValue = next
        if (emitLive)
            moved(internalValue)
    }

    onValueChanged: {
        if (!visualHoldActive)
            internalValue = value
    }


    Timer {
        id: releaseHoldTimer
        interval: root.releaseHoldMs
        repeat: false
        onTriggered: root.settling = false
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 8
        radius: 4
        color: root.trackColor
        border.color: Qt.darker(root.trackColor, 1.08)
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        width: Math.max(handleDiameter * 0.5, Math.min(parent.width, handleDiameter * 0.5 + ratioFor(root.visualValue) * Math.max(0, parent.width - handleDiameter)))
        height: 8
        radius: 4
        color: root.enabled ? root.fillColor : "#94a3b8"
    }

    Rectangle {
        id: handle
        width: root.handleDiameter
        height: root.handleDiameter
        radius: width / 2
        y: Math.round((root.height - height) / 2)
        x: Math.max(0, Math.min(root.width - width, ratioFor(root.visualValue) * Math.max(0, root.width - width)))
        color: root.handleColor
        border.color: root.borderColor
        border.width: 1
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: function(mouse) {
            releaseHoldTimer.stop()
            root.settling = false
            root.updateFromMouse(mouse.x, root.liveUpdate)
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            root.updateFromMouse(mouse.x, root.liveUpdate)
        }
        onReleased: function(mouse) {
            root.settling = true
            root.updateFromMouse(mouse.x, root.liveUpdate)
            releaseHoldTimer.restart()
            root.committed(root.internalValue)
        }
        onCanceled: {
            root.settling = true
            releaseHoldTimer.restart()
            root.committed(root.internalValue)
        }
    }
}
