pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    signal openPackage(string name)
    signal browse
    signal showUpdates

    contentWidth: width
    contentHeight: content.height + 2 * Look.pagePadding
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}

    Column {
        id: content
        x: Look.pagePadding
        y: Look.pagePadding
        width: root.width - 2 * Look.pagePadding
        spacing: 26

        // Hero
        Rectangle {
            width: parent.width
            height: 190
            radius: Look.radius + 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Look.heroStart }
                GradientStop { position: 0.55; color: Look.heroMiddle }
                GradientStop { position: 1; color: Look.heroEnd }
            }

            Image {
                anchors.right: parent.right
                anchors.rightMargin: 36
                anchors.verticalCenter: parent.verticalCenter
                width: 132
                height: 132
                source: Look.heroImage
                sourceSize: Qt.size(264, 264)
                opacity: 0.92
                visible: parent.width > 620
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 32
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(parent.width - 64, 560)
                spacing: 8

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: qsTr("Software for CachyOS")
                    color: Look.heroText
                    font.pixelSize: Look.fontHero
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: qsTr("Optimised packages from the CachyOS and Arch repositories, plus the AUR when you want it.")
                    color: Look.heroSub
                    font.pixelSize: Look.fontBody + 1
                    wrapMode: Text.WordWrap
                }

                Row {
                    topPadding: 8
                    spacing: 10

                    PillButton {
                        id: heroButton
                        text: qsTr("Browse apps")
                        onClicked: root.browse()
                        background: Rectangle {
                            radius: height / 2
                            color: heroButton.hovered ? Look.heroButtonHover : Look.heroButton
                        }
                        contentItem: Text {
                            textFormat: Text.PlainText
                            text: heroButton.text
                            font: heroButton.font
                            color: Look.heroInk
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        // Updates strip
        Rectangle {
            visible: Backend.updateCount > 0
            width: parent.width
            height: 60
            radius: Look.radius
            color: Look.accentSoft

            Text {
                textFormat: Text.PlainText
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.right: updatesButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n update(s) available", "", Backend.updateCount)
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            PillButton {
                id: updatesButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Review updates")
                onClicked: root.showUpdates()
            }
        }

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Backend.loading && Backend.curated.length === 0
            running: visible
        }

        Text {
            textFormat: Text.PlainText
            visible: Backend.error.length > 0
            width: parent.width
            text: Backend.error
            color: Look.danger
            font.pixelSize: Look.fontBody
            wrapMode: Text.WordWrap
        }

        // CachyOS picks, one shelf per section of pkglist.yaml
        Repeater {
            model: Backend.curated

            Column {
                id: shelf

                required property var modelData

                width: content.width
                spacing: 12

                // As many cards as fit next to each other; two rows until "Show all" is pressed.
                readonly property int columns: Math.max(1, Math.floor((width + 12) / 292))
                readonly property real cardWidth: (width - (columns - 1) * 12) / columns
                readonly property int total: modelData.packages.length
                readonly property int limit: 2 * columns
                property bool expanded: false

                Item {
                    width: parent.width
                    height: Math.max(shelfTitle.implicitHeight, showAll.implicitHeight)

                    Text {
                        id: shelfTitle
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: shelf.modelData.title
                        color: Look.text
                        font.pixelSize: Look.fontHeading - 2
                        font.weight: Font.Bold
                    }

                    PillButton {
                        id: showAll
                        visible: shelf.total > shelf.limit
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        tone: "soft"
                        implicitHeight: 28
                        font.pixelSize: Look.fontSmall + 1
                        text: shelf.expanded ? qsTr("Show less") : qsTr("Show all %1").arg(shelf.total)
                        Accessible.description: shelf.modelData.title
                        onClicked: shelf.expanded = !shelf.expanded
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 12

                    Repeater {
                        model: shelf.expanded ? shelf.modelData.packages : shelf.modelData.packages.slice(0, shelf.limit)

                        AppCard {
                            required property var modelData

                            width: shelf.cardWidth
                            height: 104
                            pkgName: modelData.name
                            title: modelData.title
                            summary: modelData.summary
                            source: modelData.source
                            installed: modelData.installed
                            upgradable: modelData.upgradable
                            onClicked: root.openPackage(modelData.name)
                        }
                    }
                }
            }
        }
    }
}
