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

    background: Rectangle {
        radius: Look.radius
        color: root.hovered ? Look.surfaceHover : Look.surface
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus || root.hovered ? Look.accent : Look.border
    }

    contentItem: Item {
        AppIcon {
            id: icon
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            pkgName: root.pkgName
            size: 60
        }

        Column {
            anchors.left: icon.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: root.title
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: root.summary
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
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

                Rectangle {
                    visible: root.installed
                    width: stateLabel.implicitWidth + 16
                    height: 20
                    radius: 10
                    color: root.upgradable ? Look.warningSoft : Look.surfaceAlt

                    Text {
                        id: stateLabel
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: root.upgradable ? qsTr("Update available") : qsTr("Installed")
                        color: root.upgradable ? Look.warning : Look.textMuted
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
