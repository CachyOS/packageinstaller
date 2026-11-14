pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 6

    /// Scrolls to the switches for the AUR and Flatpak.
    function revealSources(): void {
        contentY = Math.max(0, Math.min(sources.y + inset - 16, contentHeight - height));
    }

    component Section: Column {
        property alias title: heading.text

        width: root.width - 2 * Look.pagePadding
        spacing: 10

        SectionTitle {
            id: heading
        }
    }

    Heading {
        text: qsTr("Settings")
    }

    Section {
        title: qsTr("Appearance")

        Row {
            spacing: 8

            Repeater {
                model: ListModel {
                    ListElement { mode: "system"; label: qsTr("Follow the desktop") }
                    ListElement { mode: "light"; label: qsTr("Light") }
                    ListElement { mode: "dark"; label: qsTr("Dark") }
                }

                PillButton {
                    required property string mode
                    required property string label
                    readonly property bool current: Look.settings.mode === mode

                    text: label
                    tone: current ? "primary" : "soft"
                    Accessible.checkable: true
                    Accessible.checked: current
                    onClicked: Look.settings.mode = mode
                }
            }
        }
    }

    Section {
        id: colours
        title: qsTr("Colours")

        Flow {
            id: themeFlow
            width: colours.width
            spacing: 14

            Repeater {
                model: Look.themeNames

                AbstractButton {
                    id: card

                    required property string modelData
                    readonly property var entry: Look.themes[modelData]
                    readonly property bool current: Look.settings.theme === modelData

                    width: Math.max(220, Math.min(300, (themeFlow.width - 14) / 2))
                    height: 196
                    text: entry.title
                    Accessible.description: entry.hint
                    Accessible.checkable: true
                    Accessible.checked: current
                    onClicked: Look.settings.theme = modelData

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Card {
                        hovered: card.hovered
                        focused: card.current || card.visualFocus
                    }

                    contentItem: Item {
                        id: cardContent

                        ThemePreview {
                            id: preview
                            x: 12
                            y: 12
                            width: cardContent.width - 24
                            entry: card.entry
                        }

                        PlainText {
                            x: 14
                            anchors.top: preview.bottom
                            anchors.topMargin: 10
                            text: card.text
                            font.pixelSize: Look.fontLead
                            font.weight: Font.DemiBold
                        }

                        Muted {
                            x: 14
                            width: cardContent.width - 28
                            anchors.bottom: cardContent.bottom
                            anchors.bottomMargin: 12
                            text: card.entry.hint
                            font.pixelSize: Look.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Section {
        id: sources
        width: Math.min(root.width - 2 * Look.pagePadding, 914)
        title: qsTr("Sources")

        SourceCard {
            width: sources.width
            title: qsTr("Arch User Repository (AUR)")
            description: qsTr("Build recipes written by the community. They are not reviewed by CachyOS, are built on your machine and can run any code while doing so, so read the PKGBUILD before installing.")
            warning: Backend.paruAvailable ? "" : qsTr("Building AUR packages needs “paru”, which is not installed. You can still search and review packages.")
            active: Backend.aurEnabled
            onToggled: Backend.aurEnabled = !Backend.aurEnabled
        }

        SourceCard {
            width: sources.width
            title: qsTr("Flatpak")
            description: qsTr("Sandboxed apps from Flathub and other Flatpak remotes. They are packaged by their developers or the Flathub community, not by CachyOS, and bring their own libraries.")
            warning: Backend.flatpakEnabled && !Backend.flatpak.loading && !Backend.flatpak.available
                ? qsTr("Flatpak is not installed. You can install it from the Flatpak page.") : ""
            active: Backend.flatpakEnabled
            onToggled: Backend.flatpakEnabled = !Backend.flatpakEnabled
        }
    }

    Section {
        id: mirrors
        visible: Backend.canRateMirrors
        width: sources.width
        title: qsTr("Mirrors")

        SettingCard {
            width: mirrors.width
            title: qsTr("Rank the mirrors by speed")
            description: qsTr("Packages are downloaded from the first mirror that answers. Ranking measures the Arch and CachyOS mirrors from your connection and rewrites the mirror lists to prefer the fast ones. Worth doing when downloads are slow or fail.")

            PillButton {
                tone: "soft"
                text: qsTr("Rank mirrors")
                onClicked: Backend.rateMirrors()
            }
        }
    }
}
