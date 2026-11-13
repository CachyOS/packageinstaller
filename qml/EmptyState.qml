import QtQuick
import QtQuick.Controls.Basic

Column {
    id: root

    property bool busy
    property alias glyph: icon.path
    property alias title: titleLabel.text
    property alias text: body.text
    property alias textColor: body.color
    property alias action: button.text
    property alias actionTone: button.tone
    /// a quiet note for an empty list rather than a page-filling message
    property bool plain: false

    signal triggered

    width: Math.min((parent?.width ?? 0) - 2 * Look.pagePadding, 520)
    spacing: 12

    BusyIndicator {
        anchors.horizontalCenter: root.horizontalCenter
        visible: root.busy
        running: visible
    }

    Glyph {
        id: icon
        anchors.horizontalCenter: root.horizontalCenter
        visible: path.length > 0
        size: 40
        color: Look.textMuted
    }

    Heading {
        id: titleLabel
        visible: text.length > 0
        color: root.plain ? Look.textMuted : Look.text
        font.pixelSize: root.plain ? Look.fontTitle : Look.fontHeading
        font.weight: root.plain ? Font.Normal : Font.Bold
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Muted {
        id: body
        visible: text.length > 0
        width: root.width
        font.pixelSize: root.plain ? Look.fontBody : Look.fontLead
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    PillButton {
        id: button
        anchors.horizontalCenter: root.horizontalCenter
        visible: text.length > 0
        onClicked: root.triggered()
    }
}
