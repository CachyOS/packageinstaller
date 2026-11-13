import QtQuick
import QtQuick.Controls.Basic

Flickable {
    id: root

    default property alias content: column.data
    property alias spacing: column.spacing
    /// distance of the first item from the top
    property real inset: Look.pagePadding

    contentWidth: width
    contentHeight: column.height + inset + Look.pagePadding
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    WheelScroll {}

    Column {
        id: column
        x: Look.pagePadding
        y: root.inset
        width: root.width - 2 * Look.pagePadding
        spacing: 22
    }
}
