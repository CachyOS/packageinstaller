import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: root

    property string glyph
    property bool current: false
    property int badge: 0

    implicitHeight: 42
    Accessible.role: Accessible.PageTab
    Accessible.name: text

    background: Rectangle {
        radius: Look.radiusSmall
        color: root.current ? Look.accentSoft : (root.hovered ? Look.surfaceAlt : "transparent")
        border.width: root.visualFocus ? 2 : 0
        border.color: Look.accent
    }

    contentItem: Item {
        Glyph {
            id: icon
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            path: root.glyph
            color: root.current ? Look.accent : Look.textMuted
        }

        Text {
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: badge.visible ? badge.left : parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.current ? Look.text : Look.textMuted
            font.pixelSize: Look.fontBody
            font.weight: root.current ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
        }

        Rectangle {
            id: badge
            visible: root.badge > 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(22, badgeText.implicitWidth + 12)
            height: 20
            radius: 10
            color: Look.accent

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: root.badge
                color: Look.onAccent
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
