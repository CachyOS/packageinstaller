import QtQuick

Rectangle {
    id: root

    property alias text: caption.text
    property color ink: Look.textMuted
    property alias font: caption.font

    implicitWidth: caption.implicitWidth + 16
    implicitHeight: 20
    radius: height / 2
    color: Look.surfaceAlt

    PlainText {
        id: caption
        anchors.centerIn: root
        color: root.ink
        font.pixelSize: Look.fontCaption
        font.weight: Font.DemiBold
    }
}
