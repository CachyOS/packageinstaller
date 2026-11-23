pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 8

    required property string origin

    signal back

    readonly property cpakItem app: lookup(origin, Backend.cpak.revision)
    readonly property bool known: app.origin.length > 0
    // An app that is not in the signed catalog is gone from cpak's lists once it was removed, and
    // nothing is left to show: go back to the list. (An app of the catalog stays and offers Install.)
    onKnownChanged: if (!known) back()
    // The Flatpak catalog's entry for an app from Flathub: name, description and screenshots.
    readonly property flatpakItem info: flatpakInfo(app.flatpakId, Backend.flatpak.revision)

    function flatpakInfo(id: string, revision: int): flatpakItem {
        return Backend.flatpak.app(id, "")
    }

    // The hero shows its description as rich text, so the permissions are a real list, and what
    // cpak says is escaped.
    function permissionsHtml(permissions: var): string {
        if (permissions.length === 0)
            return "";
        const escape = text => text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return "<p><b>" + escape(qsTr("Permissions it asks for")) + "</b></p><ul>"
            + permissions.map(permission => "<li>" + escape(permission) + "</li>").join("") + "</ul>";
    }

    // `revision` only makes the binding look the app up again after a reload.
    function lookup(origin: string, revision: int): cpakItem {
        return Backend.cpak.app(origin)
    }

    DetailsHero {
        width: parent.width
        iconName: root.app.flatpakId.length > 0 ? "flatpak/" + root.app.flatpakId : "cpak/" + root.origin
        title: !root.known ? root.origin : root.info.title.length > 0 ? root.info.title : root.app.title
        summary: root.app.summary.length > 0 ? root.app.summary : root.info.summary
        source: root.app.flatpakId.length > 0 ? "flatpak" : "cpak"
        byline: root.info.developer.length > 0 ? root.info.developer : root.origin
        screenshots: root.info.screenshots
        unavailable: root.known ? "" : qsTr("“%1” is not in the cpak catalogue and not installed.").arg(root.origin)
        description: root.info.description + root.permissionsHtml(root.app.permissions)
        onBack: root.back()

        PillButton {
            visible: root.known && !root.app.installed && root.app.installable
            implicitWidth: 150
            size: "large"
            text: qsTr("Install")
            onClicked: Backend.installCpak(root.origin)
        }

        PillButton {
            visible: root.app.upgradable
            implicitWidth: 150
            size: "large"
            text: qsTr("Update all")
            onClicked: Backend.updateCpaks()
        }

        PillButton {
            visible: root.app.installed
            implicitWidth: 150
            tone: "danger"
            text: qsTr("Remove")
            onClicked: Backend.removeCpak(root.origin)
        }
    }

    FactsCard {
        visible: root.known
        width: parent.width

        InfoItem {
            label: qsTr("Repository")
            value: root.origin
            // wide enough for "flatpak/flathub/io.appflowy.appflowy" on one line
            width: 360
        }
        InfoItem {
            label: qsTr("Version")
            value: Backend.cpak.shortVersion(root.app.version)
        }
        InfoItem {
            label: qsTr("Installed version")
            value: Backend.cpak.shortVersion(root.app.installedVersion)
        }
    }
}
