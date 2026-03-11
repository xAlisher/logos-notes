import QtQuick
import QtQuick.Controls

Window {
    id: root
    width: 800
    height: 600
    visible: true
    title: "Immutable Notes"

    Loader {
        anchors.fill: parent
        source: {
            switch (backend.currentScreen) {
            case "import": return Qt.resolvedUrl("screens/ImportScreen.qml")
            case "unlock": return Qt.resolvedUrl("screens/UnlockScreen.qml")
            case "note":   return Qt.resolvedUrl("screens/NoteScreen.qml")
            default:       return Qt.resolvedUrl("screens/ImportScreen.qml")
            }
        }
    }
}
