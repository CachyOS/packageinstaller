pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ListView {
    id: root

    visible: count > 0
    height: 280
    orientation: ListView.Horizontal
    spacing: 14
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    delegate: Card {
        id: frame

        required property string modelData

        height: root.height
        width: shot.status === Image.Ready ? Math.min(560, shot.implicitWidth * height / Math.max(1, shot.implicitHeight)) : 440

        Image {
            id: shot
            anchors.fill: frame
            anchors.margins: 1
            source: frame.modelData
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.height: 560
        }

        BusyIndicator {
            anchors.centerIn: frame
            running: shot.status === Image.Loading
            visible: running
        }
    }
}
