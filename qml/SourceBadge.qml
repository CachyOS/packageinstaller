import QtQuick

// Where a package comes from: "cachyos", "arch", "aur", "flatpak" or "local".
Pill {
    required property string source

    text: {
        switch (source) {
        case "cachyos": return qsTr("CachyOS");
        case "arch": return qsTr("Arch");
        case "aur": return qsTr("AUR");
        case "flatpak": return qsTr("Flatpak");
        case "local": return qsTr("Local");
        }
        return source;
    }
    ink: source === "cachyos" ? Look.accent : source === "aur" ? Look.warning
        : source === "arch" || source === "flatpak" ? Look.arch : Look.textMuted
    color: source === "cachyos" ? Look.accentSoft : source === "aur" ? Look.warningSoft
        : source === "arch" || source === "flatpak" ? Look.archSoft : Look.surfaceAlt
}
