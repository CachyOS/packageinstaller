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
    property appImageItem inspected

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

    // Asked once per run, when other programs manage AppImages and nothing else is being asked.
    function offerImports(): void {
        if (ready && !client.busy && !addDialog.visible && client.takeImportPrompt())
            importDialog.open();
    }

    onReadyChanged: {
        offerStartupFile();
        offerImports();
    }
    Component.onCompleted: {
        client.clearMessage();
        offerStartupFile();
        offerImports();
    }

    Connections {
        target: root.client

        function onImportsChanged(): void {
            root.offerImports();
        }

        function onAppsChanged(): void {
            if (!optionsDialog.visible)
                return;
            const fresh = root.client.apps.find(app => app.id === root.inspected.id);
            if (fresh)
                root.inspected = fresh;
            else
                optionsDialog.close();
        }
    }

    component Sheet: Dialog {
        // the button that gets the keyboard when the sheet opens: the one that changes nothing
        required property Item firstButton

        focus: true
        closePolicy: Popup.CloseOnEscape
        maxWidth: 560
        onOpened: firstButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // A list inside a sheet that scrolls once it gets long
    component CappedList: Flickable {
        default property alias rows: column.data
        property alias spacing: column.spacing

        height: Math.min(column.height, 184)
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        WheelScroll {}

        Column {
            id: column
            width: parent.width
        }
    }

    component OptionField: TextField {
        implicitHeight: 36
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: Look.fontBody
        color: Look.text
        placeholderTextColor: Look.textMuted
        selectionColor: Look.accent
        selectedTextColor: Look.onAccent
        selectByMouse: true

        background: Rectangle {
            radius: Look.radius - 4
            color: Look.surface
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? Look.accent : Look.border
        }
    }

    component OptionChoice: ComboBox {
        id: control

        implicitHeight: 36
        font.pixelSize: Look.fontBody

        background: Rectangle {
            radius: Look.radius - 4
            color: Look.surface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? Look.accent : Look.border
        }

        contentItem: PlainText {
            leftPadding: 12
            verticalAlignment: Text.AlignVCenter
            text: control.displayText
        }

        indicator: Glyph {
            x: control.width - width - 12
            anchors.verticalCenter: control.verticalCenter
            path: "M6 9l6 6 6-6"
            size: 16
            color: Look.textMuted
        }

        delegate: ItemDelegate {
            required property int index
            required property string modelData
            width: parent.width
            height: 36
            highlighted: control.currentIndex === index

            background: Rectangle {
                radius: Look.radius - 6
                color: parent.hovered ? Look.surfaceHover : "transparent"
            }

            contentItem: PlainText {
                verticalAlignment: Text.AlignVCenter
                text: parent.modelData
                font.weight: parent.highlighted ? Font.DemiBold : Font.Normal
            }
        }

        popup: Popup {
            y: control.height + 4
            width: control.width
            padding: 4

            contentItem: ListView {
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                clip: true
            }

            background: Rectangle {
                radius: Look.radius - 4
                color: Look.surface
                border.width: 1
                border.color: Look.border
            }
        }
    }

    component OptionTitle: Column {
        property string title
        property string description

        spacing: 2

        Heading {
            width: parent.width
            text: parent.title
            font.pixelSize: Look.fontLead
        }

        Muted {
            width: parent.width
            text: parent.description
            wrapMode: Text.WordWrap
        }
    }

    component OptionSwitch: Item {
        id: optionSwitch

        property string label
        property bool active

        signal toggled

        implicitHeight: 58

        Rectangle {
            visible: optionSwitch.y > 0
            width: optionSwitch.width
            height: 1
            color: Look.border
        }

        PlainText {
            x: 16
            anchors.verticalCenter: optionSwitch.verticalCenter
            width: optionSwitch.width - 46 - 44
            text: optionSwitch.label
            elide: Text.ElideRight
        }

        Toggle {
            anchors.right: optionSwitch.right
            anchors.rightMargin: 16
            anchors.verticalCenter: optionSwitch.verticalCenter
            active: optionSwitch.active
            name: optionSwitch.label
            onClicked: optionSwitch.toggled()
        }
    }

    component OptionRow: Item {
        id: optionRow

        property string label
        property string value
        property bool link: false
        property bool editable: false
        property string placeholder
        property alias text: field.text
        property var choices: []
        property int choice
        property string buttonGlyph
        property string buttonName
        signal buttonClicked
        signal picked(int index)

        implicitHeight: Math.max(58, texts.implicitHeight + 24)

        Rectangle {
            visible: optionRow.y > 0
            width: optionRow.width
            height: 1
            color: Look.border
        }

        Column {
            id: texts
            x: 16
            anchors.verticalCenter: optionRow.verticalCenter
            width: optionRow.width - 32 - (optionButton.visible ? optionButton.width + 12 : 0)
            spacing: 2

            Muted {
                width: texts.width
                text: optionRow.label
                font.pixelSize: Look.fontSmall
            }

            PlainText {
                visible: !optionRow.editable && !optionRow.link && optionRow.choices.length === 0
                width: texts.width
                text: optionRow.value
                elide: Text.ElideMiddle
            }

            OptionChoice {
                id: picker
                visible: optionRow.choices.length > 0
                width: texts.width
                model: optionRow.choices
                Accessible.name: optionRow.label
                onActivated: index => optionRow.picked(index)

                // a pick of the user must not end what the row is told to show
                Binding on currentIndex {
                    value: optionRow.choice
                }
            }

            OptionField {
                id: field
                visible: optionRow.editable
                width: texts.width
                placeholderText: optionRow.placeholder
                Accessible.name: optionRow.label
            }

            WebLink {
                width: texts.width
                url: optionRow.link ? optionRow.value : ""
            }
        }

        PillButton {
            id: optionButton
            visible: optionRow.buttonGlyph.length > 0
            anchors.right: optionRow.right
            anchors.rightMargin: 12
            anchors.verticalCenter: optionRow.verticalCenter
            size: "small"
            tone: "soft"
            leftPadding: 10
            rightPadding: 10
            Accessible.name: optionRow.buttonName
            onClicked: optionRow.buttonClicked()

            contentItem: Glyph {
                path: optionRow.buttonGlyph
                size: 16
            }
        }
    }

    component OptionAction: AbstractButton {
        id: action

        property string glyph
        property string description

        implicitHeight: 72
        opacity: enabled ? 1 : 0.45

        background: Card {
            hovered: action.hovered
            focused: action.visualFocus
        }

        contentItem: Item {
            Glyph {
                id: actionIcon
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                path: action.glyph
                size: 22
            }

            Column {
                anchors.left: actionIcon.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                PlainText {
                    width: parent.width
                    text: action.text
                    font.weight: Font.DemiBold
                }

                Muted {
                    width: parent.width
                    text: action.description
                    wrapMode: Text.WordWrap
                }
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    component OptionGroup: Rectangle {
        default property alias rows: column.data

        implicitHeight: column.height
        radius: Look.radius
        color: Look.surfaceAlt
        border.width: 1
        border.color: Look.border

        Column {
            id: column
            width: parent.width
        }
    }

    function folderOf(path: string): url {
        const cut = path.lastIndexOf("/");
        return "file://" + encodeURI(cut > 0 ? path.substring(0, cut) : "/");
    }

    function showOptions(app: appImageItem): void {
        inspected = app;
        optionsDialog.open();
    }

    function origin(source: string): string {
        return source.length > 0 ? "github.com/" + source : "";
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
        actions: [
            PillButton {
                visible: root.client.tracking
                enabled: !root.client.checking
                tone: "soft"
                text: qsTr("Check for updates")
                onClicked: root.client.checkUpdates()
            },
            PillButton {
                enabled: !root.client.busy
                tone: "soft"
                text: qsTr("From GitHub…")
                onClicked: githubDialog.open()
            },
            PillButton {
                enabled: !root.client.busy
                text: qsTr("Add AppImage…")
                onClicked: fileDialog.open()
            }
        ]

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

        // Users of Shelly or Gear Lever can bring their AppImages along
        Notice {
            width: pageHeader.width
            text: root.client.imports.length > 0 && !root.client.busy
                ? qsTr("You have %n AppImage(s) in %1. You can bring them along to this app.", "", root.client.imports.length).arg(root.client.importTools) : ""

            PillButton {
                size: "small"
                tone: "soft"
                text: qsTr("Move from %1…").arg(root.client.importTools)
                onClicked: importDialog.open()
            }
        }

        // What the last action came to
        Row {
            id: status

            readonly property bool working: root.client.busy || root.client.checking

            visible: working || root.client.error.length > 0 || root.client.added.length > 0
            width: pageHeader.width
            spacing: 10

            BusyIndicator {
                width: 22
                height: 22
                visible: status.working
                running: visible
            }

            PlainText {
                anchors.verticalCenter: parent.verticalCenter
                width: status.width - (status.working ? 32 : 0) - (stop.visible ? stop.width + 10 : 0)
                text: root.client.downloading ? qsTr("Downloading %1… %2 %").arg(root.client.subject).arg(root.client.progress)
                    : root.client.busy ? qsTr("Adding %1…").arg(root.client.subject)
                    : root.client.checking ? qsTr("Looking for newer releases…")
                    : [root.client.error, root.client.added].filter(part => part.length > 0).join("\n")
                color: status.working ? Look.textMuted : root.client.error.length > 0 ? Look.danger : Look.accent
                wrapMode: Text.WordWrap
            }

            PillButton {
                id: stop
                visible: root.client.downloading
                size: "small"
                tone: "soft"
                text: qsTr("Stop")
                onClicked: root.client.cancelDownload()
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
            // where newer releases come from
            readonly property string origin: root.origin(modelData.source)
            readonly property bool hasUpdate: modelData.update.length > 0

            width: list.rowWidth
            height: 72
            // the app whose options are open stays highlighted
            hovered: open.containsMouse || (optionsDialog.visible && root.inspected.id === modelData.id)
            focused: row.activeFocus
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Options of %1").arg(modelData.name)
            Keys.onReturnPressed: root.showOptions(modelData)
            Keys.onEnterPressed: root.showOptions(modelData)
            Keys.onSpacePressed: root.showOptions(modelData)

            MouseArea {
                id: open
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.showOptions(row.modelData)
            }

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
                        : row.hasUpdate ? qsTr("Release %1 is available from %2").arg(row.modelData.update).arg(row.origin)
                        : [row.modelData.summary, row.modelData.version, row.modelData.size, row.origin].filter(part => part.length > 0).join(" · ")
                    color: row.modelData.missing ? Look.warning : row.hasUpdate ? Look.accent : Look.textMuted
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
                    visible: row.hasUpdate
                    enabled: !root.client.busy
                    size: "small"
                    text: qsTr("Update")
                    Accessible.name: qsTr("Update %1").arg(row.modelData.name)
                    onClicked: root.client.update(row.modelData.id)
                }

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
        text: qsTr("Drop an AppImage file here, choose one with “Add AppImage…” or take one from a project on GitHub. It is copied to %1 and gets a menu entry.").arg(root.client.directory)
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
        id: githubDialog

        // which file of the release is taken
        property int picked: 0

        firstButton: linkField
        onOpened: linkField.selectAll()
        onClosed: root.client.clearOffer()

        Connections {
            target: root.client

            function onOfferChanged(): void {
                githubDialog.picked = root.client.offer.preferred;
            }
        }

        contentItem: Column {
            id: githubBody
            spacing: 14

            Heading {
                width: githubBody.width
                text: qsTr("Add an AppImage from GitHub")
            }

            Muted {
                width: githubBody.width
                text: qsTr("Give the link of a project on GitHub that publishes AppImage files. The file of its newest release is downloaded, and newer releases are offered on this page as updates.")
                font.pixelSize: Look.fontBody
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }

            Row {
                width: githubBody.width
                spacing: 10

                TextField {
                    id: linkField
                    width: githubBody.width - lookUp.width - 10
                    implicitHeight: 40
                    leftPadding: 14
                    rightPadding: 14
                    font.pixelSize: Look.fontBody
                    color: Look.text
                    placeholderText: "https://github.com/owner/project"
                    placeholderTextColor: Look.textMuted
                    selectionColor: Look.accent
                    selectedTextColor: Look.onAccent
                    selectByMouse: true
                    Accessible.name: qsTr("Link of the project on GitHub")
                    onAccepted: if (lookUp.enabled) lookUp.clicked()

                    background: Rectangle {
                        radius: height / 2
                        color: Look.surface
                        border.width: linkField.activeFocus ? 2 : 1
                        border.color: linkField.activeFocus ? Look.accent : Look.border
                    }
                }

                PillButton {
                    id: lookUp
                    enabled: linkField.text.trim().length > 0 && !root.client.lookingUp
                    tone: "soft"
                    text: qsTr("Look up")
                    onClicked: root.client.lookUp(linkField.text)
                }
            }

            Row {
                visible: root.client.lookingUp
                spacing: 10

                BusyIndicator {
                    width: 22
                    height: 22
                    running: root.client.lookingUp
                }

                Muted {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Asking GitHub…")
                }
            }

            PlainText {
                visible: text.length > 0
                width: githubBody.width
                text: root.client.offer.error
                color: Look.danger
                wrapMode: Text.WordWrap
            }

            Column {
                visible: root.client.offer.files.length > 0
                width: githubBody.width
                spacing: 8

                PlainText {
                    width: githubBody.width
                    text: qsTr("Release %1 of %2").arg(root.client.offer.release).arg(root.client.offer.source)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                // a release can carry many files
                CappedList {
                    width: githubBody.width
                    spacing: 8

                    Repeater {
                        model: root.client.offer.files

                        AbstractButton {
                            id: choice

                            required property appImageChoice modelData
                            required property int index

                            readonly property bool chosen: index === githubDialog.picked

                            width: githubBody.width
                            implicitHeight: 40
                            Accessible.name: modelData.name
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: chosen
                            onClicked: githubDialog.picked = index

                            background: Rectangle {
                                radius: Look.radius
                                color: choice.chosen ? Look.accentSoft : "transparent"
                                border.width: choice.chosen || choice.visualFocus ? 2 : 1
                                border.color: choice.chosen || choice.visualFocus ? Look.accent : Look.border
                            }

                            contentItem: Item {
                                PlainText {
                                    x: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 28 - fileSize.width - 10
                                    text: choice.modelData.name
                                    elide: Text.ElideMiddle
                                }

                                Muted {
                                    id: fileSize
                                    anchors.right: parent.right
                                    anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: choice.modelData.size
                                }
                            }
                        }
                    }
                }

                Muted {
                    width: githubBody.width
                    text: qsTr("The file is saved in %1 and added to your menu. To read its name and icon the file itself is run right away, so only add projects you trust: nobody has checked this program and it is not sandboxed.").arg(root.client.directory)
                    font.pixelSize: Look.fontBody
                    lineHeight: 1.25
                    wrapMode: Text.WordWrap
                }
            }

            Row {
                anchors.right: githubBody.right
                spacing: 10

                PillButton {
                    tone: "soft"
                    text: qsTr("Cancel")
                    onClicked: githubDialog.close()
                }

                PillButton {
                    enabled: root.client.offer.files.length > 0 && !root.client.busy
                    text: qsTr("Download and add")
                    onClicked: {
                        root.client.addOffered(githubDialog.picked);
                        githubDialog.close();
                    }
                }
            }
        }
    }

    Sheet {
        id: importDialog
        firstButton: importLater

        contentItem: Column {
            id: importBody
            spacing: 14

            Heading {
                width: importBody.width
                text: qsTr("Bring your AppImages along from %1?").arg(root.client.importTools)
                // two programs make it longer than a line
                wrapMode: Text.WordWrap
            }

            Muted {
                width: importBody.width
                text: qsTr("You added these AppImages with %1. They can be moved to %2 and looked after by this app from now on:").arg(root.client.importTools).arg(root.client.directory)
                font.pixelSize: Look.fontBody
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }

            CappedList {
                width: importBody.width
                spacing: 6

                Repeater {
                    model: root.client.imports

                    Column {
                        id: importedApp

                        required property appImageImport modelData

                        width: importBody.width
                        spacing: 2

                        PlainText {
                            width: importedApp.width
                            text: importedApp.modelData.name
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Muted {
                            width: importedApp.width
                            text: importedApp.modelData.detail
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Muted {
                width: importBody.width
                text: qsTr("Each file gets a new menu entry and its old one is removed, so it shows up only once. Apps that are updated from GitHub keep getting their updates here. Other update sources, environment variables and extra arguments are not carried over. To read its name and icon each file is run right away.")
                font.pixelSize: Look.fontBody
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: importBody.right
                spacing: 10

                PillButton {
                    tone: "soft"
                    text: qsTr("Don't ask again")
                    onClicked: {
                        importDialog.close();
                        root.client.dismissImports();
                    }
                }

                PillButton {
                    id: importLater
                    tone: "soft"
                    text: qsTr("Not now")
                    onClicked: importDialog.close()
                }

                PillButton {
                    enabled: !root.client.busy && root.client.imports.length > 0
                    text: qsTr("Move")
                    onClicked: {
                        importDialog.close();
                        root.client.migrateImports();
                    }
                }
            }
        }
    }

    Sheet {
        id: optionsDialog
        firstButton: optionsClose
        // a click beside the sheet must not throw away what was typed
        closePolicy: dirty ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        maxWidth: 620

        readonly property bool hasSource: root.inspected.source.length > 0
        readonly property bool canChange: !root.inspected.missing && root.inspected.owned
        // counts every edit of the variables, which live in a model
        property int edits: 0
        readonly property bool dirty: {
            void edits;
            return argumentsRow.text !== root.inspected.arguments || websiteRow.text !== root.inspected.website
                || prereleases !== root.inspected.prereleases || github !== optionsDialog.hasSource
                || (github && repositoryRow.text.trim() !== optionsDialog.repository)
                || JSON.stringify(assignments()) !== JSON.stringify(Array.from(root.inspected.environment));
        }
        property bool prereleases
        property bool github
        property string problem
        readonly property string repository: hasSource ? "https://github.com/" + root.inspected.source : ""

        function assignments(): var {
            const list = [];
            for (let i = 0; i < variables.count; ++i) {
                const variable = variables.get(i);
                if (variable.varName.trim().length > 0)
                    list.push(variable.varName.trim() + "=" + variable.varValue);
            }
            return list;
        }

        function load(): void {
            argumentsRow.text = root.inspected.arguments;
            websiteRow.text = root.inspected.website;
            prereleases = root.inspected.prereleases;
            github = hasSource;
            repositoryRow.text = repository;
            variables.clear();
            for (const entry of root.inspected.environment) {
                const equals = entry.indexOf("=");
                variables.append({varName: entry.substring(0, equals), varValue: entry.substring(equals + 1)});
            }
            problem = "";
            edits++;
        }

        function save(): void {
            if (github && repositoryRow.text.trim().length === 0) {
                problem = qsTr("Enter the GitHub repository to look for updates in, or choose None.");
                return;
            }
            problem = root.client.saveOptions(root.inspected.id, argumentsRow.text, websiteRow.text, github ? repositoryRow.text : "",
                prereleases, assignments());
        }

        ListModel {
            id: variables
        }

        onAboutToShow: {
            root.client.clearMessage();
            load();
        }

        // a fixed size: what does not fit scrolls, the buttons stay
        height: Math.min(660, (Overlay.overlay?.height ?? 740) - 80)

        contentItem: Item {
            id: optionsFrame

            Flickable {
                id: optionsScroll
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: optionsButtons.top
                anchors.bottomMargin: 16
                contentHeight: optionsBody.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {}

                WheelScroll {}

                Column {
                    id: optionsBody
                    // room for the scroll bar
                    width: optionsScroll.width - 14
                    spacing: 16


                    Row {
                        width: optionsBody.width
                        spacing: 14

                        AppIcon {
                            id: optionsIcon
                            pkgName: "appimage/" + root.inspected.id
                            size: 56
                        }

                        Column {
                            anchors.verticalCenter: optionsIcon.verticalCenter
                            width: optionsBody.width - optionsIcon.width - 14
                            spacing: 2

                            Heading {
                                width: parent.width
                                text: root.inspected.name
                                elide: Text.ElideRight
                            }

                            Muted {
                                width: parent.width
                                text: [root.inspected.version, root.inspected.size].filter(part => part.length > 0).join(" · ")
                                visible: text.length > 0
                                elide: Text.ElideRight
                            }
                        }
                    }

                    OptionAction {
                        width: optionsBody.width
                        visible: optionsDialog.canChange
                        enabled: !optionsDialog.dirty && !root.client.busy
                        glyph: "M20 12a8 8 0 1 1-2.34-5.66M20 4v4h-4"
                        text: qsTr("Reload metadata")
                        description: qsTr("Update information like icon, version and description. Useful e.g if the app updated itself. The appimage needs to be run to read them.")
                        onClicked: root.client.reloadMetadata(root.inspected.id)
                    }

                    Notice {
                        width: optionsBody.width
                        text: root.inspected.missing ? qsTr("The file is gone. Remove this entry or add the AppImage again.") : ""
                        tone: Notice.Warning
                    }

                    OptionGroup {
                        width: optionsBody.width

                        OptionRow {
                            width: parent.width
                            label: qsTr("Path")
                            value: root.inspected.path
                            buttonGlyph: "M10 5H5a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-5M14 4h6v6M20 4l-9 9"
                            buttonName: qsTr("Show in file manager")
                            onButtonClicked: Qt.openUrlExternally(root.folderOf(root.inspected.path))
                        }

                        OptionRow {
                            id: argumentsRow
                            width: parent.width
                            enabled: optionsDialog.canChange
                            label: qsTr("Command line arguments")
                            editable: true
                            placeholder: "%U"
                            onTextChanged: optionsDialog.edits++
                        }

                        OptionRow {
                            id: websiteRow
                            width: parent.width
                            enabled: optionsDialog.canChange
                            label: qsTr("Website")
                            editable: true
                            placeholder: "https://"
                            buttonGlyph: /^https?:\/\/\S+$/.test(text.trim()) ? "M10 5H5a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-5M14 4h6v6M20 4l-9 9" : ""
                            buttonName: qsTr("Open the website")
                            onButtonClicked: Qt.openUrlExternally(text.trim())
                            onTextChanged: optionsDialog.edits++
                        }
                    }

                    OptionTitle {
                        width: optionsBody.width
                        title: qsTr("Update management")
                        description: qsTr("Where to get updates for this app.")
                    }

                    OptionGroup {
                        width: optionsBody.width
                        enabled: optionsDialog.canChange

                        OptionRow {
                            width: parent.width
                            label: qsTr("Source")
                            choices: [qsTr("None"), qsTr("GitHub")]
                            choice: optionsDialog.github ? 1 : 0
                            onPicked: index => {
                                optionsDialog.github = index === 1;
                                optionsDialog.edits++;
                            }
                        }

                        OptionRow {
                            id: repositoryRow
                            width: parent.width
                            visible: optionsDialog.github
                            label: qsTr("Repository")
                            editable: true
                            placeholder: qsTr("https://github.com/owner/project")
                            onTextChanged: optionsDialog.edits++
                        }

                        OptionSwitch {
                            width: parent.width
                            visible: optionsDialog.github
                            label: qsTr("Allow pre-releases")
                            active: optionsDialog.prereleases
                            onToggled: optionsDialog.prereleases = !optionsDialog.prereleases
                        }

                        OptionRow {
                            width: parent.width
                            visible: optionsDialog.hasSource && root.inspected.update.length > 0
                            label: qsTr("Newer release")
                            value: root.inspected.update
                        }
                    }

                    Item {
                        width: optionsBody.width
                        height: Math.max(variablesTitle.height, addVariable.height)
                        visible: optionsDialog.canChange

                        OptionTitle {
                            id: variablesTitle
                            width: parent.width - addVariable.width - 12
                            title: qsTr("Environment variables")
                            description: qsTr("Add or customize Enviroment variables for this app.")
                        }

                        PillButton {
                            id: addVariable
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            size: "small"
                            tone: "soft"
                            leftPadding: 10
                            rightPadding: 10
                            Accessible.name: qsTr("Add a variable")
                            onClicked: {
                                variables.append({varName: "", varValue: ""});
                                optionsDialog.edits++;
                            }

                            contentItem: Glyph {
                                path: "M12 5v14M5 12h14"
                                size: 16
                            }
                        }
                    }

                    Column {
                        width: optionsBody.width
                        spacing: 8
                        visible: optionsDialog.canChange

                        Repeater {
                            model: variables

                            Row {
                                id: variable

                                required property int index
                                required property string varName
                                required property string varValue

                                width: optionsBody.width
                                spacing: 8

                                OptionField {
                                    width: (variable.width - equals.width - remove.width - 3 * variable.spacing) * 0.4
                                    text: variable.varName
                                    placeholderText: qsTr("NAME")
                                    Accessible.name: qsTr("Variable name")
                                    onTextEdited: {
                                        variables.setProperty(variable.index, "varName", text);
                                        optionsDialog.edits++;
                                    }
                                }

                                Muted {
                                    id: equals
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "="
                                }

                                OptionField {
                                    width: (variable.width - equals.width - remove.width - 3 * variable.spacing) * 0.6
                                    text: variable.varValue
                                    placeholderText: qsTr("value")
                                    Accessible.name: qsTr("Variable value")
                                    onTextEdited: {
                                        variables.setProperty(variable.index, "varValue", text);
                                        optionsDialog.edits++;
                                    }
                                }

                                PillButton {
                                    id: remove
                                    anchors.verticalCenter: parent.verticalCenter
                                    size: "small"
                                    tone: "danger"
                                    leftPadding: 10
                                    rightPadding: 10
                                    Accessible.name: qsTr("Remove the variable")
                                    onClicked: {
                                        variables.remove(variable.index);
                                        optionsDialog.edits++;
                                    }

                                    contentItem: Glyph {
                                        path: "M4 7h16M10 11v6M14 11v6M6 7l1 12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-12M9 7V4h6v3"
                                        color: Look.danger
                                        size: 16
                                    }
                                }
                            }
                        }
                    }

                    Notice {
                        width: optionsBody.width
                        tone: Notice.Danger
                        text: optionsDialog.problem.length > 0 ? optionsDialog.problem : root.client.error
                    }
                }
            }

            Row {
                id: optionsButtons
                anchors.bottom: parent.bottom
                anchors.right: optionsFrame.right
                spacing: 10

                PillButton {
                    tone: "danger"
                    enabled: !root.client.busy
                    text: qsTr("Remove")
                    onClicked: {
                        optionsDialog.close();
                        root.doomed = root.inspected;
                        removeDialog.open();
                    }
                }

                PillButton {
                    visible: !root.inspected.missing && !root.inspected.terminal && root.inspected.owned
                    tone: "soft"
                    enabled: !optionsDialog.dirty
                    text: qsTr("Open")
                    onClicked: root.client.launch(root.inspected.id)
                }

                PillButton {
                    id: optionsClose
                    tone: optionsDialog.dirty ? "soft" : "primary"
                    text: optionsDialog.dirty ? qsTr("Discard") : qsTr("Close")
                    onClicked: optionsDialog.close()
                }

                PillButton {
                    visible: optionsDialog.canChange && optionsDialog.dirty
                    enabled: !root.client.busy
                    text: qsTr("Save")
                    onClicked: optionsDialog.save()
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
