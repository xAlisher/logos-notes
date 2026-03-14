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

                LogosText {
                    visible: mnemonicArea.text.length === 0
                    text: "Enter 12 or 24 word recovery phrase"
                    color: Theme.palette.textPlaceholder
                    font.pixelSize: Theme.typography.secondaryText
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
            text: "Restore from backup"
            color: Theme.palette.textSecondary
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
    property string pendingBackupPath: ""

    FileDialog {
        id: restoreDialog
        title: "Select Backup File"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Immutable Notes Backup (*.imnotes)", "All files (*)"]
        onAccepted: {
            pendingBackupPath = selectedFile.toLocalFile()
            restoreStatus.text = "Backup selected. Import your recovery phrase to restore."
        }
    }

}
