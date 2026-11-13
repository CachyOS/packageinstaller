import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: root

    implicitHeight: 40
    leftPadding: 40
    rightPadding: 14
    font.pixelSize: Look.fontBody
    color: Look.text
    placeholderTextColor: Look.textMuted
    selectionColor: Look.accent
    selectedTextColor: Look.onAccent
    selectByMouse: true

    background: Rectangle {
        id: frame
        radius: height / 2
        color: Look.surface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Look.accent : Look.border

        Glyph {
            x: 13
            anchors.verticalCenter: frame.verticalCenter
            size: 17
            color: Look.textMuted
            path: "M11 4a7 7 0 1 0 0 14a7 7 0 0 0 0-14zM16 16l4.5 4.5"
        }
    }
}
