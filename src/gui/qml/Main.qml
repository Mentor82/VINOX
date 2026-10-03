import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0
import "components"

ApplicationWindow {
    id: appWindow
    width: 1280
    height: 840
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "VINOX — Local Generative AI Studio & Agent Runtime"

    color: Theme.bgApp

    // State properties
    property string activeMode: "chat" // "chat", "search", "relations", "plan", "agent", "diff", "mcp", "settings"
    property string connectionMode: (typeof bridge !== "undefined") ? bridge.connectionMode : "Local (C-ABI)"
    property string currentModel: (typeof bridge !== "undefined") ? bridge.currentModel : "ov_deepseek_tools_v4"
    property string currentDevice: (typeof bridge !== "undefined") ? bridge.currentDevice : "CPU"
    property int totalTokens: (typeof bridge !== "undefined") ? bridge.totalTokens : 0
    property real tokenRate: (typeof bridge !== "undefined") ? bridge.tokenRate : 0.0

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // =====================================================================
        // SIDEBAR NAVIGATION (Grouped Semantic Categories)
        // =====================================================================
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: Theme.bgSidebar
            border.color: Theme.borderMuted
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                // Brand Header
                RowLayout {
                    spacing: 12
                    Rectangle {
                        width: 36
                        height: 36
                        radius: Theme.radiusMd
                        color: Theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: "V"
                            color: Theme.bgApp
                            font.bold: true
                            font.pixelSize: 20
                        }
                    }
                    Column {
                        Text { text: "VINOX"; color: Theme.textPrimary; font.bold: true; font.pixelSize: 16; font.family: Theme.fontUi }
                        Text { text: "OpenVINO GenAI Studio v0.1.0"; color: Theme.textSecondary; font.pixelSize: 11; font.family: Theme.fontUi }
                    }
                }

                Rectangle { height: 1; Layout.fillWidth: true; color: Theme.borderMuted }

                // Scrollable Navigation List for Categories
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 12

                        // GROUP 1: STUDIO
                        Text {
                            text: "STUDIO"
                            color: Theme.textTertiary
                            font.pixelSize: 10
                            font.bold: true
                            font.family: Theme.fontUi
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: [
                                    { id: "chat", title: "Chat & Code", icon: "💬" },
                                    { id: "search", title: "Hybrid Retrieval", icon: "🔍" },
                                    { id: "relations", title: "Knowledge Graph", icon: "🕸️" }
                                ]

                                NavigationItem {
                                    itemId: modelData.id
                                    title: modelData.title
                                    icon: modelData.icon
                                    isActive: activeMode === modelData.id
                                    onClicked: activeMode = modelData.id
                                }
                            }
                        }

                        // GROUP 2: AUTONOMOUS AGENT
                        Text {
                            text: "AUTONOMOUS AGENT"
                            color: Theme.textTertiary
                            font.pixelSize: 10
                            font.bold: true
                            font.family: Theme.fontUi
                            Layout.topMargin: 6
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: [
                                    { id: "plan", title: "Plan & Approve", icon: "📋" },
                                    { id: "agent", title: "Agent Execution", icon: "⚡" },
                                    { id: "diff", title: "Artifact & Diff", icon: "📝" }
                                ]

                                NavigationItem {
                                    itemId: modelData.id
                                    title: modelData.title
                                    icon: modelData.icon
                                    isActive: activeMode === modelData.id
                                    onClicked: activeMode = modelData.id
                                }
                            }
                        }

                        // GROUP 3: SYSTEM & GOVERNANCE
                        Text {
                            text: "SYSTEM & GOVERNANCE"
                            color: Theme.textTertiary
                            font.pixelSize: 10
                            font.bold: true
                            font.family: Theme.fontUi
                            Layout.topMargin: 6
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: [
                                    { id: "mcp", title: "MCP Tools & Audit", icon: "🔧" },
                                    { id: "settings", title: "Settings & Acceleration", icon: "⚙️" }
                                ]

                                NavigationItem {
                                    itemId: modelData.id
                                    title: modelData.title
                                    icon: modelData.icon
                                    isActive: activeMode === modelData.id
                                    onClicked: activeMode = modelData.id
                                }
                            }
                        }
                    }
                }

                // Decoupled Capabilities Status Widget in Sidebar
                CapabilityStatusWidget {
                    Layout.fillWidth: true
                }
            }
        }

        // =====================================================================
        // MAIN CONTENT STACK & TELEMETRY HEADER
        // =====================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header Bar
            Rectangle {
                Layout.fillWidth: true
                height: 52
                color: Theme.bgSidebar
                border.color: Theme.borderMuted
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 12

                    Text {
                        text: {
                            switch (activeMode) {
                                case "chat": return "💬 CHAT & GENERATION STUDIO";
                                case "search": return "🔍 HYBRID RETRIEVAL STUDIO (BM25 + RRF + VECTOR)";
                                case "relations": return "🕸️ KNOWLEDGE GRAPH & CTE RELATIONS";
                                case "plan": return "📋 PLAN & APPROVAL GOVERNANCE";
                                case "agent": return "⚡ AUTONOMOUS AGENT EXECUTION";
                                case "diff": return "📝 SANDBOX ARTIFACT & DIFF INSPECTION";
                                case "mcp": return "🔧 MCP TOOL REGISTRY & AUDIT LOG";
                                case "settings": return "⚙️ RUNTIME ACCELERATION & SETTINGS";
                                default: return activeMode.toUpperCase();
                            }
                        }
                        color: Theme.textPrimary
                        font.bold: true
                        font.pixelSize: Theme.fontSizeMd
                        font.family: Theme.fontUi
                    }

                    Item { Layout.fillWidth: true }

                    // Runtime Metrics Indicators
                    RowLayout {
                        spacing: 8

                        TelemetryPill {
                            label: "Modell:"
                            value: currentModel
                            valueColor: Theme.textPrimary
                        }

                        TelemetryPill {
                            label: "Tempo:"
                            value: tokenRate.toFixed(1) + " tok/s"
                            valueColor: Theme.accent
                        }

                        TelemetryPill {
                            label: "Tokens:"
                            value: totalTokens.toString()
                            valueColor: Theme.success
                        }

                        TelemetryPill {
                            label: ""
                            value: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? "⚡ NVMe Active" : "Storage Active"
                            valueColor: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? Theme.retrieval : Theme.textSecondary
                            pillBg: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? Theme.retrievalSubtle : Theme.bgCard
                            pillBorder: (typeof bridge !== "undefined" && bridge.isNvmeStorage) ? Theme.retrieval : Theme.borderMuted
                        }
                    }
                }
            }

            // Central Stack
            StackLayout {
                id: centralStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: {
                    switch (activeMode) {
                        case "chat": return 0;
                        case "plan": return 1;
                        case "agent": return 2;
                        case "diff": return 3;
                        case "search": return 4;
                        case "relations": return 5;
                        case "mcp": return 6;
                        case "settings": return 7;
                        default: return 0;
                    }
                }

                Loader { source: "views/ChatView.qml" }
                Loader { source: "views/PlanView.qml" }
                Loader { source: "views/AgentView.qml" }
                Loader { source: "views/DiffView.qml" }
                Loader { source: "views/SearchView.qml" }
                Loader { source: "views/RelationsView.qml" }
                Loader { source: "views/McpBrowserView.qml" }
                Loader { source: "views/SettingsView.qml" }
            }
        }
    }
}
