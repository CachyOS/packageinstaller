pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    // The search result this page was opened with.
    required property var item
    // Set by "Review and update all": the packages to review one after the other.
    // Nothing is built before the last one was reviewed or skipped.
    property var queue: []
    property int position: 0
    property var accepted: []
    readonly property bool queued: queue.length > 1
    readonly property bool last: position >= queue.length - 1

    signal back
    signal openPackage(string name)

    // Fresh RPC data once it arrives, the search result until then.
    readonly property var info: Backend.aur.details.name === item.name ? Backend.aur.details : item
    property bool reviewed: false

    readonly property bool dangerous: Backend.aur.findings.some(f => f.severity === "danger")

    Component.onCompleted: Backend.aur.loadDetails(item.name, item.pkgbase)

    // The queue is used up once its packages were built: return to the list of updates.
    Connections {
        target: Backend.transaction
        enabled: root.queued && root.last
        function onFinished(success) {
            if (success) {
                root.back()
            }
        }
    }

    // Moves on in the queue, with or without the current package.
    function advance(accept) {
        const names = accept ? accepted.concat([item.name]) : accepted
        if (!last) {
            accepted = names
            position += 1
            reviewed = false
            item = queue[position]
            Backend.aur.loadDetails(item.name, item.pkgbase)
            contentY = 0
        } else if (names.length > 0) {
            Backend.installManyFromAur(names)
        } else {
            back()
        }
    }

    function marksFor(file) {
        return Backend.aur.findings.filter(f => f.file === file && f.line > 0)
    }

    // Scrolls the listing to the line a finding refers to.
    function reveal(file, line) {
        let view = file === "PKGBUILD" ? pkgbuildView : null
        for (let i = 0; view === null && i < fileViews.count; ++i) {
            if (fileViews.itemAt(i).modelData.name === file) {
                view = fileViews.itemAt(i).view
            }
        }
        if (view === null || line <= 0) {
            return
        }
        const y = view.mapToItem(content, 0, view.lineY(line)).y + content.y
        contentY = Math.max(0, Math.min(y - height / 3, contentHeight - height))
    }

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

        Row {
            spacing: 14

            PillButton {
                tone: "soft"
                implicitHeight: 32
                text: qsTr("‹  Back")
                onClicked: root.back()
            }

            Text {
                textFormat: Text.PlainText
                visible: root.queued
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Update %1 of %2").arg(root.position + 1).arg(root.queue.length)
                color: Look.textMuted
                font.pixelSize: Look.fontBody
            }
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
                    textFormat: Text.PlainText
                    width: parent.width
                    text: root.info.name
                    color: Look.text
                    font.pixelSize: Look.fontHero - 2
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    textFormat: Text.PlainText
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
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.info.upgradable ? root.info.installedVersion + " → " : "")
                            + qsTr("%1 · %n vote(s)", "", root.info.votes).arg(root.info.version)
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
                    textFormat: Text.PlainText
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
                textFormat: Text.PlainText
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
                textFormat: Text.PlainText
                visible: root.info.depends.length > 0
                width: parent.width
                text: qsTr("Depends on: %1").arg(root.info.depends.join(", "))
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            Text {
                textFormat: Text.PlainText
                visible: root.info.makeDepends.length > 0
                width: parent.width
                text: qsTr("Needed to build: %1").arg(root.info.makeDepends.join(", "))
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }
        }

        // Automatic check
        Column {
            visible: Backend.aur.pkgbuild.length > 0
            width: parent.width
            spacing: 10

            Text {
                textFormat: Text.PlainText
                text: qsTr("Things to check")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: Backend.aur.findings.length > 0
                    ? qsTr("An automatic check marked the points below. It only knows common tricks and cannot tell you that a package is safe, so read the script as well.")
                    : qsTr("The automatic check found nothing unusual. It only knows common tricks and cannot tell you that a package is safe, so still read the script.")
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            Rectangle {
                visible: Backend.aur.findings.length > 0
                width: parent.width
                height: findingList.height
                radius: Look.radius
                color: Look.surface
                border.width: 1
                border.color: Look.border
                clip: true

                Column {
                    id: findingList
                    width: parent.width

                    Repeater {
                        model: Backend.aur.findings

                        Rectangle {
                            id: finding

                            required property var modelData
                            required property int index
                            readonly property bool located: modelData.line > 0
                            readonly property color ink: modelData.severity === "danger" ? Look.danger
                                : modelData.severity === "warning" ? Look.warning : Look.arch

                            width: findingList.width
                            height: findingBody.height + 24
                            color: findingHover.hovered && located ? Look.surfaceHover : "transparent"
                            Accessible.role: located ? Accessible.Button : Accessible.StaticText
                            Accessible.name: severityLabel.text + ": " + modelData.title + ". " + modelData.detail
                            Accessible.onPressAction: root.reveal(modelData.file, modelData.line)

                            Rectangle {
                                visible: finding.index > 0
                                x: 16
                                width: parent.width - 32
                                height: 1
                                color: Look.border
                            }

                            Rectangle {
                                id: severityPill
                                x: 16
                                y: 13
                                width: 84
                                height: 22
                                radius: 11
                                color: finding.modelData.severity === "danger" ? Look.dangerSoft
                                    : finding.modelData.severity === "warning" ? Look.warningSoft : Look.archSoft

                                Text {
                                    id: severityLabel
                                    textFormat: Text.PlainText
                                    anchors.centerIn: parent
                                    text: finding.modelData.severity === "danger" ? qsTr("Dangerous")
                                        : finding.modelData.severity === "warning" ? qsTr("Check") : qsTr("Note")
                                    color: finding.ink
                                    font.pixelSize: Look.fontSmall
                                    font.weight: Font.DemiBold
                                }
                            }

                            Column {
                                id: findingBody
                                anchors.left: severityPill.right
                                anchors.leftMargin: 14
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                y: 12
                                spacing: 4

                                Text {
                                    textFormat: Text.PlainText
                                    width: parent.width
                                    text: finding.modelData.title
                                    color: Look.text
                                    font.pixelSize: Look.fontBody
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    textFormat: Text.PlainText
                                    width: parent.width
                                    text: finding.modelData.detail
                                    color: Look.textMuted
                                    font.pixelSize: Look.fontBody
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    textFormat: Text.PlainText
                                    visible: finding.located
                                    width: parent.width
                                    text: qsTr("%1, line %2").arg(finding.modelData.file).arg(finding.modelData.line) + "   " + finding.modelData.excerpt
                                    color: finding.ink
                                    font.family: "monospace"
                                    font.pixelSize: Look.fontSmall
                                    elide: Text.ElideRight
                                }
                            }

                            HoverHandler {
                                id: findingHover
                                enabled: finding.located
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                enabled: finding.located
                                onTapped: root.reveal(finding.modelData.file, finding.modelData.line)
                            }
                        }
                    }
                }
            }

            Text {
                textFormat: Text.PlainText
                visible: Backend.aur.sourceHosts.length > 0
                width: parent.width
                text: qsTr("Downloads its sources from: %1").arg(Backend.aur.sourceHosts.join(", "))
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
                textFormat: Text.PlainText
                text: qsTr("PKGBUILD")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: qsTr("This script runs on your machine when the package is built. Read it before you continue.")
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            CodeView {
                id: pkgbuildView
                width: parent.width
                minimumHeight: 120
                text: Backend.aur.pkgbuild
                marks: root.marksFor("PKGBUILD")
                accessibleName: qsTr("PKGBUILD")

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: Backend.aur.loadingDetails && Backend.aur.pkgbuild.length === 0
                    running: visible
                }

                Text {
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    visible: !Backend.aur.loadingDetails && Backend.aur.pkgbuild.length === 0
                    text: qsTr("The PKGBUILD could not be loaded, so this package cannot be installed from here.")
                    color: Look.textMuted
                    font.pixelSize: Look.fontBody
                }
            }

            // Install scriptlets, hooks and units run on the machine as well, some of them as root.
            Repeater {
                id: fileViews
                model: Backend.aur.files

                Column {
                    id: fileSection

                    required property var modelData
                    readonly property alias view: fileView

                    width: parent.width
                    spacing: 10
                    topPadding: 8

                    Text {
                        textFormat: Text.PlainText
                        text: fileSection.modelData.name
                        color: Look.text
                        font.pixelSize: Look.fontTitle
                        font.weight: Font.DemiBold
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: fileSection.modelData.name.endsWith(".install")
                            ? qsTr("This script runs as administrator when the package is installed, updated or removed.")
                            : qsTr("This file is installed with the package and can start programs.")
                        color: Look.textMuted
                        font.pixelSize: Look.fontBody
                        wrapMode: Text.WordWrap
                    }

                    CodeView {
                        id: fileView
                        width: parent.width
                        text: fileSection.modelData.text
                        marks: root.marksFor(fileSection.modelData.name)
                        accessibleName: fileSection.modelData.name
                    }
                }
            }

            CheckBox {
                id: reviewBox
                visible: !root.info.inRepos
                enabled: Backend.aur.pkgbuild.length > 0 && !Backend.aur.loadingDetails
                checked: root.reviewed
                onToggled: root.reviewed = checked
                text: Backend.aur.files.length > 0 ? qsTr("I have read the PKGBUILD and the files above, and trust them")
                    : qsTr("I have read the PKGBUILD and trust it")

                contentItem: Text {
                    textFormat: Text.PlainText
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

            Row {
                spacing: 12

                PillButton {
                    implicitWidth: Math.max(190, implicitContentWidth + 36)
                    implicitHeight: 42
                    visible: !root.info.inRepos
                    enabled: Backend.paruAvailable && root.reviewed && Backend.aur.pkgbuild.length > 0 && !Backend.aur.loadingDetails
                    tone: root.dangerous ? "danger" : "primary"
                    text: root.queued ? (root.last ? qsTr("Build and update %n package(s)", "", root.accepted.length + 1)
                            : qsTr("Next package"))
                        : root.info.upgradable ? qsTr("Build and update")
                        : root.info.installed ? qsTr("Rebuild and reinstall") : qsTr("Build and install")
                    onClicked: root.queued ? root.advance(true) : Backend.installFromAur(root.info.name)
                }

                PillButton {
                    implicitHeight: 42
                    visible: root.queued
                    enabled: !root.last || Backend.paruAvailable || root.accepted.length === 0
                    tone: "soft"
                    text: !root.last ? qsTr("Skip this package")
                        : root.accepted.length > 0 ? qsTr("Skip and update %n package(s)", "", root.accepted.length)
                        : qsTr("Skip")
                    onClicked: root.advance(false)
                }
            }
        }
    }
}
