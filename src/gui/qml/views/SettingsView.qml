import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#121417"

    property bool isRemote: false
    property string remoteUrl: "http://127.0.0.1:8080"
    property real temperature: 0.7
    property real topP: 0.9
    property int maxTokens: 1024

    ScrollView {
        anchors.fill: parent
        anchors.margins: 24

        ColumnLayout {
            width: parent.width - 48
            spacing: 20

            Text { text: "VINOX RUNTIME & CONNECTION SETTINGS"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 16 }

            // Section 1: Backend Connection
            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#1A1D21"
                border.color: "#2E3339"
                implicitHeight: colConn.implicitHeight + 28

                ColumnLayout {
                    id: colConn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    Text { text: "Backend Mode"; color: "#007ACC"; font.bold: true; font.pixelSize: 13 }

                    RowLayout {
                        spacing: 20
                        RadioButton {
                            text: "Local Mode (Direct C-ABI in-process)"
                            checked: !isRemote
                            onClicked: isRemote = false
                        }
                        RadioButton {
                            text: "Remote Mode (HTTP/SSE Server)"
                            checked: isRemote
                            onClicked: isRemote = true
                        }
                    }

                    RowLayout {
                        visible: isRemote
                        spacing: 10
                        Text { text: "Server URL:"; color: "#9EA7B0" }
                        TextField {
                            text: remoteUrl
                            Layout.fillWidth: true
                            background: Rectangle { color: "#22262B"; radius: 6; border.color: "#2E3339" }
                            color: "#FFFFFF"
                        }
                    }
                }
            }

            // Section 2: Inference & Device
            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#1A1D21"
                border.color: "#2E3339"
                implicitHeight: colDevice.implicitHeight + 28

                ColumnLayout {
                    id: colDevice
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    Text { text: "OpenVINO Acceleration & Device"; color: "#007ACC"; font.bold: true; font.pixelSize: 13 }

                    RowLayout {
                        spacing: 20
                        RadioButton { text: "CPU"; checked: false }
                        RadioButton { text: "GPU (Intel Arc)"; checked: true }
                        RadioButton { text: "NPU (Intel NPU)"; checked: false }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Text { text: "Temperature:"; color: "#9EA7B0" }
                            Item { Layout.fillWidth: true }
                            Text { text: temperature.toFixed(2); color: "#FFFFFF"; font.bold: true }
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.0
                            to: 2.0
                            value: temperature
                            onValueChanged: temperature = value
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Text { text: "Top-P:"; color: "#9EA7B0" }
                            Item { Layout.fillWidth: true }
                            Text { text: topP.toFixed(2); color: "#FFFFFF"; font.bold: true }
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            value: topP
                            onValueChanged: topP = value
                        }
                    }
                }
            }

            // Section 3: NVMe & Fast Storage Acceleration (OpenVINO 2026.3 C++ API)
            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#1A1D21"
                border.color: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "#00E5FF44" : "#2E3339"
                implicitHeight: colNvme.implicitHeight + 28

                ColumnLayout {
                    id: colNvme
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    RowLayout {
                        spacing: 10
                        Text { text: "NVMe & Storage Acceleration"; color: "#00E5FF"; font.bold: true; font.pixelSize: 13 }
                        Rectangle {
                            height: 20
                            radius: 4
                            color: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "#10B98122" : "#22262B"
                            border.color: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "#10B981" : "#4A5568"
                            implicitWidth: lblNvmeBadge.implicitWidth + 12
                            Text {
                                id: lblNvmeBadge
                                anchors.centerIn: parent
                                text: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "⚡ NVMe ULTRA-FAST" : "STANDARD STORAGE"
                                color: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "#10B981" : "#A0AEC0"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }

                    // Hardware Readout Card
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 6
                        color: "#22262B"
                        border.color: "#2E3339"
                        implicitHeight: colHwInfo.implicitHeight + 16

                        ColumnLayout {
                            id: colHwInfo
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Text { text: "Detected Device:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? bridge.storageDeviceName : "Detecting..."
                                    color: "#FFFFFF"; font.bold: true; font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                Text { text: "Bus:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? bridge.storageBusType : "Unknown"
                                    color: "#00E5FF"; font.bold: true; font.pixelSize: 12
                                }
                            }

                            RowLayout {
                                Text { text: "Capacity & Space:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? bridge.storageCapacityText : "-"
                                    color: "#FFFFFF"; font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: "Refresh Hardware"
                                    implicitHeight: 24
                                    font.pixelSize: 11
                                    onClicked: if (typeof bridge !== "undefined") bridge.refreshStorageInfo()
                                }
                            }
                        }
                    }

                    // Acceleration Controls
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        CheckBox {
                            text: "Enable In-Memory Weight Mapping (ov::enable_mmap)"
                            checked: (typeof bridge !== "undefined") ? bridge.enableMmap : true
                            onCheckedChanged: if (typeof bridge !== "undefined") bridge.enableMmap = checked
                        }
                        Text {
                            text: "  Bypasses file system buffer copies, streaming weights directly from fast NVMe storage into virtual memory."
                            color: "#718096"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        CheckBox {
                            text: "Enable Model Compilation Blob Cache (ov::cache_dir)"
                            checked: (typeof bridge !== "undefined") ? bridge.enableCache : true
                            onCheckedChanged: if (typeof bridge !== "undefined") bridge.enableCache = checked
                        }
                        Text {
                            text: "  Saves compiled OpenVINO model blobs to NVMe disk. Eliminates cold-start compilation overhead on subsequent runs."
                            color: "#718096"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 10
                            Text { text: "Blob Cache Directory:"; color: "#9EA7B0"; font.pixelSize: 12 }
                            TextField {
                                text: (typeof bridge !== "undefined") ? bridge.cacheDir : "C:\\ai\\openvino\\cache\\blobs"
                                Layout.fillWidth: true
                                background: Rectangle { color: "#22262B"; radius: 6; border.color: "#2E3339" }
                                color: "#FFFFFF"
                                font.pixelSize: 12
                                onEditingFinished: if (typeof bridge !== "undefined") bridge.cacheDir = text
                            }
                        }

                        RowLayout {
                            spacing: 12
                            Text { text: "Current Cache Size:"; color: "#9EA7B0"; font.pixelSize: 12 }
                            Text {
                                text: (typeof bridge !== "undefined") ? bridge.cacheSizeText : "0 MB"
                                color: "#00E5FF"; font.bold: true; font.pixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Clear Blob Cache"
                                implicitHeight: 26
                                onClicked: if (typeof bridge !== "undefined") bridge.clearCache()
                            }
                        }
                    }
                }
            }

            // Section 4: Tool Calling & Structured Output (OpenVINO GenAI 2026.3)
            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#1A1D21"
                border.color: (typeof bridge !== "undefined" && bridge.enableToolCalling) ? "#10B98144" : "#2E3339"
                implicitHeight: colTools.implicitHeight + 28

                ColumnLayout {
                    id: colTools
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    RowLayout {
                        spacing: 10
                        Text { text: "Tool Calling & Structured Output"; color: "#10B981"; font.bold: true; font.pixelSize: 13 }
                        Rectangle {
                            height: 20
                            radius: 4
                            color: (typeof bridge !== "undefined" && bridge.enableToolCalling) ? "#10B98122" : "#22262B"
                            border.color: (typeof bridge !== "undefined" && bridge.enableToolCalling) ? "#10B981" : "#4A5568"
                            implicitWidth: lblToolBadge.implicitWidth + 12
                            Text {
                                id: lblToolBadge
                                anchors.centerIn: parent
                                text: (typeof bridge !== "undefined" && bridge.enableToolCalling) ? "⚡ OPENVINO 2026.3 ENGINE" : "TOOLS DISABLED"
                                color: (typeof bridge !== "undefined" && bridge.enableToolCalling) ? "#10B981" : "#A0AEC0"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }

                    // Tools Engine Readout
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 6
                        color: "#22262B"
                        border.color: "#2E3339"
                        implicitHeight: colToolsInfo.implicitHeight + 16

                        ColumnLayout {
                            id: colToolsInfo
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Text { text: "Tool Registry:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? (bridge.toolsCount + " Canonical Tools Active") : "5 Canonical Tools Active"
                                    color: "#FFFFFF"; font.bold: true; font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                Text { text: "Sampler Backend:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: "OpenVINO GenAI xgrammar"
                                    color: "#10B981"; font.bold: true; font.pixelSize: 12
                                }
                            }

                            RowLayout {
                                Text { text: "Registered Services:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? bridge.toolsSummaryText : "vinox.search, conversation_get, document_ingest, relations_query, relation_create"
                                    color: "#A0AEC0"; font.pixelSize: 11
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    // Tool Controls
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        CheckBox {
                            text: "Enable Tool Calling & Function Invocation"
                            checked: (typeof bridge !== "undefined") ? bridge.enableToolCalling : true
                            onCheckedChanged: if (typeof bridge !== "undefined") bridge.enableToolCalling = checked
                        }
                        Text {
                            text: "  Enables models to request external tools (hybrid BM25/vector search, document ingestion, knowledge graph) via C-ABI."
                            color: "#718096"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        CheckBox {
                            text: "Grammar-Guided Constrained Decoding (ov::genai::StructuredOutputConfig)"
                            checked: (typeof bridge !== "undefined") ? bridge.enableStructuredOutput : true
                            onCheckedChanged: if (typeof bridge !== "undefined") bridge.enableStructuredOutput = checked
                        }
                        Text {
                            text: "  Restricts token sampling to the tool JSON Schema directly at logits level. Mathematically prevents invalid syntax or hallucinated attributes."
                            color: "#718096"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Text { text: "Security Policy Tier (Fail-Closed Governance):"; color: "#9EA7B0"; font.pixelSize: 12; Layout.topMargin: 4 }
                        RowLayout {
                            spacing: 16
                            RadioButton {
                                text: "Safe (Auto-allow Read-Only, prompt writes)"
                                checked: (typeof bridge === "undefined") || bridge.toolSecurityTier === 0
                                onClicked: if (typeof bridge !== "undefined") bridge.toolSecurityTier = 0
                            }
                            RadioButton {
                                text: "Strict (Prompt all tools)"
                                checked: (typeof bridge !== "undefined") && bridge.toolSecurityTier === 1
                                onClicked: if (typeof bridge !== "undefined") bridge.toolSecurityTier = 1
                            }
                            RadioButton {
                                text: "Autonomous (Auto-allow Local Writes)"
                                checked: (typeof bridge !== "undefined") && bridge.toolSecurityTier === 2
                                onClicked: if (typeof bridge !== "undefined") bridge.toolSecurityTier = 2
                            }
                        }
                    }
                }
            }

            // Section 5: Decoupled Embedding & Tool Plugins
            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#1A1D21"
                border.color: "#2E3339"
                implicitHeight: colEmbedding.implicitHeight + 28

                ColumnLayout {
                    id: colEmbedding
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    RowLayout {
                        Text { text: "5. Decoupled Embedding Engine & Tool Plugins"; color: "#10B981"; font.bold: true; font.pixelSize: 13 }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            radius: 4
                            color: ((typeof bridge !== "undefined") && bridge.isEmbeddingLoaded) ? "#064E3B" : "#374151"
                            border.color: ((typeof bridge !== "undefined") && bridge.isEmbeddingLoaded) ? "#059669" : "#4B5563"
                            implicitWidth: lblEmbStatus.implicitWidth + 12
                            implicitHeight: 20
                            Text {
                                id: lblEmbStatus
                                anchors.centerIn: parent
                                text: ((typeof bridge !== "undefined") && bridge.isEmbeddingLoaded) ? "ACTIVE" : "UNLOADED"
                                color: ((typeof bridge !== "undefined") && bridge.isEmbeddingLoaded) ? "#34D399" : "#9CA3AF"
                                font.bold: true
                                font.pixelSize: 10
                            }
                        }
                    }

                    // Embedding Engine Hardware & Provenance
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 6
                        color: "#14171A"
                        border.color: "#282C31"
                        implicitHeight: colEmbDetails.implicitHeight + 20

                        ColumnLayout {
                            id: colEmbDetails
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            RowLayout {
                                Text { text: "Independent Embedding Device:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Item { Layout.fillWidth: true }
                                RowLayout {
                                    spacing: 12
                                    RadioButton {
                                        text: "CPU"
                                        checked: (typeof bridge === "undefined") || bridge.embeddingDevice === "CPU"
                                        onClicked: if (typeof bridge !== "undefined") bridge.embeddingDevice = "CPU"
                                    }
                                    RadioButton {
                                        text: "GPU"
                                        checked: (typeof bridge !== "undefined") && bridge.embeddingDevice === "GPU"
                                        onClicked: if (typeof bridge !== "undefined") bridge.embeddingDevice = "GPU"
                                    }
                                    RadioButton {
                                        text: "NPU"
                                        checked: (typeof bridge !== "undefined") && bridge.embeddingDevice === "NPU"
                                        onClicked: if (typeof bridge !== "undefined") bridge.embeddingDevice = "NPU"
                                    }
                                }
                            }

                            RowLayout {
                                spacing: 8
                                Text { text: "Model Path:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                TextField {
                                    id: txtEmbPath
                                    text: (typeof bridge !== "undefined" && bridge.embeddingModel !== "") ? bridge.embeddingModel : "C:\\ai\\models\\OpenVINO\\Qwen3-Embedding-0.6B"
                                    Layout.fillWidth: true
                                    background: Rectangle { color: "#22262B"; radius: 6; border.color: "#2E3339" }
                                    color: "#FFFFFF"
                                    font.pixelSize: 11
                                }
                                Button {
                                    text: "Load Embedding"
                                    highlighted: true
                                    onClicked: {
                                        if (typeof bridge !== "undefined") {
                                            bridge.loadEmbeddingModel(txtEmbPath.text, bridge.embeddingDevice);
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Text { text: "Engine Status:"; color: "#9EA7B0"; font.pixelSize: 12 }
                                Text {
                                    text: (typeof bridge !== "undefined") ? bridge.embeddingStatusText : "Unloaded"
                                    color: ((typeof bridge !== "undefined") && bridge.isEmbeddingLoaded) ? "#34D399" : "#F59E0B"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                        }
                    }

                    // Plugin System Architecture & Trust
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text { text: "Active Tool Plugins & Trust State:"; color: "#9EA7B0"; font.pixelSize: 12 }
                        Rectangle {
                            Layout.fillWidth: true
                            radius: 6
                            color: "#14171A"
                            border.color: "#282C31"
                            implicitHeight: colPlugList.implicitHeight + 16

                            ColumnLayout {
                                id: colPlugList
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                RowLayout {
                                    Text { text: "• std_fs:"; color: "#007ACC"; font.bold: true; font.pixelSize: 11 }
                                    Text { text: "Sandbox Filesystem (canonical path checking, traversal defense)"; color: "#D1D5DB"; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: "BUILTIN (Trusted)"; color: "#10B981"; font.pixelSize: 10; font.bold: true }
                                }
                                RowLayout {
                                    Text { text: "• std_math:"; color: "#007ACC"; font.bold: true; font.pixelSize: 11 }
                                    Text { text: "Safe Recursive Descent Arithmetic (math.calculate, bounded ops)"; color: "#D1D5DB"; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: "BUILTIN (Trusted)"; color: "#10B981"; font.pixelSize: 10; font.bold: true }
                                }
                                RowLayout {
                                    Text { text: "• std_time:"; color: "#007ACC"; font.bold: true; font.pixelSize: 11 }
                                    Text { text: "System & Local Time with timezone offset & ISO 8601"; color: "#D1D5DB"; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: "BUILTIN (Trusted)"; color: "#10B981"; font.pixelSize: 10; font.bold: true }
                                }
                                RowLayout {
                                    Text { text: "• std_retrieval:"; color: "#007ACC"; font.bold: true; font.pixelSize: 11 }
                                    Text { text: "Decoupled Search & Document Ingestion with RRF (k=60)"; color: "#D1D5DB"; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    Text { text: "BUILTIN (Trusted)"; color: "#10B981"; font.pixelSize: 10; font.bold: true }
                                }
                            }
                        }
                    }
                }
            }

            // Save Action
            RowLayout {
                spacing: 12
                Button {
                    text: "Reset Model Config"
                    onClicked: if (typeof bridge !== "undefined") bridge.resetCurrentModelConfig()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Save & Apply Settings"
                    highlighted: true
                    onClicked: if (typeof bridge !== "undefined") bridge.saveCurrentModelConfig()
                }
            }
        }
    }
}

