// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import CachyOS.PackageInstaller

// A small stepper for a whole-number setting, in the look of the settings page.
Row {
    id: root

    property int value: 0
    property int from: 0
    property int to: 999

    signal valueModified(int value)

    spacing: 4

    PillButton {
        size: "small"
        text: "−"
        Accessible.name: qsTr("Decrease")
        enabled: root.value > root.from
        onClicked: root.valueModified(root.value - 1)
    }

    PlainText {
        width: 30
        height: root.height
        text: root.value
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    PillButton {
        size: "small"
        text: "+"
        Accessible.name: qsTr("Increase")
        enabled: root.value < root.to
        onClicked: root.valueModified(root.value + 1)
    }
}
