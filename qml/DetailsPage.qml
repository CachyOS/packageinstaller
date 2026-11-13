pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 8

    required property string pkgName

    signal back
    signal openPackage(string name)
    signal showUpdates

    readonly property packageInfo pkg: lookup(pkgName, Backend.revision)
    readonly property bool known: pkg.name.length > 0

    // `revision` only makes the binding look the package up again after a reload.
    function lookup(name: string, revision: int): packageInfo {
        return Backend.package(name)
    }

    function size(bytes: real): string {
        return bytes > 0 ? Look.formatSize(bytes) : ""
    }

    DetailsHero {
        width: parent.width
        iconName: root.pkgName
        title: root.pkg.title
        summary: root.pkg.summary
        source: root.pkg.source
        byline: root.pkg.developer.length > 0 ? root.pkg.developer : root.pkgName
        unavailable: root.known ? "" : qsTr("“%1” is not available in the repositories.").arg(root.pkgName)
        screenshots: root.pkg.screenshots
        description: root.pkg.longDescription.length > 0 ? root.pkg.longDescription : root.pkg.description
        onBack: root.back()

        PillButton {
            visible: !root.pkg.installed
            implicitWidth: 150
            size: "large"
            text: qsTr("Install")
            onClicked: Backend.install(root.pkgName)
        }

        PillButton {
            visible: root.pkg.installed && root.pkg.upgradable
            implicitWidth: 150
            size: "large"
            text: qsTr("Update system")
            onClicked: root.showUpdates()
        }

        PillButton {
            visible: root.pkg.installed
            implicitWidth: 150
            tone: "danger"
            text: qsTr("Remove")
            onClicked: Backend.remove(root.pkgName)
        }
    }

    FactsCard {
        visible: root.known
        width: parent.width

        InfoItem {
            label: qsTr("Package")
            value: root.pkgName
        }
        InfoItem {
            label: qsTr("Version")
            value: root.pkg.version
        }
        InfoItem {
            label: qsTr("Installed version")
            value: root.pkg.installedVersion
        }
        InfoItem {
            label: qsTr("Repository")
            value: root.pkg.repo
        }
        InfoItem {
            label: qsTr("Download size")
            value: root.size(root.pkg.downloadSize)
        }
        InfoItem {
            label: qsTr("Installed size")
            value: root.size(root.pkg.installSize)
        }
        InfoItem {
            label: qsTr("License")
            value: root.pkg.licenses.join(", ")
        }
        InfoItem {
            label: qsTr("Website")
            value: root.pkg.url
            link: true
        }
    }

    Column {
        visible: root.pkg.depends.length > 0
        width: parent.width
        spacing: 10

        SectionTitle {
            text: qsTr("Depends on")
        }

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.pkg.depends

                Chip {
                    required property string modelData

                    small: true
                    text: modelData
                    onClicked: root.openPackage(modelData)
                }
            }
        }
    }
}
