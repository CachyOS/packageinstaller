import QtCore

// Persisted as appearance/theme, appearance/mode, appearance/translucent and
// appearance/backgroundOpacity. main.cpp reads appearance/translucent before the window exists.
Settings {
    category: "appearance"
    property string theme: "classic"
    /// "system", "light" or "dark"
    property string mode: "system"
    /// whether the desktop shows through the window background
    property bool translucent: false
    /// how opaque the background is while translucent, from 0.3 to 1
    property real backgroundOpacity: 0.85
}
