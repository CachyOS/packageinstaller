pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    signal openPackage(string name)
    signal browse
    signal showUpdates

    // These names come from pkglist.yaml; static qsTr calls let lupdate
    // extract them. Unknown future categories keep their original name.
    function localizedSectionTitle(name) {
        switch (name) {
        case "Audio": return qsTr("Audio")
        case "Browsers": return qsTr("Browsers")
        case "Communication": return qsTr("Communication")
        case "Development": return qsTr("Development")
        case "Games": return qsTr("Games")
        case "Graphics": return qsTr("Graphics")
        case "Hardware Tools": return qsTr("Hardware Tools")
        case "Internet": return qsTr("Internet")
        case "Mail": return qsTr("Mail")
        case "Multimedia": return qsTr("Multimedia")
        case "Office": return qsTr("Office")
        case "Security": return qsTr("Security")
        case "System": return qsTr("System")
        case "Video": return qsTr("Video")
        case "Virtualization": return qsTr("Virtualization")
        default: return name
        }
    }

    spacing: 26

    Rectangle {
        id: hero
        width: parent.width
        height: 190
        radius: Look.radiusLarge
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Look.heroStart }
            GradientStop { position: 0.55; color: Look.heroMiddle }
            GradientStop { position: 1; color: Look.heroEnd }
        }

        Image {
            anchors.right: hero.right
            anchors.rightMargin: 36
            anchors.verticalCenter: hero.verticalCenter
            width: 132
            height: 132
            source: Look.heroImage
            sourceSize: Qt.size(264, 264)
            opacity: 0.92
            visible: hero.width > 620
        }

        Column {
            id: heroText
            anchors.left: hero.left
            anchors.leftMargin: 32
            anchors.verticalCenter: hero.verticalCenter
            width: Math.min(hero.width - 64, 560)
            spacing: 8

            PlainText {
                width: heroText.width
                text: qsTr("Software for CachyOS")
                color: Look.heroText
                font.pixelSize: Look.fontHero
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }

            PlainText {
                width: heroText.width
                text: qsTr("Optimised packages from the CachyOS and Arch repositories, plus the AUR when you want it.")
                color: Look.heroSub
                font.pixelSize: Look.fontLead
                wrapMode: Text.WordWrap
            }

            Row {
                topPadding: 8

                PillButton {
                    tone: "hero"
                    text: qsTr("Browse apps")
                    onClicked: root.browse()
                }
            }
        }
    }

    Rectangle {
        id: updatesStrip
        visible: Backend.updateCount > 0
        width: parent.width
        height: 60
        radius: Look.radius
        color: Look.accentSoft

        UpdatesBanner {
            x: 20
            width: updatesStrip.width - 34
            anchors.verticalCenter: updatesStrip.verticalCenter
            count: Backend.updateCount
            action: qsTr("Review updates")
            onTriggered: root.showUpdates()
        }
    }

    BusyIndicator {
        x: (parent.width - width) / 2
        visible: Backend.loading && Backend.curatedCount === 0
        running: visible
    }

    Notice {
        width: parent.width
        tone: Notice.Danger
        text: Backend.error
    }

    // CachyOS picks, one shelf per section of pkglist.yaml
    Repeater {
        model: Backend.curated

        Column {
            id: shelf

            required property curatedSection modelData

            // As many cards as fit next to each other; two rows until "Show all" is pressed.
            readonly property int columns: Math.max(1, Math.floor((width + 12) / 292))
            readonly property real cardWidth: (width - (columns - 1) * 12) / columns
            readonly property int total: modelData.names.length
            readonly property int limit: 2 * columns
            property bool expanded: false

            width: parent.width
            spacing: 12

            Item {
                id: shelfHeader
                width: shelf.width
                height: Math.max(shelfTitle.implicitHeight, showAll.implicitHeight)

                PlainText {
                    id: shelfTitle
                    anchors.verticalCenter: shelfHeader.verticalCenter
                    text: root.localizedSectionTitle(shelf.modelData.title)
                    font.pixelSize: Look.fontSubheading
                    font.weight: Font.Bold
                }

                PillButton {
                    id: showAll
                    visible: shelf.total > shelf.limit
                    anchors.right: shelfHeader.right
                    anchors.verticalCenter: shelfHeader.verticalCenter
                    tone: "soft"
                    implicitHeight: 28
                    font.pixelSize: Look.fontLabel
                    text: shelf.expanded ? qsTr("Show less") : qsTr("Show all %1").arg(shelf.total)
                    Accessible.description: root.localizedSectionTitle(shelf.modelData.title)
                    onClicked: shelf.expanded = !shelf.expanded
                }
            }

            // a new column count adds or removes cards at the end instead of rebuilding the shelf
            PackageFilter {
                id: cards
                sourceModel: Backend.packages
                names: shelf.modelData.names
                limit: shelf.expanded ? -1 : shelf.limit
            }

            Flow {
                width: shelf.width
                spacing: 12

                Repeater {
                    model: cards

                    AppCard {
                        id: card

                        required property string name
                        required title
                        required summary
                        required source
                        required installed
                        required upgradable

                        width: shelf.cardWidth
                        height: 104
                        pkgName: card.name
                        onClicked: root.openPackage(card.name)
                    }
                }
            }
        }
    }
}
