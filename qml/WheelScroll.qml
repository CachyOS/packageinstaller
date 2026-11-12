import QtQuick

// Wheel scrolling for a page. Flickable's own wheel handling is tuned for touch: a notch
// moves only a little and eases in, which feels slow on long pages. Declare this inside
// the Flickable (or ListView, GridView) it should scroll.
WheelHandler {
    id: root

    // declared inside a Flickable, a handler ends up on its content item
    readonly property Flickable view: (parent as Flickable) ?? (parent?.parent as Flickable)
    /// distance for one line; a notch moves `wheelScrollLines` of them
    property real lineStep: 40
    property real destination: 0

    // the default is mice only; touchpads are handled here as well
    acceptedDevices: PointerDevice.AllDevices

    property NumberAnimation glide: NumberAnimation {
        target: root.view
        property: "contentY"
        duration: 140
        easing.type: Easing.OutCubic
    }

    function clamp(y) {
        const top = view.originY
        return Math.max(top, Math.min(y, top + view.contentHeight - view.height))
    }

    onWheel: event => {
        if (view === null || view.contentHeight <= view.height) {
            return
        }
        // sideways movement belongs to whatever is under the pointer
        if ((event.modifiers & Qt.ShiftModifier) || Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y)) {
            return
        }
        // Fingers on a touchpad report exact distances and a scroll phase; follow them.
        // A wheel can carry a pixel distance as well (Wayland does that), so that alone says nothing.
        if (event.pixelDelta.y !== 0 && event.phase !== Qt.NoScrollPhase) {
            glide.stop()
            view.contentY = clamp(view.contentY - event.pixelDelta.y)
            return
        }
        const step = event.angleDelta.y / 120 * Application.styleHints.wheelScrollLines * lineStep
        destination = clamp((glide.running ? destination : view.contentY) - step)
        glide.to = destination
        glide.restart()
    }
}
