import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: root

    property string glyph
    property bool current: false
    property int badge: 0

    implicitHeight: 42
    Accessible.role: Accessible.PageTab

    background: Rectangle {
        radius: Look.radiusSmall
        color: root.current ? Look.accentSoft : (root.hovered ? Look.surfaceAlt : "transparent")
        border.width: root.visualFocus ? 2 : 0
        border.color: Look.accent
    }

    contentItem: Item {
        id: content

        Glyph {
            id: icon
            x: 12
            anchors.verticalCenter: content.verticalCenter
            path: root.glyph
            color: root.current ? Look.accent : Look.textMuted
        }

        PlainText {
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: badge.visible ? badge.left : content.right
            anchors.rightMargin: 8
            anchors.verticalCenter: content.verticalCenter
            text: root.text
            color: root.current ? Look.text : Look.textMuted
            font.weight: root.current ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
        }

        Pill {
            id: badge
            visible: root.badge > 0
            anchors.right: content.right
            anchors.rightMargin: 10
            anchors.verticalCenter: content.verticalCenter
            width: Math.max(22, implicitWidth - 4)
            text: root.badge
            ink: Look.onAccent
            color: Look.accent
            font.weight: Font.Bold
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
