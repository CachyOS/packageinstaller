import QtQuick
import QtQuick.Controls.Basic

// A rail entry: the icon above its label, the active one filled. The rail sets
// the width; the height here keeps every entry on the same rhythm.
AbstractButton {
    id: root

    property string glyph
    property bool current: false
    property int badge: 0

    implicitHeight: 62
    Accessible.role: Accessible.PageTab
    Accessible.name: text

    background: Rectangle {
        radius: 12
        color: root.current ? Look.accentSoft : (root.hovered ? Look.surfaceAlt : "transparent")
        border.width: root.visualFocus ? 2 : 0
        border.color: Look.accent
    }

    contentItem: Item {
        id: content

        Rectangle {
            id: disc
            width: 32
            height: 32
            radius: 16
            color: Look.accent
            visible: root.current
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
        }

        Glyph {
            anchors.centerIn: disc
            path: root.glyph
            size: root.current ? 19 : 20
            color: root.current ? Look.onAccent
                : root.hovered ? Look.text : Look.textMuted
        }

        PlainText {
            anchors.top: disc.bottom
            anchors.topMargin: 5
            anchors.horizontalCenter: parent.horizontalCenter
            width: content.width - 8
            text: root.text
            color: root.current ? Look.text : Look.textMuted
            font.pixelSize: Look.fontSmall
            font.weight: root.current ? Font.DemiBold : Font.Medium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Pill {
            id: badgePill
            visible: root.badge > 0
            text: root.badge
            color: Look.accent
            ink: Look.onAccent
            font.weight: Font.Bold
            height: 18
            width: Math.max(18, implicitWidth - 6)
            anchors.right: disc.right
            anchors.rightMargin: -9
            anchors.top: disc.top
            anchors.topMargin: -7
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
