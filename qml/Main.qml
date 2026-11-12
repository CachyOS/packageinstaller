pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ApplicationWindow {
    id: root

    // "home", "browse", "installed", "updates", "aur" or "settings"
    property string section: "home"

    // History for the back and forward buttons. Pages on top of a section live in the
    // StackView (so the list below keeps its scroll position); `trail` remembers how each
    // one was opened, `visited` the sections seen before, and `undone` what Back left.
    property var trail: []
    property var visited: []
    property var undone: []
    readonly property bool canGoBack: stack.depth > 1 || historyDepth > 0
    property int historyDepth: 0
    property int undoneDepth: 0

    function show(name) {
        if (name !== section) {
            visited.push(section);
            if (visited.length > 50)
                visited.shift();
        }
        undone = [];
        showSection(name);
    }

    function showSection(name) {
        section = name;
        trail = [];
        historyDepth = visited.length;
        undoneDepth = undone.length;
        if (name !== "browse" && name !== "installed" && name !== "aur")
            search.text = "";
        const page = name === "home" ? homePage : name === "updates" ? updatesPage : name === "aur" ? aurPage
            : name === "settings" ? settingsPage : browsePage;
        stack.replace(null, page, name === "browse" || name === "installed" ? {"installedOnly": name === "installed", "query": search.text}
            : name === "aur" ? {"query": search.text} : {});
    }

    function openPage(component, properties) {
        undone = [];
        pushPage(component, properties);
    }

    function pushPage(component, properties) {
        trail.push({"component": component, "properties": properties});
        undoneDepth = undone.length;
        stack.push(component, properties);
    }

    function openPackage(name) {
        openPage(detailsPage, {"pkgName": name});
    }

    function back() {
        if (stack.depth > 1) {
            const page = trail.pop();
            if (page !== undefined)
                undone.push({"page": page});
            undoneDepth = undone.length;
            stack.pop();
        } else if (visited.length > 0) {
            undone.push({"section": section});
            showSection(visited.pop());
        }
    }

    function forward() {
        const step = undone.pop();
        if (step === undefined)
            return;
        if (step.page !== undefined) {
            pushPage(step.page.component, step.page.properties);
        } else {
            visited.push(section);
            showSection(step.section);
        }
    }

    width: 1220
    height: 800
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("CachyOS Package Installer")
    color: Look.window

    // The page that scrolls up and down, for the keyboard: the page itself or the list in it.
    function pageScroller() {
        const page = stack.currentItem;
        if (page instanceof Flickable)
            return page;
        for (const child of page?.children ?? []) {
            if (child instanceof Flickable && child.visible && child.flickableDirection !== Flickable.HorizontalFlick)
                return child;
        }
        return null;
    }

    function scrollPage(pages) {
        const view = pageScroller();
        if (view === null || view.contentHeight <= view.height)
            return;
        const top = view.originY;
        view.contentY = Math.max(top, Math.min(view.contentY + pages * (view.height - 60), top + view.contentHeight - view.height));
    }

    // Moving through the controls with Tab must not leave the focus outside the visible
    // part of a page. A click does not scroll: what was clicked is on screen already.
    onActiveFocusItemChanged: {
        const item = activeFocusItem;
        if (!item || (item.focusReason !== Qt.TabFocusReason && item.focusReason !== Qt.BacktabFocusReason))
            return;
        for (let parent = item.parent; parent; parent = parent.parent) {
            if (!(parent instanceof Flickable) || parent.contentHeight <= parent.height)
                continue;
            const top = item.mapToItem(parent.contentItem, 0, 0).y;
            const bottom = top + item.height;
            const last = parent.originY + parent.contentHeight - parent.height;
            if (top - 16 < parent.contentY)
                parent.contentY = Math.max(parent.originY, top - 16);
            else if (bottom + 16 > parent.contentY + parent.height)
                parent.contentY = Math.min(last, bottom + 16 - parent.height);
            break;
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
        enabled: root.undoneDepth > 0
        onActivated: root.forward()
    }
    Shortcut {
        sequences: ["Escape"]
        enabled: stack.depth > 1
        onActivated: root.back()
    }

    // Sidebar
    Rectangle {
        id: sidebar
        width: 224
        height: parent.height
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
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("CachyOS")
                    color: Look.text
                    font.pixelSize: Look.fontTitle
                    font.weight: Font.Bold
                }

                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Package Installer")
                    color: Look.textMuted
                    font.pixelSize: Look.fontSmall
                }
            }
        }

        Column {
            anchors.top: brand.bottom
            anchors.topMargin: 26
            x: 12
            width: parent.width - 24
            spacing: 4

            NavButton {
                width: parent.width
                text: qsTr("Discover")
                glyph: "M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"
                current: root.section === "home"
                onClicked: root.show("home")
            }
            NavButton {
                width: parent.width
                text: qsTr("Browse")
                glyph: "M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z"
                current: root.section === "browse"
                onClicked: root.show("browse")
            }
            NavButton {
                width: parent.width
                text: qsTr("Installed")
                glyph: "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM8 12l3 3l5-6"
                current: root.section === "installed"
                onClicked: root.show("installed")
            }
            NavButton {
                width: parent.width
                text: qsTr("Updates")
                glyph: "M12 4v11M7 11l5 5l5-5M5 20h14"
                current: root.section === "updates"
                badge: Backend.updateCount
                onClicked: root.show("updates")
            }
            NavButton {
                width: parent.width
                text: qsTr("AUR")
                glyph: "M12 3 20 7.5v9L12 21l-8-4.5v-9zM4 7.5 12 12l8-4.5M12 12v9"
                current: root.section === "aur"
                badge: Backend.aur.updates.length
                onClicked: root.show("aur")
            }
        }

        NavButton {
            x: 12
            width: parent.width - 24
            anchors.bottom: versionText.top
            anchors.bottomMargin: 10
            text: qsTr("Settings")
            glyph: "M4 7h9M17 7h3M4 17h3M11 17h9M15 4.5v5M9 14.5v5"
            current: root.section === "settings"
            onClicked: root.show("settings")
        }

        Text {
            id: versionText
            textFormat: Text.PlainText
            x: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            text: qsTr("Version %1").arg(Backend.version)
            color: Look.textMuted
            font.pixelSize: Look.fontSmall
        }
    }

    // Top bar
    Item {
        id: topBar
        anchors.left: sidebar.right
        anchors.right: parent.right
        height: 68

        SearchField {
            id: search
            anchors.left: parent.left
            anchors.leftMargin: Look.pagePadding
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width - 2 * Look.pagePadding, 520)
            placeholderText: root.section === "aur" ? qsTr("Search the AUR") : qsTr("Search apps and packages")
            Accessible.name: placeholderText

            onTextEdited: {
                root.undone = [];
                root.undoneDepth = 0;
                if (stack.depth > 1) {
                    root.trail = [];
                    stack.pop(null);
                }
                if (root.section !== "browse" && root.section !== "installed" && root.section !== "aur")
                    root.show("browse");
                else
                    stack.currentItem.query = text;
            }
        }

        BusyIndicator {
            anchors.right: parent.right
            anchors.rightMargin: Look.pagePadding
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            height: 26
            visible: Backend.loading
            running: visible
        }
    }

    StackView {
        id: stack
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        initialItem: homePage
        clip: true

        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }
        popEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }
        replaceEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        }
        replaceExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }
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
            onOpenSettings: root.show("settings")
            onOpenAur: item => root.openPage(aurDetailsPage, {"item": item})
            onReviewAll: items => root.openPage(aurDetailsPage, {"item": items[0], "queue": items, "updates": true})
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
        anchors.fill: parent
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
}
