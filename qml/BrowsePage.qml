pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Item {
    id: root

    property string query
    // true: only installed packages
    property bool installedOnly: false
    property string category: ""
    property bool appsOnly: true

    signal openPackage(string name)

    readonly property var categories: [
        {"id": "", "label": qsTr("All")},
        {"id": "Network", "label": qsTr("Internet")},
        {"id": "AudioVideo", "label": qsTr("Multimedia")},
        {"id": "Graphics", "label": qsTr("Graphics")},
        {"id": "Game", "label": qsTr("Games")},
        {"id": "Office", "label": qsTr("Office")},
        {"id": "Development", "label": qsTr("Development")},
        {"id": "Education", "label": qsTr("Education")},
        {"id": "Science", "label": qsTr("Science")},
        {"id": "System", "label": qsTr("System")},
        {"id": "Utility", "label": qsTr("Utilities")}
    ]

    PackageFilter {
        id: filter
        sourceModel: Backend.packages
        query: root.query
        category: root.category
        // A search or the installed view covers every package, not only desktop apps.
        mode: root.installedOnly ? PackageFilter.Installed
            : (root.appsOnly && root.query.length === 0) || root.category.length > 0 ? PackageFilter.Apps : PackageFilter.All
    }

    Column {
        id: pageHeader
        x: Look.pagePadding
        y: Look.pagePadding - 6
        width: parent.width - 2 * Look.pagePadding
        spacing: 14

        Item {
            width: parent.width
            height: 34

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.query.length > 0 ? qsTr("Results for “%1”").arg(root.query)
                    : root.installedOnly ? qsTr("Installed") : qsTr("Browse")
                color: Look.text
                font.pixelSize: Look.fontHeading
                font.weight: Font.Bold
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%n package(s)", "", filter.count)
                    color: Look.textMuted
                    font.pixelSize: Look.fontSmall + 1
                }

                PillButton {
                    visible: !root.installedOnly && root.query.length === 0 && root.category.length === 0
                    tone: "soft"
                    implicitHeight: 30
                    text: root.appsOnly ? qsTr("Show all packages") : qsTr("Show apps only")
                    onClicked: root.appsOnly = !root.appsOnly
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.categories

                AbstractButton {
                    id: chip

                    required property var modelData
                    readonly property bool current: root.category === modelData.id

                    implicitHeight: 30
                    implicitWidth: chipLabel.implicitWidth + 28
                    onClicked: root.category = modelData.id
                    Accessible.name: modelData.label

                    background: Rectangle {
                        radius: height / 2
                        color: chip.current ? Look.accent : (chip.hovered ? Look.surfaceAlt : Look.surface)
                        border.width: chip.current ? 0 : 1
                        border.color: chip.visualFocus ? Look.accent : Look.border
                    }

                    contentItem: Text {
                        id: chipLabel
                        text: chip.modelData.label
                        color: chip.current ? Look.onAccent : Look.text
                        font.pixelSize: Look.fontSmall + 1
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }
    }

    GridView {
        id: grid

        readonly property int columns: Math.max(1, Math.floor(width / 330))

        anchors.top: pageHeader.bottom
        anchors.topMargin: 18
        anchors.bottom: parent.bottom
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding + 12
        cellWidth: Math.floor(width / columns)
        cellHeight: 116
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: filter
        cacheBuffer: 600

        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: cell

            required property string name
            required property string title
            required property string summary
            required property string source
            required property bool installed
            required property bool upgradable

            width: grid.cellWidth
            height: grid.cellHeight

            AppCard {
                width: parent.width - 12
                height: parent.height - 12
                pkgName: cell.name
                title: cell.title
                summary: cell.summary
                source: cell.source
                installed: cell.installed
                upgradable: cell.upgradable
                onClicked: root.openPackage(cell.name)
            }
        }
    }

    Column {
        anchors.centerIn: grid
        visible: filter.count === 0
        spacing: 8

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Backend.loading
            running: visible
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !Backend.loading
            text: root.query.length > 0 ? qsTr("Nothing found in the repositories.") : qsTr("Nothing here yet.")
            color: Look.textMuted
            font.pixelSize: Look.fontTitle
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !Backend.loading && root.query.length > 0 && Backend.aurEnabled
            text: qsTr("Try the AUR tab for community packages.")
            color: Look.textMuted
            font.pixelSize: Look.fontBody
        }
    }
}
