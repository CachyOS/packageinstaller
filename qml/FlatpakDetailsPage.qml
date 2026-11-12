pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    required property string appId
    // The installed copy to show; empty for the one that matches the catalogue.
    property string copy

    signal back

    // Re-evaluated whenever the catalogue or the installed apps were read again.
    readonly property var app: Backend.flatpak.revision >= 0 ? Backend.flatpak.app(appId, copy) : ({})
    readonly property bool known: app.id !== undefined
    readonly property bool upgradable: app.installed === true && Backend.flatpak.updates.some(update => update.ref === app.ref)

    contentWidth: width
    contentHeight: content.height + 2 * Look.pagePadding
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}

    Column {
        id: content
        x: Look.pagePadding
        y: Look.pagePadding - 8
        width: root.width - 2 * Look.pagePadding
        spacing: 22

        PillButton {
            tone: "soft"
            implicitHeight: 32
            text: qsTr("‹  Back")
            onClicked: root.back()
        }

        Text {
            textFormat: Text.PlainText
            visible: !root.known
            text: qsTr("“%1” is not available as a Flatpak.").arg(root.appId)
            color: Look.textMuted
            font.pixelSize: Look.fontTitle
        }

        // Hero
        Item {
            visible: root.known
            width: parent.width
            height: 112

            AppIcon {
                id: heroIcon
                anchors.verticalCenter: parent.verticalCenter
                pkgName: "flatpak/" + root.appId
                size: 104
            }

            Column {
                anchors.left: heroIcon.right
                anchors.leftMargin: 22
                anchors.right: actions.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: root.app.title ?? ""
                    color: Look.text
                    font.pixelSize: Look.fontHero
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: root.app.summary ?? ""
                    color: Look.textMuted
                    font.pixelSize: Look.fontBody + 1
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 8

                    SourceBadge {
                        source: "flatpak"
                    }

                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.app.developer ?? "").length > 0 ? root.app.developer : root.appId
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall + 1
                    }
                }
            }

            Column {
                id: actions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                PillButton {
                    anchors.right: parent.right
                    visible: root.app.installed !== true && root.app.available === true
                    implicitWidth: 150
                    implicitHeight: 42
                    text: qsTr("Install")
                    onClicked: Backend.installFlatpak(root.appId)
                }

                PillButton {
                    anchors.right: parent.right
                    visible: root.app.installed === true && root.upgradable
                    implicitWidth: 150
                    implicitHeight: 42
                    text: qsTr("Update all")
                    onClicked: Backend.updateFlatpaks()
                }

                PillButton {
                    anchors.right: parent.right
                    visible: root.app.installed === true
                    implicitWidth: 150
                    tone: "danger"
                    text: qsTr("Remove")
                    onClicked: Backend.removeFlatpak(root.app.key)
                }
            }
        }

        // Screenshots
        ListView {
            visible: root.known && count > 0
            width: parent.width
            height: 280
            orientation: ListView.Horizontal
            spacing: 14
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.app.screenshots ?? []

            delegate: Rectangle {
                required property string modelData

                height: ListView.view.height
                width: shot.status === Image.Ready ? Math.min(560, shot.implicitWidth * height / Math.max(1, shot.implicitHeight)) : 440
                radius: Look.radius
                color: Look.surface
                border.width: 1
                border.color: Look.border
                clip: true

                Image {
                    id: shot
                    anchors.fill: parent
                    anchors.margins: 1
                    source: parent.modelData
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    sourceSize.height: 560
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: shot.status === Image.Loading
                    visible: running
                }
            }
        }

        // Description
        Text {
            visible: root.known && text.length > 0
            width: Math.min(parent.width, 820)
            text: root.app.description ?? ""
            textFormat: Text.RichText
            color: Look.text
            linkColor: Look.accent
            font.pixelSize: Look.fontBody + 1
            lineHeight: 1.3
            wrapMode: Text.WordWrap
        }

        // Details
        Rectangle {
            visible: root.known
            width: parent.width
            height: info.height + 40
            radius: Look.radius
            color: Look.surface
            border.width: 1
            border.color: Look.border

            Flow {
                id: info
                x: 20
                y: 20
                width: parent.width - 40
                spacing: 20

                InfoItem {
                    label: qsTr("App ID")
                    value: root.appId
                }
                InfoItem {
                    label: qsTr("Version")
                    value: root.app.version ?? ""
                }
                InfoItem {
                    label: qsTr("Installed version")
                    value: root.app.installedVersion ?? ""
                }
                InfoItem {
                    label: qsTr("Source")
                    value: root.app.installedFrom ?? root.app.remote ?? ""
                }
                InfoItem {
                    label: qsTr("Installed size")
                    value: root.app.size ?? ""
                }
                InfoItem {
                    label: qsTr("Installed for")
                    value: root.app.installation === "user" ? qsTr("Your user only")
                        : root.app.installation === "system" ? qsTr("All users") : (root.app.installation ?? "")
                }
                InfoItem {
                    label: qsTr("Branch")
                    value: root.app.branch ?? ""
                }
                InfoItem {
                    label: qsTr("License")
                    value: root.app.license ?? ""
                }

                Column {
                    visible: link.valid
                    width: 220
                    spacing: 2

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Website")
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall
                    }

                    WebLink {
                        id: link
                        width: parent.width
                        url: root.app.homepage ?? ""
                    }
                }
            }
        }
    }
}
