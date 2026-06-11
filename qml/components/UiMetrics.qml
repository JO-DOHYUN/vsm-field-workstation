import QtQml

QtObject {
    id: root
    property real uiScale: 1.0

    readonly property int radiusSm: Math.round(6 * uiScale)
    readonly property int radiusMd: Math.round(8 * uiScale)
    readonly property int radiusLg: Math.round(10 * uiScale)
    readonly property int gapXs: Math.round(4 * uiScale)
    readonly property int gapSm: Math.round(6 * uiScale)
    readonly property int gapMd: Math.round(8 * uiScale)
    readonly property int gapLg: Math.round(10 * uiScale)

    readonly property int titleFont: Math.round(18 * uiScale)
    readonly property int sectionFont: Math.round(13 * uiScale)
    readonly property int bodyFont: Math.round(11 * uiScale)
    readonly property int captionFont: Math.round(10 * uiScale)
    readonly property int minReadableFont: Math.max(8, Math.round(8.5 * uiScale))

    readonly property int buttonHeight: Math.round(28 * uiScale)
    readonly property int buttonMinWidth: Math.round(54 * uiScale)
    readonly property int buttonMaxWidth: Math.round(150 * uiScale)
    readonly property int badgeHeight: Math.round(24 * uiScale)
    readonly property int badgeMaxWidth: Math.round(180 * uiScale)
}
