import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

ColumnLayout {
    id: root
    width: parent ? parent.width : 400
    spacing: 6

    property string role: "assistant"
    property string messageText: ""
    property string reasoningText: ""
    property bool isStreaming: false

    readonly property string effectiveReasoning: {
        if (root.reasoningText && root.reasoningText.length > 0) {
            return root.reasoningText;
        }
        if (root.messageText && root.messageText.indexOf("<think>") !== -1) {
            var parts = root.messageText.split("<think>");
            var after = parts.slice(1).join("<think>");
            if (after.indexOf("</think>") !== -1) {
                return after.split("</think>")[0].trim();
            } else {
                return after.trim();
            }
        }
        return "";
    }

    readonly property string effectiveMessage: {
        if (root.messageText && root.messageText.indexOf("<think>") !== -1) {
            var parts = root.messageText.split("<think>");
            var before = parts[0];
            var after = parts.slice(1).join("<think>");
            if (after.indexOf("</think>") !== -1) {
                var rest = after.split("</think>").slice(1).join("</think>");
                return (before + rest).trim();
            } else {
                return before.trim();
            }
        }
        return root.messageText;
    }

    // Header Row: Sender Badge + Copy Message Button
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            width: 8; height: 8; radius: 4
            color: root.role === "user" ? Theme.accent : Theme.success
        }

        Text {
            text: root.role === "user" ? "BENUTZER" : "ASSISTENT (VINOX LLM)"
            color: root.role === "user" ? Theme.accent : Theme.success
            font.bold: true
            font.pixelSize: Theme.fontSizeSm
            font.family: Theme.fontUi
        }

        Item { Layout.fillWidth: true }

        // Copy Message Text Button
        Rectangle {
            id: btnCopyMsg
            implicitWidth: lblCopyMsg.implicitWidth + 14
            implicitHeight: 22
            radius: Theme.radiusXs
            color: copyMsgHover.containsMouse ? Theme.bgCardHover : Theme.bgSidebar
            border.color: copyMsgTimer.running ? Theme.success : Theme.borderMuted

            RowLayout {
                anchors.centerIn: parent
                spacing: 4
                Text {
                    id: lblCopyMsg
                    text: copyMsgTimer.running ? "✓ Kopiert!" : "📋 Kopieren"
                    color: copyMsgTimer.running ? Theme.success : (copyMsgHover.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                    font.pixelSize: Theme.fontSizeSm
                    font.family: Theme.fontUi
                }
            }

            MouseArea {
                id: copyMsgHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (typeof bridge !== "undefined") {
                        bridge.copyToClipboard(root.messageText);
                        copyMsgTimer.start();
                    }
                }
            }

            Timer {
                id: copyMsgTimer
                interval: 1500
                repeat: false
            }
        }
    }

    // Reasoning Channel Card (Displayed if reasoning stream has data)
    ReasoningCard {
        reasoningText: root.effectiveReasoning
        isStreaming: root.isStreaming
    }

    // Main Message Content Card
    Rectangle {
        visible: root.effectiveMessage.length > 0 || (root.role === "assistant" && root.effectiveReasoning.length === 0)
        Layout.fillWidth: true
        radius: Theme.radiusMd
        color: root.role === "user" ? Theme.bgSidebar : Theme.bgCard
        border.color: root.role === "user" ? Theme.borderActive : Theme.borderMuted
        implicitHeight: msgTextEdit.implicitHeight + 24

        TextEdit {
            id: msgTextEdit
            anchors.fill: parent
            anchors.margins: 12
            text: root.effectiveMessage.length > 0 ? root.effectiveMessage : (root.isStreaming ? "..." : "")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeMd
            font.family: Theme.fontUi
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            selectionColor: Theme.accent
            selectedTextColor: Theme.textOnAccent
        }
    }
}
