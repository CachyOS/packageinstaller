pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CachyOS.PackageInstaller

Item {
    id: root

    readonly property AppImageClient client: Backend.appImages
    readonly property bool ready: Backend.appImagesEnabled

    // The file that is waiting for a yes
    property appImageCandidate candidate
    // The app that is waiting to be removed
    property appImageItem doomed

    signal openSettings

    function propose(url: url): void {
        if (!ready || client.busy)
            return;
        candidate = client.inspect(url);
        addDialog.open();
    }

    // Started as "cachyos-pi some.AppImage": the file is offered once AppImage support is on.
    function offerStartupFile(): void {
        if (!ready || !Backend.hasStartupFile)
            return;
        const file = Backend.takeStartupFile();
        if (file.toString().length > 0)
            propose(file);
    }

    onReadyChanged: offerStartupFile()
    Component.onCompleted: {
        client.clearMessage();
        offerStartupFile();
    }

    component Sheet: Dialog {
        // the button that gets the keyboard when the sheet opens: the one that changes nothing
        required property Item firstButton

        focus: true
        closePolicy: Popup.CloseOnEscape
        width: Math.min(Overlay.overlay.width - 80, 560)
        onOpened: firstButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // Files can be dropped anywhere on the page
    DropArea {
        id: dropArea
        anchors.fill: root
        enabled: root.ready
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                drop.accept();
                root.propose(drop.urls[0]);
            }
        }
    }

    Rectangle {
        anchors.fill: root
        anchors.margins: 10
        visible: dropArea.containsDrag
        z: 5
        radius: Look.radius
        color: Look.accentSoft
        border.width: 2
        border.color: Look.accent

        Heading {
            anchors.centerIn: parent
            text: qsTr("Drop the AppImage to add it")
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose an AppImage")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        nameFilters: [qsTr("AppImage files (*.AppImage *.appimage)"), qsTr("All files (*)")]
        onAccepted: root.propose(selectedFile)
    }

    // Turned off: the switch lives on the settings page
    EmptyState {
        visible: !root.ready
        anchors.centerIn: root
        glyph: "M5 4h14a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM12 8v8M8 12h8"
        title: qsTr("AppImage support is turned off")
        text: qsTr("Turn it on in the settings to add AppImage files to your menu.")
        action: qsTr("Open settings")
        onTriggered: root.openSettings()
    }

    PageHeader {
        id: pageHeader
        visible: root.ready
        title: qsTr("AppImage")
        subtitle: qsTr("Apps you downloaded as a single file. Not checked, not sandboxed and not updated by CachyOS.")
        actions: PillButton {
            enabled: !root.client.busy
            text: qsTr("Add AppImage…")
            onClicked: fileDialog.open()
        }

        // Most AppImages cannot start without libfuse 2
        Notice {
            width: pageHeader.width
            tone: Notice.Warning
            text: root.client.fuseAvailable ? "" : qsTr("Many AppImages need the “fuse2” package to start, and it is not installed.")

            PillButton {
                size: "small"
                tone: "soft"
                text: qsTr("Install fuse2")
                onClicked: Backend.install("fuse2")
            }
        }

        // What the last action came to
        Row {
            visible: root.client.busy || root.client.error.length > 0 || root.client.added.length > 0
            width: pageHeader.width
            spacing: 10

            BusyIndicator {
                width: 22
                height: 22
                visible: root.client.busy
                running: visible
            }

            PlainText {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - (root.client.busy ? 32 : 0)
                text: root.client.busy ? qsTr("Adding %1…").arg(root.candidate.name)
                    : root.client.error.length > 0 ? root.client.error
                    : qsTr("%1 was added to your menu.").arg(root.client.added)
                color: root.client.busy ? Look.textMuted : root.client.error.length > 0 ? Look.danger : Look.accent
                wrapMode: Text.WordWrap
            }
        }
    }

    PageList {
        id: list
        visible: root.ready
        anchors.top: pageHeader.bottom
        anchors.topMargin: 14
        anchors.bottom: root.bottom
        reuseItems: false
        model: root.client.apps

        delegate: Card {
            id: row

            required property appImageItem modelData

            width: list.rowWidth
            height: 72

            AppIcon {
                id: icon
                x: 14
                anchors.verticalCenter: row.verticalCenter
                pkgName: "appimage/" + row.modelData.id
                size: 44
            }

            Column {
                id: texts
                anchors.left: icon.right
                anchors.leftMargin: 14
                anchors.right: buttons.left
                anchors.rightMargin: 14
                anchors.verticalCenter: row.verticalCenter
                spacing: 2

                Row {
                    width: texts.width
                    spacing: 8

                    PlainText {
                        width: Math.min(implicitWidth, texts.width - badge.width - 8)
                        text: row.modelData.name
                        font.pixelSize: Look.fontLead
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    SourceBadge {
                        id: badge
                        anchors.verticalCenter: parent.verticalCenter
                        source: "appimage"
                    }
                }

                Muted {
                    width: texts.width
                    text: row.modelData.missing ? qsTr("The file is gone: %1").arg(row.modelData.path)
                        : [row.modelData.summary, row.modelData.version, row.modelData.size].filter(part => part.length > 0).join(" · ")
                    color: row.modelData.missing ? Look.warning : Look.textMuted
                    elide: Text.ElideRight
                }
            }

            Row {
                id: buttons
                anchors.right: row.right
                anchors.rightMargin: 12
                anchors.verticalCenter: row.verticalCenter
                spacing: 8

                PillButton {
                    visible: !row.modelData.missing && !row.modelData.terminal && row.modelData.owned
                    size: "small"
                    tone: "soft"
                    text: qsTr("Open")
                    Accessible.name: qsTr("Open %1").arg(row.modelData.name)
                    onClicked: root.client.launch(row.modelData.id)
                }

                PillButton {
                    size: "small"
                    enabled: !root.client.busy
                    tone: "danger"
                    text: qsTr("Remove")
                    Accessible.name: qsTr("Remove %1").arg(row.modelData.name)
                    onClicked: {
                        root.doomed = row.modelData;
                        removeDialog.open();
                    }
                }
            }
        }
    }

    EmptyState {
        visible: root.ready && list.count === 0 && !root.client.busy
        anchors.centerIn: list
        plain: true
        title: qsTr("No AppImages added yet")
        text: qsTr("Drop an AppImage file here or choose one with “Add AppImage…”. It is copied to %1 and gets a menu entry.").arg(root.client.directory)
    }

    Sheet {
        id: addDialog
        firstButton: addCancel

        contentItem: Column {
            id: addBody
            spacing: 14

            Heading {
                width: addBody.width
                text: root.candidate.ok ? qsTr("Add %1?").arg(root.candidate.name) : qsTr("Cannot add this file")
                elide: Text.ElideMiddle
            }

            Muted {
                width: addBody.width
                text: root.candidate.ok
                    ? qsTr("The file (%1) is copied to %2 and added to your menu. To read its name and icon the file itself is run right away, so only add files from a source you trust: nobody has checked this program and it is not sandboxed.").arg(root.candidate.size).arg(root.client.directory)
                    : root.candidate.reason
                font.pixelSize: Look.fontBody
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: addBody.right
                spacing: 10

                PillButton {
                    id: addCancel
                    tone: "soft"
                    text: root.candidate.ok ? qsTr("Cancel") : qsTr("Close")
                    onClicked: addDialog.close()
                }

                PillButton {
                    visible: root.candidate.ok
                    text: qsTr("Add")
                    onClicked: {
                        addDialog.close();
                        root.client.add(root.candidate.url);
                    }
                }
            }
        }
    }

    Sheet {
        id: removeDialog
        firstButton: removeCancel

        contentItem: Column {
            id: removeBody
            spacing: 14

            Heading {
                width: removeBody.width
                text: qsTr("Remove %1?").arg(root.doomed.name)
                elide: Text.ElideRight
            }

            Muted {
                width: removeBody.width
                text: !root.doomed.owned
                    ? qsTr("Only the menu entry is deleted. It points to %1, which is not a file this app added, so that file is left alone.").arg(root.doomed.path)
                    : qsTr("The file %1 and its menu entry are deleted. Settings the app saved in your home directory are kept.").arg(root.doomed.path)
                font.pixelSize: Look.fontBody
                lineHeight: 1.25
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            Row {
                anchors.right: removeBody.right
                spacing: 10

                PillButton {
                    id: removeCancel
                    tone: "soft"
                    text: qsTr("Cancel")
                    onClicked: removeDialog.close()
                }

                PillButton {
                    tone: "danger"
                    text: qsTr("Remove")
                    onClicked: {
                        removeDialog.close();
                        root.client.remove(root.doomed.id, root.doomed.path);
                    }
                }
            }
        }
    }
}
