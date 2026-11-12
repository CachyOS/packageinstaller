pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Popup {
    id: root

    readonly property Transaction tx: Backend.transaction
    readonly property int kind: tx.kind
    property bool showLog: false

    readonly property string heading: {
        const target = tx.targets.length > 0 ? tx.targets[0] : "";
        switch (kind) {
        case Transaction.Install: return qsTr("Install %1").arg(target);
        case Transaction.Remove: return qsTr("Remove %1").arg(target);
        case Transaction.Upgrade: return qsTr("Update the system");
        default: return tx.targets.length > 1 ? qsTr("Build %n package(s) from the AUR", "", tx.targets.length)
            : qsTr("Build %1 from the AUR").arg(target);
        }
    }

    visible: tx.state !== Transaction.Idle
    onVisibleChanged: if (visible) showLog = false
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 80 : 640, 640)
    padding: 26

    Overlay.modal: Rectangle {
        color: "#99000000"
    }

    background: Rectangle {
        radius: Look.radius + 4
        color: Look.surface
        border.width: 1
        border.color: Look.border
    }

    contentItem: Column {
        spacing: 16

        Text {
            width: parent.width
            text: root.heading
            color: Look.text
            font.pixelSize: Look.fontHeading
            font.weight: Font.Bold
            elide: Text.ElideRight
        }

        // Planning
        Row {
            visible: root.tx.state === Transaction.Planning
            spacing: 12

            BusyIndicator {
                width: 28
                height: 28
                running: parent.visible
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Working out what will change…")
                color: Look.textMuted
                font.pixelSize: Look.fontBody
            }
        }

        // Confirm
        Column {
            visible: root.tx.state === Transaction.Confirm
            width: parent.width
            spacing: 12

            // pacman can be verbose; never let the message push the buttons off the window
            Flickable {
                visible: root.tx.planError.length > 0
                width: parent.width
                height: Math.min(planErrorText.implicitHeight, 220)
                contentHeight: planErrorText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {}

                Text {
                    id: planErrorText
                    width: parent.width - 12
                    text: root.tx.planError
                    color: Look.danger
                    font.pixelSize: Look.fontBody
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                visible: root.tx.planError.length === 0
                width: parent.width
                text: root.kind === Transaction.Upgrade ? qsTr("The package databases will be refreshed and every available update installed.")
                    : root.kind === Transaction.AurInstall ? (root.tx.targets.length > 1
                        ? qsTr("paru will download the sources, build %1 as your user and then ask for permission to install them. This can take a while.").arg(root.tx.targets.join(", "))
                        : qsTr("paru will download the sources, build the package as your user and then ask for permission to install it. This can take a while."))
                    : root.kind === Transaction.Remove ? qsTr("%n package(s) will be removed:", "", root.tx.plan.length)
                    : qsTr("%n package(s) will be installed (%1 to download):", "", root.tx.plan.length).arg(Backend.formatSize(root.tx.downloadSize))
                color: Look.textMuted
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            ListView {
                visible: count > 0
                width: parent.width
                height: Math.min(contentHeight, 260)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.tx.plan

                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    required property var modelData

                    width: ListView.view.width
                    height: 30

                    Text {
                        anchors.left: parent.left
                        anchors.right: versionText.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.name
                        color: Look.text
                        font.pixelSize: Look.fontBody
                        font.weight: parent.modelData.requested ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    Text {
                        id: versionText
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.version
                        color: Look.textMuted
                        font.pixelSize: Look.fontSmall + 1
                    }
                }
            }
        }

        // Running / finished
        Column {
            visible: root.tx.state === Transaction.Running || root.tx.state === Transaction.Finished
            width: parent.width
            spacing: 12

            ProgressBar {
                id: progress
                visible: root.tx.state === Transaction.Running
                width: parent.width
                indeterminate: true

                background: Rectangle {
                    implicitHeight: 6
                    radius: 3
                    color: Look.surfaceAlt
                }

                contentItem: Item {
                    implicitHeight: 6
                    clip: true

                    Rectangle {
                        width: parent.width * 0.3
                        height: parent.height
                        radius: 3
                        color: Look.accent

                        XAnimator on x {
                            from: -progress.width * 0.3
                            to: progress.width
                            duration: 1400
                            loops: Animation.Infinite
                            running: progress.visible
                        }
                    }
                }
            }

            Text {
                visible: root.tx.state === Transaction.Finished
                width: parent.width
                text: root.tx.success ? qsTr("Done.")
                    : root.tx.failureReason.length > 0 ? root.tx.failureReason : qsTr("The operation did not complete.")
                wrapMode: Text.WordWrap
                color: root.tx.success ? Look.accent : Look.danger
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                visible: root.tx.state === Transaction.Running
                width: parent.width
                text: root.tx.lastLine
                color: Look.textMuted
                font.pixelSize: Look.fontSmall + 1
                elide: Text.ElideRight
            }

            Rectangle {
                // open by default only when the log is the sole explanation of a failure
                visible: root.showLog || (root.tx.state === Transaction.Finished && !root.tx.success && root.tx.failureReason.length === 0)
                width: parent.width
                height: 240
                radius: Look.radiusSmall
                color: Look.window
                border.width: 1
                border.color: Look.border

                ScrollView {
                    id: logScroll
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true

                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        text: root.tx.log
                        color: Look.text
                        font.family: "monospace"
                        font.pixelSize: Look.fontSmall
                        wrapMode: TextEdit.WrapAnywhere
                        padding: 10
                        background: null
                        onTextChanged: cursorPosition = length
                    }
                }
            }
        }

        // Buttons
        Row {
            anchors.right: parent.right
            spacing: 10

            PillButton {
                visible: root.tx.state === Transaction.Running
                    || (root.tx.state === Transaction.Finished && (root.tx.success || root.tx.failureReason.length > 0))
                tone: "soft"
                text: root.showLog ? qsTr("Hide details") : qsTr("Show details")
                onClicked: root.showLog = !root.showLog
            }

            PillButton {
                visible: root.tx.state === Transaction.Planning || root.tx.state === Transaction.Confirm || root.tx.cancellable
                tone: "soft"
                text: root.tx.state === Transaction.Running ? qsTr("Stop build") : qsTr("Cancel")
                onClicked: root.tx.cancel()
            }

            PillButton {
                visible: root.tx.state === Transaction.Confirm && root.tx.planError.length === 0
                tone: root.kind === Transaction.Remove ? "danger" : "primary"
                text: root.kind === Transaction.Remove ? qsTr("Remove") : root.kind === Transaction.Upgrade ? qsTr("Update") : qsTr("Install")
                onClicked: root.tx.confirm()
            }

            PillButton {
                visible: root.tx.state === Transaction.Finished
                text: qsTr("Close")
                onClicked: root.tx.dismiss()
            }
        }
    }
}
