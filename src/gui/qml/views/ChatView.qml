import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0
import "../components"

Rectangle {
    id: chatRoot
    color: Theme.bgApp

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // =====================================================================
        // LEFT: CONVERSATION SIDEBAR
        // =====================================================================
        Rectangle {
            id: convSidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 260
            Layout.minimumWidth: 220
            Layout.maximumWidth: 320
            color: Theme.bgSidebar

            // Right border separator
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.borderMuted
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                // Header: Title and + Neu button
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "💬 CHATS"
                        color: Theme.textPrimary
                        font.bold: true
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "+ Neu"
                        implicitHeight: 28
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                        highlighted: true
                        onClicked: {
                            if (typeof bridge !== "undefined") {
                                bridge.startNewChat();
                            }
                        }
                    }
                }

                // Conversations List
                ListView {
                    id: convList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6

                    model: (typeof bridge !== "undefined" && bridge.conversations) ? bridge.conversations : []

                    delegate: Rectangle {
                        id: convItem
                        width: convList.width
                        height: 56
                        radius: Theme.radiusSm
                        property bool isActive: (typeof bridge !== "undefined" && bridge.currentConversationId === modelData.id)
                        property bool isHovered: itemMouseArea.containsMouse

                        color: isActive ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15) : (isHovered ? Theme.bgCard : "transparent")
                        border.color: isActive ? Theme.accent : (isHovered ? Theme.borderMuted : "transparent")
                        border.width: 1

                        MouseArea {
                            id: itemMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (typeof bridge !== "undefined") {
                                    bridge.selectConversation(modelData.id);
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            anchors.topMargin: 6
                            anchors.bottomMargin: 6
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: modelData.title || "Unbenannt"
                                    color: convItem.isActive ? Theme.textPrimary : (convItem.isHovered ? Theme.textPrimary : Theme.textSecondary)
                                    font.bold: convItem.isActive
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontUi
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                RowLayout {
                                    spacing: 6

                                    // Message count badge
                                    Rectangle {
                                        implicitWidth: countText.implicitWidth + 8
                                        implicitHeight: 16
                                        radius: 8
                                        color: convItem.isActive ? Theme.accent : Theme.bgApp
                                        border.color: Theme.borderMuted

                                        Text {
                                            id: countText
                                            anchors.centerIn: parent
                                            text: (modelData.messageCount || 0) + " Msg"
                                            color: convItem.isActive ? Theme.bgApp : Theme.textSecondary
                                            font.pixelSize: 10
                                            font.bold: true
                                            font.family: Theme.fontMono
                                        }
                                    }

                                    // Relative time
                                    Text {
                                        text: modelData.timeText || ""
                                        color: Theme.textTertiary
                                        font.pixelSize: 10
                                        font.family: Theme.fontUi
                                    }
                                }
                            }

                            // Delete button (visible when hovered or active)
                            Rectangle {
                                implicitWidth: 26
                                implicitHeight: 26
                                radius: 4
                                color: delHover.containsMouse ? Theme.dangerSubtle : "transparent"
                                visible: convItem.isHovered || convItem.isActive

                                Text {
                                    anchors.centerIn: parent
                                    text: "🗑"
                                    font.pixelSize: 12
                                    color: delHover.containsMouse ? Theme.danger : Theme.textTertiary
                                }

                                MouseArea {
                                    id: delHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (typeof bridge !== "undefined") {
                                            bridge.deleteConversation(modelData.id);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Empty state indicator
                    Text {
                        anchors.centerIn: parent
                        text: "Keine Chats vorhanden.\nKlicke '+ Neu' um zu starten."
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                        visible: convList.count === 0
                    }
                }
            }
        }

        // =====================================================================
        // RIGHT: MAIN CHAT & MODEL CONTROL AREA
        // =====================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            // =====================================================================
            // MODEL & SAMPLING CONFIGURATION BAR
            // =====================================================================
            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusMd
                color: Theme.bgSidebar
                border.color: Theme.borderMuted
                implicitHeight: modelBarCol.implicitHeight + 20

                ColumnLayout {
                    id: modelBarCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    // Row 1: Model Directory Path & Browse
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "📁 Models Path:"
                            color: Theme.textSecondary
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 32
                            radius: Theme.radiusSm
                            color: Theme.bgApp
                            border.color: pathInput.activeFocus ? Theme.borderFocus : Theme.borderMuted

                            TextInput {
                                id: pathInput
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: TextInput.AlignVCenter
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontSizeSm
                                font.family: Theme.fontMono
                                text: (typeof bridge !== "undefined") ? bridge.modelsPath : "C:/ai/models/OpenVINO"
                                selectByMouse: true
                                onAccepted: {
                                    if (typeof bridge !== "undefined") {
                                        bridge.scanModels(text);
                                    }
                                }
                            }
                        }

                        Button {
                            text: "📂 Durchsuchen..."
                            implicitHeight: 32
                            onClicked: {
                                if (typeof bridge !== "undefined") {
                                    var chosen = bridge.chooseModelFolder(pathInput.text);
                                    if (chosen && chosen.length > 0) {
                                        pathInput.text = chosen;
                                    }
                                }
                            }
                        }

                        Button {
                            text: "🔄 Scannen"
                            implicitHeight: 32
                            onClicked: {
                                if (typeof bridge !== "undefined") {
                                    bridge.scanModels(pathInput.text);
                                }
                            }
                        }
                    }

                    // Row 2: Model Selection Dropdown, Device, and Status
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "🤖 Modell:"
                            color: Theme.textSecondary
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                        }

                        ComboBox {
                            id: modelCombo
                            Layout.fillWidth: true
                            implicitHeight: 32
                            model: (typeof bridge !== "undefined" && bridge.availableModels) ? bridge.availableModels : []
                            textRole: "name"
                            valueRole: "path"

                            currentIndex: {
                                if (typeof bridge === "undefined" || !bridge.availableModels) return -1;
                                for (var i = 0; i < bridge.availableModels.length; ++i) {
                                    if (bridge.availableModels[i].isLoaded || bridge.availableModels[i].path === bridge.currentModel) {
                                        return i;
                                    }
                                }
                                return 0;
                            }

                            onActivated: function(index) {
                                if (typeof bridge !== "undefined" && bridge.availableModels.length > index) {
                                    var selected = bridge.availableModels[index];
                                    bridge.loadModelAsync(selected.path, deviceCombo.currentText);
                                }
                            }
                        }

                        // Model Info Badge (i) with rich hover tooltip
                        Rectangle {
                            id: infoBadge
                            implicitWidth: 26
                            implicitHeight: 26
                            radius: 13
                            color: infoHover.containsMouse ? Theme.accent : Theme.bgCard
                            border.color: infoHover.containsMouse ? Theme.accentHover : Theme.borderMuted

                            Text {
                                anchors.centerIn: parent
                                text: "ℹ"
                                color: infoHover.containsMouse ? Theme.bgApp : Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.fontSizeBase
                            }

                            MouseArea {
                                id: infoHover
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            ToolTip.visible: infoHover.containsMouse
                            ToolTip.delay: 100
                            ToolTip.timeout: 8000
                            ToolTip.text: (typeof bridge !== "undefined") ? bridge.currentModelInfo : "Lade Modell-Informationen..."
                        }

                        Text {
                            text: "Device:"
                            color: Theme.textSecondary
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                        }

                        ComboBox {
                            id: deviceCombo
                            implicitWidth: 85
                            implicitHeight: 32
                            model: ["CPU", "GPU", "NPU"]
                            currentIndex: 0
                            onActivated: function(index) {
                                if (typeof bridge !== "undefined" && modelCombo.currentIndex >= 0 && bridge.availableModels.length > modelCombo.currentIndex) {
                                    var selected = bridge.availableModels[modelCombo.currentIndex];
                                    bridge.loadModelAsync(selected.path, currentText);
                                }
                            }
                        }

                        // Status Pill
                        Rectangle {
                            implicitWidth: statusText.implicitWidth + 22
                            implicitHeight: 28
                            radius: Theme.radiusPill
                            color: (typeof bridge !== "undefined" && bridge.isModelLoading) ? Theme.warningSubtle : Theme.successSubtle
                            border.color: (typeof bridge !== "undefined" && bridge.isModelLoading) ? Theme.warning : Theme.success

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: (typeof bridge !== "undefined" && bridge.isModelLoading) ? Theme.warning : Theme.success
                                }
                                Text {
                                    id: statusText
                                    text: (typeof bridge !== "undefined" && bridge.isModelLoading) ? bridge.modelLoadStatus : "Bereit"
                                    color: (typeof bridge !== "undefined" && bridge.isModelLoading) ? Theme.warning : Theme.success
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontUi
                                }
                            }
                        }
                    }

                    // Row 3: Sampling Chips & Interactive Popover / Edit
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "⚙ Sampling:"
                            color: Theme.textSecondary
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                        }

                        // Repetition Penalty Chip
                        RowLayout {
                            spacing: 4
                            Text { text: "Rep. Penalty:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                            Rectangle {
                                width: 52; height: 24; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                                TextInput {
                                    anchors.centerIn: parent
                                    color: Theme.warning
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontMono
                                    text: (typeof bridge !== "undefined") ? bridge.repetitionPenalty.toFixed(2) : "1.15"
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (typeof bridge !== "undefined") {
                                            var val = parseFloat(text);
                                            if (!isNaN(val) && val >= 1.0) bridge.repetitionPenalty = val;
                                        }
                                    }
                                }
                            }
                        }

                        // Temperature Chip
                        RowLayout {
                            spacing: 4
                            Text { text: "Temp:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                            Rectangle {
                                width: 46; height: 24; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                                TextInput {
                                    anchors.centerIn: parent
                                    color: Theme.accent
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontMono
                                    text: (typeof bridge !== "undefined") ? bridge.temperature.toFixed(2) : "0.70"
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (typeof bridge !== "undefined") {
                                            var val = parseFloat(text);
                                            if (!isNaN(val) && val >= 0.0) bridge.temperature = val;
                                        }
                                    }
                                }
                            }
                        }

                        // Top-P Chip
                        RowLayout {
                            spacing: 4
                            Text { text: "Top-P:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                            Rectangle {
                                width: 46; height: 24; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                                TextInput {
                                    anchors.centerIn: parent
                                    color: Theme.success
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontMono
                                    text: (typeof bridge !== "undefined") ? bridge.topP.toFixed(2) : "0.90"
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (typeof bridge !== "undefined") {
                                            var val = parseFloat(text);
                                            if (!isNaN(val) && val > 0.0) bridge.topP = val;
                                        }
                                    }
                                }
                            }
                        }

                        // Max Tokens Chip
                        RowLayout {
                            spacing: 4
                            Text { text: "Max Tokens:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                            Rectangle {
                                width: 54; height: 24; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                                TextInput {
                                    anchors.centerIn: parent
                                    color: Theme.textPrimary
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontMono
                                    text: (typeof bridge !== "undefined") ? bridge.maxTokens.toString() : "512"
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (typeof bridge !== "undefined") {
                                            var val = parseInt(text);
                                            if (!isNaN(val) && val > 0) bridge.maxTokens = val;
                                        }
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "💾 Speichern"
                            implicitHeight: 24
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: {
                                if (typeof bridge !== "undefined") {
                                    bridge.saveCurrentModelConfig();
                                }
                            }
                        }
                    }
                }
            }

            // =====================================================================
            // CONVERSATION LIST (Using Reusable MessageCard)
            // =====================================================================
            ListView {
                id: chatList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 14

                model: ListModel {
                    ListElement {
                        role: "user"
                        text: "Kannst du mir den aktuellen Status der OpenVINO-Inferenz und Tool-Governance erklären?"
                        reasoning: ""
                    }
                    ListElement {
                        role: "assistant"
                        text: "VINOX führt alle Modelle mit nativer OpenVINO GenAI C++ Integration aus. Die Tool-Governance bleibt fail-closed; gefährliche Aktionen verlangen einen expliziten, Hash-gebundenen Freigabedialog."
                        reasoning: "Reasoning stream from native C-ABI channel: checking VINOX policy rules, hardware mmap state, and active plugin manifests."
                    }
                }

                delegate: MessageCard {
                    role: model.role
                    messageText: model.text
                    reasoningText: model.reasoning
                    isStreaming: (typeof bridge !== "undefined" && bridge.isGenerating && index === chatList.model.count - 1)
                }
            }

            // =====================================================================
            // INPUT & ACTION AREA
            // =====================================================================
            Rectangle {
                Layout.fillWidth: true
                height: 94
                radius: Theme.radiusMd
                color: Theme.bgSidebar
                border.color: promptInput.activeFocus ? Theme.borderFocus : Theme.borderMuted

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        TextArea {
                            id: promptInput
                            placeholderText: "Nachricht oder Frage eingeben... (Strg+Enter zum Senden)"
                            placeholderTextColor: Theme.textTertiary
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeMd
                            font.family: Theme.fontUi
                            wrapMode: TextEdit.Wrap
                            background: null
                            selectByMouse: true

                            Keys.onReturnPressed: function(event) {
                                if (event.modifiers & Qt.ControlModifier) {
                                    sendAction();
                                    event.accepted = true;
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 8
                        Button {
                            id: sendBtn
                            text: (typeof bridge !== "undefined" && bridge.isGenerating) ? "⏹ Stopp" : "➤ Senden"
                            highlighted: true
                            implicitWidth: 90
                            onClicked: sendAction()
                        }
                        Button {
                            text: "Löschen"
                            implicitWidth: 90
                            onClicked: {
                                chatList.model.clear();
                                if (typeof bridge !== "undefined") {
                                    bridge.clearChat();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function sendAction() {
        if (typeof bridge !== "undefined" && bridge.isGenerating) {
            bridge.cancel();
        } else if (promptInput.text.trim().length > 0) {
            var msg = promptInput.text;
            promptInput.text = "";
            chatList.model.append({
                role: "user",
                text: msg,
                reasoning: ""
            });
            chatList.positionViewAtEnd();
            if (typeof bridge !== "undefined") {
                bridge.sendMessage(msg);
            }
        }
    }

    Connections {
        target: typeof bridge !== "undefined" ? bridge : null

        function onTokenReceived(chunk, isReasoning) {
            if (chatList.model.count > 0) {
                var lastIdx = chatList.model.count - 1;
                var item = chatList.model.get(lastIdx);
                if (item.role === "assistant") {
                    if (isReasoning) {
                        chatList.model.setProperty(lastIdx, "reasoning", item.reasoning + chunk);
                    } else {
                        chatList.model.setProperty(lastIdx, "text", item.text + chunk);
                    }
                    chatList.positionViewAtEnd();
                }
            }
        }

        function onMessageStarted(role) {
            chatList.model.append({
                role: role,
                text: "",
                reasoning: ""
            });
            chatList.positionViewAtEnd();
        }

        function onGenerationFinished(success, errorMsg) {
            if (!success && errorMsg && errorMsg.length > 0) {
                chatList.model.append({
                    role: "assistant",
                    text: "❌ Fehler: " + errorMsg,
                    reasoning: ""
                });
                chatList.positionViewAtEnd();
            }
        }

        function onConversationLoaded(messages) {
            chatList.model.clear();
            for (var i = 0; i < messages.length; ++i) {
                var m = messages[i];
                chatList.model.append({
                    role: m.role || "user",
                    text: m.text || "",
                    reasoning: m.reasoning || ""
                });
            }
            chatList.positionViewAtEnd();
        }
    }

    Component.onCompleted: {
        if (typeof bridge !== "undefined") {
            bridge.refreshConversations();
            if (bridge.conversations && bridge.conversations.length > 0) {
                bridge.selectConversation(bridge.conversations[0].id);
            }
        }
    }
}
