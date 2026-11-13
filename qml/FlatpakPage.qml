pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

Item {
    id: root

    property string query

    readonly property FlatpakClient flatpak: Backend.flatpak
    readonly property bool searching: query.trim().length >= 2
    // What stands in the way of listing apps: "off", "missing", "noremote" or "".
    readonly property string obstacle: !Backend.flatpakEnabled ? "off"
        : flatpak.loading && (!flatpak.available || !flatpak.hasRemote) ? ""  // being found out
        : !flatpak.available ? "missing"
        : !flatpak.hasRemote && flatpak.installedCount === 0 ? "noremote" : ""  // apps installed for the user can still be managed
    readonly property bool ready: obstacle === ""

    // `copy` names the installed copy that was clicked; empty for a search result
    signal openApp(string appId, string copy)
    signal openSettings

    onQueryChanged: debounce.restart()
    Component.onCompleted: flatpak.search(query)

    Timer {
        id: debounce
        interval: 150
        onTriggered: root.flatpak.search(root.query)
    }

    EmptyState {
        visible: !root.ready
        anchors.centerIn: root
        glyph: "M4 8l8-4l8 4v8l-8 4l-8-4zM4 8l8 4l8-4M12 12v8M8 6l8 4"
        title: root.obstacle === "off" ? qsTr("Flatpak is turned off")
            : root.obstacle === "missing" ? qsTr("Flatpak is not installed") : qsTr("No app source is set up")
        text: root.obstacle === "off" ? qsTr("Turn it on in the settings to search and install Flatpak apps.")
            : root.obstacle === "missing" ? qsTr("The “flatpak” package is needed to install and run these apps.")
            : qsTr("Flathub is the main source of Flatpak apps. Add it to browse and install them.")
        action: root.obstacle === "off" ? qsTr("Open settings")
            : root.obstacle === "missing" ? qsTr("Install Flatpak") : qsTr("Add Flathub")
        onTriggered: {
            if (root.obstacle === "off")
                root.openSettings();
            else if (root.obstacle === "missing")
                Backend.install("flatpak");
            else
                Backend.addFlathub();
        }
    }

    PageHeader {
        id: pageHeader
        visible: root.ready
        title: qsTr("Flatpak")
        subtitle: qsTr("Sandboxed apps from Flathub. Not packaged by CachyOS.")

        UpdatesBanner {
            visible: !root.searching && count > 0
            width: pageHeader.width - Look.gutter
            count: root.flatpak.updateCount
            action: qsTr("Update all")
            text: root.flatpak.runtimeUpdates > 0
                ? qsTr("%n of them are runtimes and extensions that the apps share.", "", root.flatpak.runtimeUpdates) : ""
            onTriggered: Backend.updateFlatpaks()
        }
    }

    PageList {
        id: list
        visible: root.ready
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        model: root.searching ? root.flatpak.results : root.flatpak.installed

        delegate: PackageRow {
            required property flatpakItem modelData
            readonly property string installedVersion: modelData.installedVersion
            readonly property string newVersion: !modelData.upgradable ? ""
                : modelData.newVersion.length > 0 ? modelData.newVersion : qsTr("Update")

            width: list.rowWidth
            pkgName: "flatpak/" + modelData.id
            title: modelData.title
            summary: modelData.summary.length > 0 ? modelData.summary : modelData.id
            source: "flatpak"
            trailing: newVersion.length > 0 ? (installedVersion.length > 0 ? installedVersion + " → " + newVersion : newVersion)
                : modelData.installed ? (root.searching ? qsTr("Installed") : installedVersion)
                : modelData.version
            trailingColor: newVersion.length > 0 ? Look.warning : Look.textMuted
            onClicked: root.openApp(modelData.id, root.searching ? "" : modelData.key)
        }
    }

    EmptyState {
        visible: root.ready && list.count === 0
        anchors.centerIn: list
        busy: root.flatpak.loading
        text: root.flatpak.loading ? qsTr("Loading the app catalogue…")
            : !root.flatpak.hasRemote ? qsTr("No app source is set up for all users. Add Flathub to search for apps.")
            : root.flatpak.appCount === 0 ? qsTr("The app catalogue could not be loaded.")
            : root.searching ? qsTr("No Flatpak apps match “%1”.").arg(root.query)
            : qsTr("Type in the search field to find one of %n Flatpak app(s).", "", root.flatpak.appCount)
        action: root.flatpak.loading || root.flatpak.appCount > 0 ? ""
            : root.flatpak.hasRemote ? qsTr("Try again") : qsTr("Add Flathub")
        actionTone: "soft"
        onTriggered: root.flatpak.hasRemote ? root.flatpak.refresh() : Backend.addFlathub()
    }
}
