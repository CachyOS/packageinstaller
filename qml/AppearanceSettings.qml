import QtCore

// Persisted as appearance/theme and appearance/mode.
Settings {
    category: "appearance"
    property string theme: "classic"
    /// "system", "light" or "dark"
    property string mode: "system"
}
