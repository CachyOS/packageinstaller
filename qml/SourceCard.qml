import QtQuick

SettingCard {
    id: root

    required property bool active

    signal toggled

    Toggle {
        active: root.active
        name: root.title
        onClicked: root.toggled()
    }
}
