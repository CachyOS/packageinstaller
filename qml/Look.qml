pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: Application.styleHints.colorScheme !== Qt.ColorScheme.Light

    readonly property color window: dark ? "#0e1315" : "#f3f5f6"
    readonly property color sidebar: dark ? "#0a0e10" : "#e9edef"
    readonly property color surface: dark ? "#171e21" : "#ffffff"
    readonly property color surfaceHover: dark ? "#1e272b" : "#f7fafa"
    readonly property color surfaceAlt: dark ? "#212b30" : "#e8edef"
    readonly property color border: dark ? "#263136" : "#d9e0e3"
    readonly property color text: dark ? "#e9eff1" : "#132022"
    readonly property color textMuted: dark ? "#8c9da4" : "#5a6b71"
    readonly property color accent: dark ? "#22d3b7" : "#0a9f88"
    readonly property color accentHover: dark ? "#4be0c9" : "#0b8f7b"
    readonly property color accentSoft: dark ? "#12332f" : "#d6f3ee"
    readonly property color onAccent: dark ? "#04211c" : "#ffffff"
    readonly property color danger: dark ? "#ff7a7a" : "#c62f2f"
    readonly property color dangerSoft: dark ? "#3a1c1e" : "#fbe3e3"
    readonly property color warning: dark ? "#f4c05a" : "#9a6400"
    readonly property color warningSoft: dark ? "#372c14" : "#fbefd3"
    readonly property color arch: dark ? "#5ab0f2" : "#1273c4"
    readonly property color archSoft: dark ? "#14293a" : "#dcedfb"

    readonly property int radius: 14
    readonly property int radiusSmall: 8
    readonly property int spacing: 16
    readonly property int pagePadding: 28

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 16
    readonly property int fontHeading: 22
    readonly property int fontHero: 30
}
