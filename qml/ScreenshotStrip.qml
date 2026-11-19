pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ListView {
    id: root

    property list<string> urls

    model: urls
    visible: count > 0
    height: 280
    orientation: ListView.Horizontal
    spacing: 14
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    // a click on a screenshot, or Enter on the one with the focus, shows it large
    delegate: AbstractButton {
        id: frame

        required property string modelData
        required property int index

        height: root.height
        width: shot.status === Image.Ready ? Math.min(560, shot.implicitWidth * height / Math.max(1, shot.implicitHeight)) : 440
        padding: 1
        Accessible.name: qsTr("Screenshot %1 of %2").arg(index + 1).arg(root.count)
        onClicked: {
            viewer.active = true
            const shown = viewer.item as ScreenshotViewer
            shown.show(index)
        }
        // buttons take Space; Enter is what most people press to open something
        Keys.onReturnPressed: click()
        Keys.onEnterPressed: click()

        background: Card {
            hovered: frame.hovered
            focused: frame.visualFocus
        }

        contentItem: Image {
            id: shot
            source: frame.modelData
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.height: 560

            BusyIndicator {
                anchors.centerIn: parent
                running: shot.status === Image.Loading
                visible: running
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    // made on the first click: most details pages are left without opening a screenshot
    Loader {
        id: viewer
        active: false
        sourceComponent: ScreenshotViewer {
            urls: root.urls
        }
    }
}
