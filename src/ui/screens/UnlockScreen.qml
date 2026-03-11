import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12
        width: 300

        Text { text: "Unlock"; font.pixelSize: 20 }

        TextField {
            id: pinField
            Layout.fillWidth: true
            placeholderText: "Enter PIN"
            echoMode: TextField.Password
        }

        Text {
            text: backend.errorMessage
            color: "red"
            visible: backend.errorMessage.length > 0
        }

        Button {
            text: "Unlock"
            Layout.fillWidth: true
            onClicked: backend.unlockWithPin(pinField.text)
        }
    }
}
