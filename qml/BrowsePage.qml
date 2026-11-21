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

    readonly property var categories: [
        { category: "", label: qsTr("All") },
        { category: "Network", label: qsTr("Internet") },
        { category: "AudioVideo", label: qsTr("Multimedia") },
        { category: "Graphics", label: qsTr("Graphics") },
        { category: "Game", label: qsTr("Games") },
        { category: "Office", label: qsTr("Office") },
        { category: "Development", label: qsTr("Development") },
        { category: "Education", label: qsTr("Education") },
        { category: "Science", label: qsTr("Science") },
        { category: "System", label: qsTr("System") },
        { category: "Utility", label: qsTr("Utilities") }
    ]

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
                model: root.categories

                Chip {
                    id: chip

                    required property var modelData

                    text: modelData.label
                    current: root.category === modelData.category
                    onClicked: root.category = modelData.category
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
            id: card

            required property string name
            required title
            required summary
            required source
            required installed
            required upgradable

            width: grid.cellWidth - Look.gutter
            height: grid.cellHeight - Look.gutter
            pkgName: card.name
            onClicked: root.openPackage(card.name)
        }
    }

    EmptyState {
        anchors.centerIn: grid
        visible: filter.count === 0
        busy: Backend.loading
        plain: true
        title: Backend.loading ? "" : root.query.length === 0 ? qsTr("Nothing here yet.")
            : root.installedOnly ? qsTr("No installed package matches “%1”.").arg(root.query) : qsTr("Nothing found in the repositories.")
        text: !Backend.loading && root.query.length > 0 && !root.installedOnly && Backend.aurEnabled
            ? qsTr("Try the AUR tab for community packages.") : ""
    }
}
