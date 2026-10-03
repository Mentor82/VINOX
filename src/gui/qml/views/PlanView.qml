import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    color: "#121417"

    property string planId: "plan_20260903_8841"
    property string planHash: "f7c9082a9341b52e"
    property bool isApproved: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Header & Hash Banner
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
                    Text { text: "TASK: Standalone Native Agent Refactoring"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 15 }
                    Text { text: "Plan ID: " + planId; color: "#9EA7B0"; font.pixelSize: 12 }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    height: 36
                    width: 220
                    radius: 6
                    color: "#22262B"
                    border.color: "#3D444D"
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "Hash:"; color: "#9EA7B0"; font.pixelSize: 11 }
                        Text { text: planHash; color: "#007ACC"; font.bold: true; font.family: "Consolas"; font.pixelSize: 12 }
                    }
                }
            }
        }

        // Steps List
        ListView {
            id: stepList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10

            model: ListModel {
                ListElement {
                    idx: 1
                    title: "Inspect target workspace and locate source files"
                    capability: "fs:read"
                    risk: "Low"
                }
                ListElement {
                    idx: 2
                    title: "Modify target sources with bounded sandbox mutations"
                    capability: "fs:write"
                    risk: "High"
                }
                ListElement {
                    idx: 3
                    title: "Execute automated compiler and regression checks"
                    capability: "exec:test"
                    risk: "Medium"
                }
            }

            delegate: Rectangle {
                width: stepList.width
                height: 64
                radius: 8
                color: "#1E2228"
                border.color: "#2E3339"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        width: 30; height: 30; radius: 15
                        color: "#2E3339"
                        Text { anchors.centerIn: parent; text: idx.toString(); color: "#FFFFFF"; font.bold: true }
                    }

                    Column {
                        Layout.fillWidth: true
                        Text { text: title; color: "#F0F2F5"; font.bold: true; font.pixelSize: 13 }
                        Text { text: "Capability: " + capability; color: "#9EA7B0"; font.pixelSize: 11 }
                    }

                    Rectangle {
                        height: 24
                        width: 70
                        radius: 4
                        color: risk === "High" ? "#7A1C1C" : (risk === "Medium" ? "#7A5E1C" : "#1C5E28")
                        Text {
                            anchors.centerIn: parent
                            text: risk.toUpperCase()
                            color: "#FFFFFF"
                            font.bold: true
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        // Approval Control Area
        Rectangle {
            Layout.fillWidth: true
            height: 70
            radius: 8
            color: "#1A1D21"
            border.color: isApproved ? "#28A745" : "#2E3339"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14

                Column {
                    Text {
                        text: isApproved ? "STATUS: APPROVED 🟢" : "STATUS: PENDING APPROVAL 🔒"
                        color: isApproved ? "#28A745" : "#FFC107"
                        font.bold: true
                        font.pixelSize: 13
                    }
                    Text {
                        text: "Approvals strictly bind to the immutable plan_hash. Stale edits are rejected fail-closed."
                        color: "#9EA7B0"
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: isApproved ? "Revoke Approval" : "Approve Plan & Lock Hash"
                    highlighted: !isApproved
                    onClicked: isApproved = !isApproved
                }
            }
        }
    }
}
