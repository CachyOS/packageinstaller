pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 6

    Component.onCompleted: Backend.scanCache()

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

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: [
                    { mode: "system", label: qsTr("Follow the desktop") },
                    { mode: "light", label: qsTr("Light") },
                    { mode: "dark", label: qsTr("Dark") }
                ]

                PillButton {
                    required property var modelData
                    readonly property bool current: Look.settings.mode === modelData.mode

                    text: modelData.label
                    tone: current ? "primary" : "soft"
                    Accessible.checkable: true
                    Accessible.checked: current
                    onClicked: Look.settings.mode = modelData.mode
                }
            }
        }
    }

    Section {
        title: qsTr("Language")

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: [
                    { code: "system", label: qsTr("Follow the system") },
                    { code: "en", label: "English" },
                    { code: "zh_CN", label: "简体中文" },
                    { code: "zh_TW", label: "繁體中文" }
                ]

                PillButton {
                    required property var modelData
                    readonly property bool current: Localization.language === modelData.code

                    text: modelData.label
                    tone: current ? "primary" : "soft"
                    Accessible.checkable: true
                    Accessible.checked: current
                    onClicked: Localization.language = modelData.code
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

        SourceCard {
            width: sources.width
            title: qsTr("AppImage")
            description: qsTr("Single-file apps that you downloaded yourself. They get a menu entry and an icon. They are not checked or sandboxed, and they are not updated for you.")
            active: Backend.appImagesEnabled
            onToggled: Backend.appImagesEnabled = !Backend.appImagesEnabled
        }
    }

    Section {
        id: mirrors
        width: sources.width
        title: qsTr("Mirrors")

        SettingCard {
            width: mirrors.width
            title: qsTr("Rank the mirrors by speed")
            description: qsTr("Packages are downloaded from the first mirror that answers. Ranking measures the Arch and CachyOS mirrors from your connection and rewrites the mirror lists to prefer the fast ones. Worth doing when downloads are slow or fail.")
            warning: Backend.canRateMirrors ? "" : qsTr("Ranking needs the “cachyos-rate-mirrors” package, which is not installed.")

            PillButton {
                tone: "soft"
                text: Backend.canRateMirrors ? qsTr("Rank mirrors") : qsTr("Install cachyos-rate-mirrors")
                onClicked: Backend.canRateMirrors ? Backend.rateMirrors() : Backend.install("cachyos-rate-mirrors")
            }
        }
    }

    Section {
        id: cache
        width: sources.width
        title: qsTr("Package cache")

        SettingCard {
            width: cache.width
            title: qsTr("Installed packages")
            description: qsTr("Old versions of installed packages are kept in the pacman cache. Keeping fewer versions frees disk space.")
            warning: Backend.canCleanCache ? "" : qsTr("Cleaning needs the “pacman-contrib” package, which is not installed.")

            Row {
                spacing: 10

                Muted {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keep")
                }

                NumberStepper {
                    value: Backend.cacheKeepInstalled
                    onValueModified: value => Backend.cacheKeepInstalled = value
                }

                PillButton {
                    visible: Backend.canCleanCache
                    tone: "soft"
                    enabled: !Backend.cacheScanning && Backend.cacheInstalledCount > 0
                    text: Backend.cacheScanning ? qsTr("Checking…")
                        : Backend.cacheInstalledCount > 0 ? qsTr("Clean %1").arg(Backend.cacheInstalledSize)
                        : qsTr("Nothing to clean")
                    onClicked: Backend.cleanCache(false)
                }

                PillButton {
                    visible: !Backend.canCleanCache
                    tone: "soft"
                    text: qsTr("Install pacman-contrib")
                    onClicked: Backend.install("pacman-contrib")
                }
            }
        }

        SettingCard {
            width: cache.width
            title: qsTr("Uninstalled packages")
            description: qsTr("Cached versions of packages that are no longer installed serve no purpose. They can be removed without keeping a version.")
            warning: Backend.canCleanCache ? "" : qsTr("Cleaning needs the “pacman-contrib” package, which is not installed.")

            Row {
                spacing: 10

                Muted {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keep")
                }

                NumberStepper {
                    value: Backend.cacheKeepUninstalled
                    onValueModified: value => Backend.cacheKeepUninstalled = value
                }

                PillButton {
                    visible: Backend.canCleanCache
                    tone: "soft"
                    enabled: !Backend.cacheScanning && Backend.cacheUninstalledCount > 0
                    text: Backend.cacheScanning ? qsTr("Checking…")
                        : Backend.cacheUninstalledCount > 0 ? qsTr("Clean %1").arg(Backend.cacheUninstalledSize)
                        : qsTr("Nothing to clean")
                    onClicked: Backend.cleanCache(true)
                }

                PillButton {
                    visible: !Backend.canCleanCache
                    tone: "soft"
                    text: qsTr("Install pacman-contrib")
                    onClicked: Backend.install("pacman-contrib")
                }
            }
        }

        Notice {
            width: cache.width
            tone: Notice.Warning
            text: Backend.cacheError
        }
    }
}
