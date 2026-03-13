import QtQuick
import QtQuick.Layouts
import Logos.Theme
import Logos.Controls

Item {
    id: unlockRoot

    property int lockoutRemaining: 0

    // Parse lockout seconds from error messages like:
    //   "Wrong PIN. Locked out for 30 seconds."
    //   "Too many failed attempts. Try again in 25 seconds."
    function parseLockoutSeconds(msg) {
        var match = msg.match(/(\d+)\s*seconds/)
        return match ? parseInt(match[1]) : 0
    }

    Connections {
        target: backend
        function onErrorMessageChanged() {
            var secs = unlockRoot.parseLockoutSeconds(backend.errorMessage)
            if (secs > 0) {
                unlockRoot.lockoutRemaining = secs
                lockoutTimer.start()
            }
        }
    }

    Timer {
        id: lockoutTimer
        interval: 1000
        repeat: true
        onTriggered: {
            unlockRoot.lockoutRemaining--
            if (unlockRoot.lockoutRemaining <= 0) {
                unlockRoot.lockoutRemaining = 0
                lockoutTimer.stop()
            }
        }
    }

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
            enabled: unlockRoot.lockoutRemaining === 0
            Keys.onReturnPressed: {
                if (unlockRoot.lockoutRemaining === 0)
                    backend.unlockWithPin(pinField.text)
            }
        }

        LogosText {
            Layout.fillWidth: true
            text: unlockRoot.lockoutRemaining > 0
                  ? "Locked out. Try again in " + unlockRoot.lockoutRemaining + "s"
                  : backend.errorMessage
            color: Theme.palette.error
            font.pixelSize: Theme.typography.secondaryText
            visible: unlockRoot.lockoutRemaining > 0 || backend.errorMessage.length > 0
            wrapMode: Text.WordWrap
        }

        LogosButton {
            Layout.fillWidth: true
            text: unlockRoot.lockoutRemaining > 0
                  ? "Locked (" + unlockRoot.lockoutRemaining + "s)"
                  : "Unlock"
            enabled: unlockRoot.lockoutRemaining === 0
            onClicked: backend.unlockWithPin(pinField.text)
            background: Rectangle {
                color: unlockRoot.lockoutRemaining > 0
                       ? Theme.palette.backgroundSecondary
                       : (parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary)
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
