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

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 1
        anchors.leftMargin: gutter.width + 1
        clip: true
        contentWidth: edit.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.horizontal: ScrollBar {}

        TextEdit {
            id: edit
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
    }
}
