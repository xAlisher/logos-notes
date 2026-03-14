import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore

// Logos dark theme colors (hardcoded — QML sandbox blocks Logos.Theme import)

Item {
    id: root

    // ── Logos Dark Palette ──────────────────────────────────────────────────
    readonly property color bgColor:          "#171717"
    readonly property color bgSecondary:      "#262626"
    readonly property color bgElevated:       "#0E121B"
    readonly property color textColor:        "#FFFFFF"
    readonly property color textSecondary:    "#A4A4A4"
    readonly property color textPlaceholder:  "#717784"
    readonly property color primary:          "#ED7B58"
    readonly property color primaryHover:     "#F55702"
    readonly property color errorColor:       "#FB3748"
    readonly property color overlayOrange:    "#FF8800"
    readonly property color borderColor:      "#434343"

    // ── Screen state ────────────────────────────────────────────────────────
    property string currentScreen: "import"
    property string errorMessage:  ""
    property int lockoutRemaining: 0
    property string pendingBackupPath: ""
    property string restoreStatus: ""

    function parseLockoutSeconds(msg) {
        var match = msg.match(/(\d+)\s*seconds/)
        return match ? parseInt(match[1]) : 0
    }

    Timer {
        id: lockoutTimer
        interval: 1000
        repeat: true
        onTriggered: {
            root.lockoutRemaining--
            if (root.lockoutRemaining <= 0) {
                root.lockoutRemaining = 0
                lockoutTimer.stop()
                root.errorMessage = ""
            }
        }
    }

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var result = logos.callModule("notes", "isInitialized", [])
        root.currentScreen = (result === "true") ? "unlock" : "import"
    }

    Rectangle { anchors.fill: parent; color: root.bgColor }

    // ── Import screen ───────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            width: 420

            Text {
                Layout.fillWidth: true
                text: "Import Recovery Phrase"
                font.pixelSize: 30
                font.weight: Font.Bold
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                visible: root.pendingBackupPath.length > 0
                text: {
                    var name = root.pendingBackupPath.split("/").pop().replace(".imnotes", "")
                    var fp = name.split("_")[0]
                    return fp ? fp : ""
                }
                color: root.textPlaceholder
                font.pixelSize: 11
                font.family: "Courier New, monospace"
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: "Recovery phrase"
                color: root.textSecondary
                font.pixelSize: 12
            }

            Rectangle {
                Layout.fillWidth: true
                height: 100
                radius: 4
                color: root.bgSecondary
                border.width: 1
                border.color: mnemonicArea.activeFocus
                              ? root.overlayOrange : root.bgElevated

                TextEdit {
                    id: mnemonicArea
                    anchors { fill: parent; margins: 12 }
                    color: root.textColor
                    font.pixelSize: 12
                    wrapMode: TextEdit.Wrap

                    Text {
                        visible: mnemonicArea.text.length === 0
                        text: "Enter 12 or 24 word recovery phrase"
                        color: root.textPlaceholder
                        font.pixelSize: 12
                    }
                }
            }

            Text {
                text: "PIN"
                color: root.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id: importPinField
                Layout.fillWidth: true
                placeholderText: "PIN (min 6 characters)"
                echoMode: TextInput.Password
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textPlaceholder
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 4
                    border.width: 1
                    border.color: importPinField.activeFocus
                                  ? root.overlayOrange : root.bgElevated
                }
            }

            Text {
                text: "Confirm PIN"
                color: root.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id: importPinConfirmField
                Layout.fillWidth: true
                placeholderText: "Confirm PIN"
                echoMode: TextInput.Password
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textPlaceholder
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 4
                    border.width: 1
                    border.color: importPinConfirmField.activeFocus
                                  ? root.overlayOrange : root.bgElevated
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "import" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: "Import"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: root.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.pressed ? root.primaryHover : root.primary
                    radius: 16
                    implicitHeight: 44
                }
                onClicked: {
                    root.errorMessage = ""
                    if (typeof logos === "undefined" || !logos.callModule) return
                    var result = logos.callModule("notes", "importMnemonic",
                                                  [mnemonicArea.text,
                                                   importPinField.text,
                                                   importPinConfirmField.text,
                                                   root.pendingBackupPath])
                    root.pendingBackupPath = ""
                    var parsed = JSON.parse(result)
                    if (parsed.success) {
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Import failed"
                    }
                }
            }

            Text {
                id: pluginRestoreStatus
                Layout.fillWidth: true
                text: root.restoreStatus
                color: root.primary
                font.pixelSize: 12
                visible: text.length > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "Restore from backup"
                color: root.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // Plugin can't use FileDialog; use fixed path.
                        var home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                        // Look for most recent .imnotes file
                        root.pendingBackupPath = home + "/logos-notes-backup.imnotes"
                        root.restoreStatus = "Backup at ~/logos-notes-backup.imnotes will be restored after import."
                    }
                }
            }
        }
    }

    // ── Unlock screen ───────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "unlock"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            width: 320

            Text {
                Layout.fillWidth: true
                text: "Unlock"
                font.pixelSize: 30
                font.weight: Font.Bold
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (typeof logos !== "undefined" && logos.callModule)
                        return logos.callModule("notes", "getAccountFingerprint", [])
                    return ""
                }
                color: root.textPlaceholder
                font.pixelSize: 11
                font.family: "Courier New, monospace"
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: "PIN"
                color: root.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id: unlockPinField
                Layout.fillWidth: true
                placeholderText: "Enter PIN"
                echoMode: TextInput.Password
                enabled: root.lockoutRemaining === 0
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textPlaceholder
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 4
                    border.width: 1
                    border.color: unlockPinField.activeFocus
                                  ? root.overlayOrange : root.bgElevated
                }
                Keys.onReturnPressed: {
                    if (root.lockoutRemaining === 0)
                        unlockButton.clicked()
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.lockoutRemaining > 0
                      ? "Locked out. Try again in " + root.lockoutRemaining + "s"
                      : root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.lockoutRemaining > 0
                         || (root.currentScreen === "unlock" && root.errorMessage.length > 0)
                wrapMode: Text.WordWrap
            }

            Button {
                id: unlockButton
                Layout.fillWidth: true
                enabled: root.lockoutRemaining === 0
                text: root.lockoutRemaining > 0
                      ? "Locked (" + root.lockoutRemaining + "s)"
                      : "Unlock"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: root.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: root.lockoutRemaining > 0
                           ? root.bgSecondary
                           : (parent.pressed ? root.primaryHover : root.primary)
                    radius: 16
                    implicitHeight: 44
                }
                onClicked: {
                    root.errorMessage = ""
                    if (typeof logos === "undefined" || !logos.callModule) return
                    var result = logos.callModule("notes", "unlockWithPin",
                                                  [unlockPinField.text])
                    var parsed = JSON.parse(result)
                    if (parsed.success) {
                        root.lockoutRemaining = 0
                        lockoutTimer.stop()
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Unlock failed"
                        var secs = root.parseLockoutSeconds(root.errorMessage)
                        if (secs > 0) {
                            root.lockoutRemaining = secs
                            lockoutTimer.start()
                        }
                    }
                }
            }
        }

    }

    // ── Note screen ─────────────────────────────────────────────────────────
    Item {
        id: noteScreen
        anchors.fill: parent
        visible: root.currentScreen === "note"

        property int activeNoteId: -1
        property bool loading: false
        property bool showSettings: false

        onVisibleChanged: {
            if (visible) {
                activeNoteId = -1
                deferredRefresh.start()
            }
        }

        Timer {
            id: deferredRefresh
            interval: 150
            onTriggered: noteScreen.refreshList()
        }

        function refreshList() {
            if (typeof logos === "undefined" || !logos.callModule) return
            var json = logos.callModule("notes", "loadNotes", [])
            var arr = JSON.parse(json)
            noteModel.clear()
            for (var i = 0; i < arr.length; i++)
                noteModel.append(arr[i])

            if (noteModel.count === 0) {
                createNewNote()
            } else if (activeNoteId === -1) {
                var firstId = noteModel.get(0).id
                console.log("notes: auto-selecting first note id=" + firstId)
                selectNote(firstId)
            }
        }

        function selectNote(id) {
            activeNoteId = id
            loading = true
            if (typeof logos !== "undefined" && logos.callModule) {
                var result = logos.callModule("notes", "loadNote", [id])
                // loadNote returns plaintext on success, or empty/error JSON on failure.
                // Guard against error responses showing up in the editor.
                if (result && result.charAt(0) === '{') {
                    try {
                        var parsed = JSON.parse(result)
                        if (parsed.error) {
                            console.log("notes: loadNote error, retrying: " + parsed.error)
                            loading = false
                            retryNoteId = id
                            retryTimer.start()
                            return
                        }
                    } catch(e) {}
                }
                editor.text = result || ""
            }
            loading = false
            editor.forceActiveFocus()
        }

        property int retryNoteId: -1
        Timer {
            id: retryTimer
            interval: 300
            onTriggered: {
                if (noteScreen.retryNoteId !== -1)
                    noteScreen.selectNote(noteScreen.retryNoteId)
                noteScreen.retryNoteId = -1
            }
        }

        function createNewNote() {
            if (typeof logos === "undefined" || !logos.callModule) return
            var json = logos.callModule("notes", "createNote", [])
            var obj = JSON.parse(json)
            activeNoteId = obj.id
            loading = true
            editor.text = ""
            loading = false
            refreshList()
            selectNote(obj.id)
        }

        ListModel { id: noteModel }

        // ── Sidebar ──────────────────────────────────────────────────
        Rectangle {
            id: sidebar
            visible: !noteScreen.showSettings
            width: 220
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
            color: root.bgSecondary

            // New Note button
            Rectangle {
                id: newNoteBtn
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 40
                color: newNoteArea.containsMouse ? "#333333" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+ New Note"
                    color: root.primary
                    font.pixelSize: 13
                }

                MouseArea {
                    id: newNoteArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: noteScreen.createNewNote()
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
                    width: noteList.width
                    height: 56
                    color: delegateArea.containsMouse ? "#333333" : "transparent"

                    property int noteId: model.id
                    property bool isActive: noteId === noteScreen.activeNoteId

                    // Background click area — lowest z-order
                    MouseArea {
                        id: delegateArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: noteScreen.selectNote(parent.noteId)
                    }

                    // Orange left border for active note
                    Rectangle {
                        visible: parent.isActive
                        width: 3
                        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                        color: root.overlayOrange
                    }

                    Column {
                        anchors {
                            verticalCenter: parent.verticalCenter
                            left: parent.left; leftMargin: 12
                            right: deleteBtn.left; rightMargin: 4
                        }
                        spacing: 2

                        Text {
                            width: parent.width
                            text: model.title || "Untitled"
                            color: parent.parent.isActive ? root.textColor : root.textSecondary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: noteScreen.formatTime(model.updatedAt)
                            color: root.textPlaceholder
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    // Delete button — visible on hover
                    Text {
                        id: deleteBtn
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: "✕"
                        color: root.errorColor
                        font.pixelSize: 14
                        visible: delegateArea.containsMouse
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var nid = parent.parent.noteId
                                if (typeof logos !== "undefined" && logos.callModule)
                                    logos.callModule("notes", "deleteNote", [nid])
                                if (nid === noteScreen.activeNoteId)
                                    noteScreen.activeNoteId = -1
                                noteScreen.refreshList()
                            }
                        }
                    }
                }
            }

            // ── Sidebar bottom bar: Settings + Lock ─────────────────
            Rectangle {
                id: sidebarBottomBar
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: 44
                color: root.bgSecondary

                Rectangle {
                    anchors { bottom: parent.top; left: parent.left; right: parent.right }
                    height: 24
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: root.bgSecondary }
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 16

                    Text {
                        text: "Settings"
                        color: root.textSecondary
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: noteScreen.showSettings = true
                        }
                    }

                    Text {
                        text: "Lock"
                        color: root.primary
                        font.pixelSize: 12
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos !== "undefined" && logos.callModule)
                                    logos.callModule("notes", "lockSession", [])
                                unlockPinField.text = ""
                                root.currentScreen = "unlock"
                            }
                        }
                    }
                }
            }
        }

        // ── Editor area ──────────────────────────────────────────────
        Flickable {
            id: editorFlick
            visible: !noteScreen.showSettings
            anchors {
                top: parent.top; topMargin: 20
                left: sidebar.right; leftMargin: 20
                right: parent.right
                bottom: parent.bottom; bottomMargin: 20
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
                color: root.textColor
                font.family: "Courier New, monospace"
                font.pixelSize: 14
                selectionColor: root.overlayOrange
                selectedTextColor: root.textColor

                onTextChanged: {
                    if (!noteScreen.loading)
                        saveTimer.restart()
                }
                onCursorRectangleChanged: {
                    var r = cursorRectangle
                    if (r.y < editorFlick.contentY)
                        editorFlick.contentY = r.y
                    else if (r.y + r.height > editorFlick.contentY + editorFlick.height)
                        editorFlick.contentY = r.y + r.height - editorFlick.height
                }
            }

            // Placeholder
            Text {
                visible: editor.text.length === 0
                text: "Start writing..."
                color: root.textPlaceholder
                font.family: editor.font.family
                font.pixelSize: 14
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
                if (typeof logos !== "undefined" && logos.callModule && noteScreen.activeNoteId !== -1) {
                    logos.callModule("notes", "saveNote", [noteScreen.activeNoteId, editor.text])
                    noteScreen.refreshList()
                }
            }
        }

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

        // ── Settings panel (full width, overlays sidebar) ─────────────
        Item {
            visible: noteScreen.showSettings
            anchors {
                top: parent.top; topMargin: 20
                left: parent.left; leftMargin: 40
                right: parent.right; rightMargin: 40
                bottom: parent.bottom; bottomMargin: 20
            }

            Column {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                spacing: 16
                width: Math.min(parent.width, 480)

                Text {
                    text: "< Back"
                    color: root.primary
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: noteScreen.showSettings = false
                    }
                }

                Text {
                    text: "Settings"
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    color: root.textColor
                }

                // ── Account section ──────────────────────────────
                Rectangle {
                    width: parent.width
                    height: accountCol2.height + 32
                    color: root.bgSecondary
                    radius: 8

                    Column {
                        id: accountCol2
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                        spacing: 8

                        Text {
                            text: "Account"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: root.textColor
                        }

                        Row {
                            spacing: 12

                            Text {
                                text: "Public Key:"
                                color: root.textSecondary
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: fpText
                                text: {
                                    if (typeof logos !== "undefined" && logos.callModule)
                                        return logos.callModule("notes", "getAccountFingerprint", [])
                                    return ""
                                }
                                font.family: "Courier New, monospace"
                                font.pixelSize: 12
                                color: root.textColor
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: pluginCopyTimer.running ? "Copied!" : "Copy"
                                color: root.primary
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        fpHelper.text = fpText.text
                                        fpHelper.selectAll()
                                        fpHelper.copy()
                                        pluginCopyTimer.start()
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Backup section ────────────────────────────────
                Rectangle {
                    width: parent.width
                    height: backupCol2.height + 32
                    color: root.bgSecondary
                    radius: 8

                    Column {
                        id: backupCol2
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                        spacing: 8

                        Text {
                            text: "Backup"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: root.textColor
                        }

                        Text {
                            id: pluginExportStatus
                            text: ""
                            color: root.primary
                            font.pixelSize: 12
                            visible: text.length > 0
                        }

                        Row {
                            spacing: 12

                            Button {
                                text: "Export Backup"
                                onClicked: {
                                    var home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                                    var fp = ""
                                    if (typeof logos !== "undefined" && logos.callModule)
                                        fp = logos.callModule("notes", "getAccountFingerprint", [])
                                    var d = new Date()
                                    var date = d.getFullYear() + "-"
                                        + String(d.getMonth()+1).padStart(2,"0") + "-"
                                        + String(d.getDate()).padStart(2,"0")
                                    var shortFp = fp.substring(0, 16)
                                    var path = home + "/" + shortFp + "_" + date + ".imnotes"
                                    if (typeof logos !== "undefined" && logos.callModule) {
                                        var result = logos.callModule("notes", "exportBackup", [path])
                                        var parsed = JSON.parse(result)
                                        pluginExportStatus.text = parsed.ok
                                            ? "Exported " + parsed.noteCount + " note(s) to " + path
                                            : "Export failed: " + (parsed.error || "unknown")
                                    }
                                }
                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: 12
                                    color: "#FFFFFF"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: parent.pressed ? root.primaryHover : root.primary
                                    radius: 16
                                    implicitHeight: 36
                                }
                            }

                        }
                    }
                }

                // ── Danger Zone ──────────────────────────────────
                Rectangle {
                    width: parent.width
                    height: dangerCol2.height + 32
                    color: root.bgSecondary
                    radius: 8
                    border.width: 1
                    border.color: root.errorColor

                    Column {
                        id: dangerCol2
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                        spacing: 12

                        Text {
                            text: "Danger Zone"
                            color: root.errorColor
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }

                        Text {
                            text: "Removing your account will permanently delete all notes\nfrom this device. This cannot be undone."
                            color: root.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Row {
                            spacing: 8

                            CheckBox {
                                id: pluginConfirmCheck
                                width: 20; height: 20
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "I understand all notes will be permanently deleted"
                                color: root.textSecondary
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: pluginConfirmCheck.checked = !pluginConfirmCheck.checked
                                }
                            }
                        }

                        Button {
                            text: "Remove Account"
                            enabled: pluginConfirmCheck.checked
                            opacity: pluginConfirmCheck.checked ? 1.0 : 0.35
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: parent.pressed ? "#d9272e" : root.errorColor
                                radius: 16
                                implicitHeight: 40
                            }
                            onClicked: {
                                if (typeof logos !== "undefined" && logos.callModule)
                                    logos.callModule("notes", "resetAndWipe", [])
                                noteScreen.showSettings = false
                                root.errorMessage = ""
                                root.currentScreen = "import"
                            }
                        }
                    }
                }
            }
        }

        TextEdit { id: fpHelper; visible: false }
        Timer { id: pluginCopyTimer; interval: 2000 }
    }
}
