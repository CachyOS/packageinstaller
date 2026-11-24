pragma Singleton
import QtQuick

QtObject {
    id: look

    readonly property AppearanceSettings settings: AppearanceSettings {}

    readonly property Binding scheme: Binding {
        target: Application.styleHints
        property: "colorScheme"
        value: look.settings.mode === "light" ? Qt.ColorScheme.Light
            : look.settings.mode === "dark" ? Qt.ColorScheme.Dark : Qt.ColorScheme.Unknown
    }

    /// The "desktop" theme takes its colours from the system palette, so it follows KDE colour
    /// schemes, qt6ct and tools that generate them, for example from the wallpaper.
    readonly property bool followsDesktop: settings.theme === "desktop"

    // The setting decides; asking Qt to switch the scheme is not honoured on every desktop.
    // The desktop theme has a single palette, which is dark or light by itself.
    readonly property bool dark: followsDesktop ? luminance(system.window) < 0.5
        : settings.mode === "dark" || (settings.mode !== "light" && Application.styleHints.colorScheme !== Qt.ColorScheme.Light)

    readonly property SystemPalette system: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    /// relative luminance, 0 for black and 1 for white
    function luminance(c: color): real {
        return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
    }

    /// `a` moved by `amount` (0 to 1) towards `b`
    function mix(a: color, b: color, amount: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount, 1)
    }

    /// black or white, whichever reads better on `background`
    function inkOn(background: color): color {
        return contrast("#101418", background) >= contrast("#ffffff", background) ? "#101418" : "#ffffff"
    }

    /// WCAG contrast ratio of two colours, from 1 to 21
    function contrast(a: color, b: color): real {
        const linear = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        const relative = c => 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
        const la = relative(a);
        const lb = relative(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    /// `foreground`, moved towards `text` until it can be read as small text on `background`
    function readable(foreground: color, background: color, text: color): color {
        let result = foreground;
        for (let step = 0; step < 10 && contrast(result, background) < 4.5; ++step)
            result = mix(result, text, 0.15);
        return result
    }

    /// A palette with only "window", "text" and "accent" (or a few more) filled up with blends
    /// of them, in the same steps as the hand-made themes. Colours that are given are kept,
    /// except that accent colours are made readable as text on the cards: generated palettes
    /// (from a wallpaper, for example) do not always keep to that.
    function completePalette(given: var): var {
        const colour = (name, fallback) => given[name] ? Qt.color(given[name]) : fallback;
        const window = Qt.color(given.window);
        const text = Qt.color(given.text);
        const isDark = luminance(window) < 0.5;
        const surface = colour("surface", mix(window, text, 0.06));
        const accent = readable(Qt.color(given.accent), surface, text);
        const arch = readable(colour("arch", Qt.color(isDark ? "#5ab0f2" : "#1273c4")), surface, text);
        const result = {
            window: window, sidebar: isDark ? mix(window, "#000000", 0.25) : mix(window, text, 0.05),
            surface: surface, surfaceHover: mix(window, text, 0.09), surfaceAlt: mix(window, text, 0.12),
            border: mix(window, text, 0.18), text: text, textMuted: mix(text, window, 0.4),
            accentHover: mix(accent, text, 0.25), accentSoft: mix(window, accent, 0.2), onAccent: inkOn(accent),
            archSoft: mix(window, arch, 0.2)
        };
        Object.assign(result, given);
        // the checked colours win over the given ones
        result.accent = accent;
        result.arch = arch;
        if (contrast(Qt.color(result.onAccent), accent) < 4.5)
            result.onAccent = inkOn(accent);
        for (const name of ["danger", "warning"]) {
            if (given[name])
                result[name] = readable(Qt.color(given[name]), surface, text);
        }
        return asNames(result);
    }

    /// The colours of a palette or banner as "#rrggbb" names. Colour values kept in a plain
    /// object do not survive being read back through a `color` property: they turn black.
    function asNames(colours: var): var {
        const names = {};
        for (const [key, value] of Object.entries(colours))
            names[key] = Array.isArray(value) ? value.map(v => String(v)) : String(value);
        return names;
    }

    /// the start page banner in the colours of a palette
    function heroFor(colours: var): var {
        const window = Qt.color(colours.window);
        const accent = Qt.color(colours.accent);
        const start = mix(window, accent, 0.4);
        const middle = mix(window, accent, 0.6);
        // the text sits on the left half: it has to read on both ends of that
        const dark = Qt.color("#101418");
        const light = Qt.color("#ffffff");
        const worst = ink => Math.min(contrast(ink, start), contrast(ink, middle));
        const ink = worst(dark) > worst(light) ? dark : light;
        return asNames({
            stops: [start, middle, mix(window, Qt.color(colours.arch), 0.5)], text: ink, sub: mix(ink, middle, 0.15),
            button: colours.accent, buttonHover: colours.accentHover, ink: colours.onAccent, image: "icons/cachyos-logo.png"
        });
    }

    /// a theme with some colours left out made whole
    function completeTheme(given: var): var {
        const dark = completePalette(given.dark);
        const light = completePalette(given.light);
        const hero = given.hero ? Object.assign({ image: "icons/cachyos-logo.png" }, given.hero) : heroFor(dark);
        return { title: given.title, hint: given.hint, dark: dark, light: light, hero: hero };
    }

    readonly property var desktopTheme: {
        // The highlight, not the newer accent role: every platform theme sets it, while some
        // (GTK 3) leave the accent at Qt's default blue. Kept as names, like the built-in colours.
        const colours = { window: system.window.toString(), text: system.windowText.toString(), accent: system.highlight.toString() };
        return completeTheme({ title: qsTr("Desktop"), hint: qsTr("The colours of your desktop's colour scheme"), dark: colours, light: colours });
    }

    readonly property var themes: Object.assign({ "desktop": desktopTheme }, builtinThemes)

    // The colour themes offered on the settings page, each for dark and light desktops.
    // `hero` is the banner on the start page: gradient, text, button and picture.
    readonly property var builtinThemes: ({
        "classic": {
            title: qsTr("Classic"), hint: qsTr("The original teal look"),
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
        "midnight": {
            title: qsTr("Midnight"), hint: qsTr("Blue-black with the colours of the CachyOS logo"),
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
        "graphite": {
            title: qsTr("Graphite"), hint: qsTr("Neutral greys and one bright mint"),
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
        "aurora": {
            title: qsTr("Aurora"), hint: qsTr("Indigo tint with a cyan accent"),
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
        },
        // Community palettes, see THIRD-PARTY.md. The named colours are the official values;
        // hover, alt and "soft" shades are blends of them, and a few colours are lightened or
        // darkened so that text stays readable.
        "nord": {
            title: "Nord", hint: qsTr("Community palette by %1").arg("Sven Greb"),
            dark: {
                window: "#2e3440", sidebar: "#282d37", surface: "#3b4252", surfaceHover: "#3f4758", surfaceAlt: "#434c5e",
                border: "#4c566a", text: "#eceff4", textMuted: "#a8aeba",
                accent: "#88c0d0", accentHover: "#a2ceda", accentSoft: "#40505d", onAccent: "#2e3440",
                arch: "#b48ead", archSoft: "#494656",
                danger: "#d18d94", dangerSoft: "#4f4651", warning: "#ebcb8b", warningSoft: "#54524f"
            },
            light: {
                window: "#e5e9f0", sidebar: "#d8dee9", surface: "#eceff4", surfaceHover: "#e2e6ee", surfaceAlt: "#d8dee9",
                border: "#bfc6d2", text: "#2e3440", textMuted: "#4c566a",
                accent: "#4b678a", accentHover: "#405875", accentSoft: "#ccd4e0", onAccent: "#eceff4",
                arch: "#78667c", archSoft: "#d4d4dd",
                danger: "#a2525a", dangerSoft: "#dcd4db", warning: "#8d5c4c", warningSoft: "#d9d5d9"
            },
            hero: {
                stops: ["#475b68", "#597785", "#665a6e"], text: "#ffffff", sub: "#eaf4f7",
                button: "#88c0d0", buttonHover: "#a2ceda", ink: "#2e3440", image: "icons/cachyos-logo.png"
            }
        },
        "dracula": {
            title: "Dracula", hint: qsTr("Community palette by %1").arg("Dracula Theme"),
            dark: {
                window: "#282a36", sidebar: "#21222c", surface: "#343746", surfaceHover: "#3c3f50", surfaceAlt: "#44475a",
                border: "#4e5674", text: "#f8f8f2", textMuted: "#b4bccf",
                accent: "#bd93f9", accentHover: "#ccabfa", accentSoft: "#463f5d", onAccent: "#282a36",
                arch: "#8be9fd", archSoft: "#3c505e",
                danger: "#ff5555", dangerSoft: "#53333c", warning: "#ffb86c", warningSoft: "#534641"
            },
            light: {
                window: "#f5f1e0", sidebar: "#eae6d5", surface: "#fffbeb", surfaceHover: "#f3efde", surfaceAlt: "#e7e3d1",
                border: "#d6d1be", text: "#1f1f1f", textMuted: "#6c664b",
                accent: "#644ac9", accentHover: "#553fab", accentSoft: "#ded6dc", onAccent: "#fffbeb",
                arch: "#036a96", archSoft: "#cedbd4",
                danger: "#cb3a2a", dangerSoft: "#efd7c7", warning: "#a34d14", warningSoft: "#eadac3"
            },
            hero: {
                stops: ["#52476d", "#705c94", "#527a8a"], text: "#ffffff", sub: "#f3ecfe",
                button: "#bd93f9", buttonHover: "#ccabfa", ink: "#282a36", image: "icons/cachyos-logo.png"
            }
        },
        "catppuccin": {
            title: "Catppuccin Mocha", hint: qsTr("Community palette by %1").arg("Catppuccin"),
            dark: {
                window: "#1e1e2e", sidebar: "#181825", surface: "#2b2c3d", surfaceHover: "#2e2f40", surfaceAlt: "#313244",
                border: "#45475a", text: "#cdd6f4", textMuted: "#a6adc8",
                accent: "#cba6f7", accentHover: "#d6baf9", accentSoft: "#413956", onAccent: "#11111b",
                arch: "#89b4fa", archSoft: "#333c57",
                danger: "#f38ba8", dangerSoft: "#493446", warning: "#f9e2af", warningSoft: "#4a4548"
            },
            light: {
                window: "#e6e9ef", sidebar: "#dce0e8", surface: "#eff1f5", surfaceHover: "#e2e6ec", surfaceAlt: "#d6dae2",
                border: "#ccd0da", text: "#4c4f69", textMuted: "#6c6f85",
                accent: "#8839ef", accentHover: "#7430cb", accentSoft: "#d7cdef", onAccent: "#eff1f5",
                arch: "#1e66f5", archSoft: "#c6d4f0",
                danger: "#d20f39", dangerSoft: "#e3cad6", warning: "#a76a16", warningSoft: "#ddd7d1"
            },
            hero: {
                stops: ["#4e4466", "#715f8e", "#4b5d84"], text: "#ffffff", sub: "#f6effe",
                button: "#cba6f7", buttonHover: "#d6baf9", ink: "#11111b", image: "icons/cachyos-logo.png"
            }
        },
        "catppuccinmacchiato": {
            title: "Catppuccin Macchiato", hint: qsTr("Community palette by %1").arg("Catppuccin"),
            dark: {
                window: "#24273a", sidebar: "#1e2030", surface: "#303448", surfaceHover: "#33374c", surfaceAlt: "#363a4f",
                border: "#494d64", text: "#cad3f5", textMuted: "#a5adcb",
                accent: "#c6a0f6", accentHover: "#d3b5f8", accentSoft: "#443f60", onAccent: "#181926",
                arch: "#8aadf4", archSoft: "#38425f",
                danger: "#ed8796", dangerSoft: "#4c3a4c", warning: "#eed49f", warningSoft: "#4c4a4e"
            },
            light: {
                window: "#e6e9ef", sidebar: "#dce0e8", surface: "#eff1f5", surfaceHover: "#e2e6ec", surfaceAlt: "#d6dae2",
                border: "#ccd0da", text: "#4c4f69", textMuted: "#6c6f85",
                accent: "#8839ef", accentHover: "#7430cb", accentSoft: "#d7cdef", onAccent: "#eff1f5",
                arch: "#1e66f5", archSoft: "#c6d4f0",
                danger: "#d20f39", dangerSoft: "#e3cad6", warning: "#a76a16", warningSoft: "#ddd7d1"
            },
            hero: {
                stops: ["#51496f", "#726194", "#4f5f88"], text: "#ffffff", sub: "#f4ecfd",
                button: "#c6a0f6", buttonHover: "#d3b5f8", ink: "#181926", image: "icons/cachyos-logo.png"
            }
        },
        "catppuccinfrappe": {
            title: "Catppuccin Frappé", hint: qsTr("Community palette by %1").arg("Catppuccin"),
            dark: {
                window: "#303446", sidebar: "#292c3c", surface: "#3c4053", surfaceHover: "#3e4256", surfaceAlt: "#414559",
                border: "#51576d", text: "#c6d0f5", textMuted: "#a5adce",
                accent: "#cca2e7", accentHover: "#d7b6ec", accentSoft: "#4f4a66", onAccent: "#232634",
                arch: "#8eacee", archSoft: "#434c68",
                danger: "#ec9b9d", dangerSoft: "#564957", warning: "#e5c890", warningSoft: "#545255"
            },
            light: {
                window: "#e6e9ef", sidebar: "#dce0e8", surface: "#eff1f5", surfaceHover: "#e2e6ec", surfaceAlt: "#d6dae2",
                border: "#ccd0da", text: "#4c4f69", textMuted: "#6c6f85",
                accent: "#8839ef", accentHover: "#7430cb", accentSoft: "#d7cdef", onAccent: "#eff1f5",
                arch: "#1e66f5", archSoft: "#c6d4f0",
                danger: "#d20f39", dangerSoft: "#e3cad6", warning: "#a76a16", warningSoft: "#ddd7d1"
            },
            hero: {
                stops: ["#5c5373", "#7b6993", "#57668d"], text: "#ffffff", sub: "#f5ecfa",
                button: "#cca2e7", buttonHover: "#d7b6ec", ink: "#232634", image: "icons/cachyos-logo.png"
            }
        },
        "tokyonight": {
            title: "Tokyo Night", hint: qsTr("Community palette by %1").arg("Enkia"),
            dark: {
                window: "#1a1b26", sidebar: "#16161e", surface: "#202330", surfaceHover: "#262a38", surfaceAlt: "#2d3040",
                border: "#3a3e50", text: "#c0caf5", textMuted: "#8a90b0",
                accent: "#7aa2f7", accentHover: "#97b6f9", accentSoft: "#2d3650", onAccent: "#1a1b26",
                arch: "#bb9af7", archSoft: "#3a3450",
                danger: "#f7768e", dangerSoft: "#462d3b", warning: "#e0af68", warningSoft: "#423933"
            },
            light: {
                window: "#d6d8df", sidebar: "#cbcdd6", surface: "#e6e7ed", surfaceHover: "#d8d9e0", surfaceAlt: "#c9cbd4",
                border: "#c1c2c7", text: "#343b58", textMuted: "#616476",
                accent: "#2959aa", accentHover: "#234c90", accentSoft: "#bac4d7", onAccent: "#e6e7ed",
                arch: "#7b43ba", archSoft: "#c7c0d9",
                danger: "#8c4351", dangerSoft: "#ccc3cb", warning: "#8f5e15", warningSoft: "#ccc7c3"
            },
            hero: {
                stops: ["#354161", "#485c8a", "#5e507e"], text: "#ffffff", sub: "#e7eefe",
                button: "#7aa2f7", buttonHover: "#97b6f9", ink: "#1a1b26", image: "icons/cachyos-logo.png"
            }
        },
        "solarized": {
            title: "Solarized", hint: qsTr("Community palette by %1").arg("Ethan Schoonover"),
            dark: {
                window: "#002b36", sidebar: "#00222a", surface: "#073642", surfaceHover: "#103c48", surfaceAlt: "#19424d",
                border: "#294e57", text: "#eee8d5", textMuted: "#93a19f",
                accent: "#56a5dc", accentHover: "#7bb9e4", accentSoft: "#114357", onAccent: "#002b36",
                arch: "#989cd6", archSoft: "#1e4256",
                danger: "#e56563", dangerSoft: "#2e373f", warning: "#c09b26", warningSoft: "#264133"
            },
            light: {
                window: "#eee8d5", sidebar: "#dfdccb", surface: "#fdf6e3", surfaceHover: "#ece8d6", surfaceAlt: "#dcd9c9",
                border: "#c7c8bc", text: "#073642", textMuted: "#586e75",
                accent: "#1e6ca4", accentHover: "#1a5c8b", accentSoft: "#cdd4cd", onAccent: "#fdf6e3",
                arch: "#595da1", archSoft: "#d6d2cd",
                danger: "#ca2e2b", dangerSoft: "#e9cebd", warning: "#b74414", warningSoft: "#e6d1ba"
            },
            hero: {
                stops: ["#184d64", "#296686", "#405a79"], text: "#ffffff", sub: "#e1eff9",
                button: "#56a5dc", buttonHover: "#7bb9e4", ink: "#002b36", image: "icons/cachyos-logo.png"
            }
        },
        "gruvbox": {
            title: "Gruvbox", hint: qsTr("Community palette by %1").arg("Pavel Pertsev"),
            dark: {
                window: "#282828", sidebar: "#1d2021", surface: "#32302f", surfaceHover: "#373432", surfaceAlt: "#3c3836",
                border: "#504945", text: "#ebdbb2", textMuted: "#a89984",
                accent: "#8ec07c", accentHover: "#a7ce99", accentSoft: "#3c4639", onAccent: "#282828",
                arch: "#d3869b", archSoft: "#4a3b3f",
                danger: "#fc7161", dangerSoft: "#523733", warning: "#fabd2f", warningSoft: "#524629"
            },
            light: {
                window: "#f2e5bc", sidebar: "#ebdbb2", surface: "#fbf1c7", surfaceHover: "#eee0b7", surfaceAlt: "#e4d4a9",
                border: "#d5c4a1", text: "#3c3836", textMuted: "#665c54",
                accent: "#3b6f4f", accentHover: "#325e43", accentSoft: "#d5d2ab", onAccent: "#fbf1c7",
                arch: "#8f3f71", archSoft: "#e2cab0",
                danger: "#9d0006", dangerSoft: "#e4c09f", warning: "#946110", warningSoft: "#e3d0a0"
            },
            hero: {
                stops: ["#455340", "#597150", "#704f58"], text: "#ffffff", sub: "#e8f2e5",
                button: "#8ec07c", buttonHover: "#a7ce99", ink: "#282828", image: "icons/cachyos-logo.png"
            }
        },
        "rosepine": {
            title: "Rosé Pine", hint: qsTr("Community palette by %1").arg("Rosé Pine"),
            dark: {
                window: "#191724", sidebar: "#14121d", surface: "#1f1d2e", surfaceHover: "#222034", surfaceAlt: "#26233a",
                border: "#403d52", text: "#e0def4", textMuted: "#908caa",
                accent: "#c4a7e7", accentHover: "#d1baec", accentSoft: "#3b344b", onAccent: "#191724",
                arch: "#9ccfd8", archSoft: "#333c48",
                danger: "#eb6f92", dangerSoft: "#43293a", warning: "#f6c177", warningSoft: "#453935"
            },
            light: {
                window: "#faf4ed", sidebar: "#f2e9e1", surface: "#fffaf3", surfaceHover: "#f4ede8", surfaceAlt: "#eae2da",
                border: "#dfdad9", text: "#575279", textMuted: "#6f6c87",
                accent: "#79668e", accentHover: "#675779", accentSoft: "#e5ddde", onAccent: "#fffaf3",
                arch: "#286983", archSoft: "#d8dedc",
                danger: "#a2596e", dangerSoft: "#ecdbd9", warning: "#9a6822", warningSoft: "#ebdecd"
            },
            hero: {
                stops: ["#493f5b", "#6b5c82", "#506470"], text: "#ffffff", sub: "#f3edfa",
                button: "#c4a7e7", buttonHover: "#d1baec", ink: "#191724", image: "icons/cachyos-logo.png"
            }
        },
        "everforest": {
            title: "Everforest", hint: qsTr("Community palette by %1").arg("sainnhe"),
            dark: {
                window: "#2d353b", sidebar: "#232a2e", surface: "#343f44", surfaceHover: "#384448", surfaceAlt: "#3d484d",
                border: "#4f585e", text: "#d3c6aa", textMuted: "#a1aca4",
                accent: "#a7c080", accentHover: "#bace9c", accentSoft: "#455149", onAccent: "#232a2e",
                arch: "#7fbbb3", archSoft: "#3d5053",
                danger: "#ea9597", dangerSoft: "#53484d", warning: "#dbbc7f", warningSoft: "#505049"
            },
            light: {
                window: "#f4f0d9", sidebar: "#e6e2cc", surface: "#fdf6e3", surfaceHover: "#efebd4", surfaceAlt: "#e6e2cc",
                border: "#cfd0bb", text: "#4a555b", textMuted: "#636e62",
                accent: "#637101", accentHover: "#546001", accentSoft: "#dddcb6", onAccent: "#fdf6e3",
                arch: "#2e769e", archSoft: "#d4dcd0",
                danger: "#c14240", dangerSoft: "#ecd4c1", warning: "#936a00", warningSoft: "#e4dbb6"
            },
            hero: {
                stops: ["#4f5c4e", "#68785c", "#4f6d6d"], text: "#ffffff", sub: "#edf2e6",
                button: "#a7c080", buttonHover: "#bace9c", ink: "#232a2e", image: "icons/cachyos-logo.png"
            }
        }
    })
    /// in the order the settings page shows them
    readonly property var themeNames: ["desktop", ...builtinThemeNames]
    readonly property var builtinThemeNames: ["classic", "midnight", "graphite", "aurora", "nord", "dracula", "catppuccin", "catppuccinmacchiato", "catppuccinfrappe",
        "tokyonight", "solarized", "gruvbox", "rosepine", "everforest"]

    // an unknown name in the settings file falls back to the classic look
    readonly property var theme: themes[settings.theme] ?? themes["classic"]
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
    readonly property color danger: palette.danger ?? (dark ? "#ff7a7a" : "#c62f2f")
    readonly property color dangerSoft: palette.dangerSoft ?? (dark ? "#3a1c1e" : "#fbe3e3")
    readonly property color warning: palette.warning ?? (dark ? "#f4c05a" : "#9a6400")
    readonly property color warningSoft: palette.warningSoft ?? (dark ? "#372c14" : "#fbefd3")
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
    readonly property int radiusLarge: radius + 4
    readonly property int pagePadding: 28
    /// room kept free for a vertical scroll bar
    readonly property int gutter: 12
    readonly property int textMaxWidth: 820
    readonly property color scrim: "#99000000"

    readonly property int fontCaption: 11
    readonly property int fontSmall: 12
    readonly property int fontLabel: 13
    readonly property int fontBody: 14
    readonly property int fontLead: 15
    readonly property int fontTitle: 16
    readonly property int fontSubheading: fontHeading - 2
    readonly property int fontHeading: 22
    readonly property int fontHero: 30

    /// Colour of a pkgscan finding: "danger", "warning" or a note.
    function severityColor(severity: string, soft: bool): color {
        return severity === "danger" ? (soft ? dangerSoft : danger)
            : severity === "warning" ? (soft ? warningSoft : warning)
            : (soft ? archSoft : arch)
    }

    /// the tint of a run of changed lines, by its kind in git::DiffRun
    function diffColor(kind: string): color {
        return kind === "added" ? accentSoft : kind === "removed" ? dangerSoft : kind === "file" ? archSoft : surfaceAlt
    }

    function severityLabel(severity: string): string {
        return severity === "danger" ? qsTr("Dangerous") : severity === "warning" ? qsTr("Check") : qsTr("Note")
    }

    function formatSize(bytes: real): string {
        return Qt.locale().formattedDataSize(bytes, 1, Locale.DataSizeIecFormat)
    }
}
