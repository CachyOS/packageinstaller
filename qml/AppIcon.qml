import QtQuick

Image {
    id: root

    required property string pkgName
    property int size: 56

    width: size
    height: size
    sourceSize: Qt.size(size * 2, size * 2)
    source: pkgName.length > 0 ? "image://pkgicon/" + pkgName : ""
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
    cache: true
}
