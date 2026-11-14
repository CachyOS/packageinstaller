import QtQuick
import QtQuick.Controls.Basic

// The CachyOS update notice that was not accepted yet. A long one scrolls.
Flickable {
    id: root

    property real maxHeight: 150

    visible: Backend.updateNotice.text.length > 0
    height: Math.min(box.implicitHeight, maxHeight)
    contentHeight: box.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}

    Notice {
        id: box
        width: root.width
        tone: Notice.Warning
        text: qsTr("Update notice: %1").arg(Backend.updateNotice.text)
    }
}
