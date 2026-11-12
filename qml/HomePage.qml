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
                GradientStop { position: 0; color: "#0b7f6f" }
                GradientStop { position: 0.55; color: "#10a48d" }
                GradientStop { position: 1; color: "#1c6fb8" }
            }

            Image {
                anchors.right: parent.right
                anchors.rightMargin: 36
                anchors.verticalCenter: parent.verticalCenter
                width: 132
                height: 132
                source: "icons/cachyos-pi.png"
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
                    color: "white"
                    font.pixelSize: Look.fontHero
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: qsTr("Optimised packages from the CachyOS and Arch repositories, plus the AUR when you want it.")
                    color: "#e6fffa"
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
                            color: heroButton.hovered ? "#ffffff" : "#eafffb"
                        }
                        contentItem: Text {
                            textFormat: Text.PlainText
                            text: heroButton.text
                            font: heroButton.font
                            color: "#075f53"
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

                Text {
                    textFormat: Text.PlainText
                    text: shelf.modelData.title
                    color: Look.text
                    font.pixelSize: Look.fontHeading - 2
                    font.weight: Font.Bold
                }

                ListView {
                    width: parent.width
                    height: 104
                    orientation: ListView.Horizontal
                    spacing: 12
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: shelf.modelData.packages

                    delegate: AppCard {
                        required property var modelData

                        width: 300
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
