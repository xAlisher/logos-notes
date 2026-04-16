import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Design System ─────────────────────────────────────────────────
    readonly property color bgColor:          "#171717"
    readonly property color bgSecondary:      "#262626"
    readonly property color bgActive:         "#332A27"
    readonly property color bgOverlay:        "#141414"
    readonly property color textColor:        "#FFFFFF"
    readonly property color textSecondary:    "#A4A4A4"
    readonly property color textPlaceholder:  "#717784"
    readonly property color textDisabled:     "#5D5D5D"
    readonly property color primary:          "#FF5000"
    readonly property color primaryHover:     "#CC4000"
    readonly property color successGreen:     "#22C55E"
    readonly property color errorColor:       "#FB3748"
    readonly property color dangerBtnBg:      "#6C262D"
    readonly property color inputBorder:      "#383838"
    readonly property color borderColor:      "#3A3A3A"

    readonly property string appVersion: "V 1.0.0"

    // ── Screen state ────────────────────────────────────────────────────
    property string currentScreen: "import"
    property string errorMessage:  ""
    property string pendingBackupPath: ""
    property string restoreWarning: ""
    property string keySource: "mnemonic"

    // ── Keycard module integration ────────────────────────────────────
    property string keycardAuthId: ""
    property string keycardAuthStatus: "disconnected"  // "disconnected", "pending", "connected"
    property string keycardDerivedKey: ""
    property bool   keycardPollBusy: false

    // logos.callModule wraps the C++ QString return in an extra JSON layer — parse twice.
    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            return (typeof tmp === 'string') ? JSON.parse(tmp) : tmp
        } catch (e) { return null }
    }

    Timer {
        id: keycardStatusPoller
        interval: 1000
        running: root.keycardAuthStatus === "pending"
        repeat: true
        onTriggered: root.checkKeycardAuthStatus()
    }

    function requestKeycardAuth() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var result = logos.callModule("keycard", "requestAuth", ["notes_encryption", "notes"])
        var response = callModuleParse(result)
        if (response !== null && !response.error) {
            if (response.authId) {
                root.keycardAuthId = response.authId
                root.keycardAuthStatus = "pending"
                root.errorMessage = ""
            }
        } else {
            root.errorMessage = (response && response.error) ? response.error : "Failed to request keycard auth"
        }
    }

    function checkKeycardAuthStatus() {
        if (!root.keycardAuthId) return
        if (root.keycardPollBusy) return  // callModule blocks; guard prevents re-entrant stack buildup
        root.keycardPollBusy = true
        var result = logos.callModule("keycard", "checkAuthStatus", [root.keycardAuthId])
        var response = callModuleParse(result)
        if (response === null || response.error) {
            // Unknown/expired auth ID or parse failure — stop polling
            root.keycardAuthStatus = "disconnected"
            root.keycardAuthId = ""
            root.errorMessage = (response && response.error) ? response.error : "Keycard communication error"
        } else if (response.status === "complete" && response.key) {
            root.keycardAuthStatus = "connected"
            root.keycardDerivedKey = response.key
            root.keycardPollBusy = false
            processKeycardKey()
            return
        } else if (response.status === "failed") {
            root.keycardAuthStatus = "disconnected"
            root.keycardAuthId = ""
            root.errorMessage = response.error || "Authorization failed"
        } else if (response.status === "rejected") {
            root.keycardAuthStatus = "disconnected"
            root.keycardAuthId = ""
            root.errorMessage = "Authorization rejected"
        }
        root.keycardPollBusy = false
    }

    function processKeycardKey() {
        if (!keycardDerivedKey) return
        if (currentScreen === "import") {
            var result = logos.callModule("notes", "importWithKeycardKey",
                                          [keycardDerivedKey, root.pendingBackupPath])
            var obj = callModuleParse(result)
            if (obj && obj.success) {
                root.keySource = "keycard"
                root.currentScreen = "note"
                if (obj.warning) root.restoreWarning = obj.warning
            } else {
                root.errorMessage = (obj && obj.error) || "Import failed"
            }
        } else if (currentScreen === "unlock") {
            var unlockResult = logos.callModule("notes", "unlockWithKeycardKey",
                                                 [keycardDerivedKey])
            var unlockObj = callModuleParse(unlockResult)
            if (unlockObj && unlockObj.success) {
                root.currentScreen = "note"
                root.errorMessage = ""
            } else {
                root.errorMessage = (unlockObj && unlockObj.error) || "Unlock failed"
            }
        }
        resetKeycardAuth()
    }

    function resetKeycardAuth() {
        root.keycardAuthId = ""
        root.keycardAuthStatus = "disconnected"
        root.keycardDerivedKey = ""
    }

    // ── PIN lockout ────────────────────────────────────────────────────
    property int lockoutRemaining: 0
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
        if (result === "true") {
            var ks = logos.callModule("notes", "getKeySource", [])
            root.keySource = ks ? ks.trim() : "mnemonic"
            root.currentScreen = "unlock"
        } else {
            root.currentScreen = "import"
        }
    }

    Rectangle { anchors.fill: parent; color: root.bgColor }

    // ═══════════════════════════════════════════════════════════════════
    // ── IMPORT SCREEN ──────────────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import"

        onVisibleChanged: {
            if (visible) {
                root.pendingBackupPath = ""
                root.errorMessage = ""
                resetKeycardAuth()
            }
        }

        Text {
            x: 24; y: 16
            text: root.appVersion
            color: root.textPlaceholder
            font.pixelSize: 11
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: 16

            Text {
                text: "Immutable Notes"
                font.pixelSize: 28
                font.weight: Font.Bold
                color: root.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Encrypt your notes with Keycard"
                color: root.textPlaceholder
                font.pixelSize: 14
                Layout.alignment: Qt.AlignHCenter
            }

            // Status text
            Text {
                Layout.fillWidth: true
                text: {
                    if (root.keycardAuthStatus === "pending") return "Switch to Keycard module to approve..."
                    return ""
                }
                color: "#ff9800"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                visible: text.length > 0
            }

            // Error message
            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "import" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // Connect with Keycard button
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: 22
                color: {
                    if (root.keycardAuthStatus === "pending") return "#ff9800"
                    return connectImportArea.containsMouse ? root.primaryHover : root.primary
                }
                opacity: root.keycardAuthStatus === "disconnected" ? 1.0 : 0.8

                Text {
                    anchors.centerIn: parent
                    text: {
                        if (root.keycardAuthStatus === "pending") return "Waiting for approval..."
                        return "Connect with Keycard"
                    }
                    color: root.textColor
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: connectImportArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: root.keycardAuthStatus === "disconnected"
                    onClicked: {
                        root.errorMessage = ""
                        root.requestKeycardAuth()
                    }
                }
            }

            // Restore backup link
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.pendingBackupPath ? "Backup: " + root.pendingBackupPath.split("/").pop()
                                              : "Decrypt backup"
                color: root.primary
                font.pixelSize: 12
                visible: true
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (typeof logos === "undefined" || !logos.callModule) return
                        var json = logos.callModule("notes", "listBackups", [])
                        try {
                            var parsed = callModuleParse(json)
                            // listBackups returns a raw array, not {backups: [...]}
                            var backups = Array.isArray(parsed) ? parsed : (parsed.backups || [])
                            if (backups.length > 0) {
                                root.pendingBackupPath = backups[0].path
                            } else {
                                root.errorMessage = "No backups found"
                            }
                        } catch (e) {}
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // ── UNLOCK SCREEN ──────────────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "unlock"

        onVisibleChanged: {
            if (visible) {
                root.errorMessage = ""
                resetKeycardAuth()
                unlockPinField.text = ""
            }
        }

        Text {
            x: 24; y: 16
            text: root.appVersion
            color: root.textPlaceholder
            font.pixelSize: 11
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: 16

            Image {
                source: "Lock.svg"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                sourceSize: Qt.size(32, 32)
            }

            Text {
                text: "Unlock Notes"
                font.pixelSize: 28
                font.weight: Font.Bold
                color: root.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            // Status text for keycard mode
            Text {
                Layout.fillWidth: true
                visible: root.keySource === "keycard"
                text: {
                    if (root.keycardAuthStatus === "pending") return "Switch to Keycard module to approve..."
                    return ""
                }
                color: "#ff9800"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
            }

            // Error message
            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.errorMessage.length > 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // Keycard unlock button
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: 22
                visible: root.keySource === "keycard"
                color: {
                    if (root.keycardAuthStatus === "pending") return "#ff9800"
                    return unlockKeycardArea.containsMouse ? root.primaryHover : root.primary
                }
                opacity: root.keycardAuthStatus === "disconnected" ? 1.0 : 0.8

                Text {
                    anchors.centerIn: parent
                    text: {
                        if (root.keycardAuthStatus === "pending") return "Waiting for approval..."
                        return "Unlock with Keycard"
                    }
                    color: root.textColor
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: unlockKeycardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: root.keycardAuthStatus === "disconnected"
                    onClicked: {
                        root.errorMessage = ""
                        root.requestKeycardAuth()
                    }
                }
            }

            // Mnemonic PIN unlock (legacy mode)
            TextField {
                id: unlockPinField
                Layout.fillWidth: true
                visible: root.keySource !== "keycard"
                placeholderText: "Enter PIN"
                echoMode: TextInput.Password
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 3
                    border.width: 1
                    border.color: unlockPinField.activeFocus ? root.primary : root.inputBorder
                }
                Keys.onReturnPressed: unlockBtn.clicked()
            }

            Button {
                id: unlockBtn
                Layout.fillWidth: true
                visible: root.keySource !== "keycard"
                enabled: root.lockoutRemaining === 0
                text: root.lockoutRemaining > 0 ? "Locked (" + root.lockoutRemaining + "s)" : "Unlock"
                contentItem: Text {
                    text: unlockBtn.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: unlockBtn.enabled ? root.textColor : root.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: unlockBtn.enabled
                           ? (unlockBtn.pressed ? root.primaryHover : root.primary)
                           : root.bgSecondary
                    radius: 16
                    implicitHeight: 40
                }
                onClicked: {
                    if (typeof logos === "undefined" || !logos.callModule) return
                    root.errorMessage = ""
                    var result = logos.callModule("notes", "unlockWithPin", [unlockPinField.text])
                    var parsed = callModuleParse(result)
                    if (parsed.success) {
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Wrong PIN"
                        var secs = root.parseLockoutSeconds(parsed.error || "")
                        if (secs > 0) {
                            root.lockoutRemaining = secs
                            lockoutTimer.start()
                        }
                    }
                    unlockPinField.text = ""
                }
            }

            // Back to import
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Back to import"
                color: root.primary
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (typeof logos !== "undefined" && logos.callModule)
                            logos.callModule("notes", "resetAndWipe", [])
                        root.currentScreen = "import"
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // ── NOTE EDITOR SCREEN ─────────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════
    Item {
        id: noteScreen
        anchors.fill: parent
        visible: root.currentScreen === "note"

        property int activeNoteId: -1
        property bool showSettings: false
        property bool loading: false
        property string lastLoadedContent: ""

        onVisibleChanged: {
            if (visible) {
                refreshList()
                if (root.restoreWarning) {
                    root.errorMessage = root.restoreWarning
                    root.restoreWarning = ""
                }
            }
        }

        function refreshList() {
            if (typeof logos === "undefined" || !logos.callModule) return
            var json = logos.callModule("notes", "loadNotes", [])
            var arr = callModuleParse(json)
            noteModel.clear()
            for (var i = 0; i < arr.length; i++) {
                noteModel.append({
                    id: arr[i].id,
                    title: arr[i].title || "Untitled",
                    updatedAt: arr[i].updated_at || 0
                })
            }
        }

        function selectNote(id) {
            if (typeof logos === "undefined" || !logos.callModule) return
            saveCurrentNote()
            activeNoteId = id
            loading = true
            if (id !== -1) {
                var result = logos.callModule("notes", "loadNote", [id])
                // Shell wraps C++ return in extra JSON layer — unwrap once to get inner string
                var inner
                try { inner = JSON.parse(result) } catch(e) { inner = result }
                if (inner && inner.charAt(0) === '{') {
                    try {
                        var parsed = JSON.parse(inner)
                        if (parsed.error) {
                            loading = false
                            retryNoteId = id
                            retryTimer.start()
                            return
                        }
                    } catch(e) {}
                }
                editor.text = inner || ""
                lastLoadedContent = editor.text
            }
            loading = false
            editor.forceActiveFocus()
        }

        function saveCurrentNote() {
            if (typeof logos !== "undefined" && logos.callModule
                && activeNoteId !== -1
                && editor.text !== lastLoadedContent) {
                logos.callModule("notes", "saveNote", [activeNoteId, editor.text])
                lastLoadedContent = editor.text
            }
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
            saveCurrentNote()
            var json = logos.callModule("notes", "createNote", [])
            var obj = callModuleParse(json)
            activeNoteId = obj.id
            loading = true
            editor.text = ""
            lastLoadedContent = ""
            loading = false
            refreshList()
            selectNote(obj.id)
        }

        ListModel { id: noteModel }

        Shortcut {
            sequence: "Ctrl+N"
            onActivated: noteScreen.createNewNote()
        }
        Shortcut {
            sequence: "Ctrl+L"
            onActivated: {
                if (typeof logos !== "undefined" && logos.callModule)
                    logos.callModule("notes", "lockSession", [])
                root.currentScreen = "unlock"
            }
        }

        // ── Sidebar ─────────────────────────────────────────────────
        Rectangle {
            id: sidebar
            visible: !noteScreen.showSettings
            width: 220
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
            color: root.bgSecondary

            Rectangle {
                id: newNoteBtn
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 48
                color: newNoteArea.containsMouse ? "#333333" : "transparent"

                Row {
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    Image {
                        source: "Add.svg"
                        width: 16; height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        sourceSize: Qt.size(16, 16)
                    }
                    Text {
                        text: "Ctrl+N"
                        color: root.textColor
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: newNoteArea
                    z: 10
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: noteScreen.createNewNote()
                }
            }

            ListView {
                id: noteList
                anchors { top: newNoteBtn.bottom; bottom: sidebarBottomBar.top; left: parent.left; right: parent.right }
                model: noteModel
                clip: true
                currentIndex: -1
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOn
                    width: 4
                    opacity: noteList.contentHeight > noteList.height ? 0.4 : 0
                    contentItem: Rectangle {
                        implicitWidth: 3
                        radius: 1.5
                        color: root.borderColor
                    }
                    background: Item {}
                }

                delegate: Rectangle {
                    width: noteList.width
                    height: 56
                    color: {
                        if (noteId === noteScreen.activeNoteId) return root.bgActive
                        return delegateArea.containsMouse ? "#333333" : "transparent"
                    }

                    property int noteId: model.id
                    property bool isActive: noteId === noteScreen.activeNoteId

                    MouseArea {
                        id: delegateArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: noteScreen.selectNote(parent.noteId)
                    }

                    Rectangle {
                        visible: parent.isActive
                        width: 3
                        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                        color: root.primary
                    }

                    Column {
                        anchors {
                            verticalCenter: parent.verticalCenter
                            left: parent.left; leftMargin: 16
                            right: deleteBtn.left; rightMargin: 8
                        }
                        spacing: 4

                        Text {
                            width: parent.width
                            text: model.title || "Untitled"
                            color: parent.parent.isActive ? root.textColor : root.textSecondary
                            font.pixelSize: 13
                            font.weight: parent.parent.isActive ? Font.Bold : Font.Normal
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
                                if (typeof logos !== "undefined" && logos.callModule) {
                                    var delResult = callModuleParse(logos.callModule("notes", "deleteNote", [nid]))
                                    if (delResult && delResult.error) return
                                }
                                if (nid === noteScreen.activeNoteId)
                                    noteScreen.activeNoteId = -1
                                noteScreen.refreshList()
                            }
                        }
                    }
                }
            }

            // ── Sidebar bottom bar ──────────────────────────────────
            Rectangle {
                id: sidebarBottomBar
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: 48
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
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    spacing: 8

                    Item {
                        width: 16; height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.fill: parent
                            source: "Lock.svg"
                            sourceSize: Qt.size(16, 16)
                        }
                        MouseArea {
                            z: 10
                            anchors { fill: parent; margins: -4 }
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos !== "undefined" && logos.callModule)
                                    logos.callModule("notes", "lockSession", [])
                                root.currentScreen = "unlock"
                            }
                        }
                    }

                    Text {
                        text: "Ctrl+L"
                        color: root.textColor
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos !== "undefined" && logos.callModule)
                                    logos.callModule("notes", "lockSession", [])
                                root.currentScreen = "unlock"
                            }
                        }
                    }
                }

                Rectangle {
                    anchors { right: parent.right; rightMargin: 8; top: parent.top; bottom: parent.bottom }
                    width: Math.max(fpSidebarText.width + 16, 48)
                    color: "transparent"

                    Text {
                        id: fpSidebarText
                        anchors.centerIn: parent
                        property string fp: ""
                        text: fp || "Settings"
                        color: fpHoverArea.containsMouse ? root.primary : root.textSecondary
                        font.pixelSize: 11
                        font.family: "Courier New, monospace"

                        Timer {
                            interval: 500
                            running: noteScreen.visible
                            repeat: true
                            onTriggered: {
                                if (typeof logos !== "undefined" && logos.callModule) {
                                    var v = logos.callModule("notes", "getAccountFingerprint", [])
                                    if (v && v.length > 0) {
                                        fpSidebarText.fp = v
                                        running = false
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: fpHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: noteScreen.showSettings = true
                    }
                }
            }
        }

        // ── Editor area ─────────────────────────────────────────────
        Flickable {
            id: editorFlick
            visible: !noteScreen.showSettings && noteScreen.activeNoteId !== -1
            anchors {
                top: parent.top; topMargin: 24
                left: sidebar.right; leftMargin: 24
                right: parent.right; rightMargin: 4
                bottom: parent.bottom; bottomMargin: 24
            }
            contentWidth: width
            contentHeight: editor.height
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            TextEdit {
                id: editor
                width: editorFlick.width
                rightPadding: 24
                wrapMode: TextEdit.Wrap
                color: root.textColor
                font.family: "Courier New, monospace"
                font.pixelSize: 14
                selectionColor: root.primary
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

            Text {
                visible: editor.text.length === 0
                text: "Start writing..."
                color: root.textPlaceholder
                font.family: editor.font.family
                font.pixelSize: 14
                opacity: 0.6
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AlwaysOn
                width: 6
                opacity: editorFlick.contentHeight > editorFlick.height ? 0.4 : 0
                contentItem: Rectangle {
                    implicitWidth: 3
                    radius: 1.5
                    color: root.borderColor
                }
                background: Item {}
            }
        }

        // ── Empty state ─────────────────────────────────────────────
        Item {
            visible: !noteScreen.showSettings && noteScreen.activeNoteId === -1
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
            }

            Text {
                anchors.centerIn: parent
                text: noteModel.count > 0 ? "Select or create new note" : "Create new note"
                color: root.textPlaceholder
                font.pixelSize: 16
                opacity: 0.6
            }
        }

        Timer {
            id: saveTimer
            interval: 200
            onTriggered: {
                if (typeof logos !== "undefined" && logos.callModule
                    && noteScreen.activeNoteId !== -1
                    && editor.text !== noteScreen.lastLoadedContent) {
                    logos.callModule("notes", "saveNote", [noteScreen.activeNoteId, editor.text])
                    noteScreen.lastLoadedContent = editor.text
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

        // ═════════════════════════════════════════════════════════════
        // ── SETTINGS panel ─────────────────────────────────────────
        // ═════════════════════════════════════════════════════════════
        Item {
            visible: noteScreen.showSettings
            onVisibleChanged: {
                if (visible) {
                    pluginConfirmCheck.checked = false
                    pluginExportStatus.text = ""
                }
            }
            anchors.fill: parent

            Text {
                x: 24; y: 16
                text: root.appVersion
                color: root.textPlaceholder
                font.pixelSize: 11
            }

            Rectangle {
                anchors { top: parent.top; right: parent.right; topMargin: 12; rightMargin: 16 }
                width: 24; height: 24
                color: "transparent"
                Image {
                    anchors.centerIn: parent
                    source: "close.svg"
                    width: 20; height: 20
                    sourceSize: Qt.size(20, 20)
                    opacity: closeHoverArea.containsMouse ? 0.6 : 1.0
                }
                MouseArea {
                    id: closeHoverArea
                    z: 10
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: noteScreen.showSettings = false
                }
            }

            Column {
                anchors {
                    top: parent.top; topMargin: 48
                    left: parent.left; leftMargin: 40
                    right: parent.right; rightMargin: 40
                }
                spacing: 16
                width: Math.min(parent.width - 80, 480)

                Text {
                    text: "Settings"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: root.textColor
                }

                // Current database
                Rectangle {
                    width: parent.width
                    height: dbCol.height + 40
                    color: root.bgSecondary
                    radius: 8

                    Column {
                        id: dbCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                        spacing: 8

                        Text {
                            text: "Current database"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: root.textColor
                        }

                        Row {
                            spacing: 8
                            Text { text: "Encryption:"; color: root.textSecondary; font.pixelSize: 12 }
                            Text {
                                text: root.keySource === "keycard" ? "Keycard" : "Recovery Phrase"
                                color: root.textColor
                                font.pixelSize: 12
                            }
                        }

                        Row {
                            spacing: 8
                            Text {
                                text: "Fingerprint:"
                                color: root.textSecondary
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                id: fpText
                                property string fingerprint: ""
                                text: fingerprint
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                                color: root.textColor
                                Component.onCompleted: refreshFp()
                                function refreshFp() {
                                    if (typeof logos !== "undefined" && logos.callModule)
                                        fingerprint = logos.callModule("notes", "getAccountFingerprint", [])
                                }
                                Connections {
                                    target: noteScreen
                                    function onShowSettingsChanged() { fpText.refreshFp() }
                                    function onVisibleChanged() { if (noteScreen.visible) fpText.refreshFp() }
                                }
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

                // Backup
                Rectangle {
                    width: parent.width
                    height: backupCol.height + 40
                    color: root.bgSecondary
                    radius: 8

                    Column {
                        id: backupCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                        spacing: 8

                        Text {
                            text: "Backup"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: root.textColor
                        }

                        Text {
                            text: "Export your notes as encrypted backup file"
                            color: root.textSecondary
                            font.pixelSize: 12
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            id: pluginExportStatus
                            text: ""
                            color: root.primary
                            font.pixelSize: 12
                            visible: text.length > 0
                            width: parent.width
                            wrapMode: Text.WrapAnywhere
                        }

                        Button {
                            id: exportBackupBtn
                            text: "Export Backup"
                            leftPadding: 24; rightPadding: 24
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                var result = logos.callModule("notes", "exportBackupAuto", [])
                                var parsed = callModuleParse(result)
                                pluginExportStatus.text = parsed.ok
                                    ? "Saved to: " + parsed.path
                                    : "Export failed: " + (parsed.error || "unknown")
                            }
                            contentItem: Text {
                                text: exportBackupBtn.text
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: exportBackupBtn.pressed ? root.primaryHover : root.primary
                                radius: 16
                                implicitHeight: 36
                            }
                        }
                    }
                }

                // Logos Storage Backup (Keycard sessions only)
                Rectangle {
                    id: cloudBackupCard
                    width: parent.width
                    height: cloudBackupCol.height + 40
                    color: root.bgSecondary
                    radius: 8
                    visible: root.keySource === "keycard"

                    // Hidden TextEdit for clipboard copy
                    TextEdit {
                        id: clipHelper
                        visible: false
                        text: ""
                    }

                    // Poll storage status every 5s while settings is open
                    Timer {
                        id: backupStatusPoller
                        interval: 5000
                        running: noteScreen.showSettings && root.keySource === "keycard"
                        repeat: true
                        onTriggered: cloudBackupCard.refreshBackupStatus()
                    }

                    function refreshBackupStatus() {
                        if (typeof logos === "undefined" || !logos.callModule) return
                        var st = callModuleParse(logos.callModule("notes", "getStorageStatus", []))
                        if (typeof st === "string") cloudStatus = st
                        else if (st && typeof st === "object" && st.status) cloudStatus = st.status
                        var cidRaw = callModuleParse(logos.callModule("notes", "getBackupCid", []))
                        if (cidRaw && cidRaw.cid) {
                            cloudCid = cidRaw.cid
                            cloudTs  = cidRaw.timestamp ? parseInt(cidRaw.timestamp) : 0
                        } else {
                            cloudCid = ""
                            cloudTs  = 0
                        }
                    }

                    property string cloudStatus: "disabled"
                    property string cloudCid:    ""
                    property int    cloudTs:     0
                    property int    nowTick:     0  // incremented by tsRefreshTimer to re-evaluate relative time

                    // Refresh when settings opens
                    Connections {
                        target: noteScreen
                        function onShowSettingsChanged() {
                            if (noteScreen.showSettings && root.keySource === "keycard")
                                cloudBackupCard.refreshBackupStatus()
                        }
                    }

                    function statusColor() {
                        switch (cloudStatus) {
                            case "synced":      return "#22C55E"
                            case "uploading":   return "#FF7D46"
                            case "failed":      return root.errorColor
                            case "available":   return root.textSecondary
                            default:            return root.textSecondary
                        }
                    }

                    function statusLabel() {
                        switch (cloudStatus) {
                            case "synced":      return "Synced"
                            case "uploading":   return "Uploading…"
                            case "failed":      return "Failed"
                            case "available":   return "Ready"
                            case "unavailable": return "Logos Storage unavailable"
                            default:            return "Not available"
                        }
                    }

                    function relativeTime(epochSecs) {
                        if (epochSecs === 0) return ""
                        var diff = Math.floor(Date.now() / 1000) - epochSecs
                        if (diff < 5)   return "just now"
                        if (diff < 60)  return diff + "s ago"
                        if (diff < 3600) return Math.floor(diff / 60) + "m ago"
                        if (diff < 86400) return Math.floor(diff / 3600) + "h ago"
                        return Math.floor(diff / 86400) + "d ago"
                    }

                    function truncateCid(cid) {
                        if (!cid || cid.length <= 12) return cid
                        return cid.slice(0, 6) + "…" + cid.slice(-4)
                    }

                    Column {
                        id: cloudBackupCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                        spacing: 10

                        // Header row: label + status dot
                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                text: "Logos Storage Backup"
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                color: root.textColor
                            }

                            Rectangle {
                                width: 8; height: 8
                                radius: 4
                                color: cloudBackupCard.statusColor()
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: cloudBackupCard.statusLabel()
                                font.pixelSize: 12
                                color: cloudBackupCard.statusColor()
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // CID row (shown only when a CID exists)
                        Row {
                            width: parent.width
                            spacing: 8
                            visible: cloudBackupCard.cloudCid !== ""

                            Text {
                                text: cloudBackupCard.truncateCid(cloudBackupCard.cloudCid)
                                font.pixelSize: 12
                                color: root.textSecondary
                                font.family: "monospace"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Rectangle {
                                width: copyBtn.implicitWidth + 16
                                height: 22
                                radius: 11
                                color: copyBtn.pressed ? root.bgActive : root.bgOverlay

                                Text {
                                    id: copyBtn
                                    property bool pressed: false
                                    anchors.centerIn: parent
                                    text: "Copy"
                                    font.pixelSize: 11
                                    color: root.textSecondary
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onPressed:  copyBtn.pressed = true
                                    onReleased: copyBtn.pressed = false
                                    onClicked: {
                                        if (cloudBackupCard.cloudCid !== "") {
                                            clipHelper.text = cloudBackupCard.cloudCid
                                            clipHelper.selectAll()
                                            clipHelper.copy()
                                            cidCopyStatus.visible = true
                                            cidCopyTimer.restart()
                                        }
                                    }
                                }
                            }

                            Text {
                                id: cidCopyStatus
                                text: "Copied!"
                                font.pixelSize: 11
                                color: "#22C55E"
                                visible: false
                                anchors.verticalCenter: parent.verticalCenter

                                Timer {
                                    id: cidCopyTimer
                                    interval: 1500
                                    onTriggered: cidCopyStatus.visible = false
                                }
                            }
                        }

                        // Timestamp — nowTick is a dummy dependency that forces re-evaluation every 30s
                        Timer {
                            id: tsRefreshTimer
                            interval: 30000
                            running: cloudBackupCard.cloudTs > 0
                            repeat: true
                            onTriggered: cloudBackupCard.nowTick++
                        }

                        Text {
                            visible: cloudBackupCard.cloudTs > 0
                            // Reading nowTick establishes a binding dependency — re-evaluates every 30s tick
                            text: { var _t = cloudBackupCard.nowTick; return "Last backup: " + cloudBackupCard.relativeTime(cloudBackupCard.cloudTs) }
                            font.pixelSize: 12
                            color: root.textSecondary
                        }

                        // Empty state
                        Text {
                            visible: cloudBackupCard.cloudCid === "" && cloudBackupCard.cloudStatus !== "unavailable" && cloudBackupCard.cloudStatus !== "disabled"
                            text: "No backup yet — save a note to trigger the first upload"
                            font.pixelSize: 12
                            color: root.textSecondary
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: cloudBackupCard.cloudStatus === "unavailable" || cloudBackupCard.cloudStatus === "disabled"
                            text: "Logos Storage unavailable"
                            font.pixelSize: 12
                            color: root.textSecondary
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }

                        // Back up now button
                        Row {
                            spacing: 10
                            visible: cloudBackupCard.cloudStatus !== "uploading"
                                  && cloudBackupCard.cloudStatus !== "unavailable"
                                  && cloudBackupCard.cloudStatus !== "disabled"

                            Button {
                                id: backupNowBtn
                                text: "Back up now"
                                leftPadding: 24; rightPadding: 24
                                onClicked: {
                                    if (typeof logos === "undefined" || !logos.callModule) return
                                    var result = callModuleParse(logos.callModule("notes", "triggerBackup", []))
                                    if (result && result.error) {
                                        backupNowStatus.text = result.error
                                    } else {
                                        backupNowStatus.text = ""
                                        cloudBackupCard.cloudStatus = "uploading"
                                        backupStatusPoller.restart()
                                    }
                                }
                                contentItem: Text {
                                    text: backupNowBtn.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: "#FFFFFF"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: backupNowBtn.pressed ? root.primaryHover : root.primary
                                    radius: 16
                                    implicitHeight: 32
                                }
                            }
                        }

                        Text {
                            id: backupNowStatus
                            text: ""
                            visible: text.length > 0
                            font.pixelSize: 12
                            color: root.errorColor
                            width: parent.width
                            wrapMode: Text.WordWrap
                        }

                    }
                }

                // Danger Zone
                Rectangle {
                    width: parent.width
                    height: dangerCol.height + 40
                    color: root.bgSecondary
                    radius: 8
                    border.width: 1
                    border.color: root.errorColor

                    Column {
                        id: dangerCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                        spacing: 12

                        Text {
                            text: "Danger Zone"
                            color: root.errorColor
                            font.pixelSize: 14
                            font.weight: Font.Bold
                        }

                        Text {
                            text: "Resetting the app will wipe all notes. Make sure you backed up important data."
                            color: root.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                id: pluginConfirmCheck
                                property bool checked: false
                                width: 16; height: 16
                                radius: 3
                                color: checked ? root.primary : "transparent"
                                border.width: 2
                                border.color: checked ? root.primary : root.borderColor
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.checked ? "✓" : ""
                                    color: "#FFFFFF"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: parent.checked = !parent.checked
                                }
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
                            id: resetAppBtn
                            text: "Reset the app"
                            enabled: pluginConfirmCheck.checked
                            opacity: pluginConfirmCheck.checked ? 1.0 : 0.4
                            leftPadding: 24; rightPadding: 24
                            contentItem: Text {
                                text: resetAppBtn.text
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: root.dangerBtnBg
                                radius: 16
                                implicitHeight: 36
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
