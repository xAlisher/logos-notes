import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12
        width: 400

        Text { text: "Import Recovery Phrase"; font.pixelSize: 20 }

        Text { text: "Recovery phrase"; font.pixelSize: 13 }
        TextArea {
            id: mnemonicField
            Layout.fillWidth: true
            placeholderText: "Enter 12 or 24 word recovery phrase"
            wrapMode: TextArea.Wrap
        }

        Text { text: "PIN"; font.pixelSize: 13 }
        TextField {
            id: pinField
            Layout.fillWidth: true
            placeholderText: "PIN (min 4 digits)"
            echoMode: TextField.Password
        }

        Text { text: "Confirm PIN"; font.pixelSize: 13 }
        TextField {
            id: pinConfirmField
            Layout.fillWidth: true
            placeholderText: "Confirm PIN"
            echoMode: TextField.Password
        }

        Text {
            text: backend.errorMessage
            color: "red"
            visible: backend.errorMessage.length > 0
        }

        Button {
            text: "Import"
            Layout.fillWidth: true
            onClicked: backend.importMnemonic(mnemonicField.text,
                                              pinField.text,
                                              pinConfirmField.text)
        }
    }
}
