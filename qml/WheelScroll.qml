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
    /// touchpad fingers move the page this many times their own distance
    property real touchpadScale: 2.5
    /// how quickly touchpad momentum dies down, per second
    property real friction: 3
    /// fastest the page coasts, however many flings pile up
    property real maxVelocity: 15000
    /// a swipe at least this fast while coasting adds to the speed; a slower one stops the page
    property real refling: 1500

    // Touchpad state. Velocities are in contentY units per second.
    property bool swiping: false
    property real lastTime: 0
    // exponentially weighted displacement (px) and elapsed time (ms) of recent finger events
    property real swipeDist: 0
    property real swipeTime: 0
    property real coastVelocity: 0
    // coasting speed a same-direction swipe took over, given back if it ends in a fling
    property real carry: 0
    // the swipe began while the page was coasting
    property bool caught: false
    property real swipeStart: 0
    // the swipe has moved the page once already, so the next movement can be timed
    property bool timed: false
    // offset the coast last wrote; anything else moving the page stops the coast
    property real coastOffset: 0

    // the default is mice only; touchpads are handled here as well
    acceptedDevices: PointerDevice.AllDevices

    property NumberAnimation glide: NumberAnimation {
        target: root.view
        property: "contentY"
        duration: 140
        easing.type: Easing.OutCubic
    }

    // Momentum after the fingers lift. Wayland sends none, and Flickable.flick() uses
    // constant deceleration capped by maximumFlickVelocity, so the page coasts exponentially here.
    property FrameAnimation coast: FrameAnimation {
        onTriggered: {
            const view = root.view
            // a page covered by another, or a closed dialog, stays where it was left
            if (view === null || !view.visible || view.dragging || Math.abs(view.contentY - view.originY - root.coastOffset) > 0.5) {
                root.stopCoasting()
                return
            }
            // a stalled frame should not turn into one big jump
            const dt = Math.min(frameTime, 0.05)
            if (dt <= 0) {
                return
            }
            const nextVelocity = root.coastVelocity * Math.exp(-root.friction * dt)
            const step = root.friction > 0 ? (root.coastVelocity - nextVelocity) / root.friction : root.coastVelocity * dt
            const raw = view.contentY + step
            const y = root.clamp(raw)
            view.contentY = y
            root.coastOffset = view.contentY - view.originY
            root.coastVelocity = nextVelocity
            if (y !== raw || Math.abs(nextVelocity) < 20) {
                root.stopCoasting()
            }
        }
    }

    function startCoasting(velocity: real): void {
        glide.stop()
        coastVelocity = Math.max(-maxVelocity, Math.min(velocity, maxVelocity))
        coastOffset = view.contentY - view.originY
        coast.start()
    }

    function stopCoasting(): void {
        coast.stop()
        coastVelocity = 0
    }

    function endSwipe(): void {
        swiping = false
        swipeDist = 0
        swipeTime = 0
        carry = 0
        caught = false
        timed = false
    }

    function clamp(y: real): real {
        const top = view.originY
        return Math.max(top, Math.min(y, top + view.contentHeight - view.height))
    }

    onWheel: function (event: WheelEvent) {
        if (view === null) {
            return
        }
        // Lifting the fingers is handled before anything below can skip it,
        // so a swipe never stays open.
        if (event.phase === Qt.ScrollEnd) {
            // too few events, or the fingers came to rest first: no fling
            const now = Date.now()
            if (swiping && swipeTime > 0 && now - lastTime < 50 && view.contentHeight > view.height) {
                // a short window would make a few bunched-up events look like a fling
                const v = swipeDist / (Math.max(swipeTime, 15) / 1000)
                // A real fling in the direction it was already coasting adds to that speed.
                // A slow one is how the fingers catch the page, since Wayland reports no
                // touch-down (Qt does not support the hold gesture), so it must not restart it.
                // Only a quick swipe counts as either: after a longer one the old coast is history.
                const quick = now - swipeStart <= 250
                const fast = Math.abs(v) > refling
                const velocity = quick && v * carry > 0 && fast ? v + carry : v
                if (Math.abs(velocity) > 300 && (fast || !caught || !quick)) {
                    startCoasting(velocity)
                }
            }
            endSwipe()
            return
        }
        if (view.contentHeight <= view.height) {
            return
        }
        // sideways movement belongs to whatever is under the pointer
        if ((event.modifiers & Qt.ShiftModifier) || Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y)) {
            return
        }
        // OS-synthesized momentum: follow it directly without scaling or starting a second coast.
        if (event.phase === Qt.ScrollMomentum) {
            stopCoasting()
            endSwipe()
            if (event.pixelDelta.y !== 0) {
                glide.stop()
                view.contentY = clamp(view.contentY - event.pixelDelta.y)
            }
            return
        }
        // Fingers on a touchpad report exact distances and a scroll phase; follow them.
        // A wheel can carry a pixel distance as well (Wayland does that), so that alone says nothing.
        if (event.phase === Qt.ScrollBegin || event.phase === Qt.ScrollUpdate) {
            const dy = -event.pixelDelta.y * touchpadScale
            if (dy === 0 && event.phase !== Qt.ScrollBegin) {
                return
            }
            glide.stop()
            const now = Date.now()
            // Qt does not always send ScrollBegin; the first movement after a lift starts a swipe,
            // and so does one after a long gap, in case the lift never arrived.
            const starting = event.phase === Qt.ScrollBegin || !swiping || now - lastTime > 100
            if (starting) {
                endSwipe()
                swiping = true
                swipeStart = now
                // The fingers take over from the coast. Its speed is kept for a fling the
                // same way; a swipe against it just stops it.
                caught = coast.running && Math.abs(coastVelocity) > 300
                carry = caught && (dy === 0 || dy * coastVelocity > 0) ? coastVelocity : 0
                stopCoasting()
            } else if (dy * carry < 0) {
                // turned around partway through
                carry = 0
            }
            if (dy !== 0) {
                view.contentY = clamp(view.contentY + dy)
                // The first movement has nothing before it to time it against, so it gives no
                // speed. That is not always the first event: Wayland opens with an empty ScrollBegin.
                if (timed) {
                    const dt = Math.min(Math.max(now - lastTime, 0), 100)
                    const decay = Math.exp(-dt / 40)
                    swipeDist = swipeDist * decay + dy
                    swipeTime = swipeTime * decay + dt
                }
                timed = true
            }
            lastTime = now
            return
        }
        if (event.phase !== Qt.NoScrollPhase) {
            return
        }
        stopCoasting()
        endSwipe()
        const step = event.angleDelta.y / 120 * Application.styleHints.wheelScrollLines * lineStep
        destination = clamp((glide.running ? destination : view.contentY) - step)
        glide.to = destination
        glide.restart()
    }
}
