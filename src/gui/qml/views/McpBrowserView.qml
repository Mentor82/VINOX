import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#121417"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Text { text: "REGISTERED MCP TOOLS & CAPABILITIES"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 14 }

        ListView {
            id: toolList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10

            model: ListModel {
                ListElement { name: "vinox.search"; server: "vinox-storage"; sec: "READ"; desc: "Hybrid FTS5 full-text and sqlite-vec dense vector search." }
                ListElement { name: "vinox.conversation_get"; server: "vinox-storage"; sec: "READ"; desc: "Retrieve reconstructed parent chain of conversation messages." }
                ListElement { name: "vinox.relations_query"; server: "vinox-storage"; sec: "READ"; desc: "Recursive CTE semantic relationship graph traversal." }
                ListElement { name: "fs.read_file"; server: "vinox-sandbox"; sec: "READ"; desc: "Read workspace file content within approved sandbox boundaries." }
                ListElement { name: "fs.write_patch"; server: "vinox-sandbox"; sec: "WRITE"; desc: "Write bounded patch mutations with snapshot verification." }
            }

            delegate: Rectangle {
                width: toolList.width
                height: 64
                radius: 8
                color: "#1E2228"
                border.color: "#2E3339"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Column {
                        Layout.fillWidth: true
                        RowLayout {
                            spacing: 8
                            Text { text: name; color: "#007ACC"; font.bold: true; font.family: "Consolas"; font.pixelSize: 13 }
                            Text { text: "@ " + server; color: "#6C757D"; font.pixelSize: 11 }
                        }
                        Text { text: desc; color: "#9EA7B0"; font.pixelSize: 11 }
                    }

                    Rectangle {
                        height: 22
                        width: 54
                        radius: 4
                        color: sec === "WRITE" ? "#7A1C1C" : "#1C5E28"
                        Text {
                            anchors.centerIn: parent
                            text: sec
                            color: "#FFFFFF"
                            font.bold: true
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }
    }
}
