pragma Singleton
import QtQuick
import CachyOS.PackageInstaller

QtObject {
    // The setting decides; asking Qt to switch the scheme is not honoured on every desktop.
    readonly property bool dark: Backend.colorMode === "dark"
        || (Backend.colorMode !== "light" && Application.styleHints.colorScheme !== Qt.ColorScheme.Light)

    // The colour themes offered on the settings page, each for dark and light desktops.
    // `hero` is the banner on the start page: gradient, text, button and picture.
    readonly property var themes: ({
        "classic": {
            dark: {
                window: "#0e1315", sidebar: "#0a0e10", surface: "#171e21", surfaceHover: "#1e272b", surfaceAlt: "#212b30",
                border: "#263136", text: "#e9eff1", textMuted: "#8c9da4",
                accent: "#22d3b7", accentHover: "#4be0c9", accentSoft: "#12332f", onAccent: "#04211c",
                arch: "#5ab0f2", archSoft: "#14293a"
            },
            light: {
                window: "#f3f5f6", sidebar: "#e9edef", surface: "#ffffff", surfaceHover: "#f7fafa", surfaceAlt: "#e8edef",
                border: "#d9e0e3", text: "#132022", textMuted: "#5a6b71",
                accent: "#0a9f88", accentHover: "#0b8f7b", accentSoft: "#d6f3ee", onAccent: "#ffffff",
                arch: "#1273c4", archSoft: "#dcedfb"
            },
            hero: {
                stops: ["#0b7f6f", "#10a48d", "#1c6fb8"], text: "#ffffff", sub: "#e6fffa",
                button: "#eafffb", buttonHover: "#ffffff", ink: "#075f53", image: "icons/cachyos-pi.png"
            }
        },
        // blue-black neutrals with the mint and cyan of the CachyOS logo
        "midnight": {
            dark: {
                window: "#0a0e14", sidebar: "#070a0f", surface: "#111823", surfaceHover: "#172131", surfaceAlt: "#1b2636",
                border: "#212d3d", text: "#e6edf5", textMuted: "#8696ab",
                accent: "#2fe0c4", accentHover: "#62ead4", accentSoft: "#0e2f33", onAccent: "#03201c",
                arch: "#5ab0f2", archSoft: "#14293a"
            },
            light: {
                window: "#f2f5f9", sidebar: "#e7ecf3", surface: "#ffffff", surfaceHover: "#f6f9fc", surfaceAlt: "#e6ebf2",
                border: "#d5dde8", text: "#101a26", textMuted: "#566579",
                accent: "#0a9a8c", accentHover: "#088678", accentSoft: "#d3f1ee", onAccent: "#ffffff",
                arch: "#1273c4", archSoft: "#dcedfb"
            },
            hero: {
                stops: ["#0c2a33", "#0d3b45", "#0b5f7a"], text: "#ffffff", sub: "#bfe9ee",
                button: "#2fe0c4", buttonHover: "#62ead4", ink: "#03201c", image: "icons/cachyos-logo.png"
            }
        },
        // neutral greys and one bright mint
        "graphite": {
            dark: {
                window: "#0f1011", sidebar: "#0a0b0c", surface: "#18191b", surfaceHover: "#1f2123", surfaceAlt: "#242628",
                border: "#2c2f32", text: "#eceeef", textMuted: "#969a9e",
                accent: "#00e0a4", accentHover: "#3beabb", accentSoft: "#0c3327", onAccent: "#00241a",
                arch: "#6cb6f5", archSoft: "#182a3b"
            },
            light: {
                window: "#f4f4f5", sidebar: "#eaeaec", surface: "#ffffff", surfaceHover: "#f8f8f9", surfaceAlt: "#e9e9eb",
                border: "#dadadd", text: "#18191b", textMuted: "#62666b",
                accent: "#008f68", accentHover: "#007a59", accentSoft: "#d4f2e7", onAccent: "#ffffff",
                arch: "#1273c4", archSoft: "#dcedfb"
            },
            hero: {
                stops: ["#0d3a30", "#0b5c4a", "#079a78"], text: "#ffffff", sub: "#d5f7ec",
                button: "#f2fffb", buttonHover: "#ffffff", ink: "#064a3a", image: "icons/cachyos-pi.png"
            }
        },
        // indigo-tinted neutrals, cyan accent, violet for Arch
        "aurora": {
            dark: {
                window: "#0d0c16", sidebar: "#09080f", surface: "#161524", surfaceHover: "#1d1c30", surfaceAlt: "#222138",
                border: "#2b2a45", text: "#ebeaf6", textMuted: "#9492b0",
                accent: "#4cc9ff", accentHover: "#7ad7ff", accentSoft: "#122b40", onAccent: "#021a28",
                arch: "#a78bfa", archSoft: "#261f45"
            },
            light: {
                window: "#f4f3fb", sidebar: "#eae8f6", surface: "#ffffff", surfaceHover: "#f8f7fd", surfaceAlt: "#e9e7f5",
                border: "#d9d6ec", text: "#17152b", textMuted: "#5d5a7a",
                accent: "#0b7fc2", accentHover: "#096da8", accentSoft: "#d8ecfa", onAccent: "#ffffff",
                arch: "#6d4fd6", archSoft: "#e7e0fb"
            },
            hero: {
                stops: ["#4338ca", "#2563eb", "#06b6d4"], text: "#ffffff", sub: "#e0ecff",
                button: "#f3f8ff", buttonHover: "#ffffff", ink: "#1e2a78", image: "icons/cachyos-logo.png"
            }
        }
    })
    /// in the order the settings page shows them
    readonly property var themeNames: ["classic", "midnight", "graphite", "aurora"]

    // an unknown name in the settings file falls back to the classic look
    readonly property var theme: themes[Backend.theme] ?? themes["classic"]
    readonly property var palette: dark ? theme.dark : theme.light

    readonly property color window: palette.window
    readonly property color sidebar: palette.sidebar
    readonly property color surface: palette.surface
    readonly property color surfaceHover: palette.surfaceHover
    readonly property color surfaceAlt: palette.surfaceAlt
    readonly property color border: palette.border
    readonly property color text: palette.text
    readonly property color textMuted: palette.textMuted
    readonly property color accent: palette.accent
    readonly property color accentHover: palette.accentHover
    readonly property color accentSoft: palette.accentSoft
    readonly property color onAccent: palette.onAccent
    readonly property color danger: dark ? "#ff7a7a" : "#c62f2f"
    readonly property color dangerSoft: dark ? "#3a1c1e" : "#fbe3e3"
    readonly property color warning: dark ? "#f4c05a" : "#9a6400"
    readonly property color warningSoft: dark ? "#372c14" : "#fbefd3"
    readonly property color arch: palette.arch
    readonly property color archSoft: palette.archSoft

    readonly property color heroStart: theme.hero.stops[0]
    readonly property color heroMiddle: theme.hero.stops[1]
    readonly property color heroEnd: theme.hero.stops[2]
    readonly property color heroText: theme.hero.text
    readonly property color heroSub: theme.hero.sub
    readonly property color heroButton: theme.hero.button
    readonly property color heroButtonHover: theme.hero.buttonHover
    readonly property color heroInk: theme.hero.ink
    readonly property string heroImage: theme.hero.image

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
