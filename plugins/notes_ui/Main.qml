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
    property string restoreWarning: ""

    // ── Import tab state ─────────────────────────────────────────────────
    property int importTab: 0  // 0 = Recovery Phrase, 1 = Keycard, 2 = Logos Wallet

    // ── Keycard state ────────────────────────────────────────────────────
    property string keycardState: "unknown"
    property string keycardStatus: ""
    property bool keycardDetecting: false

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

    // Poll Keycard state while detection is active
    Timer {
        id: keycardPollTimer
        interval: 500
        repeat: true
        running: root.keycardDetecting
        onTriggered: {
            if (typeof logos === "undefined" || !logos.callModule) return
            var json = logos.callModule("notes", "getKeycardState", [])
            try {
                var obj = JSON.parse(json)
                root.keycardState = obj.state || "unknown"
                root.keycardStatus = obj.status || ""
            } catch(e) {}
        }
    }

    // Key source: "mnemonic" or "keycard"
    property string keySource: "mnemonic"

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var result = logos.callModule("notes", "isInitialized", [])
        if (result === "true") {
            var ks = logos.callModule("notes", "getKeySource", [])
            root.keySource = ks ? ks.trim() : "mnemonic"
            root.currentScreen = "unlock"
            // Auto-start Keycard detection for Keycard accounts
            if (root.keySource === "keycard") {
                logos.callModule("notes", "startKeycardDetection", [])
                root.keycardDetecting = true
            }
        } else {
            root.currentScreen = "import"
        }
    }

    Rectangle { anchors.fill: parent; color: root.bgColor }

    // ── Import screen ───────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import"

        onVisibleChanged: {
            if (visible) {
                mnemonicArea.text = ""
                importPinField.text = ""
                importPinConfirmField.text = ""
                keycardPinField.text = ""
                root.pendingBackupPath = ""
                root.restoreStatus = ""
                root.errorMessage = ""
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            width: 420

            Text {
                Layout.fillWidth: true
                text: "Import"
                font.pixelSize: 30
                font.weight: Font.Bold
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }

            // ── Tab bar ──────────────────────────────────────────────
            Row {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                Repeater {
                    model: ["Recovery Phrase", "Keycard", "Logos Wallet"]
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        width: 140
                        height: 36
                        color: root.importTab === index ? root.bgSecondary : "transparent"
                        border.width: root.importTab === index ? 1 : 0
                        border.color: root.importTab === index ? root.borderColor : "transparent"
                        radius: 8

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 12
                            font.weight: root.importTab === index ? Font.Bold : Font.Normal
                            color: root.importTab === index ? root.textColor
                                   : (index === 2 ? root.textPlaceholder : root.textSecondary)
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: index === 2 ? Qt.ArrowCursor : Qt.PointingHandCursor
                            onClicked: {
                                if (index === 2) return  // Wallet tab is TBD
                                root.importTab = index
                                root.errorMessage = ""
                            }
                        }
                    }
                }
            }

            // ── Tab 0: Recovery Phrase ─────────────────────────────────
            Item {
                Layout.fillWidth: true
                visible: root.importTab === 0
                implicitHeight: mnemonicCol.implicitHeight

                ColumnLayout {
                    id: mnemonicCol
                    anchors { left: parent.left; right: parent.right }
                    spacing: 16

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
                            opacity: activeFocus ? 1.0 : 0.0

                            Text {
                                visible: mnemonicArea.text.length === 0 && mnemonicArea.activeFocus
                                text: "Enter 12 or 24 word recovery phrase"
                                color: root.textPlaceholder
                                font.pixelSize: 12
                            }
                        }

                        Text {
                            anchors { fill: parent; margins: 12 }
                            visible: !mnemonicArea.activeFocus
                            color: mnemonicArea.text.length > 0 ? root.textColor : root.textPlaceholder
                            font.pixelSize: 12
                            text: {
                                if (mnemonicArea.text.length === 0)
                                    return "Enter 12 or 24 word recovery phrase"
                                var count = mnemonicArea.text.trim().split(/\s+/).length
                                return "••• " + count + " words entered •••"
                            }
                        }
                    }

                    Text { text: "PIN"; color: root.textSecondary; font.pixelSize: 12 }

                    TextField {
                        id: importPinField
                        Layout.fillWidth: true
                        placeholderText: "PIN (min 6 characters)"
                        echoMode: TextInput.Password
                        color: root.textColor; font.pixelSize: 14
                        placeholderTextColor: root.textPlaceholder
                        background: Rectangle {
                            color: root.bgSecondary; radius: 4; border.width: 1
                            border.color: importPinField.activeFocus ? root.overlayOrange : root.bgElevated
                        }
                    }

                    Text { text: "Confirm PIN"; color: root.textSecondary; font.pixelSize: 12 }

                    TextField {
                        id: importPinConfirmField
                        Layout.fillWidth: true
                        placeholderText: "Confirm PIN"
                        echoMode: TextInput.Password
                        color: root.textColor; font.pixelSize: 14
                        placeholderTextColor: root.textPlaceholder
                        background: Rectangle {
                            color: root.bgSecondary; radius: 4; border.width: 1
                            border.color: importPinConfirmField.activeFocus ? root.overlayOrange : root.bgElevated
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Import"
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 14; font.weight: Font.Medium
                            color: root.textColor
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.pressed ? root.primaryHover : root.primary
                            radius: 16; implicitHeight: 44
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
                                root.restoreWarning = parsed.warning || ""
                                root.currentScreen = "note"
                            } else {
                                root.errorMessage = parsed.error || "Import failed"
                            }
                        }
                    }
                }
            }

            // ── Tab 1: Keycard ───────────────────────────────────────
            Item {
                Layout.fillWidth: true
                visible: root.importTab === 1
                implicitHeight: keycardCol.implicitHeight

                ColumnLayout {
                    id: keycardCol
                    anchors { left: parent.left; right: parent.right }
                    spacing: 16

                    // Auto-start detection when switching to Keycard tab
                    Component.onCompleted: {
                        // Detection starts when user clicks the tab
                    }

                    Button {
                        Layout.fillWidth: true
                        text: {
                            if (!root.keycardDetecting) return "Connect Keycard"
                            if (root.keycardState === "authorized") return "Keycard Unlocked"
                            if (root.keycardState === "ready") return "Keycard Detected"
                            if (root.keycardState === "waitingForCard") return "Insert Keycard..."
                            if (root.keycardState === "waitingForReader") return "Connect Reader..."
                            if (root.keycardState === "connectingCard") return "Connecting..."
                            return "Detecting..."
                        }
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 14; font.weight: Font.Medium
                            color: root.textColor
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: {
                                if (root.keycardState === "authorized") return "#22c55e"
                                if (root.keycardState === "ready") return "#22c55e"
                                return parent.pressed ? "#3a3a3a" : root.bgSecondary
                            }
                            radius: 16; implicitHeight: 44; border.width: 1
                            border.color: {
                                if (root.keycardState === "ready" || root.keycardState === "authorized")
                                    return "#22c55e"
                                return root.borderColor
                            }
                        }
                        onClicked: {
                            if (typeof logos === "undefined" || !logos.callModule) return
                            if (!root.keycardDetecting) {
                                var result = logos.callModule("notes", "startKeycardDetection", [])
                                try {
                                    var parsed = JSON.parse(result)
                                    if (parsed.error) { root.errorMessage = parsed.error; return }
                                } catch(e) {}
                                root.keycardDetecting = true
                                root.errorMessage = ""
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.keycardStatus
                        color: {
                            if (root.keycardState === "ready" || root.keycardState === "authorized")
                                return "#22c55e"
                            if (root.keycardState === "emptyKeycard" || root.keycardState === "blockedPIN"
                                || root.keycardState === "blockedPUK" || root.keycardState === "notKeycard"
                                || root.keycardState === "connectionError" || root.keycardState === "noPCSC")
                                return root.errorColor
                            return root.textPlaceholder
                        }
                        font.pixelSize: 11
                        visible: root.keycardDetecting && root.keycardStatus.length > 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    // PIN field — visible when card is ready
                    Text {
                        text: "Keycard PIN"
                        color: root.textSecondary
                        font.pixelSize: 12
                        visible: root.keycardState === "ready"
                    }

                    TextField {
                        id: keycardPinField
                        Layout.fillWidth: true
                        placeholderText: "Enter Keycard PIN"
                        echoMode: TextInput.Password
                        visible: root.keycardState === "ready"
                        color: root.textColor; font.pixelSize: 14
                        placeholderTextColor: root.textPlaceholder
                        background: Rectangle {
                            color: root.bgSecondary; radius: 4; border.width: 1
                            border.color: keycardPinField.activeFocus ? root.overlayOrange : root.bgElevated
                        }
                        Keys.onReturnPressed: keycardImportBtn.clicked()
                    }

                    Button {
                        id: keycardImportBtn
                        Layout.fillWidth: true
                        visible: root.keycardState === "ready"
                        text: "Unlock & Import"
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 14; font.weight: Font.Medium
                            color: root.textColor
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.pressed ? root.primaryHover : root.primary
                            radius: 16; implicitHeight: 44
                        }
                        onClicked: {
                            if (typeof logos === "undefined" || !logos.callModule) return
                            root.errorMessage = ""
                            // Stop polling to avoid mutex race with ExportLoginKeys
                            root.keycardDetecting = false
                            var result = logos.callModule("notes", "importFromKeycard",
                                                          [keycardPinField.text])
                            var parsed = JSON.parse(result)
                            if (parsed.success) {
                                root.currentScreen = "note"
                            } else {
                                root.errorMessage = parsed.error || "Keycard import failed"
                                root.keycardDetecting = true  // Resume polling on failure
                            }
                            keycardPinField.text = ""
                        }
                    }
                }
            }

            // ── Tab 2: Logos Wallet (TBD) ─────────────────────────────
            Item {
                Layout.fillWidth: true
                visible: root.importTab === 2
                implicitHeight: 100

                Text {
                    anchors.centerIn: parent
                    text: "Logos Wallet integration coming soon"
                    color: root.textPlaceholder
                    font.pixelSize: 14
                }
            }

            // ── Error message (shared across tabs) ────────────────────
            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "import" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            // ── Restore from backup (Recovery Phrase tab only) ────────
            Text {
                id: pluginRestoreStatus
                Layout.fillWidth: true
                text: root.restoreStatus
                color: root.primary
                font.pixelSize: 12
                visible: root.importTab === 0 && text.length > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: root.importTab === 0
                text: root.pendingBackupPath.length > 0
                      ? "Change backup"
                      : "Restore from backup"
                color: root.pendingBackupPath.length > 0
                       ? root.primary
                       : root.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (typeof logos === "undefined" || !logos.callModule) return
                        var json = logos.callModule("notes", "listBackups", [])
                        var backups = JSON.parse(json)
                        if (backups.length === 0) {
                            root.restoreStatus = "No backups found in ~/.local/share/logos-notes/backups/"
                            return
                        }
                        if (backups.length === 1) {
                            root.pendingBackupPath = backups[0].path
                            root.restoreStatus = "Backup: " + backups[0].name
                            return
                        }
                        // Multiple backups — cycle to next one.
                        var currentIdx = -1
                        for (var i = 0; i < backups.length; i++) {
                            if (backups[i].path === root.pendingBackupPath) {
                                currentIdx = i
                                break
                            }
                        }
                        var nextIdx = (currentIdx + 1) % backups.length
                        root.pendingBackupPath = backups[nextIdx].path
                        root.restoreStatus = "Backup " + (nextIdx+1) + "/" + backups.length + ": " + backups[nextIdx].name
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

            // ── Mnemonic PIN unlock ──────────────────────────────
            Text {
                text: "PIN"
                color: root.textSecondary
                font.pixelSize: 12
                visible: root.keySource !== "keycard"
            }

            TextField {
                id: unlockPinField
                Layout.fillWidth: true
                placeholderText: "Enter PIN"
                echoMode: TextInput.Password
                visible: root.keySource !== "keycard"
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

            // ── Keycard unlock ───────────────────────────────────
            Text {
                text: "Insert Keycard and enter PIN"
                color: root.textSecondary
                font.pixelSize: 12
                visible: root.keySource === "keycard"
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            // Keycard status on unlock screen
            Text {
                Layout.fillWidth: true
                text: root.keycardStatus
                color: {
                    if (root.keycardState === "ready") return "#22c55e"
                    return root.textPlaceholder
                }
                font.pixelSize: 11
                visible: root.keySource === "keycard" && root.keycardDetecting
                horizontalAlignment: Text.AlignHCenter
            }

            TextField {
                id: unlockKeycardPinField
                Layout.fillWidth: true
                placeholderText: "Keycard PIN"
                echoMode: TextInput.Password
                visible: root.keySource === "keycard"
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textPlaceholder
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 4
                    border.width: 1
                    border.color: unlockKeycardPinField.activeFocus
                                  ? root.overlayOrange : root.bgElevated
                }
                Keys.onReturnPressed: unlockButton.clicked()
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
                      : (root.keySource === "keycard" ? "Unlock with Keycard" : "Unlock")
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

                    if (root.keySource === "keycard") {
                        // Stop polling to avoid mutex race with key export
                        root.keycardDetecting = false
                        var kcResult = logos.callModule("notes", "unlockWithKeycard",
                                                        [unlockKeycardPinField.text])
                        var kcParsed = JSON.parse(kcResult)
                        if (kcParsed.success) {
                            root.currentScreen = "note"
                        } else {
                            root.errorMessage = kcParsed.error || "Keycard unlock failed"
                            root.keycardDetecting = true  // Resume polling on failure
                        }
                        unlockKeycardPinField.text = ""
                        return
                    }

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

        // Warning banner for partial restore
        Rectangle {
            id: warningBanner
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: root.restoreWarning.length > 0 ? 36 : 0
            visible: root.restoreWarning.length > 0
            color: "#ca8a04"
            z: 10

            Text {
                anchors.centerIn: parent
                text: root.restoreWarning
                color: "#FFFFFF"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.restoreWarning = ""
            }
        }

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

        function saveCurrentNote() {
            saveTimer.stop()
            if (activeNoteId > 0 && !loading && typeof logos !== "undefined" && logos.callModule) {
                var result = logos.callModule("notes", "saveNote", [activeNoteId, editor.text])
                if (result !== "ok")
                    root.restoreWarning = "Failed to save note. Your changes may be lost."
            }
        }

        function selectNote(id) {
            saveCurrentNote()
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
            saveCurrentNote()
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
            onVisibleChanged: {
                if (visible) {
                    pluginConfirmCheck.checked = false
                    pluginExportStatus.text = ""
                }
            }
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
                            spacing: 8
                            Text {
                                text: "Encryption:"
                                color: root.textSecondary
                                font.pixelSize: 12
                            }
                            Text {
                                text: root.keySource === "keycard" ? "Keycard"
                                    : root.keySource === "wallet" ? "Logos Wallet"
                                    : "Recovery Phrase"
                                color: root.keySource === "keycard" ? "#22c55e" : root.textColor
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
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
                                property string fingerprint: ""
                                text: fingerprint
                                font.family: "Courier New, monospace"
                                font.pixelSize: 12
                                color: root.textColor
                                anchors.verticalCenter: parent.verticalCenter
                                Component.onCompleted: refreshFp()
                                function refreshFp() {
                                    if (typeof logos !== "undefined" && logos.callModule)
                                        fingerprint = logos.callModule("notes", "getAccountFingerprint", [])
                                }
                                Connections {
                                    target: noteScreen
                                    function onShowSettingsChanged() {
                                        if (noteScreen.showSettings) fpText.refreshFp()
                                    }
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
                            width: parent.width
                            wrapMode: Text.WrapAnywhere
                        }

                        Button {
                            text: "Export Backup"
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                var result = logos.callModule("notes", "exportBackupAuto", [])
                                var parsed = JSON.parse(result)
                                pluginExportStatus.text = parsed.ok
                                    ? "Saved to: " + parsed.path
                                    : "Export failed: " + (parsed.error || "unknown")
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

                            Rectangle {
                                id: pluginConfirmCheck
                                property bool checked: false
                                width: 20; height: 20
                                radius: 4
                                color: checked ? root.overlayOrange : "transparent"
                                border.width: 2
                                border.color: checked ? root.overlayOrange : "#3a3a3a"
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.checked ? "✓" : ""
                                    color: "#FFFFFF"
                                    font.pixelSize: 14
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
