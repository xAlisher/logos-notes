import QtQuick
import Logos.Theme
import Logos.Controls

Item {
    property string savedText: backend.loadNote()

    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
    }

    TextEdit {
        id: editor
        anchors {
            fill: parent
            topMargin: Theme.spacing.xxlarge
            leftMargin: Theme.spacing.xlarge
            rightMargin: Theme.spacing.xlarge
            bottomMargin: Theme.spacing.xlarge
        }
        text: savedText
        wrapMode: TextEdit.Wrap
        color: Theme.palette.text
        font.family: "Courier New, monospace"
        font.pixelSize: Theme.typography.primaryText
        selectionColor: Theme.palette.overlayOrange
        selectedTextColor: Theme.palette.text

        LogosText {
            visible: editor.text.length === 0
            text: "Start writing…"
            color: Theme.palette.textPlaceholder
            font.pixelSize: Theme.typography.primaryText
        }

        onTextChanged: saveTimer.restart()
    }

    Timer {
        id: saveTimer
        interval: 1500
        onTriggered: backend.saveNote(editor.text)
    }

    // Bottom bar: Lock + Reset
    Row {
        anchors { bottom: parent.bottom; right: parent.right; margins: Theme.spacing.medium }
        spacing: Theme.spacing.large

        LogosText {
            text: "Lock"
            color: Theme.palette.primary
            font.pixelSize: Theme.typography.secondaryText
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.lock()
            }
        }

        LogosText {
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
}
