import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0

Rectangle {
    id: relRoot
    color: Theme.bgApp

    // View state
    property string activeMode: "graph" // "graph" or "list"
    property real graphZoom: 1.0
    property real graphPanX: 0
    property real graphPanY: 0
    property bool isPanning: false
    property point startPan: Qt.point(0, 0)

    property var graphNodes: ({})
    property var graphEdges: []
    property var draggedNode: null
    property var hoveredNode: null
    property var selectedNode: null
    property bool physicsActive: true
    property int physicsTicks: 0
    property bool antiCollisionActive: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Query & Toolbar Card
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: toolbarCol.implicitHeight + 24
            radius: Theme.radiusMd
            color: Theme.bgSidebar
            border.color: Theme.borderMuted

            ColumnLayout {
                id: toolbarCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                // Row 1: Search, Query, Presets, Mode Switch
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "🕸️ Root Entity:"
                        color: Theme.textSecondary
                        font.bold: true
                        font.pixelSize: Theme.fontSizeBase
                        font.family: Theme.fontUi
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
                        height: 36
                        radius: Theme.radiusSm
                        color: Theme.bgApp
                        border.color: entityInput.activeFocus ? Theme.borderFocus : Theme.borderMuted

                        TextInput {
                            id: entityInput
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBase
                            font.family: Theme.fontMono
                            selectByMouse: true
                            text: "VINOX"
                            onAccepted: runRelationsQuery()
                        }
                    }

                    Button {
                        text: "⚡ Query CTE"
                        highlighted: true
                        implicitHeight: 36
                        onClicked: runRelationsQuery()
                    }

                    // Mode Switcher
                    Rectangle {
                        implicitHeight: 36
                        implicitWidth: modeRow.implicitWidth + 8
                        radius: Theme.radiusSm
                        color: Theme.bgCard
                        border.color: Theme.borderMuted

                        RowLayout {
                            id: modeRow
                            anchors.centerIn: parent
                            spacing: 4

                            Rectangle {
                                width: 140; height: 28; radius: Theme.radiusXs
                                color: activeMode === "graph" ? Theme.accent : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "🕸️ Neo4J Hex-Graph"
                                    color: activeMode === "graph" ? "#ffffff" : Theme.textSecondary
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontUi
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        activeMode = "graph";
                                        graphCanvas.requestPaint();
                                    }
                                }
                            }

                            Rectangle {
                                width: 100; height: 28; radius: Theme.radiusXs
                                color: activeMode === "list" ? Theme.accent : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "📋 CTE Liste"
                                    color: activeMode === "list" ? "#ffffff" : Theme.textSecondary
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontUi
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: activeMode = "list"
                                }
                            }
                        }
                    }
                }

                // Row 2: Quick Presets & Graph Controls
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "Schnellauswahl:"
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                    }

                    Repeater {
                        model: ["VINOX", "Intel_AI_Boost_NPU", "Hybrid_Retrieval_Engine", "Reasoning_Channel", "MCP_Protocol"]
                        delegate: Rectangle {
                            implicitWidth: lblPreset.implicitWidth + 16
                            implicitHeight: 24
                            radius: Theme.radiusXs
                            color: presetHover.containsMouse ? Theme.accentSubtle : Theme.bgCard
                            border.color: presetHover.containsMouse ? Theme.accent : Theme.borderMuted

                            Text {
                                id: lblPreset
                                anchors.centerIn: parent
                                text: modelData
                                color: presetHover.containsMouse ? Theme.accent : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeSm
                                font.family: Theme.fontMono
                            }
                            MouseArea {
                                id: presetHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    entityInput.text = modelData;
                                    runRelationsQuery();
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Graph Canvas Actions (only visible in graph mode)
                    RowLayout {
                        spacing: 6
                        visible: activeMode === "graph"

                        Button {
                            text: physicsActive ? "⏸ Pause" : "▶ Start"
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: {
                                physicsActive = !physicsActive;
                                if (physicsActive) {
                                    physicsTicks = 0;
                                    physicsTimer.start();
                                }
                            }
                        }

                        Button {
                            text: relRoot.antiCollisionActive ? "🛡️ Anti-Collision: An" : "🛡️ Anti-Collision: Aus"
                            highlighted: relRoot.antiCollisionActive
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: {
                                relRoot.antiCollisionActive = !relRoot.antiCollisionActive;
                                wakePhysics();
                                graphCanvas.requestPaint();
                            }
                        }

                        Button {
                            text: "🔍+"
                            implicitWidth: 32
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: zoomGraph(1.2)
                        }

                        Button {
                            text: "🔍-"
                            implicitWidth: 32
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: zoomGraph(0.8)
                        }

                        Button {
                            text: "⟲ Reset"
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: resetGraphView()
                        }
                    }
                }
            }
        }

        // View Content Area (Graph or List)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // -------------------------------------------------------------
            // GRAPH VIEW (Neo4J Hexagon Canvas)
            // -------------------------------------------------------------
            Item {
                id: graphStage
                anchors.fill: parent
                visible: activeMode === "graph"
                clip: true

                Canvas {
                    id: graphCanvas
                    anchors.fill: parent
                    renderTarget: Canvas.Image
                    renderStrategy: Canvas.Threaded

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.save();
                        ctx.clearRect(0, 0, width, height);

                        // 1. Subtle Grid Pattern
                        ctx.save();
                        ctx.strokeStyle = "rgba(255, 255, 255, 0.03)";
                        ctx.lineWidth = 1;
                        var gridSize = 40 * relRoot.graphZoom;
                        var startX = (relRoot.graphPanX % gridSize);
                        var startY = (relRoot.graphPanY % gridSize);
                        for (var gx = startX; gx < width; gx += gridSize) {
                            ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, height); ctx.stroke();
                        }
                        for (var gy = startY; gy < height; gy += gridSize) {
                            ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke();
                        }
                        ctx.restore();

                        // 2. Camera Transform
                        ctx.save();
                        ctx.translate(relRoot.graphPanX, relRoot.graphPanY);
                        ctx.scale(relRoot.graphZoom, relRoot.graphZoom);

                        // Draw Edges
                        for (var i = 0; i < relRoot.graphEdges.length; i++) {
                            var e = relRoot.graphEdges[i];
                            var u = relRoot.graphNodes[e.source];
                            var v = relRoot.graphNodes[e.target];
                            if (!u || !v) continue;

                            var isHighlighted = (relRoot.hoveredNode && (relRoot.hoveredNode.id === u.id || relRoot.hoveredNode.id === v.id)) ||
                                                  (relRoot.selectedNode && (relRoot.selectedNode.id === u.id || relRoot.selectedNode.id === v.id));

                            var dx = v.x - u.x;
                            var dy = v.y - u.y;
                            var dist = Math.sqrt(dx * dx + dy * dy) || 1;
                            var ux = dx / dist;
                            var uy = dy / dist;

                            var startX = u.x + ux * u.radius;
                            var startY = u.y + uy * u.radius;
                            var endX = v.x - ux * (v.radius + 6);
                            var endY = v.y - uy * (v.radius + 6);

                            // Quadratic Bezier Curve with perpendicular normal displacement
                            var midX = (startX + endX) / 2 + (-uy) * 16;
                            var midY = (startY + endY) / 2 + (ux) * 16;

                            ctx.save();
                            ctx.beginPath();
                            ctx.moveTo(startX, startY);
                            ctx.quadraticCurveTo(midX, midY, endX, endY);
                            ctx.strokeStyle = isHighlighted ? "#38bdf8" : "rgba(148, 163, 184, 0.35)";
                            ctx.lineWidth = isHighlighted ? 2.5 : 1.5;
                            ctx.stroke();

                            // Arrowhead at target
                            var headLen = isHighlighted ? 10 : 8;
                            var arrowAngle = Math.atan2(endY - midY, endX - midX);
                            ctx.beginPath();
                            ctx.moveTo(endX, endY);
                            ctx.lineTo(endX - headLen * Math.cos(arrowAngle - Math.PI / 7), endY - headLen * Math.sin(arrowAngle - Math.PI / 7));
                            ctx.lineTo(endX - headLen * Math.cos(arrowAngle + Math.PI / 7), endY - headLen * Math.sin(arrowAngle + Math.PI / 7));
                            ctx.closePath();
                            ctx.fillStyle = isHighlighted ? "#38bdf8" : "rgba(148, 163, 184, 0.7)";
                            ctx.fill();

                            // Relation Type Pill Label
                            var label = (e.type || "rel").toUpperCase();
                            ctx.font = "bold 9px monospace";
                            var textWidth = ctx.measureText(label).width;
                            var pillW = textWidth + 10;
                            var pillH = 14;

                            ctx.fillStyle = isHighlighted ? "rgba(14, 165, 233, 0.95)" : "rgba(22, 27, 34, 0.88)";
                            ctx.strokeStyle = isHighlighted ? "#38bdf8" : "rgba(148, 163, 184, 0.4)";
                            ctx.lineWidth = 1;
                            drawRoundedRect(ctx, midX - pillW / 2, midY - pillH / 2, pillW, pillH, 3);
                            ctx.fill();
                            ctx.stroke();

                            ctx.fillStyle = isHighlighted ? "#ffffff" : "#cbd5e1";
                            ctx.textAlign = "center";
                            ctx.textBaseline = "middle";
                            ctx.fillText(label, midX, midY);
                            ctx.restore();
                        }

                        // Draw Rounded Hexagon Nodes
                        for (var id in relRoot.graphNodes) {
                            var node = relRoot.graphNodes[id];
                            var isHovered = (relRoot.hoveredNode && relRoot.hoveredNode.id === node.id);
                            var isSelected = (relRoot.selectedNode && relRoot.selectedNode.id === node.id);
                            var palette = getNodePalette(node.depth);
                            var r = node.radius + (isHovered || isSelected ? 4 : 0);
                            var cr = 8; // rounded corner radius

                            ctx.save();
                            // Hexagon Path with rounded fillets
                            drawRoundedHexagon(ctx, node.x, node.y, r, cr);

                            // Gradient Fill
                            var grad = ctx.createLinearGradient(node.x, node.y - r, node.x, node.y + r);
                            grad.addColorStop(0, palette.fillStart);
                            grad.addColorStop(1, palette.fillEnd);
                            ctx.fillStyle = grad;
                            ctx.fill();

                            // Border
                            ctx.strokeStyle = isSelected ? "#ffffff" : palette.border;
                            ctx.lineWidth = isSelected ? 3.5 : (isHovered ? 2.5 : 1.8);
                            ctx.stroke();

                            // Dynamic 1-or-2 Line Entity Label
                            var lines = formatNodeLabel(node.id);
                            ctx.fillStyle = "#ffffff";
                            ctx.textAlign = "center";
                            ctx.textBaseline = "middle";

                            if (lines.length === 1) {
                                ctx.font = (node.depth === 0 ? "16px" : "13px") + " sans-serif";
                                ctx.fillText(getNodeIcon(node.id), node.x, node.y - 7);

                                ctx.font = "bold 9.5px monospace";
                                ctx.fillText(lines[0], node.x, node.y + 8);
                            } else {
                                ctx.font = (node.depth === 0 ? "14px" : "12px") + " sans-serif";
                                ctx.fillText(getNodeIcon(node.id), node.x, node.y - 11);

                                ctx.font = "bold 8.8px monospace";
                                ctx.fillText(lines[0], node.x, node.y + 3);
                                ctx.fillText(lines[1], node.x, node.y + 14);
                            }

                            // Depth Badge Pill
                            if (node.depth >= 0) {
                                var depthTag = "D" + node.depth;
                                ctx.font = "bold 7.5px sans-serif";
                                ctx.fillStyle = "rgba(15, 23, 42, 0.85)";
                                drawRoundedRect(ctx, node.x - 9, node.y - r + 3, 18, 9, 3);
                                ctx.fill();
                                ctx.fillStyle = palette.border;
                                ctx.fillText(depthTag, node.x, node.y - r + 8);
                            }

                            ctx.restore();
                        }

                        ctx.restore();

                        // 3. Screen-Space Hover Tooltip
                        if (relRoot.hoveredNode && !relRoot.draggedNode) {
                            var sx = relRoot.graphPanX + relRoot.hoveredNode.x * relRoot.graphZoom;
                            var sy = relRoot.graphPanY + relRoot.hoveredNode.y * relRoot.graphZoom;
                            var tipPalette = getNodePalette(relRoot.hoveredNode.depth);
                            var nodeRadiusScreen = (relRoot.hoveredNode.radius + 4) * relRoot.graphZoom;

                            ctx.save();
                            var fullTitle = relRoot.hoveredNode.id;
                            var hintText = "⚡ Doppelklick: Als Root • Klick: Details";

                            ctx.font = "bold 11.5px monospace";
                            var titleWidth = ctx.measureText(fullTitle).width;
                            ctx.font = "9.5px sans-serif";
                            var hintWidth = ctx.measureText(hintText).width;

                            var tooltipW = Math.max(titleWidth + 70, hintWidth + 24, 180);
                            var tooltipH = 46;
                            var tipX = sx - tooltipW / 2;
                            var tipY = sy - nodeRadiusScreen - tooltipH - 12;

                            if (tipX < 10) tipX = 10;
                            if (tipX + tooltipW > width - 10) tipX = width - tooltipW - 10;
                            if (tipY < 10) tipY = sy + nodeRadiusScreen + 14;

                            ctx.fillStyle = "rgba(15, 23, 42, 0.96)";
                            ctx.strokeStyle = tipPalette.border;
                            ctx.lineWidth = 1.5;
                            drawRoundedRect(ctx, tipX, tipY, tooltipW, tooltipH, 7);
                            ctx.fill();
                            ctx.stroke();

                            // Icon + Full Title
                            ctx.textAlign = "left";
                            ctx.textBaseline = "middle";
                            ctx.font = "12px sans-serif";
                            ctx.fillStyle = "#ffffff";
                            ctx.fillText(getNodeIcon(relRoot.hoveredNode.id), tipX + 10, tipY + 15);

                            ctx.font = "bold 11px monospace";
                            ctx.fillStyle = "#ffffff";
                            ctx.fillText(fullTitle, tipX + 28, tipY + 15);

                            // Depth Badge Pill
                            ctx.font = "bold 8.5px sans-serif";
                            var badgeW = 42;
                            var badgeH = 15;
                            var badgeX = tipX + tooltipW - badgeW - 8;
                            var badgeY = tipY + 7;
                            ctx.fillStyle = tipPalette.fillEnd || "rgba(30, 41, 59, 0.8)";
                            ctx.strokeStyle = tipPalette.border;
                            ctx.lineWidth = 1;
                            drawRoundedRect(ctx, badgeX, badgeY, badgeW, badgeH, 3);
                            ctx.fill();
                            ctx.stroke();

                            ctx.fillStyle = "#ffffff";
                            ctx.textAlign = "center";
                            ctx.fillText("Tiefe " + relRoot.hoveredNode.depth, badgeX + badgeW / 2, badgeY + badgeH / 2);

                            // Hint Line
                            ctx.textAlign = "left";
                            ctx.font = "9px sans-serif";
                            ctx.fillStyle = "rgba(148, 163, 184, 0.95)";
                            ctx.fillText(hintText, tipX + 10, tipY + 34);

                            ctx.restore();
                        }

                        ctx.restore();
                    }
                }

                // Interactive MouseArea for Graph
                MouseArea {
                    id: graphMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: relRoot.draggedNode ? Qt.ClosedHandCursor : (relRoot.hoveredNode ? Qt.PointingHandCursor : (relRoot.isPanning ? Qt.ClosedHandCursor : Qt.ArrowCursor))

                    function getGraphCoords(mouse) {
                        return {
                            gx: (mouse.x - relRoot.graphPanX) / relRoot.graphZoom,
                            gy: (mouse.y - relRoot.graphPanY) / relRoot.graphZoom
                        };
                    }

                    function findNodeAt(gx, gy) {
                        for (var id in relRoot.graphNodes) {
                            var n = relRoot.graphNodes[id];
                            var dx = n.x - gx;
                            var dy = n.y - gy;
                            if (dx * dx + dy * dy <= (n.radius + 6) * (n.radius + 6)) {
                                return n;
                            }
                        }
                        return null;
                    }

                    onPositionChanged: function(mouse) {
                        var c = getGraphCoords(mouse);
                        if (relRoot.draggedNode) {
                            relRoot.draggedNode.x = c.gx;
                            relRoot.draggedNode.y = c.gy;
                            relRoot.draggedNode.vx = 0;
                            relRoot.draggedNode.vy = 0;
                            if (relRoot.antiCollisionActive) updatePhysicsStep();
                            wakePhysics();
                            graphCanvas.requestPaint();
                        } else if (relRoot.isPanning) {
                            relRoot.graphPanX = mouse.x - relRoot.startPan.x;
                            relRoot.graphPanY = mouse.y - relRoot.startPan.y;
                            graphCanvas.requestPaint();
                        } else {
                            var hit = findNodeAt(c.gx, c.gy);
                            if (hit !== relRoot.hoveredNode) {
                                relRoot.hoveredNode = hit;
                                graphCanvas.requestPaint();
                            }
                        }
                    }

                    onPressed: function(mouse) {
                        var c = getGraphCoords(mouse);
                        var hit = findNodeAt(c.gx, c.gy);
                        if (hit) {
                            relRoot.draggedNode = hit;
                            relRoot.selectedNode = hit;
                            inspectorDrawer.visible = true;
                            wakePhysics();
                            graphCanvas.requestPaint();
                        } else {
                            relRoot.isPanning = true;
                            relRoot.startPan = Qt.point(mouse.x - relRoot.graphPanX, mouse.y - relRoot.graphPanY);
                        }
                    }

                    onReleased: function(mouse) {
                        relRoot.draggedNode = null;
                        relRoot.isPanning = false;
                        graphCanvas.requestPaint();
                    }

                    onDoubleClicked: function(mouse) {
                        var c = getGraphCoords(mouse);
                        var hit = findNodeAt(c.gx, c.gy);
                        if (hit) {
                            entityInput.text = hit.id;
                            runRelationsQuery();
                        }
                    }

                    onWheel: function(wheel) {
                        var factor = wheel.angleDelta.y > 0 ? 1.15 : 0.85;
                        var mouseX = wheel.x;
                        var mouseY = wheel.y;
                        var newZoom = Math.max(0.25, Math.min(4.0, relRoot.graphZoom * factor));
                        relRoot.graphPanX = mouseX - (mouseX - relRoot.graphPanX) * (newZoom / relRoot.graphZoom);
                        relRoot.graphPanY = mouseY - (mouseY - relRoot.graphPanY) * (newZoom / relRoot.graphZoom);
                        relRoot.graphZoom = newZoom;
                        graphCanvas.requestPaint();
                    }
                }

                // Floating Entity Inspector (Neo4J style side card)
                Rectangle {
                    id: inspectorDrawer
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 14
                    width: 300
                    implicitHeight: inspectorCol.implicitHeight + 24
                    radius: Theme.radiusMd
                    color: "#F2161B22"
                    border.color: Theme.borderActive
                    visible: relRoot.selectedNode !== null

                    ColumnLayout {
                        id: inspectorCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Hexagon Entity"
                                color: Theme.accent
                                font.bold: true
                                font.pixelSize: Theme.fontSizeMd
                                font.family: Theme.fontUi
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "✕"
                                implicitWidth: 26; implicitHeight: 24
                                onClicked: {
                                    relRoot.selectedNode = null;
                                    inspectorDrawer.visible = false;
                                    graphCanvas.requestPaint();
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.borderMuted
                        }

                        Text {
                            text: relRoot.selectedNode ? relRoot.selectedNode.id : ""
                            color: Theme.textPrimary
                            font.bold: true
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeBase
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 8
                            Rectangle {
                                implicitWidth: lblDepthTag.implicitWidth + 12
                                implicitHeight: 22
                                radius: 4
                                color: Theme.accentSubtle
                                border.color: Theme.accent
                                Text {
                                    id: lblDepthTag
                                    anchors.centerIn: parent
                                    text: relRoot.selectedNode ? ("Tiefe: " + relRoot.selectedNode.depth) : "Tiefe: 0"
                                    color: Theme.accent
                                    font.bold: true
                                    font.pixelSize: Theme.fontSizeSm
                                }
                            }
                        }

                        Button {
                            text: "⚡ Als neuen Root abfragen"
                            highlighted: true
                            Layout.fillWidth: true
                            implicitHeight: 32
                            onClicked: {
                                if (relRoot.selectedNode) {
                                    entityInput.text = relRoot.selectedNode.id;
                                    runRelationsQuery();
                                }
                            }
                        }
                    }
                }

                // Legend Bar at Bottom
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 14
                    implicitHeight: 32
                    implicitWidth: legendRow.implicitWidth + 24
                    radius: Theme.radiusSm
                    color: "#D90D1117"
                    border.color: Theme.borderMuted

                    RowLayout {
                        id: legendRow
                        anchors.centerIn: parent
                        spacing: 16

                        RowLayout {
                            spacing: 6
                            Rectangle { width: 10; height: 10; radius: 2; color: "#f59e0b" }
                            Text { text: "Root (D0)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm }
                        }
                        RowLayout {
                            spacing: 6
                            Rectangle { width: 10; height: 10; radius: 2; color: "#0ea5e9" }
                            Text { text: "Direkt (D1)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm }
                        }
                        RowLayout {
                            spacing: 6
                            Rectangle { width: 10; height: 10; radius: 2; color: "#10b981" }
                            Text { text: "Transitiv 1 (D2)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm }
                        }
                        RowLayout {
                            spacing: 6
                            Rectangle { width: 10; height: 10; radius: 2; color: "#8b5cf6" }
                            Text { text: "Transitiv 2+ (D3+)"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm }
                        }
                    }
                }
            }

            // -------------------------------------------------------------
            // LIST VIEW (CTE Table View)
            // -------------------------------------------------------------
            ListView {
                id: relList
                anchors.fill: parent
                visible: activeMode === "list"
                clip: true
                spacing: 8

                model: ListModel {
                    id: relationsModel
                }

                // Empty state
                Rectangle {
                    anchors.centerIn: parent
                    width: 380
                    height: 140
                    radius: Theme.radiusMd
                    color: Theme.bgSidebar
                    border.color: Theme.borderMuted
                    visible: relationsModel.count === 0

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            text: "🕸️ Keine Relationen gefunden"
                            color: Theme.textPrimary
                            font.bold: true
                            font.pixelSize: Theme.fontSizeMd
                            font.family: Theme.fontUi
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: "Gib eine Entitäts-ID ein (z. B. 'VINOX')\noder wähle einen Schnellauswahl-Chip."
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                delegate: Rectangle {
                    width: relList.width
                    height: 52
                    radius: Theme.radiusSm
                    color: Theme.bgSidebar
                    border.color: itemHover.containsMouse ? Theme.borderActive : Theme.borderMuted

                    MouseArea {
                        id: itemHover
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12

                        // Depth Badge
                        Rectangle {
                            width: 26; height: 26; radius: 13
                            color: Theme.bgCard
                            border.color: Theme.borderMuted
                            Text {
                                anchors.centerIn: parent
                                text: model.depth ? model.depth.toString() : "1"
                                color: Theme.accent
                                font.bold: true
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeSm
                            }
                        }

                        // Source
                        Text {
                            text: model.sourceId ? model.sourceId : ""
                            color: Theme.textPrimary
                            font.family: Theme.fontMono
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                        }

                        // Relation Arrow
                        Rectangle {
                            implicitWidth: lblRel.implicitWidth + 14
                            implicitHeight: 22
                            radius: Theme.radiusXs
                            color: Theme.bgCard
                            border.color: Theme.warning
                            Text {
                                id: lblRel
                                anchors.centerIn: parent
                                text: "──[" + (model.type ? model.type : "rel") + "]──▶"
                                color: Theme.warning
                                font.bold: true
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeSm
                            }
                        }

                        // Target
                        Text {
                            text: model.targetId ? model.targetId : ""
                            color: Theme.success
                            font.family: Theme.fontMono
                            font.bold: true
                            font.pixelSize: Theme.fontSizeSm
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "Tiefe " + (model.depth ? model.depth : 1)
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                        }

                        Button {
                            text: copyRelTimer.running ? "✓" : "📋"
                            implicitWidth: 32
                            implicitHeight: 26
                            font.pixelSize: Theme.fontSizeSm
                            onClicked: {
                                if (typeof bridge !== "undefined") {
                                    var triple = model.sourceId + " -> [" + model.type + "] -> " + model.targetId;
                                    bridge.copyToClipboard(triple);
                                    copyRelTimer.start();
                                }
                            }

                            Timer {
                                id: copyRelTimer
                                interval: 1500
                                repeat: false
                            }
                        }
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Force-Directed Physics Engine
    // -------------------------------------------------------------------------
    Timer {
        id: physicsTimer
        interval: 16
        running: true
        repeat: true
        onTriggered: {
            if (relRoot.physicsActive && relRoot.physicsTicks < 300) {
                updatePhysicsStep();
                relRoot.physicsTicks++;
                graphCanvas.requestPaint();
            }
        }
    }

    function wakePhysics() {
        relRoot.physicsTicks = 0;
        physicsTimer.running = true;
    }

    function updatePhysicsStep() {
        var nodes = [];
        for (var id in relRoot.graphNodes) {
            nodes.push(relRoot.graphNodes[id]);
        }
        if (nodes.length === 0) return;

        var kRep = 4500;
        var kSpring = 0.05;
        var targetDist = 135;

        // 1. Coulomb Repulsion between all node pairs
        for (var i = 0; i < nodes.length; i++) {
            for (var j = i + 1; j < nodes.length; j++) {
                var u = nodes[i];
                var v = nodes[j];
                var dx = v.x - u.x;
                var dy = v.y - u.y;
                var d = Math.sqrt(dx * dx + dy * dy);
                if (d < 1) { dx = (Math.random() - 0.5); dy = (Math.random() - 0.5); d = 1; }
                if (d < 450) {
                    var f = kRep / (d * d);
                    var fx = (dx / d) * f;
                    var fy = (dy / d) * f;
                    if (u !== relRoot.draggedNode) { u.vx -= fx; u.vy -= fy; }
                    if (v !== relRoot.draggedNode) { v.vx += fx; v.vy += fy; }
                }
            }
        }

        // 2. Hooke's Spring Attraction along edges
        for (var eIdx = 0; eIdx < relRoot.graphEdges.length; eIdx++) {
            var edge = relRoot.graphEdges[eIdx];
            var nu = relRoot.graphNodes[edge.source];
            var nv = relRoot.graphNodes[edge.target];
            if (nu && nv) {
                var edx = nv.x - nu.x;
                var edy = nv.y - nu.y;
                var edist = Math.sqrt(edx * edx + edy * edy);
                if (edist < 1) edist = 1;
                var force = (edist - targetDist) * kSpring;
                var efx = (edx / edist) * force;
                var efy = (edy / edist) * force;
                if (nu !== relRoot.draggedNode) { nu.vx += efx; nu.vy += efy; }
                if (nv !== relRoot.draggedNode) { nv.vx -= efx; nv.vy -= efy; }
            }
        }

        // 3. Central Gravity & Damping
        var gravity = 0.02;
        for (var k = 0; k < nodes.length; k++) {
            var n = nodes[k];
            if (n === relRoot.draggedNode) continue;
            n.vx -= n.x * gravity;
            n.vy -= n.y * gravity;
            n.vx *= 0.85;
            n.vy *= 0.85;
            n.x += n.vx;
            n.y += n.vy;
        }

        // 4. Elastic Anti-Collision Solver (Guarantees zero hexagon overlapping)
        if (relRoot.antiCollisionActive) {
            var collisionPadding = 22;
            var collisionPasses = 3;
            for (var pass = 0; pass < collisionPasses; pass++) {
                for (var ci = 0; ci < nodes.length; ci++) {
                    for (var cj = ci + 1; cj < nodes.length; cj++) {
                        var cu = nodes[ci];
                        var cv = nodes[cj];
                        var cMinDist = (cu.radius || 38) + (cv.radius || 38) + collisionPadding;
                        var cdx = cv.x - cu.x;
                        var cdy = cv.y - cu.y;
                        var cdist = Math.sqrt(cdx * cdx + cdy * cdy);
                        if (cdist < cMinDist) {
                            if (cdist < 0.01) {
                                cdx = (Math.random() - 0.5) * 2;
                                cdy = (Math.random() - 0.5) * 2;
                                cdist = Math.sqrt(cdx * cdx + cdy * cdy) || 1;
                            }
                            var coverlap = (cMinDist - cdist);
                            var cnx = cdx / cdist;
                            var cny = cdy / cdist;

                            if (cu === relRoot.draggedNode) {
                                cv.x += cnx * coverlap;
                                cv.y += cny * coverlap;
                                cv.vx += cnx * coverlap * 0.25;
                                cv.vy += cny * coverlap * 0.25;
                            } else if (cv === relRoot.draggedNode) {
                                cu.x -= cnx * coverlap;
                                cu.y -= cny * coverlap;
                                cu.vx -= cnx * coverlap * 0.25;
                                cu.vy -= cny * coverlap * 0.25;
                            } else {
                                var chalf = coverlap * 0.5;
                                cu.x -= cnx * chalf;
                                cu.y -= cny * chalf;
                                cv.x += cnx * chalf;
                                cv.y += cny * chalf;

                                var cdvx = cv.vx - cu.vx;
                                var cdvy = cv.vy - cu.vy;
                                var cvn = cdvx * cnx + cdvy * cny;
                                if (cvn < 0) {
                                    var crestitution = 0.35;
                                    var cimpulse = -(1 + crestitution) * cvn * 0.5;
                                    cu.vx -= cnx * cimpulse;
                                    cu.vy -= cny * cimpulse;
                                    cv.vx += cnx * cimpulse;
                                    cv.vy += cny * cimpulse;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Canvas Geometry Drawing Helpers
    // -------------------------------------------------------------------------
    function drawRoundedHexagon(ctx, cx, cy, r, cr) {
        var angle = Math.PI / 3;
        var offset = Math.PI / 6; // flat-top hexagon orientation
        var pts = [];
        for (var i = 0; i < 6; i++) {
            var a = offset + i * angle;
            pts.push({ x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) });
        }
        ctx.beginPath();
        ctx.moveTo((pts[0].x + pts[5].x) / 2, (pts[0].y + pts[5].y) / 2);
        for (var j = 0; j < 6; j++) {
            var next = pts[(j + 1) % 6];
            ctx.arcTo(pts[j].x, pts[j].y, next.x, next.y, cr);
        }
        ctx.closePath();
    }

    function drawRoundedRect(ctx, x, y, w, h, r) {
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        ctx.beginPath();
        ctx.moveTo(x + r, y);
        ctx.lineTo(x + w - r, y);
        ctx.arcTo(x + w, y, x + w, y + r, r);
        ctx.lineTo(x + w, y + h - r);
        ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
        ctx.lineTo(x + r, y + h);
        ctx.arcTo(x, y + h, x, y + h - r, r);
        ctx.lineTo(x, y + r);
        ctx.arcTo(x, y, x + r, y, r);
        ctx.closePath();
    }

    function formatNodeLabel(id) {
        if (!id) return [""];
        if (id.length <= 11) return [id];
        var parts = id.split("_");
        if (parts.length >= 2) {
            if (parts.length === 2) {
                return [truncateLabel(parts[0], 12), truncateLabel(parts[1], 12)];
            }
            var mid = Math.ceil(parts.length / 2);
            var l1 = parts.slice(0, mid).join("_");
            var l2 = parts.slice(mid).join("_");
            return [truncateLabel(l1, 12), truncateLabel(l2, 12)];
        }
        return [id.substring(0, 10), truncateLabel(id.substring(10), 10)];
    }

    function truncateLabel(s, max) {
        return s.length > max ? s.substring(0, max - 1) + "…" : s;
    }

    function getNodePalette(depth) {
        if (depth === 0) {
            return { fillStart: "#f59e0b", fillEnd: "#b45309", border: "#fef08a" };
        } else if (depth === 1) {
            return { fillStart: "#0ea5e9", fillEnd: "#0369a1", border: "#bae6fd" };
        } else if (depth === 2) {
            return { fillStart: "#10b981", fillEnd: "#047857", border: "#a7f3d0" };
        } else {
            return { fillStart: "#8b5cf6", fillEnd: "#6d28d9", border: "#ddd6fe" };
        }
    }

    function getNodeIcon(id) {
        var s = (id || "").toLowerCase();
        if (s.indexOf("vinox") !== -1) return "⚡";
        if (s.indexOf("npu") !== -1 || s.indexOf("boost") !== -1) return "🚀";
        if (s.indexOf("gpu") !== -1 || s.indexOf("arc") !== -1) return "🎮";
        if (s.indexOf("cpu") !== -1 || s.indexOf("ultra") !== -1) return "🖥️";
        if (s.indexOf("retrieval") !== -1 || s.indexOf("search") !== -1 || s.indexOf("bm25") !== -1 || s.indexOf("vec") !== -1) return "🔍";
        if (s.indexOf("reason") !== -1 || s.indexOf("think") !== -1) return "🧠";
        if (s.indexOf("mcp") !== -1 || s.indexOf("tool") !== -1) return "🛠️";
        if (s.indexOf("quant") !== -1 || s.indexOf("int4") !== -1 || s.indexOf("int8") !== -1 || s.indexOf("fp16") !== -1) return "📐";
        if (s.indexOf("model") !== -1 || s.indexOf("qwen") !== -1 || s.indexOf("deepseek") !== -1) return "🤖";
        return "⬡";
    }

    function zoomGraph(factor) {
        var cx = graphStage.width / 2;
        var cy = graphStage.height / 2;
        var newZoom = Math.max(0.25, Math.min(4.0, relRoot.graphZoom * factor));
        relRoot.graphPanX = cx - (cx - relRoot.graphPanX) * (newZoom / relRoot.graphZoom);
        relRoot.graphPanY = cy - (cy - relRoot.graphPanY) * (newZoom / relRoot.graphZoom);
        relRoot.graphZoom = newZoom;
        graphCanvas.requestPaint();
    }

    function resetGraphView() {
        relRoot.graphZoom = 1.0;
        relRoot.graphPanX = graphStage.width / 2;
        relRoot.graphPanY = graphStage.height / 2;
        wakePhysics();
        graphCanvas.requestPaint();
    }

    // -------------------------------------------------------------------------
    // Query & Data Ingestion
    // -------------------------------------------------------------------------
    function runRelationsQuery() {
        var sid = entityInput.text.trim();
        if (sid.length === 0) return;

        var items = [];
        if (typeof bridge !== "undefined") {
            items = bridge.getRelations(sid);
        }

        relationsModel.clear();
        var nodeMap = {};
        var edgeList = [];

        // Add root node
        nodeMap[sid] = {
            id: sid,
            depth: 0,
            x: 0,
            y: 0,
            vx: 0,
            vy: 0,
            radius: 44
        };

        if (items && items.length > 0) {
            for (var i = 0; i < items.length; ++i) {
                var it = items[i];
                relationsModel.append(it);

                var s = it.sourceId;
                var t = it.targetId;
                var d = it.depth || 1;

                if (!nodeMap[s]) {
                    nodeMap[s] = {
                        id: s,
                        depth: (s === sid ? 0 : d),
                        x: (Math.random() - 0.5) * 200,
                        y: (Math.random() - 0.5) * 200,
                        vx: 0,
                        vy: 0,
                        radius: (d === 1 ? 40 : 36)
                    };
                }

                if (!nodeMap[t]) {
                    nodeMap[t] = {
                        id: t,
                        depth: d,
                        x: (Math.random() - 0.5) * 240,
                        y: (Math.random() - 0.5) * 240,
                        vx: 0,
                        vy: 0,
                        radius: (d === 1 ? 40 : 36)
                    };
                } else if (d < nodeMap[t].depth) {
                    nodeMap[t].depth = d;
                }

                edgeList.push({
                    source: s,
                    target: t,
                    type: it.type || "rel"
                });
            }
        }

        relRoot.graphNodes = nodeMap;
        relRoot.graphEdges = edgeList;

        if (relRoot.graphPanX === 0 && relRoot.graphPanY === 0) {
            relRoot.graphPanX = graphStage.width / 2;
            relRoot.graphPanY = graphStage.height / 2;
        }

        wakePhysics();
        graphCanvas.requestPaint();
    }

    Component.onCompleted: {
        runRelationsQuery();
    }
}
