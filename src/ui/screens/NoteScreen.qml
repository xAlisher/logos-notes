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
}
