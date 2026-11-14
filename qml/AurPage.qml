pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

Item {
    id: root

    property string query
    readonly property AurClient aur: Backend.aur
    readonly property bool searching: query.trim().length >= 2
    // Without a search the page lists the installed AUR packages that can be updated.
    readonly property bool showUpdates: !searching && aur.updateCount > 0

    signal openAur(item: aurPackage)
    // Review several packages one after the other, then build them together.
    signal reviewAll(items: list<aurPackage>)
    signal openSettings

    onQueryChanged: debounce.restart()
    Component.onCompleted: if (Backend.aurEnabled) aur.search(query)

    Timer {
        id: debounce
        interval: 350
        onTriggered: if (Backend.aurEnabled) root.aur.search(root.query)
    }

    EmptyState {
        visible: !Backend.aurEnabled
        anchors.centerIn: root
        glyph: "M12 3 20 7.5v9L12 21l-8-4.5v-9zM4 7.5 12 12l8-4.5M12 12v9"
        title: qsTr("The AUR is turned off")
        text: qsTr("Turn it on in the settings to search and build community packages.")
        action: qsTr("Open settings")
        onTriggered: root.openSettings()
    }

    PageHeader {
        id: pageHeader
        visible: Backend.aurEnabled
        title: qsTr("AUR")
        subtitle: qsTr("Community packages, built on your machine. Not reviewed by CachyOS.")

        UpdatesBanner {
            visible: root.showUpdates
            width: pageHeader.width - Look.gutter
            count: root.aur.updateCount
            action: count > 1 ? qsTr("Review and update all") : ""
            text: count > 1
                ? qsTr("Each update is built from a new PKGBUILD. Open a package to review it, or review all of them one after the other and build them together.")
                : qsTr("Each update is built from a new PKGBUILD. Open a package to review it before updating.")
            onTriggered: root.reviewAll(root.aur.updates)
        }
    }

    PageList {
        id: list
        visible: Backend.aurEnabled
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        model: root.showUpdates ? root.aur.updates : root.aur.results

        delegate: PackageRow {
            required property aurPackage modelData

            width: list.rowWidth
            pkgName: modelData.name
            title: modelData.name
            summary: modelData.description
            source: "aur"
            trailing: modelData.upgradable ? modelData.installedVersion + " → " + modelData.version
                : modelData.installed ? qsTr("Installed")
                : modelData.inRepos ? qsTr("In the repositories")
                : modelData.outOfDate ? qsTr("Out of date")
                : qsTr("%1 · %n vote(s)", "", modelData.votes).arg(modelData.version)
            trailingColor: modelData.upgradable || (modelData.outOfDate && !modelData.installed) ? Look.warning : Look.textMuted
            Accessible.description: modelData.upgradable
                ? qsTr("Update from %1 to %2").arg(modelData.installedVersion).arg(modelData.version) : ""
            onClicked: root.openAur(modelData)
        }
    }

    EmptyState {
        visible: Backend.aurEnabled && list.count === 0
        anchors.centerIn: list
        busy: root.aur.searching
        title: !busy && root.aur.error.length === 0 && !root.searching && (root.aur.checkingUpdates || root.aur.installedCount > 0)
            ? (root.aur.checkingUpdates ? qsTr("Checking your AUR packages for updates…")
                : qsTr("Your %n AUR package(s) are up to date.", "", root.aur.installedCount))
            : ""
        text: busy ? ""
            : root.aur.error.length > 0 ? root.aur.error
            : !root.searching ? qsTr("Type in the search field to find AUR packages.")
            : qsTr("No AUR packages match “%1”.").arg(root.query)
        textColor: root.aur.error.length > 0 ? Look.danger : Look.textMuted
    }
}
