pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Flickable {
    id: root

    function themeTitle(name) {
        // community palettes keep their own name
        return Look.themes[name].title ?? (name === "midnight" ? qsTr("Midnight") : name === "graphite" ? qsTr("Graphite")
            : name === "aurora" ? qsTr("Aurora") : qsTr("Classic"))
    }

    function themeHint(name) {
        const author = Look.themes[name].author
        return author !== undefined ? qsTr("Community palette by %1").arg(author)
            : name === "midnight" ? qsTr("Blue-black with the colours of the CachyOS logo")
            : name === "graphite" ? qsTr("Neutral greys and one bright mint")
            : name === "aurora" ? qsTr("Indigo tint with a cyan accent")
            : qsTr("The original teal look")
    }

    contentWidth: width
    contentHeight: content.height + 2 * Look.pagePadding
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}

    Column {
        id: content
        x: Look.pagePadding
        y: Look.pagePadding - 6
        width: root.width - 2 * Look.pagePadding
        spacing: 22

        Text {
            textFormat: Text.PlainText
            text: qsTr("Settings")
            color: Look.text
            font.pixelSize: Look.fontHeading
            font.weight: Font.Bold
        }

        // Light or dark
        Column {
            width: parent.width
            spacing: 10

            Text {
                textFormat: Text.PlainText
                text: qsTr("Appearance")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Row {
                spacing: 8

                Repeater {
                    model: [
                        {"mode": "system", "label": qsTr("Follow the desktop")},
                        {"mode": "light", "label": qsTr("Light")},
                        {"mode": "dark", "label": qsTr("Dark")}
                    ]

                    PillButton {
                        required property var modelData

                        text: modelData.label
                        tone: Backend.colorMode === modelData.mode ? "primary" : "soft"
                        Accessible.checkable: true
                        Accessible.checked: Backend.colorMode === modelData.mode
                        onClicked: Backend.colorMode = modelData.mode
                    }
                }
            }
        }

        // Colour theme
        Column {
            width: parent.width
            spacing: 10

            Text {
                textFormat: Text.PlainText
                text: qsTr("Colours")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Flow {
                id: themeFlow
                width: parent.width
                spacing: 14

                Repeater {
                    model: Look.themeNames

                    AbstractButton {
                        id: card

                        required property string modelData
                        readonly property var colors: Look.dark ? Look.themes[modelData].dark : Look.themes[modelData].light
                        readonly property var hero: Look.themes[modelData].hero
                        readonly property bool current: Look.theme === Look.themes[modelData]

                        width: Math.max(220, Math.min(300, (themeFlow.width - 14) / 2))
                        height: 196
                        text: root.themeTitle(modelData)
                        Accessible.name: text
                        Accessible.description: root.themeHint(modelData)
                        Accessible.checkable: true
                        Accessible.checked: current
                        onClicked: Backend.theme = modelData

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            radius: Look.radius
                            color: card.hovered ? Look.surfaceHover : Look.surface
                            border.width: card.current || card.visualFocus ? 2 : 1
                            border.color: card.current || card.visualFocus ? Look.accent : Look.border
                        }

                        contentItem: Item {
                            // a small picture of the window in that theme
                            Rectangle {
                                id: preview
                                x: 12
                                y: 12
                                width: parent.width - 24
                                height: 110
                                radius: Look.radiusSmall
                                color: card.colors.window
                                border.width: 1
                                border.color: card.colors.border
                                clip: true

                                Rectangle {
                                    x: 1
                                    y: 1
                                    width: 46
                                    height: parent.height - 2
                                    color: card.colors.sidebar

                                    Rectangle {
                                        x: 7
                                        y: 12
                                        width: 32
                                        height: 9
                                        radius: 4
                                        color: card.colors.accentSoft
                                    }

                                    Repeater {
                                        model: 3

                                        Rectangle {
                                            required property int index

                                            x: 7
                                            y: 28 + index * 14
                                            width: 26
                                            height: 6
                                            radius: 3
                                            color: card.colors.border
                                        }
                                    }
                                }

                                Rectangle {
                                    x: 56
                                    y: 10
                                    width: parent.width - 66
                                    height: 34
                                    radius: 6
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: card.hero.stops[0] }
                                        GradientStop { position: 0.55; color: card.hero.stops[1] }
                                        GradientStop { position: 1; color: card.hero.stops[2] }
                                    }

                                    Rectangle {
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 34
                                        height: 10
                                        radius: 5
                                        color: card.hero.button
                                    }
                                }

                                Repeater {
                                    model: 2

                                    Rectangle {
                                        required property int index

                                        x: 56 + index * ((preview.width - 66 + 6) / 2)
                                        y: 52
                                        width: (preview.width - 66 - 6) / 2
                                        height: 46
                                        radius: 6
                                        color: card.colors.surface
                                        border.width: 1
                                        border.color: card.colors.border

                                        Rectangle {
                                            x: 8
                                            y: 9
                                            width: parent.width - 30
                                            height: 6
                                            radius: 3
                                            color: card.colors.text
                                            opacity: 0.85
                                        }

                                        Rectangle {
                                            x: 8
                                            y: 21
                                            width: parent.width - 16
                                            height: 5
                                            radius: 2.5
                                            color: card.colors.textMuted
                                            opacity: 0.6
                                        }

                                        Rectangle {
                                            x: 8
                                            y: 32
                                            width: 22
                                            height: 7
                                            radius: 3.5
                                            color: parent.index === 0 ? card.colors.accent : card.colors.arch
                                        }
                                    }
                                }
                            }

                            Text {
                                textFormat: Text.PlainText
                                x: 14
                                anchors.top: preview.bottom
                                anchors.topMargin: 10
                                text: card.text
                                color: Look.text
                                font.pixelSize: Look.fontBody + 1
                                font.weight: Font.DemiBold
                            }

                            Text {
                                textFormat: Text.PlainText
                                x: 14
                                width: parent.width - 28
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                text: root.themeHint(card.modelData)
                                color: Look.textMuted
                                font.pixelSize: Look.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
        // AUR and Flatpak
        Column {
            width: parent.width
            spacing: 10

            Text {
                textFormat: Text.PlainText
                text: qsTr("Sources")
                color: Look.text
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Rectangle {
                width: Math.min(parent.width, 914)
                height: aurText.height + 32
                radius: Look.radius
                color: Look.surface
                border.width: 1
                border.color: Look.border

                Column {
                    id: aurText
                    x: 18
                    y: 16
                    width: parent.width - 36 - aurSwitch.width - 18
                    spacing: 4

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Arch User Repository (AUR)")
                        color: Look.text
                        font.pixelSize: Look.fontBody + 1
                        font.weight: Font.DemiBold
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: qsTr("Build recipes written by the community. They are not reviewed by CachyOS, are built on your machine and can run any code while doing so, so read the PKGBUILD before installing.")
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall + 1
                        lineHeight: 1.25
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: !Backend.paruAvailable
                        width: parent.width
                        text: qsTr("Building AUR packages needs “paru”, which is not installed. You can still search and review packages.")
                        color: Look.warning
                        font.pixelSize: Look.fontSmall + 1
                        wrapMode: Text.WordWrap
                    }
                }

                Toggle {
                    id: aurSwitch
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    active: Backend.aurEnabled
                    name: qsTr("Arch User Repository (AUR)")
                    onClicked: Backend.aurEnabled = !Backend.aurEnabled
                }
            }

            Rectangle {
                width: Math.min(parent.width, 914)
                height: flatpakText.height + 32
                radius: Look.radius
                color: Look.surface
                border.width: 1
                border.color: Look.border

                Column {
                    id: flatpakText
                    x: 18
                    y: 16
                    width: parent.width - 36 - flatpakSwitch.width - 18
                    spacing: 4

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Flatpak")
                        color: Look.text
                        font.pixelSize: Look.fontBody + 1
                        font.weight: Font.DemiBold
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        text: qsTr("Sandboxed apps from Flathub and other Flatpak remotes. They are packaged by their developers or the Flathub community, not by CachyOS, and bring their own libraries.")
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall + 1
                        lineHeight: 1.25
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: Backend.flatpakEnabled && !Backend.flatpak.loading && !Backend.flatpak.available
                        width: parent.width
                        text: qsTr("Flatpak is not installed. You can install it from the Flatpak page.")
                        color: Look.warning
                        font.pixelSize: Look.fontSmall + 1
                        wrapMode: Text.WordWrap
                    }
                }

                Toggle {
                    id: flatpakSwitch
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    active: Backend.flatpakEnabled
                    name: qsTr("Flatpak")
                    onClicked: Backend.flatpakEnabled = !Backend.flatpakEnabled
                }
            }
        }
    }
}
