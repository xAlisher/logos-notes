import QtQuick
import QtQuick.Layouts
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
            placeholderText: "PIN (min 4 digits)"
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
            text: "Import"
            onClicked: backend.importMnemonic(mnemonicArea.text,
                                              pinField.text,
                                              pinConfirmField.text)
            background: Rectangle {
                color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                radius: Theme.spacing.radiusXlarge
            }
        }
    }
}
