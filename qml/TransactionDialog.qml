pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import CachyOS.PackageInstaller

Dialog {
    id: root

    readonly property Transaction tx: Backend.transaction
    readonly property int phase: tx.state
    readonly property bool hasPlanError: tx.planError.length > 0
    readonly property bool removes: tx.kind === Transaction.Remove || tx.kind === Transaction.FlatpakRemove
    readonly property bool failedSilently: phase === Transaction.Finished && !tx.success && tx.failureReason.length === 0
    property bool showLog: false

    visible: phase !== Transaction.Idle
    onVisibleChanged: if (visible) showLog = false
    closePolicy: Popup.NoAutoClose
    width: Math.min(Overlay.overlay.width - 80, 640)

    contentItem: Column {
        id: body
        spacing: 16

        Heading {
            width: body.width
            text: root.tx.title
            elide: Text.ElideRight
        }

        Row {
            id: planning
            visible: root.phase === Transaction.Planning
            spacing: 12

            BusyIndicator {
                width: 28
                height: 28
                running: planning.visible
            }

            Muted {
                anchors.verticalCenter: planning.verticalCenter
                text: qsTr("Working out what will change…")
                font.pixelSize: Look.fontBody
            }
        }

        Column {
            id: confirm
            visible: root.phase === Transaction.Confirm
            width: body.width
            spacing: 12

            // pacman can be verbose; never let the message push the buttons off the window
            Flickable {
                visible: root.hasPlanError
                width: confirm.width
                height: Math.min(planError.implicitHeight, 220)
                contentHeight: planError.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {}

                PlainText {
                    id: planError
                    width: confirm.width - Look.gutter
                    text: root.tx.planError
                    color: Look.danger
                    wrapMode: Text.WordWrap
                }
            }

            Muted {
                visible: !root.hasPlanError
                width: confirm.width
                text: root.tx.summary
                font.pixelSize: Look.fontBody
                wrapMode: Text.WordWrap
            }

            ListView {
                visible: count > 0
                width: confirm.width
                height: Math.min(contentHeight, 260)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.tx.plan

                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: planRow

                    required property planEntry modelData

                    width: ListView.view.width
                    height: 30

                    PlainText {
                        anchors.left: planRow.left
                        anchors.right: planVersion.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: planRow.verticalCenter
                        text: planRow.modelData.name
                        font.weight: planRow.modelData.requested ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    Muted {
                        id: planVersion
                        anchors.right: planRow.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: planRow.verticalCenter
                        text: planRow.modelData.version
                    }
                }
            }
        }

        Column {
            id: progressArea
            visible: root.phase === Transaction.Running || root.phase === Transaction.Finished
            width: body.width
            spacing: 12

            ProgressBar {
                id: progress
                visible: root.phase === Transaction.Running
                width: progressArea.width
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
                        width: progress.width * 0.3
                        height: 6
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

            PlainText {
                visible: root.phase === Transaction.Finished
                width: progressArea.width
                text: root.tx.success ? qsTr("Done.")
                    : root.tx.failureReason.length > 0 ? root.tx.failureReason : qsTr("The operation did not complete.")
                wrapMode: Text.WordWrap
                color: root.tx.success ? Look.accent : Look.danger
                font.pixelSize: Look.fontTitle
                font.weight: Font.DemiBold
            }

            Muted {
                visible: root.phase === Transaction.Running
                width: progressArea.width
                text: root.tx.lastLine
                elide: Text.ElideRight
            }

            Rectangle {
                // open by default only when the log is the sole explanation of a failure
                visible: root.showLog || root.failedSilently
                width: progressArea.width
                height: 240
                radius: Look.radiusSmall
                color: Look.window
                border.width: 1
                border.color: Look.border

                ListView {
                    id: logView
                    anchors.fill: parent
                    anchors.margins: 1
                    leftMargin: 10
                    rightMargin: 10
                    topMargin: 10
                    bottomMargin: 10
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true
                    model: root.tx.log

                    ScrollBar.vertical: ScrollBar {}

                    delegate: PlainText {
                        required property string display

                        width: logView.width - 20
                        text: display
                        font.family: "monospace"
                        font.pixelSize: Look.fontSmall
                        wrapMode: Text.WrapAnywhere
                    }

                    // rows get their text only after they are inserted, so wait until they are laid out
                    onCountChanged: Qt.callLater(positionViewAtEnd)
                }
            }
        }

        Row {
            anchors.right: body.right
            spacing: 10

            PillButton {
                visible: root.phase === Transaction.Running || (root.phase === Transaction.Finished && !root.failedSilently)
                tone: "soft"
                text: root.showLog ? qsTr("Hide details") : qsTr("Show details")
                onClicked: root.showLog = !root.showLog
            }

            PillButton {
                visible: root.showLog && logView.count > 0
                tone: "soft"
                text: qsTr("Copy log")
                onClicked: root.tx.copyLog()
            }

            PillButton {
                visible: root.phase === Transaction.Planning || root.phase === Transaction.Confirm || root.tx.cancellable
                tone: "soft"
                text: root.phase !== Transaction.Running ? qsTr("Cancel")
                    : root.tx.kind === Transaction.AurInstall ? qsTr("Stop build") : qsTr("Stop")
                onClicked: root.tx.cancel()
            }

            PillButton {
                visible: root.phase === Transaction.Confirm && !root.hasPlanError
                tone: root.removes ? "danger" : "primary"
                text: root.tx.confirmLabel
                onClicked: root.tx.confirm()
            }

            PillButton {
                visible: root.phase === Transaction.Finished
                text: qsTr("Close")
                onClicked: root.tx.dismiss()
            }
        }
    }
}
