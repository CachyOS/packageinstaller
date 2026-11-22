pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import CachyOS.PackageInstaller

Item {
    id: root

    property string query

    readonly property SnapClient snap: Backend.snap
    readonly property bool searching: query.trim().length >= 2
    // Only the source switch blocks the page; the store is searched over HTTP, without snapd.
    readonly property string obstacle: !Backend.snapEnabled ? "off" : ""
    readonly property bool ready: obstacle === ""
    /// Installing and managing snaps needs snapd; browsing and searching do not.
    readonly property bool needsSnapd: !snap.available || !snap.daemonRunning

    signal openApp(string name)
    signal openSettings

    onQueryChanged: debounce.restart()
    // Read the installed snaps and updates on the first visit too, and seed the client's query.
    Component.onCompleted: {
        snap.refresh()
        snap.search(root.query)
    }

    Timer {
        id: debounce
        interval: 150
        onTriggered: root.snap.search(root.query)
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose a Snap file")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        nameFilters: [qsTr("Snap files (*.snap)"), qsTr("All files (*)")]
        onAccepted: Backend.installSnapFile(selectedFile)
    }

    EmptyState {
        visible: !root.ready
        anchors.centerIn: root
        glyph: "M4 8l8-4l8 4v8l-8 4l-8-4zM4 8l8 4l8-4M12 12v8M8 6l8 4"
        title: qsTr("Snap is turned off")
        text: qsTr("Turn it on in the settings to search for and install snaps.")
        action: qsTr("Open settings")
        actionTone: "soft"
        onTriggered: root.openSettings()
    }

    PageHeader {
        id: pageHeader
        visible: root.ready
        title: qsTr("Snap")
        subtitle: qsTr("Confined apps from the Snap Store. Not packaged by CachyOS.")
        actions: [
            PillButton {
                enabled: !root.needsSnapd
                tone: "soft"
                text: qsTr("Refresh all")
                onClicked: root.snap.refresh()
            },
            PillButton {
                enabled: !root.needsSnapd
                text: qsTr("Install from file…")
                onClicked: fileDialog.open()
            }
        ]

        // Browsing the store is over HTTP; only installing and managing snaps needs snapd.
        Notice {
            width: pageHeader.width
            tone: Notice.Warning
            text: !root.needsSnapd ? ""
                : qsTr("Browsing and searching the Snap Store works without snapd. Installing snaps needs the “snapd” package from the AUR, with snapd.socket running and AppArmor set up.")
                  + (!root.snap.available && (!Backend.aurEnabled || !Backend.paruAvailable)
                     ? " " + qsTr("To build it, install “paru” from a terminal or another AUR helper, then turn on the AUR source in Settings → Sources.")
                     : "")

            PillButton {
                visible: !root.snap.available
                size: "small"
                tone: "soft"
                text: qsTr("Install snapd")
                enabled: Backend.aurEnabled && Backend.paruAvailable
                onClicked: Backend.installFromAur("snapd")
            }

            PillButton {
                visible: root.snap.available && !root.snap.daemonRunning
                size: "small"
                text: qsTr("Start snapd")
                enabled: Backend.snapEnabled
                onClicked: Backend.setUpSnapd()
            }
        }

        UpdatesBanner {
            visible: !root.searching && count > 0
            enabled: !root.needsSnapd
            width: pageHeader.width - Look.gutter
            count: root.snap.updateCount
            action: qsTr("Update all")
            onTriggered: Backend.refreshSnaps()
        }
    }

    PageList {
        id: list
        visible: root.ready
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        model: root.searching ? root.snap.results : root.snap.installed

        delegate: PackageRow {
            required property snapItem modelData
            readonly property bool upgradable: modelData.upgradable

            width: list.rowWidth
            pkgName: "snap/" + modelData.name
            title: modelData.name
            summary: modelData.summary.length > 0 ? modelData.summary : modelData.publisher
            source: "snap"
            trailing: upgradable ? qsTr("Update")
                : modelData.installed ? (root.searching ? qsTr("Installed") : modelData.version)
                : modelData.version
            trailingColor: upgradable ? Look.warning : Look.textMuted
            onClicked: root.openApp(modelData.name)
        }
    }

    EmptyState {
        visible: root.ready && list.count === 0
        anchors.centerIn: list
        busy: root.snap.loading
        text: root.snap.loading ? qsTr("Loading the Snap Store…")
            : root.snap.searchError.length > 0 ? root.snap.searchError
            : root.searching ? qsTr("No snaps match “%1”.").arg(root.query)
            : qsTr("No snaps installed yet. Search for an app above to install it.")
        action: root.snap.loading || root.snap.searchError.length === 0 ? ""
            : qsTr("Try again")
        actionTone: "soft"
        onTriggered: root.snap.refresh()
    }
}
