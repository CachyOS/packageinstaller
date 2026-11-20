pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// One screenshot as large as the window allows, never larger than the image itself, with
// the others of the app a click or an arrow key away. Esc or a click beside it closes.
Popup {
    id: root

    property list<string> urls
    property int index: 0
    // only while open: a closed viewer holds no full-size image
    readonly property string current: visible && index < urls.length ? urls[index] : ""

    function show(at: int): void {
        index = at
        justOpened.restart()
        open()
    }

    function step(by: int): void {
        index = Math.max(0, Math.min(urls.length - 1, index + by))
    }

    // the whole window, sidebar included; a page's viewer exists before there is an overlay
    parent: Overlay.overlay
    width: parent?.width ?? 0
    height: parent?.height ?? 0
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    // The viewer covers the window, so its own background is the backdrop: dark in every
    // theme, as a screenshot is easiest to look at on black.
    Overlay.modal: Item {}
    background: Rectangle {
        color: "#db000000"
    }

    // a round button with a line icon, drawn light on the dark backdrop
    component ViewerButton: AbstractButton {
        id: button

        property string glyph

        width: 44
        height: 44

        contentItem: Glyph {
            path: button.glyph
            color: "white"
            size: 22
            strokeWidth: 2.2
        }
        background: Rectangle {
            radius: width / 2
            color: button.hovered ? "#3dffffff" : "#1fffffff"
            border.width: button.visualFocus ? 2 : 0
            border.color: "white"
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    contentItem: Item {
        id: stage

        focus: true
        Keys.onLeftPressed: root.step(-1)
        Keys.onRightPressed: root.step(1)
        // step() stops at the ends
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                root.step(event.key === Qt.Key_Home ? -root.urls.length : root.urls.length)
                event.accepted = true
            }
        }

        // A click next to the image closes; underneath everything, so the buttons get theirs.
        // Not the second click of the double-click that opened the viewer.
        MouseArea {
            anchors.fill: stage
            onClicked: if (!justOpened.running) root.close()
        }

        Timer {
            id: justOpened
            interval: Application.styleHints.mouseDoubleClickInterval
        }

        Image {
            id: shot

            // the share of its size that fits beside the buttons, never more than all of it
            readonly property real fit: Math.max(0, Math.min(1, (stage.width - 2 * 76) / Math.max(1, implicitWidth),
                (stage.height - 2 * 64) / Math.max(1, implicitHeight)))

            anchors.centerIn: stage
            width: implicitWidth * fit
            height: implicitHeight * fit
            source: root.current
            // an original upload can be several thousand pixels wide; larger ones are only
            // shrunk (with a fill mode Qt would scale smaller ones up to this as well)
            sourceSize: Qt.size(2560, 2560)
            asynchronous: true

            // a click on the image itself keeps it open
            MouseArea {
                anchors.fill: shot
            }
        }

        BusyIndicator {
            anchors.centerIn: stage
            running: shot.status === Image.Loading
            visible: running
        }

        PlainText {
            anchors.centerIn: stage
            visible: shot.status === Image.Error
            text: qsTr("The screenshot could not be loaded.")
            color: "white"
        }

        ViewerButton {
            anchors.left: stage.left
            anchors.leftMargin: 16
            anchors.verticalCenter: stage.verticalCenter
            // stays at the end, dimmed, so that a click there does not fall through and close
            visible: root.urls.length > 1
            opacity: root.index > 0 ? 1 : 0.35
            text: qsTr("Previous screenshot")
            glyph: "M15 5l-7 7l7 7"
            onClicked: root.step(-1)
        }

        ViewerButton {
            anchors.right: stage.right
            anchors.rightMargin: 16
            anchors.verticalCenter: stage.verticalCenter
            visible: root.urls.length > 1
            opacity: root.index < root.urls.length - 1 ? 1 : 0.35
            text: qsTr("Next screenshot")
            glyph: "M9 5l7 7l-7 7"
            onClicked: root.step(1)
        }

        ViewerButton {
            anchors.right: stage.right
            anchors.top: stage.top
            anchors.margins: 16
            text: qsTr("Close")
            glyph: "M6 6l12 12M18 6L6 18"
            onClicked: root.close()
        }

        PlainText {
            anchors.horizontalCenter: stage.horizontalCenter
            anchors.bottom: stage.bottom
            anchors.bottomMargin: 22
            visible: root.urls.length > 1
            text: qsTr("%1 of %2").arg(root.index + 1).arg(root.urls.length)
            color: "white"
            font.pixelSize: Look.fontLabel
        }
    }
}
