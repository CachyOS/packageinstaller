import QtQuick
import QtQuick.Controls.Basic

// An on/off switch that shows a setting instead of keeping a state of its own:
// bind `active` and change the setting in onClicked.
AbstractButton {
    id: root

    required property bool active
    required property string name

    width: 46
    height: 26
    Accessible.role: Accessible.CheckBox
    Accessible.name: name
    Accessible.checkable: true
    Accessible.checked: active

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        id: track
        radius: height / 2
        color: root.active ? Look.accent : Look.surfaceAlt
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? Look.text : (root.active ? Look.accent : Look.border)

        Behavior on color {
            ColorAnimation { duration: 120 }
        }

        Rectangle {
            x: root.active ? track.width - width - 4 : 4
            anchors.verticalCenter: track.verticalCenter
            width: 18
            height: 18
            radius: height / 2
            color: root.active ? Look.onAccent : Look.textMuted

            Behavior on x {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }
}
