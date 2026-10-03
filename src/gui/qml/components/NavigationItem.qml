import QtQuick 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: root

    property string itemId: ""
    property string title: ""
    property string icon: ""
    property bool isActive: false

    signal clicked()

    Layout.fillWidth: true
    height: 38
    radius: Theme.radiusSm
    color: root.isActive ? Theme.accentSubtle : (itemMouse.containsMouse ? Theme.bgCard : "transparent")
    border.color: root.isActive ? Theme.borderActive : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10

        Text {
            text: root.icon
            font.pixelSize: Theme.fontSizeMd
        }

        Text {
            text: root.title
            color: root.isActive ? Theme.accent : (itemMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
            font.bold: root.isActive
            font.pixelSize: Theme.fontSizeBase
            font.family: Theme.fontUi
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            width: 4
            height: 16
            radius: 2
            color: Theme.accent
            visible: root.isActive
        }
    }

    MouseArea {
        id: itemMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
