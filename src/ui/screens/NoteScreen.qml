import QtQuick
import QtQuick.Controls

Item {
    property string savedText: backend.loadNote()

    TextArea {
        id: editor
        anchors.fill: parent
        anchors.margins: 16
        text: savedText
        wrapMode: TextArea.Wrap
        placeholderText: "Start writing…"
        font.pixelSize: 15

        onTextChanged: saveTimer.restart()
    }

    Timer {
        id: saveTimer
        interval: 1500
        onTriggered: backend.saveNote(editor.text)
    }

    // DEV/DEMO reset — remove before production
    Text {
        anchors { bottom: parent.bottom; right: parent.right; margins: 12 }
        text: "Reset"
        color: "#cc4444"
        font.pixelSize: 12

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.resetAndWipe()
        }
    }
}
