pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    // The search result this page was opened with.
    required property var item

    signal back
    signal openPackage(string name)

    // Fresh RPC data once it arrives, the search result until then.
    readonly property var info: Backend.aur.details.name === item.name ? Backend.aur.details : item
    property bool reviewed: false

    Component.onCompleted: Backend.aur.loadDetails(item.name, item.pkgbase)

    contentWidth: width
    contentHeight: content.height + 2 * Look.pagePadding
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    Column {
        id: content
        x: Look.pagePadding
        y: Look.pagePadding - 8
        width: root.width - 2 * Look.pagePadding
        spacing: 20

        PillButton {
            tone: "soft"
            implicitHeight: 32
            text: qsTr("‹  Back")
            onClicked: root.back()
        }

        Item {
            width: parent.width
            height: 104

            AppIcon {
                id: heroIcon
                anchors.verticalCenter: parent.verticalCenter
                pkgName: root.item.name
                size: 96
            }

            Column {
                anchors.left: heroIcon.right
                anchors.leftMargin: 22
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Text {
                    width: parent.width
                    text: root.info.name
                    color: Look.text
                    font.pixelSize: Look.fontHero - 2
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: root.info.description
                    color: Look.textMuted
                    font.pixelSize: Look.fontBody + 1
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 8

                    SourceBadge {
                        source: "aur"
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("%1 · %n vote(s)", "", root.info.votes).arg(root.info.version)
                            + (root.info.maintainer.length > 0 ? " · " + root.info.maintainer : " · " + qsTr("orphaned"))
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall + 1
                    }
                }
            }
        }

        // Notices
        Rectangle {
            visible: root.info.inRepos
            width: parent.width
            height: repoNotice.height + 28
            radius: Look.radius
            color: Look.accentSoft

            Row {
                id: repoNotice
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 12

                Text {
                    width: parent.width - openRepo.width - 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("A package with this name is available in the repositories. Install that one instead.")
                    color: Look.text
                    font.pixelSize: Look.fontBody
                    wrapMode: Text.WordWrap
                }

                PillButton {
                    id: openRepo
                    text: qsTr("Open")
                    onClicked: root.openPackage(root.info.name)
                }
            }
        }

        Rectangle {
            visible: root.info.outOfDate || !Backend.paruAvailable
            width: parent.width
            height: warnText.height + 28
            radius: Look.radius
            color: Look.warningSoft

            Text {
                id: warnText
                x: 16
                y: 14
                width: parent.width - 32
                text: !Backend.paruAvailable ? qsTr("Install “paru” to build AUR packages.")
                    : qsTr("This package is flagged as out of date by AUR users.")
                color: Look.warning
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }
        }

        // Dependencies
        Column {
            visible: root.info.depends.length + root.info.makeDepends.length > 0
            width: parent.width
            spacing: 6

            Text {
                visible: root.info.depends.length > 0
                width: parent.width
                text: qsTr("Depends on: %1").arg(root.info.depends.join(", "))
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            Text {
                visible: root.info.makeDepends.length > 0
                width: parent.width
                text: qsTr("Needed to build: %1").arg(root.info.makeDepends.join(", "))
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }
        }

        // PKGBUILD review
        Column {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("PKGBUILD")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: qsTr("This script runs on your machine when the package is built. Read it before you continue.")
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            // Shown at full height: a nested vertical scroll area would swallow the wheel
            // and make it easy to skip the end of the script.
            Rectangle {
                width: parent.width
                height: Math.max(120, pkgbuildText.implicitHeight + 2)
                radius: Look.radius
                color: Look.surface
                border.width: 1
                border.color: Look.border

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: Backend.aur.loadingDetails
                    running: visible
                }

                Text {
                    anchors.centerIn: parent
                    visible: !Backend.aur.loadingDetails && Backend.aur.pkgbuild.length === 0
                    text: qsTr("The PKGBUILD could not be loaded, so this package cannot be installed from here.")
                    color: Look.textMuted
                    font.pixelSize: Look.fontBody
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    contentWidth: pkgbuildText.implicitWidth
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.horizontal: ScrollBar {}

                    TextEdit {
                        id: pkgbuildText
                        readOnly: true
                        selectByMouse: true
                        text: Backend.aur.pkgbuild
                        color: Look.text
                        selectionColor: Look.accent
                        selectedTextColor: Look.onAccent
                        font.family: "monospace"
                        font.pixelSize: Look.fontSmall + 1
                        padding: 14
                        Accessible.name: qsTr("PKGBUILD")
                    }
                }
            }

            CheckBox {
                id: reviewBox
                visible: !root.info.inRepos
                enabled: Backend.aur.pkgbuild.length > 0
                checked: root.reviewed
                onToggled: root.reviewed = checked
                text: qsTr("I have read the PKGBUILD and trust it")

                contentItem: Text {
                    leftPadding: reviewBox.indicator.width + 10
                    text: reviewBox.text
                    color: Look.text
                    font.pixelSize: Look.fontBody
                    verticalAlignment: Text.AlignVCenter
                }

                indicator: Rectangle {
                    implicitWidth: 20
                    implicitHeight: 20
                    y: (reviewBox.height - height) / 2
                    radius: 5
                    color: reviewBox.checked ? Look.accent : Look.surface
                    border.width: reviewBox.checked ? 0 : (reviewBox.visualFocus ? 2 : 1)
                    border.color: reviewBox.visualFocus ? Look.accent : Look.textMuted

                    Glyph {
                        anchors.centerIn: parent
                        visible: reviewBox.checked
                        size: 16
                        strokeWidth: 2.6
                        color: Look.onAccent
                        path: "M5 12.5l4.5 4.5L19 7"
                    }
                }
            }

            PillButton {
                implicitWidth: 190
                implicitHeight: 42
                visible: !root.info.inRepos
                enabled: Backend.paruAvailable && root.reviewed && Backend.aur.pkgbuild.length > 0
                text: root.info.installed ? qsTr("Rebuild and reinstall") : qsTr("Build and install")
                onClicked: Backend.installFromAur(root.info.name)
            }
        }
    }
}
