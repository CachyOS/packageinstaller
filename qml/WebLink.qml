import QtQuick
import QtQuick.Controls.Basic

// A web address that opens in the browser. The address comes from package metadata, so only
// http and https are offered; anything else (a local file, another program's scheme) is hidden.
// Shows the address itself unless `text` says what it leads to.
AbstractButton {
    id: root

    required property string url

    readonly property bool valid: /^https?:\/\/[^\s]+$/i.test(url)

    visible: valid
    implicitHeight: label.implicitHeight
    padding: 0
    Accessible.role: Accessible.Link
    Accessible.name: label.text
    onClicked: if (valid) Qt.openUrlExternally(url)

    contentItem: PlainText {
        id: label
        text: root.text.length > 0 ? root.text : root.url
        color: Look.accent
        font.underline: root.visualFocus || root.hovered
        elide: Text.ElideRight
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
