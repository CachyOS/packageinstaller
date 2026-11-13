import QtQuick
import QtQuick.Controls.Basic

Popup {
    modal: true
    anchors.centerIn: Overlay.overlay
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
