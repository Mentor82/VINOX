import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: root

    property string reasoningText: ""
    property bool isStreaming: false
    property bool isExpanded: false

    visible: reasoningText.length > 0
    Layout.fillWidth: true
    radius: Theme.radiusMd
    color: Theme.bgSidebar
    border.color: root.isStreaming ? Theme.reasoning : Theme.reasoningSubtle
    implicitHeight: reasonCol.implicitHeight + 16

    ColumnLayout {
        id: reasonCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Header Bar: Stream Channel Indicator, Pulse, Token counter, Copy Button, Collapse Toggle
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // Channel Indicator Dot (Pulse during streaming)
            Rectangle {
                width: 8; height: 8; radius: 4
                color: Theme.reasoning
                opacity: root.isStreaming ? 0.5 : 1.0

                SequentialAnimation on opacity {
                    running: root.isStreaming
                    loops: Animation.Infinite
                    PropertyAnimation { to: 1.0; duration: 500 }
                    PropertyAnimation { to: 0.3; duration: 500 }
                }
            }

            Text {
                text: root.isStreaming ? "🧠 Denken..." : "🧠 Denken (<think>)"
                color: Theme.reasoning
                font.bold: true
                font.pixelSize: Theme.fontSizeSm
                font.family: Theme.fontUi
            }

            Text {
                visible: !root.isExpanded && root.reasoningText.length > 0
                text: "· " + Math.max(1, Math.round(root.reasoningText.length / 4)) + " Tokens (eingeklappt)"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontUi
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.isExpanded = !root.isExpanded
                }
            }

            // Copy Reasoning Button
            Rectangle {
                implicitWidth: lblCopyReason.implicitWidth + 12
                implicitHeight: 20
                radius: Theme.radiusXs
                color: copyHover.containsMouse ? Theme.bgCardHover : Theme.bgCard
                border.color: copyTimer.running ? Theme.success : Theme.borderMuted

                Text {
                    id: lblCopyReason
                    anchors.centerIn: parent
                    text: copyTimer.running ? "✓ Kopiert!" : "📋 Kopieren"
                    color: copyTimer.running ? Theme.success : (copyHover.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontUi
                }

                MouseArea {
                    id: copyHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (typeof bridge !== "undefined") {
                            bridge.copyToClipboard(root.reasoningText);
                            copyTimer.start();
                        }
                    }
                }

                Timer {
                    id: copyTimer
                    interval: 1500
                    repeat: false
                }
            }

            // Collapse / Expand Toggle Button
            Rectangle {
                implicitWidth: 22
                implicitHeight: 20
                radius: Theme.radiusXs
                color: toggleHover.containsMouse ? Theme.bgCardHover : Theme.bgCard
                border.color: Theme.borderMuted

                Text {
                    anchors.centerIn: parent
                    text: root.isExpanded ? "▲" : "▼"
                    color: Theme.reasoning
                    font.pixelSize: Theme.fontSizeXs
                }

                MouseArea {
                    id: toggleHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.isExpanded = !root.isExpanded
                }
            }
        }

        // Divider when expanded
        Rectangle {
            visible: root.isExpanded
            Layout.fillWidth: true
            height: 1
            color: Theme.borderMuted
            opacity: 0.6
        }

        // Trace Text Edit (Selectable + Monospace + Hellgrau)
        TextEdit {
            visible: root.isExpanded
            Layout.fillWidth: true
            text: root.reasoningText
            color: "#C9D1D9" // Hellgrau im Text
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSizeSm
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            selectionColor: Theme.reasoning
            selectedTextColor: Theme.textOnAccent
        }
    }
}
