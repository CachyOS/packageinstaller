import QtQuick
import QtQuick.Controls.Basic

ListView {
    readonly property real rowWidth: width - Look.gutter

    x: Look.pagePadding
    width: (parent?.width ?? 0) - 2 * Look.pagePadding
    spacing: 8
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true
    clip: true

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}
}
