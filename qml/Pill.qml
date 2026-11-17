import QtQuick

Rectangle {
    id: root

    property alias text: caption.text
    property color ink: Look.textMuted
    property alias font: caption.font
    /// An optional line icon in front of the text, as a 24x24 SVG path.
    property string glyph
    property color glyphInk: ink

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 20
    radius: height / 2
    color: Look.surfaceAlt

    Row {
        id: row
        anchors.centerIn: root
        spacing: 4

        Glyph {
            visible: root.glyph !== ""
            anchors.verticalCenter: row.verticalCenter
            path: root.glyph
            color: root.glyphInk
            size: 12
            strokeWidth: 2.2
        }

        PlainText {
            id: caption
            anchors.verticalCenter: row.verticalCenter
            color: root.ink
            font.pixelSize: Look.fontCaption
            font.weight: Font.DemiBold
        }
    }
}
