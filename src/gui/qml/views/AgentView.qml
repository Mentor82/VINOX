import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#121417"

    property string runState: "running" // "idle", "running", "cancelled", "completed"
    property int tokensSpent: 12500
    property int tokenBudget: 100000

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Status & Budget Bar
        Rectangle {
            Layout.fillWidth: true
            height: 70
            radius: 8
            color: "#1A1D21"
            border.color: "#2E3339"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14

                Column {
                    Text { text: "AGENT RUN: run_20260903_9912"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 14 }
                    Text { text: "State: " + runState.toUpperCase(); color: runState === "running" ? "#007ACC" : (runState === "completed" ? "#28A745" : "#DC3545"); font.bold: true; font.pixelSize: 12 }
                }

                Item { Layout.fillWidth: true }

                Column {
                    Layout.preferredWidth: 200
                    spacing: 4
                    RowLayout {
                        Text { text: "Budget:"; color: "#9EA7B0"; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text { text: tokensSpent + " / " + tokenBudget + " tok"; color: "#FFFFFF"; font.pixelSize: 11 }
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        value: tokensSpent / tokenBudget
                    }
                }

                Button {
                    text: "⏹ Abort Run"
                    enabled: runState === "running"
                    onClicked: runState = "cancelled"
                }
            }
        }

        // Timeline of Sequenced Events
        ListView {
            id: timelineList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10

            model: ListModel {
                ListElement { seq: 1; type: "step_start"; msg: "Starting Step 1: Inspect environment"; ts: "+0.02s" }
                ListElement { seq: 2; type: "action_tool"; msg: "Executing tool fs:read path='main.cpp'"; ts: "+0.15s" }
                ListElement { seq: 3; type: "observation"; msg: "Received file content (248 lines, 8.2 KB)"; ts: "+0.21s" }
                ListElement { seq: 4; type: "step_start"; msg: "Starting Step 2: Modify sources"; ts: "+0.45s" }
                ListElement { seq: 5; type: "action_tool"; msg: "Executing tool fs:write patch='main.cpp.diff'"; ts: "+0.80s" }
                ListElement { seq: 6; type: "checkpoint"; msg: "Snapshot checkpoint created (snap_root_001)"; ts: "+0.92s" }
            }

            delegate: Rectangle {
                width: timelineList.width
                height: 52
                radius: 6
                color: "#1E2228"
                border.color: "#2E3339"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 12

                    Text { text: "#" + seq; color: "#9EA7B0"; font.bold: true; font.pixelSize: 11 }

                    Rectangle {
                        width: 76
                        height: 22
                        radius: 4
                        color: type === "action_tool" ? "#007ACC" : (type === "observation" ? "#28A745" : (type === "checkpoint" ? "#6F42C1" : "#495057"))
                        Text {
                            anchors.centerIn: parent
                            text: type.toUpperCase()
                            color: "#FFFFFF"
                            font.bold: true
                            font.pixelSize: 9
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: msg
                        color: "#F0F2F5"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text { text: ts; color: "#6C757D"; font.family: "Consolas"; font.pixelSize: 11 }
                }
            }
        }
    }
}
