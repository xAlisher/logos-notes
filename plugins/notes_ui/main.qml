import QtQuick
import QtQuick.Layouts

// Logos design system — available when loaded inside Logos App.
// Colours/spacing match the Logos dark palette exactly.
import Logos.Theme
import Logos.Controls

Item {
    id: root

    // ── Screen state ─────────────────────────────────────────────────────────
    // Managed locally; driven by callModule return values.
    property string currentScreen: "import"
    property string errorMessage:  ""

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var result = logos.callModule("notes", "isInitialized", [])
        root.currentScreen = (result === "true") ? "unlock" : "import"
    }

    Rectangle { anchors.fill: parent; color: Theme.palette.background }

    // ── Import screen ────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "import"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.spacing.large
            width: 420

            LogosText {
                Layout.fillWidth: true
                text: "Import Recovery Phrase"
                font.pixelSize: Theme.typography.titleText
                font.weight: Theme.typography.weightBold
                horizontalAlignment: Text.AlignHCenter
            }

            LogosText {
                text: "Recovery phrase"
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
            }

            Rectangle {
                Layout.fillWidth: true
                height: 100
                radius: Theme.spacing.radiusSmall
                color: Theme.palette.backgroundSecondary
                border.width: 1
                border.color: mnemonicArea.activeFocus
                              ? Theme.palette.overlayOrange
                              : Theme.palette.backgroundElevated

                TextEdit {
                    id: mnemonicArea
                    anchors { fill: parent; margins: Theme.spacing.medium }
                    color: Theme.palette.text
                    font.family: Theme.typography.publicSans
                    font.pixelSize: Theme.typography.secondaryText
                    wrapMode: TextEdit.Wrap

                    LogosText {
                        visible: mnemonicArea.text.length === 0
                        text: "Enter 12 or 24 word recovery phrase"
                        color: Theme.palette.textPlaceholder
                        font.pixelSize: Theme.typography.secondaryText
                    }
                }
            }

            LogosText {
                text: "PIN"
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
            }

            LogosTextField {
                id: importPinField
                Layout.fillWidth: true
                placeholderText: "PIN (min 4 digits)"
                echoMode: TextInput.Password
            }

            LogosText {
                text: "Confirm PIN"
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
            }

            LogosTextField {
                id: importPinConfirmField
                Layout.fillWidth: true
                placeholderText: "Confirm PIN"
                echoMode: TextInput.Password
            }

            LogosText {
                Layout.fillWidth: true
                text: root.errorMessage
                color: Theme.palette.error
                font.pixelSize: Theme.typography.secondaryText
                visible: root.currentScreen === "import" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            LogosButton {
                Layout.fillWidth: true
                text: "Import"
                background: Rectangle {
                    color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                    radius: Theme.spacing.radiusXlarge
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

    // ── Unlock screen ────────────────────────────────────────────────────────
    Item {
        anchors.fill: parent
        visible: root.currentScreen === "unlock"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.spacing.large
            width: 320

            LogosText {
                Layout.fillWidth: true
                text: "Unlock"
                font.pixelSize: Theme.typography.titleText
                font.weight: Theme.typography.weightBold
                horizontalAlignment: Text.AlignHCenter
            }

            LogosText {
                text: "PIN"
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.secondaryText
            }

            LogosTextField {
                id: unlockPinField
                Layout.fillWidth: true
                placeholderText: "Enter PIN"
                echoMode: TextInput.Password
                Keys.onReturnPressed: unlockButton.clicked()
            }

            LogosText {
                Layout.fillWidth: true
                text: root.errorMessage
                color: Theme.palette.error
                font.pixelSize: Theme.typography.secondaryText
                visible: root.currentScreen === "unlock" && root.errorMessage.length > 0
                wrapMode: Text.WordWrap
            }

            LogosButton {
                id: unlockButton
                Layout.fillWidth: true
                text: "Unlock"
                background: Rectangle {
                    color: parent.isActive ? Theme.palette.primaryHover : Theme.palette.primary
                    radius: Theme.spacing.radiusXlarge
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
        LogosText {
            anchors { bottom: parent.bottom; right: parent.right; margins: Theme.spacing.medium }
            text: "Reset"
            color: Theme.palette.error
            font.pixelSize: Theme.typography.secondaryText
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

    // ── Note screen ──────────────────────────────────────────────────────────
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
                topMargin: Theme.spacing.xxlarge
                leftMargin: Theme.spacing.xlarge
                rightMargin: Theme.spacing.xlarge
                bottomMargin: Theme.spacing.xlarge
            }
            color: Theme.palette.text
            font.family: "Courier New, monospace"
            font.pixelSize: Theme.typography.primaryText
            wrapMode: TextEdit.Wrap
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
            onTriggered: {
                if (typeof logos !== "undefined" && logos.callModule)
                    logos.callModule("notes", "saveNote", [editor.text])
            }
        }

        // DEV/DEMO reset
        LogosText {
            anchors { bottom: parent.bottom; right: parent.right; margins: Theme.spacing.medium }
            text: "Reset"
            color: Theme.palette.error
            font.pixelSize: Theme.typography.secondaryText
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
