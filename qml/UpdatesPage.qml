pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

Item {
    id: root

    signal openPackage(string name)

    PackageFilter {
        id: filter
        sourceModel: Backend.packages
        mode: PackageFilter.Updates
    }

    PageHeader {
        id: pageHeader
        title: filter.count > 0 ? qsTr("%n update(s) available", "", filter.count) : qsTr("Updates")
        subtitle: Backend.checkingUpdates ? qsTr("Checking the repositories…")
            : qsTr("Updates are always applied to the whole system.")
        actions: [
            PillButton {
                tone: "soft"
                text: qsTr("Check again")
                enabled: !Backend.loading && !Backend.checkingUpdates
                onClicked: Backend.reload()
            },
            PillButton {
                text: qsTr("Update all")
                enabled: filter.count > 0
                onClicked: Backend.upgradeAll()
            }
        ]
    }

    PageList {
        id: list
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        model: filter

        delegate: PackageRow {
            required property packageInfo pkg

            width: list.rowWidth
            pkgName: pkg.name
            title: pkg.title
            summary: pkg.summary
            source: pkg.source
            trailing: pkg.installedVersion + "  →  " + pkg.version
            trailingColor: Look.accent
            onClicked: root.openPackage(pkg.name)
        }
    }

    EmptyState {
        anchors.centerIn: list
        visible: filter.count === 0
        busy: Backend.loading || Backend.checkingUpdates
        plain: true
        title: busy ? "" : qsTr("Your system is up to date.")
    }
}
