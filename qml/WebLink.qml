import QtQuick
import QtQuick.Controls.Basic

// A web address that opens in the browser. The address comes from package metadata, so only
// http and https are offered; anything else (a local file, another program's scheme) is hidden.
AbstractButton {
    id: root

    required property string url

    readonly property bool valid: /^https?:\/\/[^\s]+$/i.test(url)

    visible: valid
    implicitHeight: label.implicitHeight
    padding: 0
    Accessible.role: Accessible.Link
    Accessible.name: url
    onClicked: if (valid) Qt.openUrlExternally(url)

    contentItem: Text {
        id: label
        textFormat: Text.PlainText
        text: root.url
        color: Look.accent
        font.pixelSize: Look.fontBody
        font.underline: root.visualFocus || root.hovered
        elide: Text.ElideRight
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
