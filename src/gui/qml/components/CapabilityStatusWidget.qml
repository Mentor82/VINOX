import QtQuick 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: root

    property string genDevice: (typeof bridge !== "undefined") ? bridge.currentDevice : "NPU"
    property bool isGenerating: (typeof bridge !== "undefined") ? bridge.isGenerating : false
    property bool isEmbLoaded: (typeof bridge !== "undefined") ? bridge.isEmbeddingLoaded : false
    property string embDevice: (typeof bridge !== "undefined") ? bridge.embeddingDevice : "CPU"
    property bool isNvme: (typeof bridge !== "undefined") ? bridge.isNvmeStorage : false
    property int toolsCount: (typeof bridge !== "undefined") ? bridge.toolsCount : 4

    implicitHeight: capCol.implicitHeight + 16
    radius: Theme.radiusMd
    color: Theme.bgCard
    border.color: Theme.borderMuted

    ColumnLayout {
        id: capCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // Title Row
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "CAPABILITIES"
                color: Theme.textTertiary
                font.bold: true
                font.pixelSize: 10
                font.family: Theme.fontUi
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 6; height: 6; radius: 3
                color: root.isGenerating ? Theme.warning : Theme.success
            }
        }

        Rectangle { height: 1; Layout.fillWidth: true; color: Theme.borderSubtle }

        // Row 0: NPU Acceleration (Always Priority)
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "NPU Accel"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
            Item { Layout.fillWidth: true }
            Text { text: "AI Boost"; color: Theme.success; font.bold: true; font.pixelSize: 10; font.family: Theme.fontMono }
            Rectangle {
                implicitWidth: lblNpuStatus.implicitWidth + 8
                implicitHeight: 16
                radius: 3
                color: Theme.successSubtle
                border.color: Theme.success
                Text {
                    id: lblNpuStatus
                    anchors.centerIn: parent
                    text: "PRIORITY"
                    color: Theme.success
                    font.bold: true
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                }
            }
        }

        // Row 1: Generation
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Generation"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
            Item { Layout.fillWidth: true }
            Text { text: root.genDevice; color: Theme.textPrimary; font.bold: true; font.pixelSize: 10; font.family: Theme.fontMono }
            Rectangle {
                implicitWidth: lblGenStatus.implicitWidth + 8
                implicitHeight: 16
                radius: 3
                color: root.isGenerating ? Theme.warningSubtle : Theme.successSubtle
                border.color: root.isGenerating ? Theme.warning : Theme.success
                Text {
                    id: lblGenStatus
                    anchors.centerIn: parent
                    text: root.isGenerating ? "BUSY" : "READY"
                    color: root.isGenerating ? Theme.warning : Theme.success
                    font.bold: true
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                }
            }
        }

        // Row 2: Embedding
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Embedding"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
            Item { Layout.fillWidth: true }
            Text { text: root.embDevice; color: Theme.textPrimary; font.bold: true; font.pixelSize: 10; font.family: Theme.fontMono }
            Rectangle {
                implicitWidth: lblEmbStatus.implicitWidth + 8
                implicitHeight: 16
                radius: 3
                color: root.isEmbLoaded ? Theme.successSubtle : Theme.borderSubtle
                border.color: root.isEmbLoaded ? Theme.success : Theme.borderMuted
                Text {
                    id: lblEmbStatus
                    anchors.centerIn: parent
                    text: root.isEmbLoaded ? "ACTIVE" : "READY"
                    color: root.isEmbLoaded ? Theme.success : Theme.textTertiary
                    font.bold: true
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                }
            }
        }

        // Row 3: Storage
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Storage"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
            Item { Layout.fillWidth: true }
            Text { text: root.isNvme ? "NVMe" : "SSD"; color: Theme.retrieval; font.bold: true; font.pixelSize: 10; font.family: Theme.fontMono }
            Rectangle {
                implicitWidth: lblStorStatus.implicitWidth + 8
                implicitHeight: 16
                radius: 3
                color: Theme.retrievalSubtle
                border.color: Theme.retrieval
                Text {
                    id: lblStorStatus
                    anchors.centerIn: parent
                    text: root.isNvme ? "FAST-MMAP" : "CACHED"
                    color: Theme.retrieval
                    font.bold: true
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                }
            }
        }

        // Row 4: Tools
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Tools"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
            Item { Layout.fillWidth: true }
            Text { text: root.toolsCount + " Plugins"; color: Theme.textPrimary; font.bold: true; font.pixelSize: 10; font.family: Theme.fontMono }
            Rectangle {
                implicitWidth: lblToolStatus.implicitWidth + 8
                implicitHeight: 16
                radius: 3
                color: Theme.reasoningSubtle
                border.color: Theme.reasoning
                Text {
                    id: lblToolStatus
                    anchors.centerIn: parent
                    text: "TRUSTED"
                    color: Theme.reasoning
                    font.bold: true
                    font.pixelSize: 8
                    font.family: Theme.fontMono
                }
            }
        }
    }
}
