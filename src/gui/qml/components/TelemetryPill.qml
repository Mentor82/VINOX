import QtQuick 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: root

    property string label: ""
    property string value: ""
    property color valueColor: Theme.textPrimary
    property color pillBorder: Theme.borderMuted
    property color pillBg: Theme.bgCard

    height: 28
    radius: Theme.radiusSm
    color: pillBg
    border.color: pillBorder
    implicitWidth: pillRow.implicitWidth + 16

    RowLayout {
        id: pillRow
        anchors.centerIn: parent
        spacing: 6

        Text {
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSm
            font.family: Theme.fontUi
            visible: root.label.length > 0
        }

        Text {
            text: root.value
            color: root.valueColor
            font.bold: true
            font.pixelSize: Theme.fontSizeSm
            font.family: Theme.fontMono
        }
    }
}
