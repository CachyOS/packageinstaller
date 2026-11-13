pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Item {
    id: root

    property string query
    property bool installedOnly: false
    property string category: ""
    property bool appsOnly: true

    signal openPackage(string name)

    PackageFilter {
        id: filter
        sourceModel: Backend.packages
        query: root.query
        category: root.category
        // A search or the installed view covers every package, not only desktop apps.
        mode: root.installedOnly ? PackageFilter.Installed
            : (root.appsOnly && root.query.length === 0) || root.category.length > 0 ? PackageFilter.Apps : PackageFilter.All
    }

    ListModel {
        id: categories
        ListElement { category: ""; label: qsTr("All") }
        ListElement { category: "Network"; label: qsTr("Internet") }
        ListElement { category: "AudioVideo"; label: qsTr("Multimedia") }
        ListElement { category: "Graphics"; label: qsTr("Graphics") }
        ListElement { category: "Game"; label: qsTr("Games") }
        ListElement { category: "Office"; label: qsTr("Office") }
        ListElement { category: "Development"; label: qsTr("Development") }
        ListElement { category: "Education"; label: qsTr("Education") }
        ListElement { category: "Science"; label: qsTr("Science") }
        ListElement { category: "System"; label: qsTr("System") }
        ListElement { category: "Utility"; label: qsTr("Utilities") }
    }

    PageHeader {
        id: pageHeader
        compact: true
        title: root.query.length > 0 ? qsTr("Results for “%1”").arg(root.query)
            : root.installedOnly ? qsTr("Installed") : qsTr("Browse")
        actions: [
            Muted {
                anchors.verticalCenter: parent?.verticalCenter
                text: qsTr("%n package(s)", "", filter.count)
            },
            PillButton {
                visible: !root.installedOnly && root.query.length === 0 && root.category.length === 0
                tone: "soft"
                implicitHeight: 30
                text: root.appsOnly ? qsTr("Show all packages") : qsTr("Show apps only")
                onClicked: root.appsOnly = !root.appsOnly
            }
        ]

        Flow {
            width: pageHeader.width
            spacing: 8

            Repeater {
                model: categories

                Chip {
                    id: chip

                    required property string category
                    required property string label

                    text: chip.label
                    current: root.category === chip.category
                    onClicked: root.category = chip.category
                }
            }
        }
    }

    GridView {
        id: grid

        readonly property int columns: Math.max(1, Math.floor(width / 330))

        anchors.top: pageHeader.bottom
        anchors.topMargin: 18
        anchors.bottom: root.bottom
        x: Look.pagePadding
        // every card keeps a gap on its right, so the grid is one gutter wider to end flush with the header
        width: root.width - 2 * Look.pagePadding + Look.gutter
        cellWidth: Math.floor(width / columns)
        cellHeight: 116
        boundsBehavior: Flickable.StopAtBounds
        model: filter
        cacheBuffer: 600
        reuseItems: true
        clip: true

        ScrollBar.vertical: ScrollBar {}

        WheelScroll {}

        delegate: AppCard {
            required property packageInfo pkg

            width: grid.cellWidth - Look.gutter
            height: grid.cellHeight - Look.gutter
            pkgName: pkg.name
            title: pkg.title
            summary: pkg.summary
            source: pkg.source
            installed: pkg.installed
            upgradable: pkg.upgradable
            onClicked: root.openPackage(pkg.name)
        }
    }

    EmptyState {
        anchors.centerIn: grid
        visible: filter.count === 0
        busy: Backend.loading
        plain: true
        title: Backend.loading ? "" : root.query.length > 0 ? qsTr("Nothing found in the repositories.") : qsTr("Nothing here yet.")
        text: !Backend.loading && root.query.length > 0 && Backend.aurEnabled ? qsTr("Try the AUR tab for community packages.") : ""
    }
}
