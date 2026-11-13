import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root

    /// "primary", "soft", "danger" or "hero" (the start page banner)
    property string tone: "primary"
    /// "small", "normal" or "large"
    property string size: "normal"

    readonly property color fill: tone === "primary" ? (hovered ? Look.accentHover : Look.accent)
        : tone === "hero" ? (hovered ? Look.heroButtonHover : Look.heroButton)
        : tone === "danger" ? Look.dangerSoft
        : (hovered ? Look.border : Look.surfaceAlt)
    readonly property color ink: tone === "primary" ? Look.onAccent
        : tone === "hero" ? Look.heroInk
        : tone === "danger" ? Look.danger : Look.text

    implicitHeight: size === "small" ? 32 : size === "large" ? 42 : 36
    leftPadding: 18
    rightPadding: 18
    font.pixelSize: Look.fontBody
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.45

    contentItem: PlainText {
        text: root.text
        font: root.font
        color: root.ink
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: height / 2
        color: root.fill
        border.width: root.visualFocus ? 2 : 0
        border.color: Look.text
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
