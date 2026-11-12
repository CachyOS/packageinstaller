pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    required property string pkgName

    signal back
    signal openPackage(string name)
    signal showUpdates

    // Re-evaluated whenever the databases were reloaded.
    readonly property var pkg: Backend.revision >= 0 ? Backend.package(pkgName) : ({})
    readonly property bool known: pkg.name !== undefined

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
            text: qsTr("“%1” is not available in the repositories.").arg(root.pkgName)
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
                pkgName: root.pkgName
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
                    text: root.pkg.title ?? ""
                    color: Look.text
                    font.pixelSize: Look.fontHero
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    textFormat: Text.PlainText
                    width: parent.width
                    text: root.pkg.summary ?? ""
                    color: Look.textMuted
                    font.pixelSize: Look.fontBody + 1
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 8

                    SourceBadge {
                        source: root.pkg.source ?? "arch"
                    }

                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.pkg.developer ?? "").length > 0 ? root.pkg.developer : root.pkgName
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
                    visible: !root.pkg.installed
                    implicitWidth: 150
                    implicitHeight: 42
                    text: qsTr("Install")
                    onClicked: Backend.install(root.pkgName)
                }

                PillButton {
                    anchors.right: parent.right
                    visible: root.pkg.installed === true && root.pkg.upgradable === true
                    implicitWidth: 150
                    implicitHeight: 42
                    text: qsTr("Update system")
                    onClicked: root.showUpdates()
                }

                PillButton {
                    anchors.right: parent.right
                    visible: root.pkg.installed === true
                    implicitWidth: 150
                    tone: "danger"
                    text: qsTr("Remove")
                    onClicked: Backend.remove(root.pkgName)
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
            model: root.pkg.screenshots ?? []

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
            visible: root.known
            width: Math.min(parent.width, 820)
            text: (root.pkg.longDescription ?? "").length > 0 ? root.pkg.longDescription : (root.pkg.description ?? "")
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
                    label: qsTr("Package")
                    value: root.pkgName
                }
                InfoItem {
                    label: qsTr("Version")
                    value: root.pkg.version ?? ""
                }
                InfoItem {
                    label: qsTr("Installed version")
                    value: root.pkg.installedVersion ?? ""
                }
                InfoItem {
                    label: qsTr("Repository")
                    value: root.pkg.repo ?? ""
                }
                InfoItem {
                    label: qsTr("Download size")
                    value: (root.pkg.downloadSize ?? 0) > 0 ? Backend.formatSize(root.pkg.downloadSize) : ""
                }
                InfoItem {
                    label: qsTr("Installed size")
                    value: (root.pkg.installSize ?? 0) > 0 ? Backend.formatSize(root.pkg.installSize) : ""
                }
                InfoItem {
                    label: qsTr("License")
                    value: (root.pkg.licenses ?? []).join(", ")
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
                        url: root.pkg.url ?? ""
                    }
                }
            }
        }

        // Dependencies
        Column {
            visible: root.known && (root.pkg.depends ?? []).length > 0
            width: parent.width
            spacing: 10

            Text {
                textFormat: Text.PlainText
                text: qsTr("Depends on")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Flow {
                width: parent.width
                spacing: 8

                Repeater {
                    model: root.pkg.depends ?? []

                    AbstractButton {
                        id: dep

                        required property string modelData

                        implicitHeight: 28
                        implicitWidth: depLabel.implicitWidth + 24
                        onClicked: root.openPackage(modelData)

                        background: Rectangle {
                            radius: height / 2
                            color: dep.hovered ? Look.surfaceAlt : Look.surface
                            border.width: 1
                            border.color: dep.visualFocus ? Look.accent : Look.border
                        }

                        contentItem: Text {
                            id: depLabel
                            textFormat: Text.PlainText
                            text: dep.modelData
                            color: Look.text
                            font.pixelSize: Look.fontSmall + 1
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }
    }
}
