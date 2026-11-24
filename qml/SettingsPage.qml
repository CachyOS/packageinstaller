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

        SettingCard {
            id: transparency
            width: Math.min(parent.width, 914)
            title: qsTr("Transparent background")
            description: qsTr("Lets the desktop show through the window background. Text, icons and dialogs stay solid. Whether the background is blurred is up to your compositor.")
            warning: Look.needsRestart ? qsTr("Restart the app to turn transparency on.") : ""

            Row {
                spacing: 8

                Repeater {
                    model: ListModel {
                        ListElement { enable: false; label: qsTr("Off") }
                        ListElement { enable: true; label: qsTr("On") }
                    }

                    PillButton {
                        required property bool enable
                        required property string label
                        readonly property bool current: Look.settings.translucent === enable

                        text: label
                        tone: current ? "primary" : "soft"
                        Accessible.role: Accessible.RadioButton
                        Accessible.checkable: true
                        Accessible.checked: current
                        onClicked: Look.settings.translucent = enable
                    }
                }
            }
        }

        SettingCard {
            width: transparency.width
            enabled: Look.settings.translucent
            opacity: enabled ? 1 : 0.5
            title: qsTr("Background opacity")
            description: qsTr("Lower values show more of the desktop. Drag the slider or type a percentage.")

            Row {
                spacing: 12

                Slider {
                    id: opacitySlider
                    anchors.verticalCenter: parent.verticalCenter
                    width: 220
                    from: Look.minimumOpacity
                    to: 1
                    stepSize: 0.01
                    value: Look.chosenOpacity
                    Accessible.name: qsTr("Background opacity")
                    onMoved: Look.settings.backgroundOpacity = value

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        x: opacitySlider.leftPadding
                        y: opacitySlider.topPadding + (opacitySlider.availableHeight - height) / 2
                        width: opacitySlider.availableWidth
                        height: 6
                        radius: 3
                        color: Look.surfaceAlt
                        border.width: 1
                        border.color: Look.border

                        Rectangle {
                            width: opacitySlider.visualPosition * parent.width
                            height: parent.height
                            radius: 3
                            color: Look.accent
                        }
                    }

                    handle: Rectangle {
                        x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width)
                        y: opacitySlider.topPadding + (opacitySlider.availableHeight - height) / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: opacitySlider.pressed ? Look.accentHover : Look.accent
                        border.width: opacitySlider.visualFocus ? 2 : 0
                        border.color: Look.text
                    }
                }

                TextField {
                    id: percentField

                    function show(): void {
                        text = Math.round(Look.chosenOpacity * 100);
                    }

                    // Takes the typed percentage; anything outside the range is clamped.
                    function apply(): void {
                        const percent = parseInt(text, 10);
                        if (!isNaN(percent))
                            Look.settings.backgroundOpacity = Math.max(Look.minimumOpacity, Math.min(1, percent / 100));
                        show();
                    }

                    anchors.verticalCenter: parent.verticalCenter
                    width: 64
                    implicitHeight: 34
                    rightPadding: 24
                    horizontalAlignment: TextInput.AlignRight
                    font.pixelSize: Look.fontBody
                    color: Look.text
                    selectionColor: Look.accent
                    selectedTextColor: Look.onAccent
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    // up to three digits; the range is enforced in apply()
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,3}/ }
                    Accessible.name: qsTr("Background opacity in percent")
                    Component.onCompleted: show()
                    onEditingFinished: apply()
                    Keys.onUpPressed: { text = (parseInt(text, 10) || 0) + 1; apply(); }
                    Keys.onDownPressed: { text = (parseInt(text, 10) || 0) - 1; apply(); }

                    Connections {
                        target: Look
                        function onChosenOpacityChanged(): void {
                            if (!percentField.activeFocus)
                                percentField.show();
                        }
                    }

                    background: Rectangle {
                        radius: Look.radiusSmall
                        color: Look.surface
                        border.width: percentField.activeFocus ? 2 : 1
                        border.color: percentField.activeFocus ? Look.accent : Look.border

                        Muted {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "%"
                            font.pixelSize: Look.fontBody
                        }
                    }
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
}
