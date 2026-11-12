import QtQuick

// Where a package comes from: "cachyos", "arch", "aur", "flatpak" or "local".
Rectangle {
    id: root

    required property string source

    readonly property var look: ({
            "cachyos": [qsTr("CachyOS"), Look.accentSoft, Look.accent],
            "arch": [qsTr("Arch"), Look.archSoft, Look.arch],
            "aur": [qsTr("AUR"), Look.warningSoft, Look.warning],
            "flatpak": [qsTr("Flatpak"), Look.archSoft, Look.arch],
            "local": [qsTr("Local"), Look.surfaceAlt, Look.textMuted]
        })[source] ?? [source, Look.surfaceAlt, Look.textMuted]

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 20
    radius: 10
    color: look[1]

    Text {
        id: label
        textFormat: Text.PlainText
        anchors.centerIn: parent
        text: root.look[0]
        color: root.look[2]
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
