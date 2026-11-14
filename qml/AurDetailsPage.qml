pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 8

    // The search result this page was opened with.
    required property aurPackage item
    // The packages to review one after the other: the ones this page was opened for, then
    // the AUR packages they need. Nothing is built before the last one was reviewed or skipped.
    property list<aurPackage> queue
    // Opened by "Review and update all".
    property bool updates: false
    property int position: 0
    property list<string> accepted
    // Why the accepted packages cannot be built, if they cannot.
    property string blocked
    readonly property bool queued: queue.length > 1
    readonly property bool last: position >= queue.length - 1

    signal back
    signal openPackage(string name)

    // Fresh RPC data once it arrives, the search result until then.
    readonly property aurPackage info: Backend.aur.details.name === item.name ? Backend.aur.details : item
    readonly property bool ready: Backend.aur.pkgbuild.length > 0 && !Backend.aur.loadingDetails

    component Paragraph: PlainText {
        color: Look.textMuted
        wrapMode: Text.WordWrap
    }

    component FileSection: Column {
        id: section

        required property string name
        required property string note
        property alias text: codeView.text
        property alias marks: codeView.marks
        property alias minimumHeight: codeView.minimumHeight
        readonly property alias view: codeView
        default property alias overlay: codeView.data

        spacing: 10

        SectionTitle {
            text: section.name
        }

        Paragraph {
            width: section.width
            text: section.note
        }

        CodeView {
            id: codeView
            width: section.width
            accessibleName: section.name
        }
    }

    spacing: 20

    Component.onCompleted: {
        if (queue.length === 0) {
            queue = [item]
        }
        Backend.aur.loadDetails(item.name, item.pkgbase)
        Backend.aur.resolve(queue.map(entry => entry.name))
    }

    // AUR packages that the queue needs are reviewed as well, after the ones asked for.
    Connections {
        target: Backend.aur
        function onPlanChanged() {
            if (Backend.aur.resolving) {
                return
            }
            const known = root.queue.map(entry => entry.name)
            const extra = Backend.aur.plan.filter(entry => !known.includes(entry.name))
            if (extra.length > 0) {
                root.queue = root.queue.concat(extra)
            }
        }
    }

    // The queue is used up once its packages were built: return to where it was started.
    Connections {
        target: Backend.transaction
        enabled: root.queued && root.last
        function onFinished(success: bool) {
            if (success) {
                root.back()
            }
        }
    }

    // Moves on in the queue, with or without the current package.
    function advance(accept: bool) {
        if (accept) {
            Backend.aur.accept(item.name)
        }
        const names = accept ? accepted.concat([item.name]) : accepted
        blocked = ""
        if (!last) {
            accepted = names
            position += 1
            reviewBox.checked = false
            item = queue[position]
            Backend.aur.loadDetails(item.name, item.pkgbase)
            contentY = 0
        } else if (names.length > 0) {
            blocked = Backend.aur.blockedReason(names)
            if (blocked.length === 0) {
                Backend.installManyFromAur(names)
            }
        } else {
            back()
        }
    }

    function marksFor(file: string): list<finding> {
        return Backend.aur.findings.filter(f => f.file === file && f.line > 0)
    }

    function viewOf(file: string): CodeView {
        if (file === "PKGBUILD") {
            return pkgbuild.view
        }
        for (let i = 0; i < fileViews.count; ++i) {
            const section = fileViews.itemAt(i) as FileSection
            if (section && section.name === file) {
                return section.view
            }
        }
        return null
    }

    // Scrolls the listing to the line a finding refers to.
    function reveal(file: string, line: int) {
        const view = viewOf(file)
        if (view === null || line <= 0) {
            return
        }
        const y = view.mapToItem(contentItem, 0, view.lineY(line)).y
        contentY = Math.max(0, Math.min(y - height / 3, contentHeight - height))
    }

    DetailsHero {
        width: parent.width
        spacing: root.spacing
        status: root.queued ? qsTr("Package %1 of %2").arg(root.position + 1).arg(root.queue.length) : ""
        onBack: root.back()
        iconName: root.item.name
        title: root.info.name
        summary: root.info.description
        source: "aur"
        byline: (root.info.upgradable ? root.info.installedVersion + " → " : "")
            + qsTr("%1 · %n vote(s)", "", root.info.votes).arg(root.info.version)
            + " · " + (root.info.maintainer.length > 0 ? root.info.maintainer : qsTr("orphaned"))
            + (Backend.aur.revision.length > 0 ? " · " + qsTr("revision %1").arg(Backend.aur.revision.substring(0, 7)) : "")

        PillButton {
            visible: root.info.inRepos
            text: qsTr("Open from the repositories")
            onClicked: root.openPackage(root.info.name)
        }
    }

    Notice {
        width: parent.width
        text: root.item.neededBy.length > 0
            ? qsTr("%1 needs this package. It is built from the AUR as well, so it is reviewed like the others.").arg(root.item.neededBy.join(", "))
            : Backend.aur.resolving ? qsTr("Checking which other AUR packages are needed…")
            : root.position === 0 && Backend.aur.planCount > 0
            ? qsTr("Other AUR packages are needed and are built first: %1. You will review each of them before anything is built.").arg(Backend.aur.plan.map(entry => entry.name).join(", "))
            : ""
    }

    Notice {
        width: parent.width
        tone: Notice.Danger
        text: Backend.aur.problems.join("\n")
    }

    Notice {
        width: parent.width
        text: root.info.inRepos ? qsTr("A package with this name is available in the repositories. Install that one instead.") : ""
    }

    Notice {
        width: parent.width
        tone: Notice.Warning
        text: !Backend.paruAvailable ? qsTr("Install “paru” to build AUR packages.")
            : root.info.outOfDate ? qsTr("This package is flagged as out of date by AUR users.") : ""

        PillButton {
            visible: !Backend.paruAvailable
            text: qsTr("Install paru")
            onClicked: Backend.install("paru")
        }
    }

    Column {
        visible: root.info.depends.length + root.info.makeDepends.length > 0
        width: parent.width
        spacing: 6

        Paragraph {
            visible: root.info.depends.length > 0
            width: parent.width
            text: qsTr("Depends on: %1").arg(root.info.depends.join(", "))
        }

        Paragraph {
            visible: root.info.makeDepends.length > 0
            width: parent.width
            text: qsTr("Needed to build: %1").arg(root.info.makeDepends.join(", "))
        }
    }

    Column {
        id: checks
        visible: Backend.aur.pkgbuild.length > 0
        width: parent.width
        spacing: 10

        SectionTitle {
            text: qsTr("Things to check")
        }

        Paragraph {
            width: checks.width
            text: Backend.aur.findingCount > 0
                ? qsTr("An automatic check marked the points below. It only knows common tricks and cannot tell you that a package is safe, so read the script as well.")
                : qsTr("The automatic check found nothing unusual. It only knows common tricks and cannot tell you that a package is safe, so still read the script.")
        }

        Card {
            visible: Backend.aur.findingCount > 0
            width: checks.width
            height: findingList.height

            Column {
                id: findingList
                width: checks.width

                Repeater {
                    model: Backend.aur.findings

                    Rectangle {
                        id: finding

                        required property finding modelData
                        required property int index
                        readonly property string severity: modelData.severity
                        readonly property bool located: modelData.line > 0
                        readonly property color ink: Look.severityColor(severity, false)

                        width: findingList.width
                        height: findingBody.height + 24
                        color: findingHover.hovered && located ? Look.surfaceHover : "transparent"
                        Accessible.role: located ? Accessible.Button : Accessible.StaticText
                        Accessible.name: severityPill.text + ": " + modelData.title + ". " + modelData.detail
                        Accessible.onPressAction: root.reveal(modelData.file, modelData.line)

                        Rectangle {
                            visible: finding.index > 0
                            x: 16
                            width: finding.width - 32
                            height: 1
                            color: Look.border
                        }

                        Pill {
                            id: severityPill
                            x: 16
                            y: 13
                            width: 84
                            implicitHeight: 22
                            text: Look.severityLabel(finding.severity)
                            ink: finding.ink
                            color: Look.severityColor(finding.severity, true)
                            font.pixelSize: Look.fontSmall
                        }

                        Column {
                            id: findingBody
                            anchors.left: severityPill.right
                            anchors.leftMargin: 14
                            anchors.right: finding.right
                            anchors.rightMargin: 16
                            y: 12
                            spacing: 4

                            PlainText {
                                width: findingBody.width
                                text: finding.modelData.title
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Paragraph {
                                width: findingBody.width
                                text: finding.modelData.detail
                            }

                            PlainText {
                                visible: finding.located
                                width: findingBody.width
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

        Paragraph {
            visible: Backend.aur.sourceHosts.length > 0
            width: checks.width
            text: qsTr("Downloads its sources from: %1").arg(Backend.aur.sourceHosts.join(", "))
        }
    }

    Column {
        id: review
        width: parent.width
        spacing: 10

        FileSection {
            id: pkgbuild
            width: review.width
            name: "PKGBUILD"
            note: qsTr("This script runs on your machine when the package is built. Read it before you continue.")
            text: Backend.aur.pkgbuild
            marks: root.marksFor("PKGBUILD")
            minimumHeight: 120

            BusyIndicator {
                anchors.centerIn: parent
                visible: Backend.aur.loadingDetails && Backend.aur.pkgbuild.length === 0
                running: visible
            }

            Paragraph {
                anchors.centerIn: parent
                visible: !Backend.aur.loadingDetails && Backend.aur.pkgbuild.length === 0
                text: qsTr("The PKGBUILD could not be loaded, so this package cannot be installed from here.")
            }
        }

        // Install scriptlets, hooks and units run on the machine as well, some of them as root.
        Repeater {
            id: fileViews
            model: Backend.aur.files

            FileSection {
                required property scriptFile modelData

                width: review.width
                topPadding: 8
                name: modelData.name
                note: modelData.name.endsWith(".install")
                    ? qsTr("This script runs as administrator when the package is installed, updated or removed.")
                    : qsTr("This file is installed with the package and can start programs.")
                text: modelData.text
                marks: root.marksFor(modelData.name)
            }
        }

        CheckBox {
            id: reviewBox
            visible: !root.info.inRepos
            enabled: root.ready
            text: Backend.aur.fileCount > 0 ? qsTr("I have read the PKGBUILD and the files above, and trust them")
                : qsTr("I have read the PKGBUILD and trust it")

            contentItem: PlainText {
                leftPadding: reviewBox.indicator.width + 10
                text: reviewBox.text
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

        Notice {
            width: review.width
            tone: Notice.Danger
            text: root.blocked
        }

        Row {
            spacing: 12

            PillButton {
                implicitWidth: Math.max(190, implicitContentWidth + 36)
                size: "large"
                visible: !root.info.inRepos
                enabled: Backend.paruAvailable && reviewBox.checked && root.ready && (!root.last || !Backend.aur.resolving)
                tone: Backend.aur.dangerous ? "danger" : "primary"
                text: !root.last ? qsTr("Next package")
                    : root.queued ? (root.updates ? qsTr("Build and update %n package(s)", "", root.accepted.length + 1)
                        : qsTr("Build and install %n package(s)", "", root.accepted.length + 1))
                    : root.info.upgradable ? qsTr("Build and update")
                    : root.info.installed ? qsTr("Rebuild and reinstall") : qsTr("Build and install")
                onClicked: root.advance(true)
            }

            PillButton {
                size: "large"
                visible: root.queued
                enabled: !root.last || Backend.paruAvailable || root.accepted.length === 0
                tone: "soft"
                text: !root.last ? qsTr("Skip this package")
                    : root.accepted.length === 0 ? qsTr("Skip")
                    : root.updates ? qsTr("Skip and update %n package(s)", "", root.accepted.length)
                    : qsTr("Skip and install %n package(s)", "", root.accepted.length)
                onClicked: root.advance(false)
            }
        }
    }
}
