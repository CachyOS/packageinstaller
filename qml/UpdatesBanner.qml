import QtQuick

Column {
    id: root

    required property int count
    property alias action: button.text
    property alias text: hint.text

    signal triggered

    spacing: 2

    Item {
        id: bar
        width: root.width
        height: Math.max(heading.height, button.height)

        SectionTitle {
            id: heading
            anchors.verticalCenter: bar.verticalCenter
            text: qsTr("%n update(s) available", "", root.count)
        }

        PillButton {
            id: button
            visible: text.length > 0
            anchors.right: bar.right
            anchors.verticalCenter: bar.verticalCenter
            onClicked: root.triggered()
        }
    }

    Muted {
        id: hint
        visible: text.length > 0
        width: root.width
        wrapMode: Text.WordWrap
    }
}
