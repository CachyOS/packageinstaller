import QtQuick

Card {
    id: root

    required property string title
    required property string description
    required property bool active
    property string warning

    signal toggled

    height: texts.height + 32

    Column {
        id: texts
        x: 18
        y: 16
        width: root.width - 54 - toggle.width
        spacing: 4

        PlainText {
            text: root.title
            font.pixelSize: Look.fontLead
            font.weight: Font.DemiBold
        }

        Muted {
            width: texts.width
            text: root.description
            lineHeight: 1.25
            wrapMode: Text.WordWrap
        }

        Muted {
            visible: root.warning.length > 0
            width: texts.width
            text: root.warning
            color: Look.warning
            wrapMode: Text.WordWrap
        }
    }

    Toggle {
        id: toggle
        anchors.right: root.right
        anchors.rightMargin: 18
        anchors.verticalCenter: root.verticalCenter
        active: root.active
        name: root.title
        onClicked: root.toggled()
    }
}
