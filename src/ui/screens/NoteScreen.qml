import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Logos.Theme
import Logos.Controls

Item {
    id: root

    // Currently selected note id (-1 = none)
    property int activeNoteId: -1
    // Suppress save while loading a note
    property bool loading: false
    // Show settings screen
    property bool showSettings: false

    Component.onCompleted: deferredRefresh.start()

    Timer {
        id: deferredRefresh
        interval: 150
        onTriggered: root.refreshList()
    }

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

    function saveCurrentNote() {
        saveTimer.stop()
        if (activeNoteId > 0 && !loading)
            backend.saveNote(activeNoteId, editor.text)
    }

    function selectNote(id) {
        saveCurrentNote()
        activeNoteId = id
        loading = true
        editor.text = backend.loadNote(id)
        loading = false
        editor.forceActiveFocus()
    }

    function createNewNote() {
        saveCurrentNote()
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

    // Warning banner (partial restore, etc.)
    property string warningText: ""
    Connections {
        target: backend
        function onErrorMessageChanged() {
            if (backend.errorMessage.length > 0 && backend.currentScreen === "note")
                root.warningText = backend.errorMessage
        }
    }
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: root.warningText.length > 0 ? 36 : 0
        visible: height > 0
        color: "#ca8a04"
        z: 10

        LogosText {
            anchors.centerIn: parent
            text: root.warningText
            color: "#FFFFFF"
            font.pixelSize: Theme.typography.secondaryText
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.warningText = ""
        }
    }

    // ── Sidebar ──────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        visible: !root.showSettings
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

        // ── Sidebar bottom bar: Settings + Lock ─────────────────────
        Rectangle {
            id: sidebarBottomBar
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 44
            color: Theme.palette.backgroundSecondary

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
                    text: "Settings"
                    color: Theme.palette.textSecondary
                    font.pixelSize: Theme.typography.secondaryText
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showSettings = true
                    }
                }

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
            }
        }
    }

    // ── Editor area ──────────────────────────────────────────────────
    Flickable {
        id: editorFlick
        visible: !root.showSettings
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

    // ── Settings panel (full width, overlays sidebar) ─────────────────
    Item {
        id: settingsPanel
        visible: root.showSettings
        onVisibleChanged: {
            if (visible) {
                confirmCheck.checked = false
                exportStatus.text = ""
            }
        }
        anchors {
            top: parent.top; topMargin: Theme.spacing.xxlarge
            left: parent.left; leftMargin: 40
            right: parent.right; rightMargin: 40
            bottom: parent.bottom; bottomMargin: Theme.spacing.xlarge
        }

        ColumnLayout {
            anchors { top: parent.top; left: parent.left; right: parent.right }
            spacing: Theme.spacing.large
            width: Math.min(parent.width, 480)

            // Back button
            LogosText {
                text: "< Back"
                color: Theme.palette.primary
                font.pixelSize: Theme.typography.secondaryText
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.showSettings = false
                }
            }

            LogosText {
                text: "Settings"
                font.pixelSize: Theme.typography.titleText
                font.weight: Theme.typography.weightBold
            }

            // ── Account section ──────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: accountCol.height + 32
                color: Theme.palette.backgroundSecondary
                radius: 8

                Column {
                    id: accountCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 8

                    LogosText {
                        text: "Account"
                        font.pixelSize: Theme.typography.primaryText
                        font.weight: Theme.typography.weightBold
                    }

                    Row {
                        spacing: 12

                        LogosText {
                            text: "Public Key:"
                            color: Theme.palette.textSecondary
                            font.pixelSize: Theme.typography.secondaryText
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        LogosText {
                            id: fingerprintText
                            text: backend.getAccountFingerprint()
                            font.family: "Courier New, monospace"
                            font.pixelSize: Theme.typography.secondaryText
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        LogosText {
                            text: copyTimer.running ? "Copied!" : "Copy"
                            color: Theme.palette.primary
                            font.pixelSize: Theme.typography.secondaryText
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    // Copy fingerprint to clipboard
                                    fingerprintHelper.text = fingerprintText.text
                                    fingerprintHelper.selectAll()
                                    fingerprintHelper.copy()
                                    copyTimer.start()
                                }
                            }
                        }
                    }
                }
            }

            // ── Backup section ────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: backupCol.height + 32
                color: Theme.palette.backgroundSecondary
                radius: 8

                Column {
                    id: backupCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 8

                    LogosText {
                        text: "Backup"
                        font.pixelSize: Theme.typography.primaryText
                        font.weight: Theme.typography.weightBold
                    }

                    LogosText {
                        id: exportStatus
                        text: ""
                        color: Theme.palette.primary
                        font.pixelSize: Theme.typography.secondaryText
                        visible: text.length > 0
                    }

                    Row {
                        spacing: 12

                        LogosButton {
                            text: "Export Backup"
                            onClicked: exportDialog.open()
                            background: Rectangle {
                                color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                                radius: Theme.spacing.radiusXlarge
                                implicitHeight: 36
                            }
                            contentItem: LogosText {
                                text: parent.text
                                color: "#FFFFFF"
                                font.pixelSize: Theme.typography.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                    }
                }
            }

            // ── Danger Zone ──────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: dangerCol.height + 32
                color: Theme.palette.backgroundSecondary
                radius: 8
                border.width: 1
                border.color: Theme.palette.error

                Column {
                    id: dangerCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 12

                    LogosText {
                        text: "Danger Zone"
                        color: Theme.palette.error
                        font.pixelSize: Theme.typography.primaryText
                        font.weight: Theme.typography.weightBold
                    }

                    LogosText {
                        text: "Removing your account will permanently delete all notes\nfrom this device. This cannot be undone."
                        color: Theme.palette.textSecondary
                        font.pixelSize: Theme.typography.secondaryText
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    Row {
                        spacing: 8

                        CheckBox {
                            id: confirmCheck
                            width: 20; height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        LogosText {
                            text: "I understand all notes will be permanently deleted"
                            color: Theme.palette.textSecondary
                            font.pixelSize: Theme.typography.secondaryText
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: confirmCheck.checked = !confirmCheck.checked
                            }
                        }
                    }

                    LogosButton {
                        text: "Remove Account"
                        enabled: confirmCheck.checked
                        opacity: confirmCheck.checked ? 1.0 : 0.35
                        contentItem: LogosText {
                            text: parent.text
                            color: "#FFFFFF"
                            font.pixelSize: Theme.typography.primaryText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            backend.resetAndWipe()
                            root.showSettings = false
                        }
                        background: Rectangle {
                            color: parent.isActive ? "#d9272e" : Theme.palette.error
                            radius: Theme.spacing.radiusXlarge
                            implicitHeight: 40
                        }
                    }
                }
            }
        }
    }

    // Hidden TextEdit for clipboard copy
    TextEdit {
        id: fingerprintHelper
        visible: false
    }

    Timer {
        id: copyTimer
        interval: 2000
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

    // ── File dialogs ────────────────────────────────────────────────
    FileDialog {
        id: exportDialog
        title: "Export Backup"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Immutable Notes Backup (*.imnotes)"]
        currentFile: {
            var fp = backend.getAccountFingerprint()
            var d = new Date()
            var date = d.getFullYear() + "-"
                + String(d.getMonth()+1).padStart(2,"0") + "-"
                + String(d.getDate()).padStart(2,"0") + "_"
                + String(d.getHours()).padStart(2,"0")
                + String(d.getMinutes()).padStart(2,"0")
            var shortFp = fp.substring(0, 16)
            return "file://" + StandardPaths.writableLocation(StandardPaths.HomeLocation)
                   + "/" + shortFp + "_" + date + ".imnotes"
        }
        onAccepted: {
            var path = selectedFile.toLocalFile
                       ? selectedFile.toLocalFile()
                       : selectedFile.toString().replace(/^file:\/\//, "")
            if (!path.endsWith(".imnotes")) path += ".imnotes"
            console.log("export path:", path)
            var result = backend.exportBackup(path)
            console.log("export result:", result)
            var parsed = JSON.parse(result)
            exportStatus.text = parsed.ok
                ? "Exported " + parsed.noteCount + " note(s) to " + path
                : "Export failed: " + (parsed.error || "unknown error")
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
