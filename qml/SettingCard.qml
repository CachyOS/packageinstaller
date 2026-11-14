import QtQuick

// A row of the settings page: a title and its explanation, with a control on the right.
Card {
    id: root

    required property string title
    required property string description
    property string warning
    /// the control, for example a Toggle or a PillButton
    default property alias control: slot.data

    height: texts.height + 32

    Column {
        id: texts
        x: 18
        y: 16
        width: root.width - 54 - slot.width
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

    Row {
        id: slot
        anchors.right: root.right
        anchors.rightMargin: 18
        anchors.verticalCenter: root.verticalCenter
    }
}
