pragma ComponentBehavior: Bound
import QtQuick

Column {
    id: root

    required property string iconName
    required property string title
    required property string summary
    required property string source
    required property string byline
    property string unavailable
    /// a web page about the package: the title opens it, and so does `linkText` next to the byline
    property string link
    property string linkText
    property alias status: statusText.text
    property alias screenshots: strip.model
    property string description
    default property alias actions: actionColumn.data

    signal back

    spacing: 22

    Row {
        spacing: 14

        PillButton {
            id: backButton
            tone: "soft"
            size: "small"
            text: qsTr("‹  Back")
            onClicked: root.back()
        }

        Muted {
            id: statusText
            visible: text.length > 0
            anchors.verticalCenter: backButton.verticalCenter
        }
    }

    Muted {
        visible: text.length > 0
        text: root.unavailable
        font.pixelSize: Look.fontTitle
    }

    Item {
        id: head
        visible: root.unavailable.length === 0
        width: root.width
        implicitHeight: Math.max(icon.height + 8, texts.implicitHeight, actionColumn.implicitHeight)

        AppIcon {
            id: icon
            anchors.verticalCenter: head.verticalCenter
            pkgName: root.iconName
            size: 104
        }

        Column {
            id: texts
            anchors.left: icon.right
            anchors.leftMargin: 22
            anchors.right: actionColumn.left
            anchors.rightMargin: 16
            anchors.verticalCenter: head.verticalCenter
            spacing: 6

            PlainText {
                // only as wide as the name, so the space next to it is not part of the link
                width: Math.min(implicitWidth, texts.width)
                text: root.title
                font.pixelSize: Look.fontHero
                font.weight: Font.Bold
                font.underline: pageLink.valid && titleHover.hovered
                elide: Text.ElideRight

                // the keyboard reaches the same page through the link below
                HoverHandler {
                    id: titleHover
                    enabled: pageLink.valid
                    cursorShape: Qt.PointingHandCursor
                }

                // the link below decides what may be opened
                TapHandler {
                    enabled: pageLink.valid
                    onTapped: pageLink.click()
                }
            }

            Muted {
                width: texts.width
                text: root.summary
                font.pixelSize: Look.fontLead
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Row {
                spacing: 8

                SourceBadge {
                    id: badge
                    source: root.source
                }

                Muted {
                    anchors.verticalCenter: badge.verticalCenter
                    text: root.byline
                }

                WebLink {
                    id: pageLink
                    anchors.verticalCenter: badge.verticalCenter
                    url: root.link
                    text: root.linkText
                }
            }
        }

        Column {
            id: actionColumn
            anchors.right: head.right
            anchors.verticalCenter: head.verticalCenter
            spacing: 8
        }
    }

    ScreenshotStrip {
        id: strip
        width: root.width
    }

    Text {
        visible: head.visible && text.length > 0
        width: Math.min(root.width, Look.textMaxWidth)
        text: root.description
        textFormat: Text.RichText
        color: Look.text
        linkColor: Look.accent
        font.pixelSize: Look.fontLead
        lineHeight: 1.3
        wrapMode: Text.WordWrap
    }
}
