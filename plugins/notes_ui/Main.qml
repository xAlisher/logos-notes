import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Design System (#43) ─────────────────────────────────────────────────
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
    readonly property color statusGray:       "#808080"
    readonly property color inputBorder:      "#383838"
    readonly property color borderColor:      "#3A3A3A"

    readonly property string appVersion: "V 1.0.0"

    // ── Screen state ────────────────────────────────────────────────────────
    property string currentScreen: "import"
    property string errorMessage:  ""
    property int lockoutRemaining: 0
    property string pendingBackupPath: ""
    property string restoreStatus: ""
    property string restoreWarning: ""

    // ── Keycard state ────────────────────────────────────────────────────
    property string keycardState: "unknown"
    property string keycardStatus: ""
    property bool keycardDetecting: false
    property string keySource: "mnemonic"

    // ── Derived state for two-line status ────────────────────────────────
    property bool readerConnected: keycardState !== "unknown" && keycardState !== "noPCSC"
                                   && keycardState !== "waitingForReader"
    property bool cardDetected: keycardState === "ready" || keycardState === "authorized"
    property bool keycardReady: cardDetected

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

    // Poll Keycard state
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

    // Auto-lock on card/reader removal
    Timer {
        id: keycardGuardTimer
        interval: 2000
        repeat: true
        running: root.keySource === "keycard" && root.currentScreen === "note"
        onTriggered: {
            if (typeof logos === "undefined" || !logos.callModule) return
            var json = logos.callModule("notes", "getKeycardState", [])
            try {
                var obj = JSON.parse(json)
                var st = obj.state || "unknown"
                if (st === "waitingForCard" || st === "waitingForReader"
                    || st === "unknown" || st === "noPCSC") {
                    logos.callModule("notes", "lockSession", [])
                    root.keycardDetecting = false
                    root.currentScreen = "unlock"
                    root.errorMessage = st === "waitingForReader"
                        ? "Card reader disconnected — session locked"
                        : "Keycard removed — session locked"
                    logos.callModule("notes", "startKeycardDetection", [])
                    root.keycardDetecting = true
                }
            } catch(e) {}
        }
    }

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var result = logos.callModule("notes", "isInitialized", [])
        if (result === "true") {
            var ks = logos.callModule("notes", "getKeySource", [])
            root.keySource = ks ? ks.trim() : "mnemonic"
            root.currentScreen = "unlock"
            if (root.keySource === "keycard") {
                logos.callModule("notes", "startKeycardDetection", [])
                root.keycardDetecting = true
            }
        } else {
            root.currentScreen = "import"
            // Auto-start Keycard detection on import screen
            logos.callModule("notes", "startKeycardDetection", [])
            root.keycardDetecting = true
        }
    }

    Rectangle { anchors.fill: parent; color: root.bgColor }

    // ── Blinking dot animation for searching states ─────────────────────
    SequentialAnimation {
        id: dotBlink
        loops: Animation.Infinite
        running: root.keycardDetecting && (!root.readerConnected || !root.cardDetected)
        PropertyAnimation { target: dotBlinkTarget; property: "opacity"; from: 1.0; to: 0.3; duration: 750 }
        PropertyAnimation { target: dotBlinkTarget; property: "opacity"; from: 0.3; to: 1.0; duration: 750 }
    }
    QtObject { id: dotBlinkTarget; property real opacity: 1.0 }

    // ═══════════════════════════════════════════════════════════════════════
    // ── CREATE NEW DATABASE screen (#39) ─────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import"

        onVisibleChanged: {
            if (visible) {
                keycardPinField.text = ""
                root.pendingBackupPath = ""
                root.restoreStatus = ""
                root.errorMessage = ""
            }
        }

        // Version label
        Text {
            x: 24; y: 16
            text: root.appVersion
            color: root.textPlaceholder
            font.pixelSize: 11
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 24
            width: 420

            Column {
                spacing: 4
                Text {
                    text: "Create new database"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: root.textColor
                }
                Text {
                    text: "Import keys to encrypt your notes"
                    color: root.textPlaceholder
                    font.pixelSize: 14
                }
            }

            // ── Two-line status indicators ──────────────────────────
            Column {
                spacing: 8
                Layout.fillWidth: true

                // Line 1: Reader status
                Row {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.readerConnected ? root.successGreen
                             : (root.keycardState === "noPCSC" || root.keycardState === "waitingForReader" && root.keycardDetecting && root.keycardState !== "unknown"
                                ? root.errorColor : root.statusGray)
                        opacity: (!root.readerConnected && root.keycardDetecting
                                  && root.keycardState !== "noPCSC") ? dotBlinkTarget.opacity : 1.0
                    }
                    Text {
                        text: root.readerConnected ? "Smart card reader connected"
                            : (root.keycardState === "noPCSC" ? "Smart card reader not found"
                            : "Looking for smart card reader")
                        color: root.readerConnected ? root.successGreen
                             : (root.keycardState === "noPCSC" ? root.errorColor : root.textSecondary)
                        font.pixelSize: 13
                    }
                }

                // Line 2: Card status (only visible when reader connected)
                Row {
                    spacing: 8
                    visible: root.readerConnected
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.cardDetected ? root.successGreen
                             : (root.keycardState === "waitingForCard" ? root.statusGray
                             : (root.keycardState === "notKeycard" || root.keycardState === "emptyKeycard"
                                || root.keycardState === "connectionError" ? root.errorColor : root.statusGray))
                        opacity: (root.readerConnected && !root.cardDetected
                                  && root.keycardState === "waitingForCard") ? dotBlinkTarget.opacity : 1.0
                    }
                    Text {
                        text: root.cardDetected ? "Keycard detected"
                            : (root.keycardState === "notKeycard" ? "Not a Keycard"
                            : (root.keycardState === "emptyKeycard" ? "Keycard not initialized"
                            : (root.keycardState === "connectionError" ? "Connection error"
                            : (root.keycardState === "waitingForCard" ? "Looking for Keycard"
                            : "Keycard not found"))))
                        color: root.cardDetected ? root.successGreen
                             : (root.keycardState === "waitingForCard" ? root.textSecondary : root.errorColor)
                        font.pixelSize: 13
                    }
                }
            }

            // ── PIN field ───────────────────────────────────────────
            TextField {
                id: keycardPinField
                Layout.fillWidth: true
                placeholderText: "Enter Keycard PIN"
                echoMode: TextInput.Password
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary
                    radius: 3
                    border.width: 1
                    border.color: keycardPinField.activeFocus ? root.primary : root.inputBorder
                }
                Keys.onReturnPressed: {
                    if (root.keycardReady) createBtn.clicked()
                }
            }

            // ── Error message ───────────────────────────────────────
            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "import" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            // ── Create button ───────────────────────────────────────
            Button {
                id: createBtn
                Layout.fillWidth: true
                enabled: root.keycardReady
                text: "Create"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: root.keycardReady ? root.textColor : root.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: root.keycardReady
                           ? (parent.pressed ? root.primaryHover : root.primary)
                           : root.bgSecondary
                    radius: 16
                    implicitHeight: 40
                }
                onClicked: {
                    if (typeof logos === "undefined" || !logos.callModule) return
                    root.errorMessage = ""
                    root.keycardDetecting = false
                    var result = logos.callModule("notes", "importFromKeycard",
                                                  [keycardPinField.text])
                    var parsed = JSON.parse(result)
                    if (parsed.success) {
                        root.keySource = "keycard"
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Failed to create database"
                        root.keycardDetecting = true
                    }
                    keycardPinField.text = ""
                }
            }

            // ── Decrypt backup link ─────────────────────────────────
            Text {
                Layout.fillWidth: true
                text: root.pendingBackupPath.length > 0
                      ? "Change backup: " + root.pendingBackupPath.split("/").pop()
                      : "Decrypt existing .imnotes backup"
                color: root.primary
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
                            root.restoreStatus = "No backups found"
                            return
                        }
                        if (backups.length === 1) {
                            root.pendingBackupPath = backups[0].path
                            root.restoreStatus = "Backup: " + backups[0].name
                            return
                        }
                        var currentIdx = -1
                        for (var i = 0; i < backups.length; i++) {
                            if (backups[i].path === root.pendingBackupPath) { currentIdx = i; break }
                        }
                        var nextIdx = (currentIdx + 1) % backups.length
                        root.pendingBackupPath = backups[nextIdx].path
                        root.restoreStatus = "Backup " + (nextIdx+1) + "/" + backups.length + ": " + backups[nextIdx].name
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.restoreStatus
                color: root.primary
                font.pixelSize: 11
                visible: text.length > 0
                horizontalAlignment: Text.AlignHCenter
            }

        }

        Text {
            anchors { bottom: parent.bottom; bottomMargin: 24; horizontalCenter: parent.horizontalCenter }
            text: "Create new database with recovery phrase (legacy)"
            color: root.textPlaceholder
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.currentScreen = "import_mnemonic"
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ── LEGACY MNEMONIC IMPORT screen ────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import_mnemonic"

        onVisibleChanged: {
            if (visible) {
                mnemonicArea.text = ""
                importPinField.text = ""
                importPinConfirmField.text = ""
                root.errorMessage = ""
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
            spacing: 16
            width: 420

            Text {
                text: "Create with recovery phrase"
                font.pixelSize: 28
                font.weight: Font.Bold
                color: root.textColor
            }

            Text {
                text: "Recovery phrase"
                color: root.textSecondary
                font.pixelSize: 12
            }

            Rectangle {
                Layout.fillWidth: true
                height: 100
                radius: 3
                color: root.bgSecondary
                border.width: 1
                border.color: mnemonicArea.activeFocus ? root.primary : root.inputBorder

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
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary; radius: 3; border.width: 1
                    border.color: importPinField.activeFocus ? root.primary : root.inputBorder
                }
            }

            Text { text: "Confirm PIN"; color: root.textSecondary; font.pixelSize: 12 }

            TextField {
                id: importPinConfirmField
                Layout.fillWidth: true
                placeholderText: "Confirm PIN"
                echoMode: TextInput.Password
                color: root.textColor; font.pixelSize: 14
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary; radius: 3; border.width: 1
                    border.color: importPinConfirmField.activeFocus ? root.primary : root.inputBorder
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "import_mnemonic" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: "Create"
                contentItem: Text {
                    text: parent.text; font.pixelSize: 14; font.weight: Font.Medium
                    color: root.textColor
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.pressed ? root.primaryHover : root.primary
                    radius: 16; implicitHeight: 40
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
                        root.keySource = "mnemonic"
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Import failed"
                    }
                }
            }

            Text {
                text: "Back to Keycard import"
                color: root.primary
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.currentScreen = "import"
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ── UNLOCK NOTES screen (#40) ────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "unlock"

        Text {
            x: 24; y: 16
            text: root.appVersion
            color: root.textPlaceholder
            font.pixelSize: 11
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 24
            width: 420

            Column {
                spacing: 4
                Text {
                    text: "Unlock notes"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: root.textColor
                }
                Text {
                    text: {
                        var fp = ""
                        if (typeof logos !== "undefined" && logos.callModule)
                            fp = logos.callModule("notes", "getAccountFingerprint", [])
                        return "Decrypt database. Fingerprint: " + fp
                    }
                    color: root.textPlaceholder
                    font.pixelSize: 14
                }
            }

            // ── Keycard two-line status (keycard accounts only) ─────
            Column {
                spacing: 8
                Layout.fillWidth: true
                visible: root.keySource === "keycard"

                Row {
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.readerConnected ? root.successGreen
                             : (root.keycardState === "noPCSC" ? root.errorColor : root.statusGray)
                        opacity: (!root.readerConnected && root.keycardDetecting
                                  && root.keycardState !== "noPCSC") ? dotBlinkTarget.opacity : 1.0
                    }
                    Text {
                        text: root.readerConnected ? "Smart card reader connected"
                            : (root.keycardState === "noPCSC" ? "Smart card reader not found"
                            : "Looking for smart card reader")
                        color: root.readerConnected ? root.successGreen
                             : (root.keycardState === "noPCSC" ? root.errorColor : root.textSecondary)
                        font.pixelSize: 13
                    }
                }

                Row {
                    spacing: 8
                    visible: root.readerConnected
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.cardDetected ? root.successGreen
                             : (root.keycardState === "waitingForCard" ? root.statusGray : root.errorColor)
                        opacity: (root.readerConnected && !root.cardDetected
                                  && root.keycardState === "waitingForCard") ? dotBlinkTarget.opacity : 1.0
                    }
                    Text {
                        text: root.cardDetected ? "Keycard detected"
                            : (root.keycardState === "waitingForCard" ? "Looking for Keycard"
                            : "Keycard not found")
                        color: root.cardDetected ? root.successGreen
                             : (root.keycardState === "waitingForCard" ? root.textSecondary : root.errorColor)
                        font.pixelSize: 13
                    }
                }
            }

            // ── Mnemonic PIN field ──────────────────────────────────
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
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary; radius: 3; border.width: 1
                    border.color: unlockPinField.activeFocus ? root.primary : root.inputBorder
                }
                Keys.onReturnPressed: {
                    if (root.lockoutRemaining === 0) unlockButton.clicked()
                }
            }

            // ── Keycard PIN field ───────────────────────────────────
            TextField {
                id: unlockKeycardPinField
                Layout.fillWidth: true
                placeholderText: "Enter Keycard PIN"
                echoMode: TextInput.Password
                visible: root.keySource === "keycard"
                color: root.textColor
                font.pixelSize: 14
                placeholderTextColor: root.textDisabled
                background: Rectangle {
                    color: root.bgSecondary; radius: 3; border.width: 1
                    border.color: unlockKeycardPinField.activeFocus ? root.primary : root.inputBorder
                }
                Keys.onReturnPressed: unlockButton.clicked()
            }

            // ── Error message ───────────────────────────────────────
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

            // ── Unlock button ───────────────────────────────────────
            Button {
                id: unlockButton
                Layout.fillWidth: true
                enabled: root.keySource === "keycard" ? root.keycardReady : root.lockoutRemaining === 0
                text: root.lockoutRemaining > 0
                      ? "Locked (" + root.lockoutRemaining + "s)"
                      : "Unlock"
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: unlockButton.enabled ? root.textColor : root.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: unlockButton.enabled
                           ? (parent.pressed ? root.primaryHover : root.primary)
                           : root.bgSecondary
                    radius: 16
                    implicitHeight: 40
                }
                onClicked: {
                    root.errorMessage = ""
                    if (typeof logos === "undefined" || !logos.callModule) return

                    if (root.keySource === "keycard") {
                        root.keycardDetecting = false
                        var kcResult = logos.callModule("notes", "unlockWithKeycard",
                                                        [unlockKeycardPinField.text])
                        var kcParsed = JSON.parse(kcResult)
                        if (kcParsed.success) {
                            root.currentScreen = "note"
                        } else {
                            root.errorMessage = kcParsed.error || "Unlock failed"
                            root.keycardDetecting = true
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

    // ═══════════════════════════════════════════════════════════════════════
    // ── NOTE EDITOR screen (#41) ─────────────────────────────────────────
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        id: noteScreen
        anchors.fill: parent
        visible: root.currentScreen === "note"

        property int activeNoteId: -1
        property bool loading: false
        property bool showSettings: false

        // Warning banner
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
                selectNote(noteModel.get(0).id)
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
                if (result && result.charAt(0) === '{') {
                    try {
                        var parsed = JSON.parse(result)
                        if (parsed.error) {
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

        // Keyboard shortcuts
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

            // New Note button
            Rectangle {
                id: newNoteBtn
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 48
                color: newNoteArea.containsMouse ? "#333333" : "transparent"

                Row {
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    Text {
                        text: "+"
                        color: root.primary
                        font.pixelSize: 16
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "Ctrl+N"
                        color: root.textColor
                        font.pixelSize: 13
                    }
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

                    // Active indicator bar
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

                    // Lock icon (simple padlock shape)
                    Text {
                        text: "🔒"
                        color: root.primary
                        font.pixelSize: 14
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

                Text {
                    anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                    text: {
                        if (typeof logos !== "undefined" && logos.callModule)
                            return logos.callModule("notes", "getAccountFingerprint", [])
                        return ""
                    }
                    color: fpHoverArea.containsMouse ? root.primary : root.textSecondary
                    font.pixelSize: 11
                    font.family: "Courier New, monospace"
                    MouseArea {
                        id: fpHoverArea
                        anchors { fill: parent; margins: -8 }
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
            visible: !noteScreen.showSettings
            anchors {
                top: parent.top; topMargin: 24
                left: sidebar.right; leftMargin: 24
                right: parent.right; rightMargin: 24
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
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: root.borderColor
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

        // ═════════════════════════════════════════════════════════════
        // ── SETTINGS panel (#42) ─────────────────────────────────────
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

            // Version
            Text {
                x: 24; y: 16
                text: root.appVersion
                color: root.textPlaceholder
                font.pixelSize: 11
            }

            // Close button (X)
            Rectangle {
                anchors { top: parent.top; right: parent.right; topMargin: 12; rightMargin: 16 }
                width: 24; height: 24; radius: 12
                color: root.primary
                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: root.textColor
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                MouseArea {
                    anchors.fill: parent
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

                // ── Current database section ────────────────────────
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
                                text: {
                                    if (root.keySource === "keycard") {
                                        var uid = ""
                                        if (typeof logos !== "undefined" && logos.callModule) {
                                            var json = logos.callModule("notes", "getKeycardState", [])
                                            try {
                                                // Try to get UID from state, fallback to short display
                                            } catch(e) {}
                                        }
                                        return "Keycard"
                                    }
                                    return root.keySource === "wallet" ? "Logos Wallet" : "Recovery Phrase"
                                }
                                color: root.textColor
                                font.pixelSize: 12
                            }
                        }

                        Row {
                            spacing: 8
                            Text { text: "Fingerprint:"; color: root.textSecondary; font.pixelSize: 12 }
                            Text {
                                id: fpText
                                property string fingerprint: ""
                                text: fingerprint
                                font.family: "Courier New, monospace"
                                font.pixelSize: 12
                                color: root.textColor
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

                // ── Backup section ──────────────────────────────────
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
                            text: "Export your notes as encrypted {fingerprint}_{datestamp}.imnotes file"
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
                                implicitHeight: 32
                            }
                        }
                    }
                }

                // ── Danger Zone ─────────────────────────────────────
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
                            text: "Resetting the app will wipe all notes from current app database. Make sure you backed up important data."
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
                                text: "I understand all notes will be permanently deleted from app database"
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
                            text: "Reset the app"
                            enabled: pluginConfirmCheck.checked
                            opacity: pluginConfirmCheck.checked ? 1.0 : 0.35
                            contentItem: Text {
                                text: parent.text
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: root.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: root.dangerBtnBg
                                radius: 16
                                implicitHeight: 32
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
