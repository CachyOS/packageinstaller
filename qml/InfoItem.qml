import QtQuick

// A link is only offered for a valid web address.
Column {
    id: root

    property string label
    property string value
    property bool link

    visible: link ? web.valid : value.length > 0
    width: 220
    spacing: 2

    Muted {
        text: root.label
        font.pixelSize: Look.fontSmall
    }

    PlainText {
        visible: !root.link
        width: root.width
        text: root.value
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    WebLink {
        id: web
        width: root.width
        url: root.link ? root.value : ""
    }
}
