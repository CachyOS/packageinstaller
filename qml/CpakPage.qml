pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

Item {
    id: root

    property string query

    readonly property CpakClient cpak: Backend.cpak
    readonly property bool searching: query.trim().length >= 2
    // What stands in the way of listing apps: "off", "missing" or "".
    readonly property string obstacle: !Backend.cpakEnabled ? "off"
        : cpak.loading && !cpak.available ? ""  // being found out
        : !cpak.available ? "missing" : ""
    readonly property bool ready: obstacle === ""

    // What the Flatpak catalog knows about a Flatpak app that cpak has installed: its name and
    // description. Empty while that source is off. `revision` makes the binding ask again after a reload.
    function flatpakInfo(id: string, revision: int): flatpakItem {
        return id.length > 0 ? Backend.flatpak.app(id, "") : Backend.flatpak.app("", "")
    }

    signal openApp(string origin)
    signal openSettings

    onQueryChanged: debounce.restart()
    Component.onCompleted: cpak.search(query)

    Timer {
        id: debounce
        interval: 150
        onTriggered: root.cpak.search(root.query)
    }

    EmptyState {
        visible: !root.ready
        anchors.centerIn: root
        glyph: "M12 3 20 7.5v9L12 21l-8-4.5v-9zM9 10.5a3 3 0 1 1 0 3"
        title: root.obstacle === "off" ? qsTr("cpak is turned off") : qsTr("cpak is not installed")
        text: root.obstacle === "off" ? qsTr("Turn it on in the settings to search and install cpak apps.")
            : qsTr("The “cpak” command is needed to install and run these apps. See cpak.it/docs for how to install it.")
        action: root.obstacle === "off" ? qsTr("Open settings") : ""
        onTriggered: root.openSettings()
    }

    PageHeader {
        id: pageHeader
        visible: root.ready
        title: qsTr("cpak")
        subtitle: qsTr("Apps that run in a sandbox of their own. Not packaged by CachyOS.")

        UpdatesBanner {
            visible: !root.searching && count > 0
            width: pageHeader.width - Look.gutter
            count: root.cpak.updateCount
            action: qsTr("Update all")
            text: qsTr("An update that asks for more permissions is skipped; open the app to update it.")
            onTriggered: Backend.updateCpaks()
        }
    }

    PageList {
        id: list
        visible: root.ready
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        model: root.searching ? root.cpak.results : root.cpak.installed

        delegate: PackageRow {
            required property cpakItem modelData
            readonly property flatpakItem info: root.flatpakInfo(modelData.flatpakId, Backend.flatpak.revision)
            readonly property string newVersion: !modelData.upgradable ? ""
                : modelData.newVersion.length > 0 ? modelData.newVersion : qsTr("Update")

            width: list.rowWidth
            // a Flatpak app has no icon in cpak's catalog; the Flatpak one is used when that is on
            pkgName: modelData.flatpakId.length > 0 ? "flatpak/" + modelData.flatpakId : "cpak/" + modelData.origin
            title: info.title.length > 0 ? info.title : modelData.title
            summary: modelData.summary.length > 0 ? modelData.summary : info.summary.length > 0 ? info.summary
                : modelData.flatpakId.length > 0 ? modelData.flatpakId : modelData.origin
            source: modelData.flatpakId.length > 0 ? "flatpak" : "cpak"
            trailing: newVersion.length > 0 ? root.cpak.shortVersion(modelData.installedVersion) + " → " + root.cpak.shortVersion(newVersion)
                : modelData.installed ? (root.searching ? qsTr("Installed") : root.cpak.shortVersion(modelData.installedVersion))
                : root.cpak.shortVersion(modelData.version)
            trailingColor: newVersion.length > 0 ? Look.warning : Look.textMuted
            onClicked: root.openApp(modelData.origin)
        }
    }

    EmptyState {
        visible: root.ready && list.count === 0
        anchors.centerIn: list
        busy: root.cpak.loading
        text: root.cpak.loading ? qsTr("Loading the app catalogue…")
            : root.cpak.failed ? qsTr("The app catalogue could not be loaded.")
            : root.searching ? qsTr("No cpak apps match “%1”.").arg(root.query)
            : qsTr("Type in the search field to find one of %n cpak app(s).", "", root.cpak.appCount)
        action: root.cpak.loading || !root.cpak.failed ? "" : qsTr("Try again")
        actionTone: "soft"
        onTriggered: root.cpak.refresh()
    }
}
