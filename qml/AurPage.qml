pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Item {
    id: root

    property string query

    signal openAur(var item)

    onQueryChanged: debounce.restart()
    Component.onCompleted: if (Backend.aurEnabled) Backend.aur.search(query)

    Timer {
        id: debounce
        interval: 350
        onTriggered: if (Backend.aurEnabled) Backend.aur.search(root.query)
    }

    // Opt-in
    Rectangle {
        visible: !Backend.aurEnabled
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Look.pagePadding, 620)
        height: optIn.height + 56
        radius: Look.radius + 2
        color: Look.surface
        border.width: 1
        border.color: Look.border

        Column {
            id: optIn
            x: 28
            y: 28
            width: parent.width - 56
            spacing: 14

            Row {
                spacing: 12

                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 28
                    color: Look.warning
                    path: "M12 4 21 20H3zM12 10v5M12 17.5v.5"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Arch User Repository")
                    color: Look.text
                    font.pixelSize: Look.fontHeading
                    font.weight: Font.Bold
                }
            }

            Text {
                width: parent.width
                text: qsTr("The AUR holds build recipes written by the community. They are not reviewed or supported by CachyOS, are built on your machine and can run any code while doing so. Always read the PKGBUILD before installing.")
                color: Look.textMuted
                font.pixelSize: Look.fontBody + 1
                lineHeight: 1.3
                wrapMode: Text.WordWrap
            }

            Text {
                visible: !Backend.paruAvailable
                width: parent.width
                text: qsTr("Building AUR packages needs “paru”, which is not installed. You can still search and review packages.")
                color: Look.warning
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            PillButton {
                text: qsTr("Enable the AUR")
                onClicked: {
                    Backend.aurEnabled = true;
                    Backend.aur.search(root.query);
                }
            }
        }
    }

    Item {
        id: pageHeader
        visible: Backend.aurEnabled
        x: Look.pagePadding
        y: Look.pagePadding - 6
        width: parent.width - 2 * Look.pagePadding
        height: 56

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                text: qsTr("AUR")
                color: Look.text
                font.pixelSize: Look.fontHeading
                font.weight: Font.Bold
            }

            Text {
                text: qsTr("Community packages, built on your machine. Not reviewed by CachyOS.")
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
            }
        }

        PillButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            tone: "soft"
            implicitHeight: 30
            text: qsTr("Disable the AUR")
            onClicked: Backend.aurEnabled = false
        }
    }

    ListView {
        id: list
        visible: Backend.aurEnabled
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Backend.aur.results

        ScrollBar.vertical: ScrollBar {}

        delegate: PackageRow {
            required property var modelData

            width: ListView.view.width - 12
            pkgName: modelData.name
            title: modelData.name
            summary: modelData.description
            source: "aur"
            trailing: modelData.installed ? qsTr("Installed")
                : modelData.outOfDate ? qsTr("Out of date")
                : qsTr("%1 · %n vote(s)", "", modelData.votes).arg(modelData.version)
            trailingColor: modelData.outOfDate && !modelData.installed ? Look.warning : Look.textMuted
            onClicked: root.openAur(modelData)
        }
    }

    Column {
        visible: Backend.aurEnabled && list.count === 0
        anchors.centerIn: list
        spacing: 10

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Backend.aur.searching
            running: visible
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !Backend.aur.searching
            text: Backend.aur.error.length > 0 ? Backend.aur.error
                : root.query.length < 2 ? qsTr("Type in the search field to find AUR packages.")
                : qsTr("No AUR packages match “%1”.").arg(root.query)
            color: Backend.aur.error.length > 0 ? Look.danger : Look.textMuted
            font.pixelSize: Look.fontTitle
        }
    }
}
