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

    background: Card {
        radius: Look.radiusSmall + 2
        hovered: root.hovered
        focused: root.visualFocus
    }

    contentItem: Item {
        id: content

        AppIcon {
            id: icon
            x: 12
            anchors.verticalCenter: content.verticalCenter
            pkgName: root.pkgName
            size: 40
        }

        Column {
            id: texts
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: trailingText.left
            anchors.rightMargin: 12
            anchors.verticalCenter: content.verticalCenter
            spacing: 2

            Row {
                id: titleRow
                width: texts.width
                spacing: 8

                PlainText {
                    width: Math.min(implicitWidth, texts.width - badge.width - 8)
                    text: root.title
                    font.pixelSize: Look.fontLead
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                SourceBadge {
                    id: badge
                    anchors.verticalCenter: titleRow.verticalCenter
                    source: root.source
                }
            }

            Muted {
                width: texts.width
                text: root.summary
                elide: Text.ElideRight
            }
        }

        Muted {
            id: trailingText
            anchors.right: content.right
            anchors.rightMargin: 14
            anchors.verticalCenter: content.verticalCenter
            width: Math.min(implicitWidth, content.width * 0.42)
            horizontalAlignment: Text.AlignRight
            text: root.trailing
            color: root.trailingColor
            elide: Text.ElideMiddle
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
