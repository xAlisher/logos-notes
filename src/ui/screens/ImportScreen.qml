import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Logos.Theme
import Logos.Controls

Item {
    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacing.large
        width: 420

        LogosText {
            Layout.fillWidth: true
            text: "Import Recovery Phrase"
            font.pixelSize: Theme.typography.titleText
            font.weight: Theme.typography.weightBold
            horizontalAlignment: Text.AlignHCenter
        }

        LogosText {
            Layout.fillWidth: true
            visible: pendingBackupPath.length > 0
            text: {
                // Parse fingerprint from filename: FINGERPRINT_DATE.imnotes
                var name = pendingBackupPath.split("/").pop().replace(".imnotes", "")
                var fp = name.split("_")[0]
                return fp ? fp : ""
            }
            color: Theme.palette.textPlaceholder
            font.pixelSize: 11
            font.family: "Courier New, monospace"
            horizontalAlignment: Text.AlignHCenter
        }

        LogosText {
            text: "Recovery phrase"
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
        }

        // Multi-line mnemonic input — styled to match LogosTextField visually.
        // LogosTextField wraps a single-line TextInput, so we use TextEdit here.
        Rectangle {
            Layout.fillWidth: true
            height: 100
            radius: Theme.spacing.radiusSmall
            color: Theme.palette.backgroundSecondary
            border.width: 1
            border.color: mnemonicArea.activeFocus
                          ? Theme.palette.overlayOrange
                          : Theme.palette.backgroundElevated

            TextEdit {
                id: mnemonicArea
                anchors { fill: parent; margins: Theme.spacing.medium }
                color: Theme.palette.text
                font.family: Theme.typography.publicSans
                font.pixelSize: Theme.typography.secondaryText
                wrapMode: TextEdit.Wrap
                opacity: activeFocus ? 1.0 : 0.0

                LogosText {
                    visible: mnemonicArea.text.length === 0 && mnemonicArea.activeFocus
                    text: "Enter 12 or 24 word recovery phrase"
                    color: Theme.palette.textPlaceholder
                    font.pixelSize: Theme.typography.secondaryText
                }
            }

            // Mask overlay — shows word count when not focused
            LogosText {
                anchors { fill: parent; margins: Theme.spacing.medium }
                visible: !mnemonicArea.activeFocus
                color: mnemonicArea.text.length > 0 ? Theme.palette.text : Theme.palette.textPlaceholder
                font.pixelSize: Theme.typography.secondaryText
                text: {
                    if (mnemonicArea.text.length === 0)
                        return "Enter 12 or 24 word recovery phrase"
                    var count = mnemonicArea.text.trim().split(/\s+/).length
                    return "••• " + count + " words entered •••"
                }
            }
        }

        LogosText {
            text: "PIN"
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
        }

        LogosTextField {
            id: pinField
            Layout.fillWidth: true
            placeholderText: "PIN (min 6 characters)"
            echoMode: TextInput.Password
        }

        LogosText {
            text: "Confirm PIN"
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
        }

        LogosTextField {
            id: pinConfirmField
            Layout.fillWidth: true
            placeholderText: "Confirm PIN"
            echoMode: TextInput.Password
        }

        LogosText {
            Layout.fillWidth: true
            text: backend.errorMessage
            color: Theme.palette.error
            font.pixelSize: Theme.typography.secondaryText
            visible: backend.errorMessage.length > 0
            wrapMode: Text.WordWrap
        }

        LogosButton {
            Layout.fillWidth: true
            text: pendingBackupPath.length > 0 ? "Import & Restore" : "Import"
            onClicked: backend.importMnemonic(mnemonicArea.text,
                                              pinField.text,
                                              pinConfirmField.text,
                                              pendingBackupPath)
            background: Rectangle {
                color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                radius: Theme.spacing.radiusXlarge
            }
        }

        // ── Keycard section ────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.palette.backgroundElevated
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }

        LogosText {
            Layout.fillWidth: true
            text: "or"
            color: Theme.palette.textPlaceholder
            font.pixelSize: Theme.typography.secondaryText
            horizontalAlignment: Text.AlignHCenter
        }

        LogosButton {
            Layout.fillWidth: true
            text: {
                if (!keycardDetecting) return "Connect Keycard"
                var st = backend.keycardState
                if (st === "ready" || st === "authorized") return "Keycard Detected"
                if (st === "waitingForCard") return "Insert Keycard..."
                if (st === "waitingForReader") return "Connect Reader..."
                if (st === "connectingCard") return "Connecting..."
                return "Detecting..."
            }
            onClicked: {
                if (!keycardDetecting) {
                    backend.startKeycardDetection()
                    keycardDetecting = true
                }
            }
            background: Rectangle {
                color: {
                    var st = backend.keycardState
                    if (st === "ready" || st === "authorized") return "#22c55e"
                    return parent.isActive ? "#3a3a3a" : Theme.palette.backgroundSecondary
                }
                radius: Theme.spacing.radiusXlarge
                border.width: 1
                border.color: {
                    var st = backend.keycardState
                    if (st === "ready" || st === "authorized") return "#22c55e"
                    return Theme.palette.backgroundElevated
                }
            }
        }

        LogosText {
            Layout.fillWidth: true
            text: backend.keycardStatus
            color: {
                var st = backend.keycardState
                if (st === "ready" || st === "authorized") return "#22c55e"
                if (st === "emptyKeycard" || st === "blockedPIN" || st === "blockedPUK"
                    || st === "notKeycard" || st === "connectionError" || st === "noPCSC")
                    return Theme.palette.error
                return Theme.palette.textPlaceholder
            }
            font.pixelSize: 11
            visible: keycardDetecting && backend.keycardStatus.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        // ── End Keycard section ───────────────────────────────────

        LogosText {
            id: restoreStatus
            Layout.fillWidth: true
            text: ""
            color: Theme.palette.primary
            font.pixelSize: Theme.typography.secondaryText
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        LogosText {
            Layout.fillWidth: true
            text: pendingBackupPath.length > 0
                  ? "Change backup file"
                  : "Restore from backup"
            color: pendingBackupPath.length > 0
                   ? Theme.palette.primary
                   : Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
            horizontalAlignment: Text.AlignHCenter
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: restoreDialog.open()
            }
        }
    }

    // Store pending backup path — restored after import succeeds.
    // Reset on every load (Loader recreates, but explicit is safer).
    property string pendingBackupPath: ""
    property bool keycardDetecting: false
    Component.onCompleted: { pendingBackupPath = ""; restoreStatus.text = ""; keycardDetecting = false }

    FileDialog {
        id: restoreDialog
        title: "Select Backup File"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Immutable Notes Backup (*.imnotes)", "All files (*)"]
        onAccepted: {
            pendingBackupPath = selectedFile.toLocalFile
                                ? selectedFile.toLocalFile()
                                : selectedFile.toString().replace(/^file:\/\//, "")
            var name = pendingBackupPath.split("/").pop()
            restoreStatus.text = "Backup: " + name
            console.log("restore: selected backup:", pendingBackupPath)
        }
    }

}
