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
        width: 320

        LogosText {
            Layout.fillWidth: true
            text: "Unlock"
            font.pixelSize: Theme.typography.titleText
            font.weight: Theme.typography.weightBold
            horizontalAlignment: Text.AlignHCenter
        }

        LogosText {
            text: "PIN"
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
        }

        LogosTextField {
            id: pinField
            Layout.fillWidth: true
            placeholderText: "Enter PIN"
            echoMode: TextInput.Password
            Keys.onReturnPressed: backend.unlockWithPin(pinField.text)
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
            text: "Unlock"
            onClicked: backend.unlockWithPin(pinField.text)
            background: Rectangle {
                color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                radius: Theme.spacing.radiusXlarge
            }
        }
    }

    // DEV/DEMO reset — remove before production
    LogosText {
        anchors { bottom: parent.bottom; right: parent.right; margins: Theme.spacing.medium }
        text: "Reset"
        color: Theme.palette.error
        font.pixelSize: Theme.typography.secondaryText

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.resetAndWipe()
        }
    }
}
