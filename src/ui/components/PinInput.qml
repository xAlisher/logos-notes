import QtQuick
import QtQuick.Controls

// Reusable PIN input field. Emits accepted() when the user presses Return.
TextField {
    id: root

    signal accepted(string pin)

    echoMode: TextField.Password
    inputMethodHints: Qt.ImhDigitsOnly
    placeholderText: "PIN"
    maximumLength: 8

    Keys.onReturnPressed: root.accepted(root.text)
}
