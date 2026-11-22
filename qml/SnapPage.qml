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
    // What stands in the way of listing snaps: "off", "missing", "no-daemon" or "".
    readonly property string obstacle: !Backend.snapEnabled ? "off"
        : snap.loading && !snap.available ? ""  // being found out
        : !snap.available ? "missing"
        : !snap.daemonRunning ? "no-daemon" : ""
    readonly property bool ready: obstacle === ""

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
        title: root.obstacle === "off" ? qsTr("Snap is turned off")
            : root.obstacle === "missing" ? qsTr("Snap is not installed") : qsTr("The Snap daemon is not running")
        text: root.obstacle === "off" ? qsTr("Turn it on in the settings to search for and install snaps.")
            : root.obstacle === "missing" ? qsTr("Snaps need the “snapd” package from the AUR on CachyOS, with snapd.socket running and AppArmor set up.")
            : qsTr("snapd needs its socket running and AppArmor set up to confine snaps on CachyOS. Start snapd and check AppArmor, then try again.")
        action: root.obstacle === "off" ? qsTr("Open settings") : qsTr("Try again")
        actionTone: "soft"
        onTriggered: {
            if (root.obstacle === "off")
                root.openSettings();
            else
                root.snap.refresh();
        }
    }

    PageHeader {
        id: pageHeader
        visible: root.ready
        title: qsTr("Snap")
        subtitle: qsTr("Confined apps from the Snap Store. Not packaged by CachyOS.")
        actions: [
            PillButton {
                tone: "soft"
                text: qsTr("Refresh all")
                onClicked: root.snap.refresh()
            },
            PillButton {
                text: qsTr("Install from file…")
                onClicked: fileDialog.open()
            }
        ]

        UpdatesBanner {
            visible: !root.searching && count > 0
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
