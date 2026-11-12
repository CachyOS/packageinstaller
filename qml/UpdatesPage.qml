pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Item {
    id: root

    signal openPackage(string name)

    PackageFilter {
        id: filter
        sourceModel: Backend.packages
        mode: PackageFilter.Updates
    }

    Item {
        id: pageHeader
        x: Look.pagePadding
        y: Look.pagePadding - 6
        width: parent.width - 2 * Look.pagePadding
        height: 56

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                textFormat: Text.PlainText
                text: filter.count > 0 ? qsTr("%n update(s) available", "", filter.count) : qsTr("Updates")
                color: Look.text
                font.pixelSize: Look.fontHeading
                font.weight: Font.Bold
            }

            Text {
                textFormat: Text.PlainText
                text: Backend.checkingUpdates ? qsTr("Checking the repositories…")
                    : qsTr("Updates are always applied to the whole system.")
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            PillButton {
                tone: "soft"
                text: qsTr("Check again")
                enabled: !Backend.loading && !Backend.checkingUpdates
                onClicked: Backend.reload()
            }

            PillButton {
                text: qsTr("Update all")
                enabled: filter.count > 0
                onClicked: Backend.upgradeAll()
            }
        }
    }

    ListView {
        id: list
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: filter

        ScrollBar.vertical: ScrollBar {}

        delegate: PackageRow {
            required property string name
            required property string version
            required property string installedVersion
            required title
            required summary
            required source

            width: ListView.view.width - 12
            pkgName: name
            trailing: installedVersion + "  →  " + version
            trailingColor: Look.accent
            onClicked: root.openPackage(name)
        }
    }

    Column {
        anchors.centerIn: list
        visible: filter.count === 0
        spacing: 10

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Backend.loading || Backend.checkingUpdates
            running: visible
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !Backend.loading && !Backend.checkingUpdates
            text: qsTr("Your system is up to date.")
            color: Look.textMuted
            font.pixelSize: Look.fontTitle
        }
    }
}
