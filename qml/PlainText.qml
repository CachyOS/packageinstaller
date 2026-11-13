import QtQuick

// Text from packages and the network is shown as is, never as markup.
Text {
    textFormat: Text.PlainText
    color: Look.text
    font.pixelSize: Look.fontBody
}
