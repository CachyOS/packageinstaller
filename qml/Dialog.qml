import QtQuick
import QtQuick.Controls.Basic

Popup {
    /// how wide the dialog gets in a large window
    property real maxWidth: 480

    modal: true
    // takes the keyboard while open, so that Esc and Tab reach the dialog and not the page below
    focus: true
    anchors.centerIn: Overlay.overlay
    // a dialog on a page is created before the page is in a window, when there is no overlay yet
    width: Math.min((Overlay.overlay?.width ?? Infinity) - 80, maxWidth)
    padding: 26

    Overlay.modal: Rectangle {
        color: Look.scrim
    }

    background: Rectangle {
        radius: Look.radiusLarge
        color: Look.surface
        border.width: 1
        border.color: Look.border
    }
}
