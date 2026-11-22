pragma ComponentBehavior: Bound
import QtQuick
import CachyOS.PackageInstaller

ScrollPage {
    id: root

    inset: Look.pagePadding - 8

    required property string name

    signal back

    readonly property snapItem app: lookup(name, Backend.snap.revision)
    readonly property bool known: app.name.length > 0
    /// The store can be browsed without snapd; installing a snap cannot.
    readonly property bool needsSnapd: !Backend.snap.available || !Backend.snap.daemonRunning

    // `revision` only makes the binding look the app up again after a reload.
    function lookup(name: string, revision: int): snapItem {
        return Backend.snap.app(name)
    }

    DetailsHero {
        width: parent.width
        iconName: "snap/" + root.name
        title: root.app.name
        summary: root.app.summary
        source: "snap"
        byline: root.app.publisher.length > 0 ? root.app.publisher : root.name
        unavailable: root.known ? "" : qsTr("“%1” is not available as a snap.").arg(root.name)
        onBack: root.back()

        PillButton {
            visible: !root.app.installed && root.known
            enabled: !root.needsSnapd
            implicitWidth: 150
            size: "large"
            text: qsTr("Install")
            onClicked: Backend.installSnap(root.name)
        }

        PillButton {
            visible: root.app.installed
            implicitWidth: 150
            tone: "danger"
            text: qsTr("Remove")
            onClicked: Backend.removeSnap(root.name)
        }
    }

    FactsCard {
        visible: root.known
        width: parent.width

        InfoItem {
            label: qsTr("Name")
            value: root.app.name
        }
        InfoItem {
            label: qsTr("Version")
            value: root.app.version
        }
        InfoItem {
            label: qsTr("Revision")
            value: root.app.revision
        }
        InfoItem {
            label: qsTr("Channel")
            value: root.app.channel
        }
        InfoItem {
            label: qsTr("Publisher")
            value: root.app.publisher
        }
        InfoItem {
            label: qsTr("Notes")
            value: root.app.notes
        }
    }
}
