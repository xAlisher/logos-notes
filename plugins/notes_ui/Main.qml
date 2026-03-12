import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// Logos dark theme colors (hardcoded — QML sandbox blocks Logos.Theme import)
pragma ComponentBehavior: Bound

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
                placeholderText: "PIN (min 4 digits)"
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
                                                   importPinConfirmField.text])
                    var parsed = JSON.parse(result)
                    if (parsed.success) {
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Import failed"
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
                text: "PIN"
                color: root.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id: unlockPinField
                Layout.fillWidth: true
                placeholderText: "Enter PIN"
                echoMode: TextInput.Password
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
                Keys.onReturnPressed: unlockButton.clicked()
            }

            Text {
                Layout.fillWidth: true
                text: root.errorMessage
                color: root.errorColor
                font.pixelSize: 12
                visible: root.currentScreen === "unlock" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            Button {
                id: unlockButton
                Layout.fillWidth: true
                text: "Unlock"
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
                    var result = logos.callModule("notes", "unlockWithPin",
                                                  [unlockPinField.text])
                    var parsed = JSON.parse(result)
                    if (parsed.success) {
                        root.currentScreen = "note"
                    } else {
                        root.errorMessage = parsed.error || "Unlock failed"
                    }
                }
            }
        }

        // DEV/DEMO reset
        Text {
            anchors { bottom: parent.bottom; right: parent.right; margins: 12 }
            text: "Reset"
            color: root.errorColor
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (typeof logos !== "undefined" && logos.callModule)
                        logos.callModule("notes", "resetAndWipe", [])
                    root.errorMessage = ""
                    root.currentScreen = "import"
                }
            }
        }
    }

    // ── Note screen ─────────────────────────────────────────────────────────
    Item {
        id: noteScreen
        anchors.fill: parent
        visible: root.currentScreen === "note"

        property string savedText: ""

        onVisibleChanged: {
            if (visible && typeof logos !== "undefined" && logos.callModule) {
                noteScreen.savedText = logos.callModule("notes", "loadNote", [])
                editor.text = noteScreen.savedText
            }
        }

        TextEdit {
            id: editor
            anchors {
                fill: parent
                topMargin: 40
                leftMargin: 20
                rightMargin: 20
                bottomMargin: 20
            }
            color: root.textColor
            font.family: "Courier New, monospace"
            font.pixelSize: 14
            wrapMode: TextEdit.Wrap
            selectionColor: root.overlayOrange
            selectedTextColor: root.textColor

            Text {
                visible: editor.text.length === 0
                text: "Start writing..."
                color: root.textPlaceholder
                font.pixelSize: 14
            }

            onTextChanged: saveTimer.restart()
        }

        Timer {
            id: saveTimer
            interval: 1500
            onTriggered: {
                if (typeof logos !== "undefined" && logos.callModule)
                    logos.callModule("notes", "saveNote", [editor.text])
            }
        }

        // Bottom bar: Lock + Reset
        Row {
            anchors { bottom: parent.bottom; right: parent.right; margins: 12 }
            spacing: 16

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

            Text {
                text: "Reset"
                color: root.errorColor
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (typeof logos !== "undefined" && logos.callModule)
                            logos.callModule("notes", "resetAndWipe", [])
                        root.errorMessage = ""
                        root.currentScreen = "import"
                    }
                }
            }
        }
    }
}
