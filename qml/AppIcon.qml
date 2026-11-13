import QtQuick

Image {
    id: root

    required property string pkgName
    property int size: 56

    width: size
    height: size
    // Qt multiplies this by the scale factor of the screen when it asks the provider
    sourceSize: Qt.size(size, size)
    source: pkgName.length > 0 ? "image://pkgicon/" + pkgName : ""
    fillMode: Image.PreserveAspectFit
    smooth: true
    cache: true
}
