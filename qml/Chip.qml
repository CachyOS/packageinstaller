import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: root

    property bool current: false
    /// a lighter chip for lists of names, e.g. dependencies
    property bool small: false

    implicitHeight: small ? 28 : 30
    implicitWidth: label.implicitWidth + (small ? 24 : 28)

    background: Rectangle {
        radius: height / 2
        color: root.current ? Look.accent : root.hovered ? Look.surfaceAlt : Look.surface
        border.width: root.current ? 0 : 1
        border.color: root.visualFocus ? Look.accent : Look.border
    }

    contentItem: PlainText {
        id: label
        text: root.text
        color: root.current ? Look.onAccent : Look.text
        font.pixelSize: Look.fontLabel
        font.weight: root.small ? Font.Normal : Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
