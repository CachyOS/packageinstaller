pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Read-only script listing with line numbers. Shown at full height: a nested vertical
// scroll area would swallow the wheel and make it easy to skip the end of the script.
Rectangle {
    id: root

    property string text
    // Lines to highlight: [{line, severity}]
    property var marks: []
    property string accessibleName
    property int minimumHeight: 0

    readonly property real lineHeight: edit.lineCount > 0 ? edit.contentHeight / edit.lineCount : 0
    readonly property int padding: 14

    function lineY(line) {
        return padding + (line - 1) * lineHeight
    }

    function markColor(severity) {
        return severity === "danger" ? Look.dangerSoft : severity === "warning" ? Look.warningSoft : Look.archSoft
    }

    function inkColor(severity) {
        return severity === "danger" ? Look.danger : severity === "warning" ? Look.warning : Look.arch
    }

    height: Math.max(minimumHeight, edit.implicitHeight + 2)
    radius: Look.radius
    color: Look.surface
    border.width: 1
    border.color: Look.border
    clip: true

    Repeater {
        model: root.text.length > 0 ? root.marks : []

        Rectangle {
            required property var modelData

            x: 1
            y: root.lineY(modelData.line)
            width: root.width - 2
            height: root.lineHeight
            color: root.markColor(modelData.severity)

            Rectangle {
                width: 3
                height: parent.height
                color: root.inkColor(parent.modelData.severity)
            }
        }
    }

    Text {
        id: gutter
        textFormat: Text.PlainText
        x: 1
        width: implicitWidth
        visible: root.text.length > 0
        text: Array.from({ length: edit.lineCount }, (_, i) => i + 1).join("\n")
        color: Look.textMuted
        opacity: 0.7
        horizontalAlignment: Text.AlignRight
        font: edit.font
        topPadding: root.padding
        bottomPadding: root.padding
        leftPadding: 12
        rightPadding: 4
    }

    // Not a Flickable: that would take slightly diagonal touchpad scrolling as sideways
    // movement and hold back the page. Only a clearly sideways gesture moves the text.
    Item {
        id: viewport

        property real offset: 0
        readonly property real range: Math.max(0, edit.implicitWidth - width)

        function scrollBy(distance) {
            offset = Math.max(0, Math.min(offset + distance, range))
        }

        anchors.fill: parent
        anchors.margins: 1
        anchors.leftMargin: gutter.width + 1
        clip: true
        onRangeChanged: scrollBy(0)

        WheelHandler {
            // the default is mice only; a wheel can arrive as a touchpad (Wayland)
            acceptedDevices: PointerDevice.AllDevices
            blocking: false
            onWheel: event => {
                const fingers = event.phase !== Qt.NoScrollPhase
                const dx = event.angleDelta.x
                const dy = event.angleDelta.y
                if (event.modifiers & Qt.ShiftModifier) {
                    // Shift turns the wheel sideways; some platforms swap the axes themselves
                    const pixels = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.pixelDelta.y
                    viewport.scrollBy(fingers && pixels !== 0 ? -pixels : -(dx !== 0 ? dx : dy) / 120 * 80)
                } else if (Math.abs(dx) > Math.abs(dy)) {
                    viewport.scrollBy(fingers && event.pixelDelta.x !== 0 ? -event.pixelDelta.x : -dx / 120 * 80)
                }
            }
        }

        TextEdit {
            id: edit
            x: -viewport.offset
            readOnly: true
            selectByMouse: true
            text: root.text
            color: Look.text
            selectionColor: Look.accent
            selectedTextColor: Look.onAccent
            font.family: "monospace"
            font.pixelSize: Look.fontSmall + 1
            padding: root.padding
            leftPadding: 10
            Accessible.name: root.accessibleName
        }

        ScrollBar {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            orientation: Qt.Horizontal
            visible: viewport.range > 0
            size: viewport.width / Math.max(viewport.width, edit.implicitWidth)
            position: viewport.offset / Math.max(1, edit.implicitWidth)
            onPositionChanged: if (pressed) viewport.offset = Math.max(0, Math.min(position * edit.implicitWidth, viewport.range))
        }
    }
}
