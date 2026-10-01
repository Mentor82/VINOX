import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: root

    property int rank: 1
    property string title: ""
    property string documentId: ""
    property string matchType: "HYBRID RRF"
    property real score: 0.0
    property string snippet: ""

    width: parent ? parent.width : 500
    radius: Theme.radiusMd
    color: Theme.bgSidebar
    border.color: cardHover.containsMouse ? Theme.borderActive : Theme.borderMuted
    implicitHeight: resCardCol.implicitHeight + 20

    MouseArea {
        id: cardHover
        anchors.fill: parent
        hoverEnabled: true
    }

    ColumnLayout {
        id: resCardCol
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Header Row: Doc Title, ID, Badges, Copy Button
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                width: 28; height: 28; radius: Theme.radiusSm
                color: Theme.bgCard
                border.color: Theme.borderMuted
                Text {
                    anchors.centerIn: parent
                    text: "#" + root.rank
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: Theme.fontSizeSm
                    font.family: Theme.fontMono
                }
            }

            Column {
                Layout.fillWidth: true
                Text {
                    text: root.title && root.title.length > 0 ? root.title : "Dokument " + root.documentId
                    color: Theme.textPrimary
                    font.bold: true
                    font.pixelSize: Theme.fontSizeBase
                    font.family: Theme.fontUi
                }
                Text {
                    text: "ID: " + root.documentId
                    color: Theme.textTertiary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Match Policy Badge
            Rectangle {
                implicitWidth: lblType.implicitWidth + 12
                implicitHeight: 22
                radius: Theme.radiusXs
                color: root.matchType.indexOf("VECTOR") !== -1 ? Theme.retrievalSubtle : (root.matchType.indexOf("RRF") !== -1 ? Theme.accentSubtle : Theme.bgCard)
                border.color: root.matchType.indexOf("VECTOR") !== -1 ? Theme.retrieval : (root.matchType.indexOf("RRF") !== -1 ? Theme.accent : Theme.borderMuted)
                Text {
                    id: lblType
                    anchors.centerIn: parent
                    text: root.matchType ? root.matchType : "HYBRID RRF"
                    color: root.matchType.indexOf("VECTOR") !== -1 ? Theme.retrieval : (root.matchType.indexOf("RRF") !== -1 ? Theme.accent : Theme.textSecondary)
                    font.bold: true
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontMono
                }
            }

            // Calibrated Score Display
            Rectangle {
                implicitWidth: lblScore.implicitWidth + 12
                implicitHeight: 22
                radius: Theme.radiusXs
                color: Theme.bgCard
                border.color: Theme.borderMuted
                Text {
                    id: lblScore
                    anchors.centerIn: parent
                    text: "Score: " + root.score.toFixed(4)
                    color: Theme.success
                    font.bold: true
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Copy Snippet Button
            Button {
                text: copyFeedback.running ? "✓ Kopiert!" : "📋 Kopieren"
                implicitHeight: 26
                font.pixelSize: Theme.fontSizeSm
                onClicked: {
                    if (typeof bridge !== "undefined") {
                        bridge.copyToClipboard(root.snippet ? root.snippet : root.title);
                        copyFeedback.start();
                    }
                }

                Timer {
                    id: copyFeedback
                    interval: 1500
                    repeat: false
                }
            }
        }

        // Content Snippet
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusSm
            color: Theme.bgApp
            border.color: Theme.borderSubtle
            implicitHeight: snipText.implicitHeight + 16

            TextEdit {
                id: snipText
                anchors.fill: parent
                anchors.margins: 8
                text: root.snippet && root.snippet.length > 0 ? root.snippet : "(Keine Vorschau verfügbar)"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSm
                font.family: Theme.fontUi
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                selectionColor: Theme.accentSubtle
                selectedTextColor: Theme.textPrimary
            }
        }
    }
}
