import QtQuick
import QtQuick.Controls
import Logos.Theme
import Logos.Controls

Item {
    id: root

    // Currently selected note id (-1 = none)
    property int activeNoteId: -1
    // Suppress save while loading a note
    property bool loading: false

    Component.onCompleted: refreshList()

    function refreshList() {
        var json = backend.loadNotes()
        var arr = JSON.parse(json)
        noteModel.clear()
        for (var i = 0; i < arr.length; i++)
            noteModel.append(arr[i])

        if (noteModel.count === 0) {
            createNewNote()
        } else if (activeNoteId === -1) {
            selectNote(noteModel.get(0).id)
        }
    }

    function selectNote(id) {
        activeNoteId = id
        loading = true
        editor.text = backend.loadNote(id)
        loading = false
        editor.forceActiveFocus()
    }

    function createNewNote() {
        var json = backend.createNote()
        var obj = JSON.parse(json)
        activeNoteId = obj.id
        loading = true
        editor.text = ""
        loading = false
        refreshList()
        selectNote(obj.id)
    }

    function deleteCurrentNote() {
        if (activeNoteId === -1) return
        backend.deleteNote(activeNoteId)
        activeNoteId = -1
        refreshList()
    }

    ListModel { id: noteModel }

    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
    }

    // ── Sidebar ──────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        width: 220
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        color: Theme.palette.backgroundSecondary

        // New Note button
        Rectangle {
            id: newNoteBtn
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 40
            color: newNoteArea.containsMouse ? Theme.palette.backgroundTertiary : "transparent"

            LogosText {
                anchors.centerIn: parent
                text: "+ New Note"
                color: Theme.palette.primary
                font.pixelSize: Theme.typography.secondaryText
            }

            MouseArea {
                id: newNoteArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.createNewNote()
            }
        }

        // Note list
        ListView {
            id: noteList
            anchors { top: newNoteBtn.bottom; bottom: sidebarBottomBar.top; left: parent.left; right: parent.right }
            model: noteModel
            clip: true
            currentIndex: -1

            delegate: Rectangle {
                id: noteDelegate
                width: noteList.width
                height: 56
                color: delegateArea.containsMouse ? Theme.palette.backgroundTertiary : "transparent"

                property bool isActive: model.id === root.activeNoteId

                // Background click area — declared first so it has lowest z-order
                MouseArea {
                    id: delegateArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectNote(model.id)
                }

                // Orange left border for active note
                Rectangle {
                    visible: noteDelegate.isActive
                    width: 3
                    anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                    color: Theme.palette.accentOrange
                }

                Column {
                    anchors {
                        verticalCenter: parent.verticalCenter
                        left: parent.left; leftMargin: 12
                        right: deleteBtn.left; rightMargin: 4
                    }
                    spacing: 2

                    LogosText {
                        width: parent.width
                        text: model.title || "Untitled"
                        color: noteDelegate.isActive ? Theme.palette.text : Theme.palette.textSecondary
                        font.pixelSize: Theme.typography.secondaryText
                        elide: Text.ElideRight
                    }

                    LogosText {
                        width: parent.width
                        text: formatTime(model.updatedAt)
                        color: Theme.palette.textPlaceholder
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // Delete button — visible on hover, higher z-order than delegateArea
                LogosText {
                    id: deleteBtn
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: "✕"
                    color: Theme.palette.error
                    font.pixelSize: 14
                    visible: delegateArea.containsMouse
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("deleteNote id=" + model.id)
                            backend.deleteNote(model.id)
                            if (model.id === root.activeNoteId)
                                root.activeNoteId = -1
                            root.refreshList()
                        }
                    }
                }
            }
        }

        // ── Sidebar bottom bar: Lock + Reset ─────────────────────────
        Rectangle {
            id: sidebarBottomBar
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 44
            color: Theme.palette.backgroundSecondary

            // Top edge gradient so list fades under
            Rectangle {
                anchors { bottom: parent.top; left: parent.left; right: parent.right }
                height: 24
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.palette.backgroundSecondary }
                }
            }

            Row {
                anchors.centerIn: parent
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
    }

    // ── Editor area ──────────────────────────────────────────────────
    Flickable {
        id: editorFlick
        anchors {
            top: parent.top; topMargin: Theme.spacing.xxlarge
            left: sidebar.right; leftMargin: 20
            right: parent.right
            bottom: parent.bottom; bottomMargin: Theme.spacing.xlarge
        }
        contentWidth: width
        contentHeight: editor.height
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        TextEdit {
            id: editor
            width: editorFlick.width
            rightPadding: 20
            wrapMode: TextEdit.Wrap
            color: Theme.palette.text
            font.family: "Courier New, monospace"
            font.pixelSize: Theme.typography.primaryText
            selectionColor: Theme.palette.overlayOrange
            selectedTextColor: Theme.palette.text

            onTextChanged: {
                if (!root.loading)
                    saveTimer.restart()
            }
            onCursorRectangleChanged: {
                // Keep cursor visible while typing
                var r = cursorRectangle
                if (r.y < editorFlick.contentY)
                    editorFlick.contentY = r.y
                else if (r.y + r.height > editorFlick.contentY + editorFlick.height)
                    editorFlick.contentY = r.y + r.height - editorFlick.height
            }
        }

        // Placeholder overlay
        Text {
            visible: editor.text.length === 0
            text: "Start writing…"
            color: Theme.palette.textPlaceholder
            font.family: editor.font.family
            font.pixelSize: Theme.typography.primaryText
            opacity: 0.6
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            topPadding: 0
            bottomPadding: 0
            leftPadding: 0
            rightPadding: 0
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: "#3a3a3a"
            }
            background: Item {}
        }
    }

    Timer {
        id: saveTimer
        interval: 1500
        onTriggered: {
            if (root.activeNoteId !== -1) {
                backend.saveNote(root.activeNoteId, editor.text)
                root.refreshList()
            }
        }
    }

    // ── Helper: relative timestamp ───────────────────────────────────
    function formatTime(epoch) {
        if (!epoch) return ""
        var now = Math.floor(Date.now() / 1000)
        var diff = now - epoch
        if (diff < 60)    return "just now"
        if (diff < 3600)  return Math.floor(diff / 60) + "m ago"
        if (diff < 86400) return Math.floor(diff / 3600) + "h ago"
        var d = new Date(epoch * 1000)
        return d.toLocaleDateString()
    }
}
