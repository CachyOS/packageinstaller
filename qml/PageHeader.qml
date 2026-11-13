import QtQuick

Column {
    id: root

    property alias title: titleLabel.text
    property alias subtitle: subtitleLabel.text
    property alias actions: actionRow.data
    /// a title bar without room for a subtitle
    property bool compact: false

    x: Look.pagePadding
    y: Look.pagePadding - 6
    width: (parent?.width ?? 0) - 2 * Look.pagePadding
    spacing: 14

    Item {
        id: bar
        width: root.width
        height: root.compact ? 34 : 56

        Column {
            id: titles
            anchors.verticalCenter: bar.verticalCenter
            width: bar.width - actionRow.width - 12
            spacing: 2

            Heading {
                id: titleLabel
                width: titles.width
                elide: Text.ElideRight
            }

            Muted {
                id: subtitleLabel
                visible: text.length > 0
                width: titles.width
                elide: Text.ElideRight
            }
        }

        Row {
            id: actionRow
            anchors.right: bar.right
            anchors.verticalCenter: bar.verticalCenter
            spacing: 10
        }
    }
}
