import QtCore

// Persisted as appearance/theme, appearance/mode and appearance/startMaximized.
Settings {
    category: "appearance"
    property string theme: "classic"
    /// "system", "light" or "dark"
    property string mode: "system"
    /// Open the window maximized on the next start.
    property bool startMaximized: false
}
