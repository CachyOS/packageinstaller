pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
        : !flatpak.hasRemote && flatpak.installed.length === 0 ? "noremote" : ""  // apps installed for the user can still be managed
    readonly property bool ready: Backend.flatpakEnabled && obstacle === ""
    // reference -> new version, to mark the installed apps that can be updated
    readonly property var newVersions: {
        const versions = {};
        for (const update of flatpak.updates)
            versions[update.ref] = update.version.length > 0 ? update.version : qsTr("Update");
        return versions;
    }
    readonly property int runtimeUpdates: flatpak.updates.filter(update => !update.isApp).length

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

    // Turned off, not installed or without a source: say so and offer the next step
    Column {
        visible: root.obstacle !== ""
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Look.pagePadding, 520)
        spacing: 12

        Glyph {
            anchors.horizontalCenter: parent.horizontalCenter
            size: 40
            color: Look.textMuted
            path: "M4 8l8-4l8 4v8l-8 4l-8-4zM4 8l8 4l8-4M12 12v8M8 6l8 4"
        }

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: root.obstacle === "off" ? qsTr("Flatpak is turned off")
                : root.obstacle === "missing" ? qsTr("Flatpak is not installed") : qsTr("No app source is set up")
            color: Look.text
            font.pixelSize: Look.fontHeading
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: root.obstacle === "off" ? qsTr("Turn it on in the settings to search and install Flatpak apps.")
                : root.obstacle === "missing" ? qsTr("The “flatpak” package is needed to install and run these apps.")
                : qsTr("Flathub is the main source of Flatpak apps. Add it to browse and install them.")
            color: Look.textMuted
            font.pixelSize: Look.fontBody + 1
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        PillButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.obstacle === "off" ? qsTr("Open settings")
                : root.obstacle === "missing" ? qsTr("Install Flatpak") : qsTr("Add Flathub")
            onClicked: {
                if (root.obstacle === "off")
                    root.openSettings();
                else if (root.obstacle === "missing")
                    Backend.install("flatpak");
                else
                    Backend.addFlathub();
            }
        }
    }

    Item {
        id: pageHeader
        visible: root.ready
        x: Look.pagePadding
        y: Look.pagePadding - 6
        width: parent.width - 2 * Look.pagePadding
        height: 56

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                textFormat: Text.PlainText
                text: qsTr("Flatpak")
                color: Look.text
                font.pixelSize: Look.fontHeading
                font.weight: Font.Bold
            }

            Text {
                textFormat: Text.PlainText
                text: qsTr("Sandboxed apps from Flathub. Not packaged by CachyOS.")
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
            }
        }
    }

    Column {
        id: updatesHeader
        visible: root.ready && !root.searching && root.flatpak.updates.length > 0
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
                text: qsTr("%n update(s) available", "", root.flatpak.updates.length)
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            PillButton {
                id: updateAll
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 34
                text: qsTr("Update all")
                onClicked: Backend.updateFlatpaks()
            }
        }

        Text {
            textFormat: Text.PlainText
            visible: root.runtimeUpdates > 0
            width: parent.width
            text: qsTr("%n of them are runtimes and extensions that the apps share.", "", root.runtimeUpdates)
            color: Look.textMuted
            font.pixelSize: Look.fontSmall + 1
            wrapMode: Text.WordWrap
        }
    }

    ListView {
        id: list
        visible: root.ready
        anchors.top: updatesHeader.visible ? updatesHeader.bottom : pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        x: Look.pagePadding
        width: parent.width - 2 * Look.pagePadding
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.searching ? root.flatpak.results : root.flatpak.installed

        ScrollBar.vertical: ScrollBar {}

        WheelScroll {}

        delegate: PackageRow {
            required property var modelData
            readonly property string newVersion: modelData.installed ? (root.newVersions[modelData.ref] ?? "") : ""

            width: ListView.view.width - 12
            pkgName: "flatpak/" + modelData.id
            title: modelData.title
            summary: modelData.summary.length > 0 ? modelData.summary : modelData.id
            source: "flatpak"
            trailing: newVersion.length > 0 ? ((modelData.installedVersion ?? "").length > 0 ? modelData.installedVersion + " → " + newVersion : newVersion)
                : modelData.installed ? (root.searching ? qsTr("Installed") : (modelData.installedVersion ?? ""))
                : (modelData.version ?? "")
            trailingColor: newVersion.length > 0 ? Look.warning : Look.textMuted
            onClicked: root.openApp(modelData.id, root.searching ? "" : modelData.key)
        }
    }

    Column {
        visible: root.ready && list.count === 0
        anchors.centerIn: list
        spacing: 10

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.flatpak.loading
            running: visible
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.flatpak.loading ? qsTr("Loading the app catalogue…")
                : !root.flatpak.hasRemote ? qsTr("No app source is set up for all users. Add Flathub to search for apps.")
                : root.flatpak.appCount === 0 ? qsTr("The app catalogue could not be loaded.")
                : root.searching ? qsTr("No Flatpak apps match “%1”.").arg(root.query)
                : qsTr("Type in the search field to find one of %n Flatpak app(s).", "", root.flatpak.appCount)
            color: Look.textMuted
            font.pixelSize: Look.fontTitle
        }

        PillButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.flatpak.loading && root.flatpak.appCount === 0
            tone: "soft"
            text: root.flatpak.hasRemote ? qsTr("Try again") : qsTr("Add Flathub")
            onClicked: root.flatpak.hasRemote ? root.flatpak.refresh() : Backend.addFlathub()
        }
    }
}
