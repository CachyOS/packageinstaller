import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: root

    required property string pkgName
    property string title
    property string summary
    property string source
    property bool installed: false
    property bool upgradable: false

    implicitWidth: 300
    implicitHeight: 104
    Accessible.name: title
    Accessible.description: summary

    background: Card {
        hovered: root.hovered
        focused: root.visualFocus
    }

    contentItem: Item {
        id: content

        AppIcon {
            id: icon
            x: 14
            anchors.verticalCenter: content.verticalCenter
            pkgName: root.pkgName
            size: 60
        }

        Column {
            id: texts
            anchors.left: icon.right
            anchors.leftMargin: 14
            anchors.right: content.right
            anchors.rightMargin: 14
            anchors.verticalCenter: content.verticalCenter
            spacing: 4

            SectionTitle {
                width: texts.width
                text: root.title
                elide: Text.ElideRight
            }

            Muted {
                width: texts.width
                text: root.summary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.1
            }

            Row {
                spacing: 6
                topPadding: 2

                SourceBadge {
                    source: root.source
                }

                Pill {
                    visible: root.installed
                    text: root.upgradable ? qsTr("Update available") : qsTr("Installed")
                    glyph: root.upgradable ? "M12 19V5M6 11l6-6l6 6" : "M5 12.5l4.5 4.5L19 7"
                    ink: root.upgradable ? Look.warning : Look.text
                    glyphInk: root.upgradable ? Look.warning : Look.accent
                    color: root.upgradable ? Look.warningSoft : Look.surfaceAlt
                }
            }
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
