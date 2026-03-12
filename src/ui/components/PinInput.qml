import QtQuick
import Logos.Controls

// Reusable PIN input field. Emits pinAccepted() when the user presses Return.
LogosTextField {
    id: root

    signal pinAccepted(string pin)

    echoMode: TextInput.Password
    placeholderText: "PIN"

    textInput.inputMethodHints: Qt.ImhDigitsOnly
    textInput.maximumLength: 8
    textInput.Keys.onReturnPressed: root.pinAccepted(root.text)
}
