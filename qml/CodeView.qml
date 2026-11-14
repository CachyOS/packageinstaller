pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Read-only script listing with line numbers. Shown at full height: a nested vertical
// scroll area would swallow the wheel and make it easy to skip the end of the script.
// With `diff` it shows changes instead, from pkgscan::readableDiff(): added and removed
// lines are tinted, and the line numbers, which would mean nothing, are left out.
Card {
    id: root

    property string text
    property list<finding> marks
    property string accessibleName
    property int minimumHeight: 0
    property bool diff: false
    // runs of lines of one kind, so that a long change needs only a few tints
    readonly property var diffRuns: diff ? runs(text) : []

    readonly property real lineHeight: edit.lineCount > 0 ? edit.contentHeight / edit.lineCount : 0
    readonly property int padding: 14

    function lineY(line: int): real {
        return padding + (line - 1) * lineHeight
    }

    // "add", "remove", "hunk" or "file" for the line; context lines get no tint
    function kindOf(line: string): string {
        const first = line.charAt(0)
        return first === "+" ? "add" : first === "-" ? "remove" : first === "@" ? "hunk"
            : first === " " || first === "\\" || first === "" ? "" : "file"
    }

    function runs(text: string): var {
        const found = []
        const lines = text.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            const kind = kindOf(lines[i])
            const last = found.length > 0 ? found[found.length - 1] : null
            if (kind === "") {
                continue
            } else if (last !== null && last.kind === kind && last.line + last.count === i + 1) {
                last.count += 1
            } else {
                found.push({"line": i + 1, "count": 1, "kind": kind})
            }
        }
        return found
    }

    function lineNumbers(count: int): string {
        let numbers = ""
        for (let i = 1; i <= count; ++i) {
            numbers += i > 1 ? "\n" + i : "1"
        }
        return numbers
    }

    height: Math.max(minimumHeight, edit.implicitHeight + 2)

    Repeater {
        model: root.diffRuns

        Rectangle {
            required property var modelData

            x: 1
            y: root.lineY(modelData.line)
            width: root.width - 2
            height: modelData.count * root.lineHeight
            color: modelData.kind === "add" ? Look.accentSoft : modelData.kind === "remove" ? Look.dangerSoft
                : modelData.kind === "file" ? Look.archSoft : Look.surfaceAlt
        }
    }

    Repeater {
        model: root.marks

        Rectangle {
            id: mark

            required property finding modelData

            visible: root.text.length > 0
            x: 1
            y: root.lineY(modelData.line)
            width: root.width - 2
            height: root.lineHeight
            color: Look.severityColor(modelData.severity, true)

            Rectangle {
                width: 3
                height: mark.height
                color: Look.severityColor(mark.modelData.severity, false)
            }
        }
    }

    PlainText {
        id: gutter
        x: 1
        width: root.diff ? 0 : implicitWidth
        visible: root.text.length > 0 && !root.diff
        text: root.lineNumbers(edit.lineCount)
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

        function scrollBy(distance: real) {
            offset = Math.max(0, Math.min(offset + distance, range))
        }

        anchors.fill: parent
        anchors.margins: 1
        anchors.leftMargin: gutter.width + 1
        clip: true
        onRangeChanged: scrollBy(0)

        // A wheel handler only sees movement along its own axis, so there is one for each:
        // sideways gestures move the text, and Shift turns an ordinary wheel sideways.
        WheelHandler {
            acceptedDevices: PointerDevice.AllDevices  // the default is mice only
            orientation: Qt.Horizontal
            blocking: false
            onWheel: event => {
                if (Math.abs(event.angleDelta.x) <= Math.abs(event.angleDelta.y)) {
                    return
                }
                const fingers = event.phase !== Qt.NoScrollPhase && event.pixelDelta.x !== 0
                viewport.scrollBy(fingers ? -event.pixelDelta.x : -event.angleDelta.x / 120 * 80)
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.AllDevices
            acceptedModifiers: Qt.ShiftModifier
            blocking: false
            onWheel: event => {
                // some platforms swap the axes themselves while Shift is held
                const angle = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                const pixels = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.pixelDelta.x
                const fingers = event.phase !== Qt.NoScrollPhase && pixels !== 0
                viewport.scrollBy(fingers ? -pixels : -angle / 120 * 80)
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
            font.pixelSize: Look.fontLabel
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
