import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#121417"

    property string baseSnapshot: "snap_root_001"
    property int additions: 14
    property int deletions: 3
    property bool isApplied: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Diff Header
        Rectangle {
            Layout.fillWidth: true
            height: 64
            radius: 8
            color: "#1A1D21"
            border.color: "#2E3339"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14

                Column {
                    Text { text: "TARGET ARTIFACT: src/server/http_server.cpp"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 14 }
                    RowLayout {
                        spacing: 8
                        Text { text: "Base Snapshot: " + baseSnapshot; color: "#9EA7B0"; font.pixelSize: 11; font.family: "Consolas" }
                        Text { text: "(+" + additions + ")"; color: "#28A745"; font.bold: true; font.pixelSize: 11 }
                        Text { text: "(-" + deletions + ")"; color: "#DC3545"; font.bold: true; font.pixelSize: 11 }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: isApplied ? "✔ Changes Applied" : "Apply Selected Hunks"
                    highlighted: !isApplied
                    enabled: !isApplied
                    onClicked: isApplied = true
                }
            }
        }

        // Diff Code View
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: "#15181C"
            border.color: "#2E3339"

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10

                Column {
                    spacing: 2
                    width: parent.width

                    Repeater {
                        model: [
                            { type: "header", text: "@@ -120,6 +120,10 @@ int HttpServer::start() {" },
                            { type: "context", text: "     server_->set_payload_max_length(16 * 1024 * 1024);" },
                            { type: "deletion", text: "-    server_->set_read_timeout(5, 0);" },
                            { type: "addition", text: "+    server_->set_read_timeout(60, 0);" },
                            { type: "addition", text: "+    server_->set_write_timeout(60, 0);" },
                            { type: "addition", text: "+    // Enable client disconnect detection for SSE streaming" },
                            { type: "context", text: "     server_->set_logger([](const auto& req, const auto& res) {" }
                        ]

                        Rectangle {
                            width: parent.width
                            height: 22
                            color: modelData.type === "addition" ? "#1B3B24" : (modelData.type === "deletion" ? "#3B1B1F" : "transparent")

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                spacing: 8
                                Text {
                                    text: modelData.text
                                    color: modelData.type === "addition" ? "#56D364" : (modelData.type === "deletion" ? "#F85149" : (modelData.type === "header" ? "#79C0FF" : "#8B949E"))
                                    font.family: "Consolas, monospace"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
