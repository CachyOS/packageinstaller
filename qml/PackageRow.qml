import QtQuick
import QtQuick.Controls.Basic

// A dense single-line entry, used for updates and AUR results.
AbstractButton {
    id: root

    required property string pkgName
    property string title
    property string summary
    property string source
    property string trailing
    property color trailingColor: Look.textMuted

    implicitHeight: 64
    Accessible.name: title

    background: Rectangle {
        radius: Look.radiusSmall + 2
        color: root.hovered ? Look.surfaceHover : Look.surface
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus || root.hovered ? Look.accent : Look.border
    }

    contentItem: Item {
        AppIcon {
            id: icon
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            pkgName: root.pkgName
            size: 40
        }

        Column {
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: trailingText.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Row {
                width: parent.width
                spacing: 8

                Text {
                    textFormat: Text.PlainText
                    width: Math.min(implicitWidth, parent.width - badge.width - 8)
                    text: root.title
                    color: Look.text
                    font.pixelSize: Look.fontBody + 1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                SourceBadge {
                    id: badge
                    anchors.verticalCenter: parent.verticalCenter
                    source: root.source
                }
            }

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: root.summary
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
                elide: Text.ElideRight
            }
        }

        Text {
            id: trailingText
            textFormat: Text.PlainText
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.42)
            horizontalAlignment: Text.AlignRight
            text: root.trailing
            color: root.trailingColor
            font.pixelSize: Look.fontSmall + 1
            elide: Text.ElideMiddle
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
