import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root

    // "primary", "soft" or "danger"
    property string tone: "primary"

    readonly property color fill: tone === "primary" ? (hovered ? Look.accentHover : Look.accent)
        : tone === "danger" ? Look.dangerSoft
        : (hovered ? Look.border : Look.surfaceAlt)
    readonly property color ink: tone === "primary" ? Look.onAccent : tone === "danger" ? Look.danger : Look.text

    implicitHeight: 36
    leftPadding: 18
    rightPadding: 18
    font.pixelSize: Look.fontBody
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.45

    contentItem: Text {
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
