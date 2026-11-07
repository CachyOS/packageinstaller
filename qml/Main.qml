pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ApplicationWindow {
    id: root

    // "home", "browse", "installed", "updates" or "aur"
    property string section: "home"

    function show(name) {
        section = name;
        if (name !== "browse" && name !== "installed" && name !== "aur")
            search.text = "";
        const page = name === "home" ? homePage : name === "updates" ? updatesPage : name === "aur" ? aurPage : browsePage;
        stack.replace(null, page, name === "browse" || name === "installed" ? {"installedOnly": name === "installed", "query": search.text}
            : name === "aur" ? {"query": search.text} : {});
    }

    function openPackage(name) {
        stack.push(detailsPage, {"pkgName": name});
    }

    width: 1220
    height: 800
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("CachyOS Package Installer")
    color: Look.window

    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+K"]
        onActivated: search.forceActiveFocus()
    }
    Shortcut {
        sequences: [StandardKey.Back, "Escape"]
        enabled: stack.depth > 1
        onActivated: stack.pop()
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
                    text: qsTr("CachyOS")
                    color: Look.text
                    font.pixelSize: Look.fontTitle
                    font.weight: Font.Bold
                }

                Text {
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
                onClicked: root.show("aur")
            }
        }

        Text {
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
                if (stack.depth > 1)
                    stack.pop(null);
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
            onOpenAur: item => stack.push(aurDetailsPage, {"item": item})
        }
    }
    Component {
        id: detailsPage
        DetailsPage {
            onBack: stack.pop()
            onOpenPackage: name => root.openPackage(name)
            onShowUpdates: root.show("updates")
        }
    }
    Component {
        id: aurDetailsPage
        AurDetailsPage {
            onBack: stack.pop()
            onOpenPackage: name => root.openPackage(name)
        }
    }

    TransactionDialog {}
}
