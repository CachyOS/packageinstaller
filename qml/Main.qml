pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ApplicationWindow {
    id: root

    // "home", "browse", "installed", "updates", "aur", "flatpak" or "settings"
    property string section: "home"

    // History for the back and forward buttons. Pages on top of a section live in the
    // StackView (so the list below keeps its scroll position); `trail` remembers how each
    // one was opened, `visited` the sections seen before, and `undone` what Back left.
    property list<var> trail
    property list<string> visited
    property list<var> undone
    readonly property bool canGoBack: stack.depth > 1 || visited.length > 0
    readonly property bool canGoForward: undone.length > 0

    function show(name: string): void {
        if (name !== section) {
            visited.push(section);
            if (visited.length > 50)
                visited.shift();
        }
        undone = [];
        showSection(name);
    }

    function showSection(name: string): void {
        section = name;
        trail = [];
        if (!searchable(name))
            search.text = "";
        stack.replace(null, pageFor(name));
    }

    function pageFor(name: string): Component {
        switch (name) {
        case "browse": return browsePage;
        case "installed": return installedPage;
        case "updates": return updatesPage;
        case "aur": return aurPage;
        case "flatpak": return flatpakPage;
        case "settings": return settingsPage;
        default: return homePage;
        }
    }

    // Sections with a list that the search field filters.
    function searchable(name: string): bool {
        return name === "browse" || name === "installed" || name === "aur" || name === "flatpak";
    }

    function badge(name: string): int {
        switch (name) {
        case "updates": return Backend.updateCount;
        case "aur": return Backend.aur.updateCount;
        case "flatpak": return Backend.flatpak.updateCount;
        default: return 0;
        }
    }

    function openPage(component: Component, properties: var): void {
        undone = [];
        pushPage(component, properties);
    }

    function pushPage(component: Component, properties: var): void {
        trail.push({"component": component, "properties": properties});
        stack.push(component, properties);
    }

    function openPackage(name: string): void {
        openPage(detailsPage, {"pkgName": name});
    }

    function back(): void {
        if (stack.depth > 1) {
            const page = trail.pop();
            if (page !== undefined)
                undone.push(page);
            stack.pop();
        } else if (visited.length > 0) {
            undone.push({"section": section});
            showSection(visited.pop());
        }
    }

    function forward(): void {
        const step = undone.pop();
        if (step === undefined)
            return;
        if (step.section !== undefined) {
            visited.push(section);
            showSection(step.section);
        } else {
            pushPage(step.component, step.properties);
        }
    }

    // The page that scrolls up and down, for the keyboard: the page itself or the list in it.
    function pageScroller(): Flickable {
        const page = stack.currentItem;
        if (page === null || page as Flickable !== null)
            return page as Flickable;
        for (let i = 0; i < page.children.length; ++i) {
            const child = page.children[i] as Flickable;
            if (child !== null && child.visible && child.flickableDirection !== Flickable.HorizontalFlick)
                return child;
        }
        return null;
    }

    function scrollPage(pages: int): void {
        const view = pageScroller();
        if (view === null || view.contentHeight <= view.height)
            return;
        const top = view.originY;
        view.contentY = Math.max(top, Math.min(view.contentY + pages * (view.height - 60), top + view.contentHeight - view.height));
    }

    // Moving through the controls with Tab must not leave the focus outside the visible
    // part of a page. A click does not scroll: what was clicked is on screen already.
    function reveal(item: Item): void {
        const control = item as Control;
        const field = item as TextField;
        const reason = control !== null ? control.focusReason : field !== null ? field.focusReason : Qt.OtherFocusReason;
        if (reason !== Qt.TabFocusReason && reason !== Qt.BacktabFocusReason)
            return;
        for (let ancestor = item.parent; ancestor !== null; ancestor = ancestor.parent) {
            const view = ancestor as Flickable;
            if (view === null || view.contentHeight <= view.height)
                continue;
            const top = item.mapToItem(view.contentItem, 0, 0).y;
            const bottom = top + item.height;
            if (top - 16 < view.contentY)
                view.contentY = Math.max(view.originY, top - 16);
            else if (bottom + 16 > view.contentY + view.height)
                view.contentY = Math.min(view.originY + view.contentHeight - view.height, bottom + 16 - view.height);
            return;
        }
    }

    width: 1220
    height: 800
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("CachyOS Package Installer")
    color: Look.window

    onActiveFocusItemChanged: if (activeFocusItem !== null) reveal(activeFocusItem)

    // Closing the window must not cut a running operation short.
    onClosing: close => {
        if (Backend.transaction.state === Transaction.Running) {
            close.accepted = false;
            quitDialog.open();
        }
    }

    Shortcut {
        sequences: [StandardKey.MoveToNextPage]
        onActivated: root.scrollPage(1)
    }
    Shortcut {
        sequences: [StandardKey.MoveToPreviousPage]
        onActivated: root.scrollPage(-1)
    }
    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+K"]
        onActivated: search.forceActiveFocus()
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: root.canGoBack
        onActivated: root.back()
    }
    Shortcut {
        sequences: [StandardKey.Forward]
        enabled: root.canGoForward
        onActivated: root.forward()
    }
    Shortcut {
        sequences: ["Escape"]
        enabled: stack.depth > 1
        onActivated: root.back()
    }

    Rectangle {
        id: sidebar
        width: 224
        height: root.height
        color: Look.sidebar

        Row {
            id: brand
            x: 18
            y: 20
            spacing: 10

            Image {
                width: 34
                height: 34
                source: "icons/cachyos-pi.png"
                sourceSize: Qt.size(68, 68)
            }

            Column {
                anchors.verticalCenter: brand.verticalCenter

                PlainText {
                    text: qsTr("CachyOS")
                    font.pixelSize: Look.fontTitle
                    font.weight: Font.Bold
                }

                Muted {
                    text: qsTr("Package Installer")
                    font.pixelSize: Look.fontSmall
                }
            }
        }

        Column {
            id: nav
            anchors.top: brand.bottom
            anchors.topMargin: 26
            x: 12
            width: sidebar.width - 24
            spacing: 4

            Repeater {
                model: ListModel {
                    ListElement { name: "home"; label: qsTr("Discover"); glyph: "M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z" }
                    ListElement { name: "browse"; label: qsTr("Browse"); glyph: "M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z" }
                    ListElement { name: "installed"; label: qsTr("Installed"); glyph: "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM8 12l3 3l5-6" }
                    ListElement { name: "updates"; label: qsTr("Updates"); glyph: "M12 4v11M7 11l5 5l5-5M5 20h14" }
                    ListElement { name: "aur"; label: qsTr("AUR"); glyph: "M12 3 20 7.5v9L12 21l-8-4.5v-9zM4 7.5 12 12l8-4.5M12 12v9" }
                    ListElement { name: "flatpak"; label: qsTr("Flatpak"); glyph: "M4 8l8-4l8 4v8l-8 4l-8-4zM4 8l8 4l8-4M12 12v8M8 6l8 4" }
                }

                NavButton {
                    required property string name
                    required property string label
                    required glyph

                    width: nav.width
                    text: label
                    current: root.section === name
                    badge: root.badge(name)
                    onClicked: root.show(name)
                }
            }
        }

        NavButton {
            x: 12
            width: sidebar.width - 24
            anchors.bottom: versionText.top
            anchors.bottomMargin: 10
            text: qsTr("Settings")
            glyph: "M4 7h9M17 7h3M4 17h3M11 17h9M15 4.5v5M9 14.5v5"
            current: root.section === "settings"
            onClicked: root.show("settings")
        }

        Muted {
            id: versionText
            x: 18
            anchors.bottom: sidebar.bottom
            anchors.bottomMargin: 16
            text: qsTr("Version %1").arg(Qt.application.version)
            font.pixelSize: Look.fontSmall
        }
    }

    Item {
        id: topBar
        x: sidebar.width
        width: root.width - sidebar.width
        height: 68

        SearchField {
            id: search
            x: Look.pagePadding
            anchors.verticalCenter: topBar.verticalCenter
            width: Math.min(topBar.width - 2 * Look.pagePadding, 520)
            placeholderText: root.section === "aur" ? qsTr("Search the AUR")
                : root.section === "flatpak" ? qsTr("Search Flatpak apps") : qsTr("Search apps and packages")
            Accessible.name: placeholderText

            // the searchable pages bind their query to the text
            onTextEdited: {
                root.undone = [];
                if (stack.depth > 1) {
                    root.trail = [];
                    stack.pop(null);
                }
                if (!root.searchable(root.section))
                    root.show("browse");
            }
        }

        BusyIndicator {
            anchors.right: topBar.right
            anchors.rightMargin: Look.pagePadding
            anchors.verticalCenter: topBar.verticalCenter
            width: 26
            height: 26
            visible: Backend.loading
            running: visible
        }
    }

    StackView {
        id: stack
        x: sidebar.width
        y: topBar.height
        width: root.width - sidebar.width
        height: root.height - topBar.height
        initialItem: homePage
        clip: true

        readonly property Transition fadeIn: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        }
        readonly property Transition fadeOut: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }

        pushEnter: fadeIn
        pushExit: fadeOut
        popEnter: fadeIn
        popExit: fadeOut
        replaceEnter: fadeIn
        replaceExit: fadeOut
    }

    Component {
        id: homePage
        HomePage {
            onOpenPackage: name => root.openPackage(name)
            onBrowse: root.show("browse")
            onShowUpdates: root.show("updates")
        }
    }
    Component {
        id: browsePage
        BrowsePage {
            query: search.text
            onOpenPackage: name => root.openPackage(name)
        }
    }
    Component {
        id: installedPage
        BrowsePage {
            installedOnly: true
            query: search.text
            onOpenPackage: name => root.openPackage(name)
        }
    }
    Component {
        id: updatesPage
        UpdatesPage {
            onOpenPackage: name => root.openPackage(name)
        }
    }
    Component {
        id: aurPage
        AurPage {
            query: search.text
            onOpenSettings: root.show("settings")
            onOpenAur: item => root.openPage(aurDetailsPage, {"item": item})
            onReviewAll: items => root.openPage(aurDetailsPage, {"item": items[0], "queue": items, "updates": true})
        }
    }
    Component {
        id: flatpakPage
        FlatpakPage {
            query: search.text
            onOpenSettings: root.show("settings")
            onOpenApp: (appId, copy) => root.openPage(flatpakDetailsPage, {"appId": appId, "copy": copy})
        }
    }
    Component {
        id: flatpakDetailsPage
        FlatpakDetailsPage {
            onBack: root.back()
        }
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: detailsPage
        DetailsPage {
            onBack: root.back()
            onOpenPackage: name => root.openPackage(name)
            onShowUpdates: root.show("updates")
        }
    }
    Component {
        id: aurDetailsPage
        AurDetailsPage {
            onBack: root.back()
            onOpenPackage: name => root.openPackage(name)
        }
    }

    // The back and forward buttons of a mouse, anywhere in the window. Other buttons pass
    // through, and a modal dialog covers this like everything else.
    MouseArea {
        width: root.width
        height: root.height
        z: 100
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onPressed: mouse => {
            if (mouse.button === Qt.BackButton)
                root.back();
            else
                root.forward();
        }
    }

    TransactionDialog {}

    Dialog {
        id: quitDialog

        readonly property bool running: Backend.transaction.state === Transaction.Running

        onRunningChanged: if (!running) close()
        width: Math.min(Overlay.overlay.width - 80, 480)

        contentItem: Column {
            id: quitBody
            spacing: 16

            Heading {
                width: quitBody.width
                text: qsTr("A transaction is running")
                wrapMode: Text.WordWrap
            }

            Muted {
                width: quitBody.width
                text: qsTr("Interrupting it could leave the system in a broken state. The window can be closed once it has finished.")
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            PillButton {
                anchors.right: quitBody.right
                tone: "primary"
                text: qsTr("Keep running")
                onClicked: quitDialog.close()
            }
        }
    }

    Popup {
        id: toast
        x: (root.width - width) / 2
        y: root.height - height - 24
        width: Math.min(root.width - 80, 560)
        padding: 0
        background: null
        contentItem: Notice {
            id: toastText
            tone: Notice.Danger
        }

        Timer {
            id: toastTimer
            interval: 8000
            onTriggered: toast.close()
        }
    }

    Connections {
        target: Backend
        function onNotice(text: string): void {
            toastText.text = text;
            toast.open();
            toastTimer.restart();
        }
    }
}
