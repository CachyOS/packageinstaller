import QtQuick

Rectangle {
    property bool hovered: false
    property bool focused: false

    radius: Look.radius
    color: hovered ? Look.surfaceHover : Look.surface
    border.width: focused ? 2 : 1
    border.color: focused || hovered ? Look.accent : Look.border
}
