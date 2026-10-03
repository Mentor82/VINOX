import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Vinox 1.0
import "../components"

Rectangle {
    id: searchRoot
    color: Theme.bgApp

    property string retrievalMode: "rrf" // "rrf", "weighted", "bm25", "vector"
    property real searchAlpha: 0.5
    property int rrfK: 60
    property int searchLimit: 10
    property bool showIngestDrawer: false
    property string ingestStatusMsg: ""
    property bool isIngestSuccess: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // =====================================================================
        // HEADER & QUERY CONTROLS BAR
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusMd
            color: Theme.bgSidebar
            border.color: Theme.borderMuted
            implicitHeight: controlsCol.implicitHeight + 24

            ColumnLayout {
                id: controlsCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                // Row 1: Search Input & Action
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "🔍 Suchanfrage:"
                        color: Theme.textSecondary
                        font.bold: true
                        font.pixelSize: Theme.fontSizeBase
                        font.family: Theme.fontUi
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Theme.radiusSm
                        color: Theme.bgApp
                        border.color: queryInput.activeFocus ? Theme.borderFocus : Theme.borderMuted

                        TextInput {
                            id: queryInput
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeBase
                            font.family: Theme.fontUi
                            selectByMouse: true
                            text: ""

                            Text {
                                text: "Suchbegriffe oder semantische Fragen eingeben (z. B. 'OpenVINO mmap' oder 'SQLite CTE')..."
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeBase
                                font.family: Theme.fontUi
                                visible: !queryInput.text && !queryInput.activeFocus
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                            }

                            onAccepted: doSearch()
                        }
                    }

                    Button {
                        text: "⚡ Suchen"
                        highlighted: true
                        implicitHeight: 36
                        onClicked: doSearch()
                    }

                    Button {
                        text: showIngestDrawer ? "▲ Schließen" : "➕ Dokument Ingest"
                        implicitHeight: 36
                        onClicked: showIngestDrawer = !showIngestDrawer
                    }
                }

                // Row 2: Retrieval Policy Selector (RRF is an explicit mode!)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Text {
                        text: "Retrieval Policy:"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                    }

                    // Radio Button 1: Hybrid RRF
                    RadioButton {
                        text: "Hybrid RRF (Reciprocal Rank Fusion)"
                        checked: retrievalMode === "rrf"
                        font.pixelSize: Theme.fontSizeSm
                        onClicked: retrievalMode = "rrf"
                    }

                    // Radio Button 2: Hybrid Weighted
                    RadioButton {
                        text: "Hybrid Weighted (α)"
                        checked: retrievalMode === "weighted"
                        font.pixelSize: Theme.fontSizeSm
                        onClicked: retrievalMode = "weighted"
                    }

                    // Radio Button 3: Keyword Only
                    RadioButton {
                        text: "Keyword (BM25)"
                        checked: retrievalMode === "bm25"
                        font.pixelSize: Theme.fontSizeSm
                        onClicked: retrievalMode = "bm25"
                    }

                    // Radio Button 4: Vector Only
                    RadioButton {
                        text: "Dense Vector"
                        checked: retrievalMode === "vector"
                        font.pixelSize: Theme.fontSizeSm
                        onClicked: retrievalMode = "vector"
                    }

                    Item { Layout.fillWidth: true }

                    // Results Count
                    RowLayout {
                        spacing: 6
                        Text { text: "Treffer:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                        ComboBox {
                            implicitWidth: 70
                            implicitHeight: 28
                            model: [5, 10, 20, 50]
                            currentIndex: 1
                            onActivated: function(index) {
                                searchLimit = model[index];
                            }
                        }
                    }
                }

                // Row 3: Advanced Policy Parameters (Context-Sensitive)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // Weighted Alpha Slider (Only visible when weighted mode is active)
                    RowLayout {
                        visible: retrievalMode === "weighted"
                        spacing: 8
                        Text { text: "Gewichtung (α):"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                        Text { text: "BM25 (0.0)"; color: Theme.textTertiary; font.pixelSize: Theme.fontSizeXs; font.family: Theme.fontMono }
                        Slider {
                            id: alphaSlider
                            implicitWidth: 140
                            from: 0.0
                            to: 1.0
                            stepSize: 0.05
                            value: searchAlpha
                            onValueChanged: searchAlpha = value
                        }
                        Text { text: "Vector (1.0)"; color: Theme.retrieval; font.pixelSize: Theme.fontSizeXs; font.family: Theme.fontMono }
                        Rectangle {
                            implicitWidth: 42; implicitHeight: 20; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                            Text {
                                anchors.centerIn: parent
                                text: searchAlpha.toFixed(2)
                                color: Theme.accent
                                font.bold: true
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }

                    // RRF k parameter (Only visible when RRF mode is active)
                    RowLayout {
                        visible: retrievalMode === "rrf"
                        spacing: 8
                        Text { text: "RRF Glättungskonstante (k):"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                        Rectangle {
                            implicitWidth: 46; implicitHeight: 22; radius: Theme.radiusXs; color: Theme.bgApp; border.color: Theme.borderMuted
                            TextInput {
                                anchors.centerIn: parent
                                text: rrfK.toString()
                                color: Theme.accent
                                font.bold: true
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeSm
                                selectByMouse: true
                                onEditingFinished: {
                                    var val = parseInt(text);
                                    if (!isNaN(val) && val > 0) rrfK = val;
                                }
                            }
                        }
                        Text {
                            text: "(Formel: score = Σ 1/(k + rank_i), Standard k=60)"
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontUi
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // =====================================================================
        // COLLAPSIBLE DOCUMENT INGESTION DRAWER
        // =====================================================================
        Rectangle {
            visible: showIngestDrawer
            Layout.fillWidth: true
            radius: Theme.radiusMd
            color: Theme.bgSidebar
            border.color: Theme.borderActive
            implicitHeight: ingestCol.implicitHeight + 24

            ColumnLayout {
                id: ingestCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    spacing: 8
                    Text { text: "📥 DOKUMENT IN DEN VEKTORSPEICHER INDEXIEREN"; color: Theme.accent; font.bold: true; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: ingestStatusMsg.length > 0
                        text: ingestStatusMsg
                        color: isIngestSuccess ? Theme.success : Theme.danger
                        font.bold: true
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Titel:"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSm; font.family: Theme.fontUi }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        radius: Theme.radiusXs
                        color: Theme.bgApp
                        border.color: Theme.borderMuted
                        TextInput {
                            id: docTitleInput
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                            text: ""
                            selectByMouse: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    radius: Theme.radiusXs
                    color: Theme.bgApp
                    border.color: Theme.borderMuted

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 6
                        TextArea {
                            id: docContentInput
                            placeholderText: "Dokumenttext hier einfügen, um SQLite FTS5 Volltext und Qwen3 Vektor-Embeddings zu generieren..."
                            placeholderTextColor: Theme.textTertiary
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeSm
                            font.family: Theme.fontUi
                            wrapMode: TextEdit.Wrap
                            background: null
                            selectByMouse: true
                        }
                    }
                }

                RowLayout {
                    spacing: 10
                    Button {
                        text: "Indexieren & Einbetten"
                        highlighted: true
                        implicitHeight: 30
                        onClicked: {
                            if (docTitleInput.text.trim().length === 0 || docContentInput.text.trim().length === 0) {
                                ingestStatusMsg = "Bitte Titel und Inhalt eingeben.";
                                isIngestSuccess = false;
                                return;
                            }
                            if (typeof bridge !== "undefined") {
                                var ok = bridge.ingestDocument(docTitleInput.text.trim(), docContentInput.text.trim());
                                if (ok) {
                                    ingestStatusMsg = "✓ Dokument erfolgreich indexiert & eingebettet!";
                                    isIngestSuccess = true;
                                    docTitleInput.text = "";
                                    docContentInput.text = "";
                                } else {
                                    ingestStatusMsg = "❌ Ingestion fehlgeschlagen.";
                                    isIngestSuccess = false;
                                }
                            }
                        }
                    }
                    Button {
                        text: "Abbrechen"
                        implicitHeight: 30
                        onClicked: {
                            showIngestDrawer = false;
                            ingestStatusMsg = "";
                        }
                    }
                }
            }
        }

        // =====================================================================
        // RESULTS LIST VIEW (Using Reusable RetrievalResultCard)
        // =====================================================================
        ListView {
            id: resultListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10

            model: ListModel {
                id: resultsModel
            }

            // Empty State
            Rectangle {
                anchors.centerIn: parent
                width: 420
                height: 160
                radius: Theme.radiusMd
                color: Theme.bgSidebar
                border.color: Theme.borderMuted
                visible: resultsModel.count === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: "🔎 Keine Suchergebnisse"
                        color: Theme.textPrimary
                        font.bold: true
                        font.pixelSize: Theme.fontSizeLg
                        font.family: Theme.fontUi
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: "Gib oben einen Suchbegriff ein oder nutze 'Dokument Ingest',\num neues Wissen im SQLite-vec & FTS5-Speicher abzulegen."
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontUi
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }

            delegate: RetrievalResultCard {
                rank: index + 1
                title: model.title
                documentId: model.documentId
                matchType: model.matchType
                score: model.score
                snippet: model.snippet
            }
        }
    }

    function doSearch() {
        if (typeof bridge === "undefined") return;
        var q = queryInput.text.trim();
        if (q.length === 0) return;

        resultsModel.clear();

        // Map UI retrieval mode to alpha parameter
        var effectiveAlpha = 0.5;
        if (retrievalMode === "bm25") {
            effectiveAlpha = 0.0;
        } else if (retrievalMode === "vector") {
            effectiveAlpha = 1.0;
        } else if (retrievalMode === "weighted") {
            effectiveAlpha = searchAlpha;
        } else {
            // RRF: default hybrid fusion
            effectiveAlpha = 0.5;
        }

        var hits = bridge.searchHybrid(q, effectiveAlpha, searchLimit);
        for (var i = 0; i < hits.length; ++i) {
            var item = hits[i];
            if (retrievalMode === "rrf") {
                item.matchType = "HYBRID RRF";
            }
            resultsModel.append(item);
        }
    }
}
