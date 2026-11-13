import QtQuick

Rectangle {
    id: root

    enum Tone { Info, Warning, Danger }

    property alias text: label.text
    property int tone: Notice.Info
    default property alias actions: actionRow.data

    visible: text.length > 0
    implicitHeight: label.implicitHeight + 28
    radius: Look.radius
    color: tone === Notice.Danger ? Look.dangerSoft : tone === Notice.Warning ? Look.warningSoft : Look.accentSoft

    PlainText {
        id: label
        x: 16
        y: 14
        width: root.width - 32 - (actionRow.width > 0 ? actionRow.width + 12 : 0)
        color: root.tone === Notice.Danger ? Look.danger : Look.text
        wrapMode: Text.WordWrap
    }

    Row {
        id: actionRow
        anchors.right: root.right
        anchors.rightMargin: 14
        anchors.verticalCenter: root.verticalCenter
        spacing: 8
    }
}
