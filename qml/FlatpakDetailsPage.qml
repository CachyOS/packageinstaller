pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 8

    required property string appId
    // The installed copy to show; empty for the one that matches the catalogue.
    property string copy

    signal back

    readonly property flatpakItem app: lookup(appId, copy, Backend.flatpak.revision)
    readonly property bool known: app.id.length > 0

    // `revision` only makes the binding look the app up again after a reload.
    function lookup(id: string, key: string, revision: int): flatpakItem {
        return Backend.flatpak.app(id, key)
    }

    DetailsHero {
        width: parent.width
        iconName: "flatpak/" + root.appId
        title: root.app.title
        summary: root.app.summary
        source: "flatpak"
        byline: root.app.developer.length > 0 ? root.app.developer : root.appId
        unavailable: root.known ? "" : qsTr("“%1” is not available as a Flatpak.").arg(root.appId)
        screenshots: root.app.screenshots
        description: root.app.description
        onBack: root.back()

        PillButton {
            visible: !root.app.installed && root.app.available
            implicitWidth: 150
            size: "large"
            text: qsTr("Install")
            onClicked: Backend.installFlatpak(root.appId)
        }

        PillButton {
            visible: root.app.upgradable
            implicitWidth: 150
            size: "large"
            text: qsTr("Update all")
            onClicked: Backend.updateFlatpaks()
        }

        PillButton {
            visible: root.app.installed
            implicitWidth: 150
            tone: "danger"
            text: qsTr("Remove")
            onClicked: Backend.removeFlatpak(root.app.key)
        }
    }

    FactsCard {
        visible: root.known
        width: parent.width

        InfoItem {
            label: qsTr("App ID")
            value: root.appId
        }
        InfoItem {
            label: qsTr("Version")
            value: root.app.version
        }
        InfoItem {
            label: qsTr("Installed version")
            value: root.app.installedVersion
        }
        InfoItem {
            label: qsTr("Source")
            value: root.app.installed ? root.app.installedFrom : root.app.remote
        }
        InfoItem {
            label: qsTr("Installed size")
            value: root.app.size
        }
        InfoItem {
            label: qsTr("Installed for")
            value: root.app.installation === "user" ? qsTr("Your user only")
                : root.app.installation === "system" ? qsTr("All users") : root.app.installation
        }
        InfoItem {
            label: qsTr("Branch")
            value: root.app.branch
        }
        InfoItem {
            label: qsTr("License")
            value: root.app.license
        }
        InfoItem {
            label: qsTr("Website")
            value: root.app.homepage
            link: true
        }
    }
}
