import QtQuick

Card {
    id: root

    default property alias facts: flow.data

    height: flow.height + 40

    Flow {
        id: flow
        x: 20
        y: 20
        width: root.width - 40
        spacing: 20
    }
}
