pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Item {
    id: root

    property string query
    // Without a search the page lists the installed AUR packages that can be updated.
    readonly property bool showUpdates: query.trim().length < 2 && Backend.aur.updates.length > 0

    signal openAur(var item)
    // Review several packages one after the other, then build them together.
    signal reviewAll(var items)

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
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Arch User Repository")
                    color: Look.text
                    font.pixelSize: Look.fontHeading
                    font.weight: Font.Bold
                }
            }

            Text {
                textFormat: Text.PlainText
                width: parent.width
                text: qsTr("The AUR holds build recipes written by the community. They are not reviewed or supported by CachyOS, are built on your machine and can run any code while doing so. Always read the PKGBUILD before installing.")
                color: Look.textMuted
                font.pixelSize: Look.fontBody + 1
                lineHeight: 1.3
                wrapMode: Text.WordWrap
            }

            Text {
                textFormat: Text.PlainText
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
                textFormat: Text.PlainText
                text: qsTr("AUR")
                color: Look.text
                font.pixelSize: Look.fontHeading
                font.weight: Font.Bold
            }

            Text {
                textFormat: Text.PlainText
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

    Column {
        id: updatesHeader
        visible: Backend.aurEnabled && root.showUpdates
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding - 12
        spacing: 2

        Item {
            width: parent.width
            height: Math.max(updatesTitle.height, updateAll.height)

            Text {
                id: updatesTitle
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n update(s) available", "", Backend.aur.updates.length)
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            PillButton {
                id: updateAll
                visible: Backend.aur.updates.length > 1
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 34
                text: qsTr("Review and update all")
                onClicked: root.reviewAll(Backend.aur.updates)
            }
        }

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: Backend.aur.updates.length > 1
                ? qsTr("Each update is built from a new PKGBUILD. Open a package to review it, or review all of them one after the other and build them together.")
                : qsTr("Each update is built from a new PKGBUILD. Open a package to review it before updating.")
            color: Look.textMuted
            font.pixelSize: Look.fontSmall + 1
            wrapMode: Text.WordWrap
        }
    }

    ListView {
        id: list
        visible: Backend.aurEnabled
        anchors.top: updatesHeader.visible ? updatesHeader.bottom : pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.showUpdates ? Backend.aur.updates : Backend.aur.results

        ScrollBar.vertical: ScrollBar {}

        delegate: PackageRow {
            required property var modelData

            width: ListView.view.width - 12
            pkgName: modelData.name
            title: modelData.name
            summary: modelData.description
            source: "aur"
            trailing: modelData.upgradable ? modelData.installedVersion + " → " + modelData.version
                : modelData.installed ? qsTr("Installed")
                : modelData.outOfDate ? qsTr("Out of date")
                : qsTr("%1 · %n vote(s)", "", modelData.votes).arg(modelData.version)
            trailingColor: modelData.upgradable || (modelData.outOfDate && !modelData.installed) ? Look.warning : Look.textMuted
            Accessible.description: modelData.upgradable
                ? qsTr("Update from %1 to %2").arg(modelData.installedVersion).arg(modelData.version) : ""
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
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !Backend.aur.searching && Backend.aur.error.length === 0 && root.query.trim().length < 2
                && (Backend.aur.checkingUpdates || Backend.aur.installedCount > 0)
            text: Backend.aur.checkingUpdates ? qsTr("Checking your AUR packages for updates…")
                : qsTr("Your %n AUR package(s) are up to date.", "", Backend.aur.installedCount)
            color: Look.text
            font.pixelSize: Look.fontTitle
        }

        Text {
            textFormat: Text.PlainText
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
