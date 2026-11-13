pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root

    required property var entry
    readonly property var colors: Look.dark ? entry.dark : entry.light

    height: 110
    radius: Look.radiusSmall
    color: colors.window
    border.width: 1
    border.color: colors.border

    Rectangle {
        x: 1
        y: 1
        width: 46
        height: root.height - 2
        color: root.colors.sidebar

        Rectangle {
            x: 7
            y: 12
            width: 32
            height: 9
            radius: 4
            color: root.colors.accentSoft
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
                color: root.colors.border
            }
        }
    }

    Rectangle {
        id: hero
        x: 56
        y: 10
        width: root.width - 66
        height: 34
        radius: 6
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: root.entry.hero.stops[0] }
            GradientStop { position: 0.55; color: root.entry.hero.stops[1] }
            GradientStop { position: 1; color: root.entry.hero.stops[2] }
        }

        Rectangle {
            x: 8
            anchors.verticalCenter: hero.verticalCenter
            width: 34
            height: 10
            radius: 5
            color: root.entry.hero.button
        }
    }

    Repeater {
        model: 2

        Rectangle {
            id: card

            required property int index

            x: 56 + index * ((root.width - 66 + 6) / 2)
            y: 52
            width: (root.width - 66 - 6) / 2
            height: 46
            radius: 6
            color: root.colors.surface
            border.width: 1
            border.color: root.colors.border

            Rectangle {
                x: 8
                y: 9
                width: card.width - 30
                height: 6
                radius: 3
                color: root.colors.text
                opacity: 0.85
            }

            Rectangle {
                x: 8
                y: 21
                width: card.width - 16
                height: 5
                radius: 2.5
                color: root.colors.textMuted
                opacity: 0.6
            }

            Rectangle {
                x: 8
                y: 32
                width: 22
                height: 7
                radius: 3.5
                color: card.index === 0 ? root.colors.accent : root.colors.arch
            }
        }
    }
}
