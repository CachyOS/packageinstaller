import QtQuick

// A labelled fact on a details page; hidden while it has no value.
Column {
    id: root

    property string label
    property string value

    visible: value.length > 0
    width: 220
    spacing: 2

    Text {
        textFormat: Text.PlainText
        text: root.label
        color: Look.textMuted
        font.pixelSize: Look.fontSmall
    }

    Text {
        textFormat: Text.PlainText
        width: parent.width
        text: root.value
        color: Look.text
        font.pixelSize: Look.fontBody
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 3
        elide: Text.ElideRight
    }
}
