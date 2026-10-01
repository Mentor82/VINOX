#pragma once

namespace vinox::server {

static const char* VINOX_WEB_STUDIO_HTML = R"rawliteral(<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>VINOX — Local Generative AI Studio</title>
  <style>
    :root {
      --bg-app: #0D1117;
      --bg-sidebar: #161B22;
      --bg-card: #21262D;
      --bg-card-hover: #292E36;
      --border-muted: #30363D;
      --border-active: #388BFD;
      --border-subtle: #21262D;
      --text-primary: #F0F6FC;
      --text-secondary: #8B949E;
      --text-tertiary: #6E7681;
      --accent: #58A6FF;
      --accent-subtle: rgba(56, 139, 253, 0.15);
      --success: #3FB950;
      --success-subtle: rgba(63, 185, 80, 0.15);
      --warning: #D29922;
      --warning-subtle: rgba(210, 153, 34, 0.15);
      --danger: #F85149;
      --reasoning: #BC8CFF;
      --reasoning-subtle: rgba(188, 140, 255, 0.15);
      --retrieval: #39C5BB;
      --retrieval-subtle: rgba(57, 197, 187, 0.15);
      --font-ui: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
      --font-mono: "Cascadia Code", "Consolas", "Courier New", monospace;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg-app);
      color: var(--text-primary);
      font-family: var(--font-ui);
      height: 100vh;
      display: flex;
      overflow: hidden;
      font-size: 13px;
    }

    /* Sidebar */
    .sidebar {
      width: 250px;
      background: var(--bg-sidebar);
      border-right: 1px solid var(--border-muted);
      display: flex;
      flex-direction: column;
      flex-shrink: 0;
    }
    .brand-header {
      padding: 16px;
      display: flex;
      align-items: center;
      gap: 12px;
      border-bottom: 1px solid var(--border-muted);
    }
    .brand-logo {
      width: 34px;
      height: 34px;
      background: var(--accent);
      color: var(--bg-app);
      font-weight: bold;
      font-size: 18px;
      display: flex;
      align-items: center;
      justify-content: center;
      border-radius: 8px;
    }
    .brand-title h1 { font-size: 15px; font-weight: bold; color: var(--text-primary); }
    .brand-title p { font-size: 11px; color: var(--text-secondary); }

    .nav-section-title {
      padding: 14px 16px 6px;
      font-size: 10px;
      font-weight: bold;
      color: var(--text-tertiary);
      letter-spacing: 0.5px;
    }
    .nav-item {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 9px 16px;
      color: var(--text-secondary);
      cursor: pointer;
      text-decoration: none;
      font-weight: 500;
      transition: all 0.15s;
    }
    .nav-item:hover { background: var(--bg-card); color: var(--text-primary); }
    .nav-item.active {
      background: var(--accent-subtle);
      color: var(--accent);
      border-left: 3px solid var(--accent);
      font-weight: 600;
    }

    .capabilities-card {
      margin: auto 12px 12px;
      padding: 10px;
      background: var(--bg-card);
      border: 1px solid var(--border-muted);
      border-radius: 8px;
      font-size: 11px;
    }
    .cap-header { display: flex; justify-content: space-between; margin-bottom: 6px; font-size: 10px; color: var(--text-tertiary); font-weight: bold; }
    .cap-row { display: flex; justify-content: space-between; align-items: center; margin-top: 4px; }
    .cap-label { color: var(--text-secondary); }
    .cap-badge {
      font-size: 9px;
      font-family: var(--font-mono);
      padding: 2px 6px;
      border-radius: 4px;
      font-weight: bold;
    }
    .badge-ready { background: var(--success-subtle); color: var(--success); }
    .badge-active { background: var(--success-subtle); color: var(--success); }
    .badge-cached { background: var(--retrieval-subtle); color: var(--retrieval); }
    .badge-trusted { background: var(--reasoning-subtle); color: var(--reasoning); }

    /* Main Area */
    .main-area {
      flex: 1;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    .header-bar {
      height: 52px;
      background: var(--bg-sidebar);
      border-bottom: 1px solid var(--border-muted);
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 20px;
      flex-shrink: 0;
    }
    .header-title { font-size: 14px; font-weight: bold; color: var(--text-primary); }
    .telemetry-bar { display: flex; gap: 8px; }
    .pill {
      background: var(--bg-card);
      border: 1px solid var(--border-muted);
      padding: 4px 10px;
      border-radius: 6px;
      font-size: 11px;
      display: flex;
      gap: 6px;
    }
    .pill-label { color: var(--text-secondary); }
    .pill-val { font-family: var(--font-mono); font-weight: bold; color: var(--accent); }

    /* Views */
    .view-container {
      flex: 1;
      display: flex;
      flex-direction: column;
      padding: 16px 20px;
      overflow-y: auto;
      gap: 14px;
    }
    .view-content { display: none; height: 100%; flex-direction: column; gap: 12px; }
    .view-content.active { display: flex; }

    /* Sampling Controls */
    .panel {
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      border-radius: 8px;
      padding: 12px 14px;
    }
    .controls-row { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }
    label { color: var(--text-secondary); font-size: 11px; font-weight: bold; }
    select, input[type="text"], input[type="number"] {
      background: var(--bg-app);
      border: 1px solid var(--border-muted);
      color: var(--text-primary);
      padding: 5px 8px;
      border-radius: 4px;
      font-family: var(--font-mono);
      font-size: 11px;
      outline: none;
    }
    select:focus, input:focus { border-color: var(--border-active); }

    /* Chat Layout & Conversation Sidebar */
    #viewChat.active {
      display: flex;
      flex-direction: row;
      gap: 14px;
      height: 100%;
      overflow: hidden;
    }
    .chat-sidebar {
      width: 240px;
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      border-radius: 8px;
      display: flex;
      flex-direction: column;
      flex-shrink: 0;
      overflow: hidden;
    }
    .conv-sidebar-header {
      padding: 10px 12px;
      border-bottom: 1px solid var(--border-muted);
      display: flex;
      align-items: center;
      justify-content: space-between;
      background: var(--bg-card);
    }
    .conv-header-title {
      font-weight: bold;
      font-size: 11px;
      color: var(--text-tertiary);
      letter-spacing: 0.5px;
    }
    .conv-list {
      flex: 1;
      overflow-y: auto;
      padding: 6px;
      display: flex;
      flex-direction: column;
      gap: 3px;
    }
    .conv-item {
      padding: 8px 10px;
      border-radius: 6px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      cursor: pointer;
      color: var(--text-secondary);
      border: 1px solid transparent;
      transition: all 0.15s ease;
      font-size: 12px;
    }
    .conv-item:hover {
      background: var(--bg-card);
      color: var(--text-primary);
    }
    .conv-item.active {
      background: var(--accent-subtle);
      color: var(--accent);
      border-color: rgba(56, 139, 253, 0.4);
      font-weight: 600;
    }
    .conv-info {
      flex: 1;
      min-width: 0;
      display: flex;
      flex-direction: column;
      gap: 2px;
    }
    .conv-title {
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      font-size: 12px;
    }
    .conv-sub {
      display: flex;
      align-items: center;
      gap: 6px;
      font-size: 10px;
      color: var(--text-tertiary);
    }
    .conv-badge {
      font-family: var(--font-mono);
      font-size: 9px;
      padding: 1px 4px;
      border-radius: 4px;
      background: rgba(255, 255, 255, 0.06);
    }
    .conv-del-btn {
      opacity: 0;
      background: transparent;
      border: none;
      color: var(--text-tertiary);
      cursor: pointer;
      padding: 2px 4px;
      border-radius: 4px;
      font-size: 12px;
      line-height: 1;
      transition: opacity 0.15s, color 0.15s;
    }
    .conv-item:hover .conv-del-btn {
      opacity: 1;
    }
    .conv-del-btn:hover {
      color: var(--danger);
      background: rgba(248, 81, 73, 0.1);
    }
    .chat-main {
      flex: 1;
      min-width: 0;
      display: flex;
      flex-direction: column;
      gap: 12px;
      height: 100%;
    }
    @media (max-width: 768px) {
      #viewChat.active {
        flex-direction: column;
      }
      .chat-sidebar {
        width: 100%;
        max-height: 140px;
      }
    }

    /* Chat Messages */
    .chat-list {
      flex: 1;
      overflow-y: auto;
      display: flex;
      flex-direction: column;
      gap: 12px;
      padding-right: 6px;
    }
    .msg-group { display: flex; flex-direction: column; gap: 6px; }
    .msg-header { display: flex; justify-content: space-between; align-items: center; font-size: 11px; }
    .role-user { color: var(--accent); font-weight: bold; }
    .role-asst { color: var(--success); font-weight: bold; }

    .btn-copy {
      background: var(--bg-card);
      border: 1px solid var(--border-muted);
      color: var(--text-secondary);
      font-size: 10px;
      padding: 2px 8px;
      border-radius: 4px;
      cursor: pointer;
      transition: all 0.15s;
    }
    .btn-copy:hover { color: var(--text-primary); border-color: var(--border-active); }
    .btn-copy.copied { color: var(--success); border-color: var(--success); }

    .reasoning-card {
      background: var(--bg-sidebar);
      border: 1px solid rgba(188, 140, 255, 0.35);
      border-radius: 6px;
      padding: 8px 12px;
      font-family: var(--font-mono);
      font-size: 11px;
      color: #c9d1d9; /* eher hellgrau im text */
      display: flex;
      flex-direction: column;
      gap: 6px;
      margin-bottom: 4px;
    }
    .reason-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      color: var(--reasoning);
      font-weight: bold;
      font-size: 11px;
      cursor: pointer;
      user-select: none;
    }
    .reason-head:hover {
      color: #d8b4fe;
    }
    .reason-badge {
      font-size: 10px;
      color: var(--text-tertiary);
      font-weight: normal;
      margin-left: 6px;
    }
    .reason-text {
      white-space: pre-wrap;
      line-height: 1.45;
      color: #c9d1d9; /* eher hellgrau im text */
      padding-top: 6px;
      border-top: 1px solid rgba(255, 255, 255, 0.08);
      max-height: 380px;
      overflow-y: auto;
    }

    .msg-bubble {
      padding: 12px 14px;
      border-radius: 8px;
      line-height: 1.5;
      font-size: 13px;
      white-space: pre-wrap;
      user-select: text;
    }
    .msg-bubble.user {
      background: var(--bg-sidebar);
      border: 1px solid var(--border-active);
    }
    .msg-bubble.asst {
      background: var(--bg-card);
      border: 1px solid var(--border-muted);
    }

    /* Composer */
    .composer {
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      border-radius: 8px;
      padding: 10px;
      display: flex;
      gap: 10px;
      align-items: flex-end;
    }
    .composer:focus-within { border-color: var(--border-active); }
    textarea {
      flex: 1;
      background: transparent;
      border: none;
      outline: none;
      color: var(--text-primary);
      font-family: var(--font-ui);
      font-size: 13px;
      resize: none;
      height: 48px;
      line-height: 1.4;
    }
    .btn {
      background: var(--bg-card);
      border: 1px solid var(--border-muted);
      color: var(--text-primary);
      padding: 8px 16px;
      border-radius: 6px;
      font-weight: bold;
      cursor: pointer;
      font-size: 12px;
      transition: all 0.15s;
    }
    .btn:hover { background: var(--bg-card-hover); }
    .btn-primary {
      background: var(--accent);
      color: var(--bg-app);
      border: none;
    }
    .btn-primary:hover { background: #79c0ff; }
    .btn-danger { background: #da3633; color: white; border: none; }

    /* Search Hits */
    .hit-card {
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      border-radius: 8px;
      padding: 12px;
      display: flex;
      flex-direction: column;
      gap: 8px;
    }
    .hit-header { display: flex; justify-content: space-between; align-items: center; }
    .hit-title { font-weight: bold; color: var(--text-primary); font-size: 13px; }
    .hit-badge { font-family: var(--font-mono); font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: bold; }
    .hit-snippet {
      background: var(--bg-app);
      padding: 8px;
      border-radius: 4px;
      font-size: 12px;
      color: var(--text-secondary);
      white-space: pre-wrap;
    }

    /* Relations */
    .rel-item {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 10px 12px;
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      border-radius: 6px;
      font-family: var(--font-mono);
      font-size: 12px;
    }
    .rel-arrow { color: var(--warning); font-weight: bold; }

    /* Neo4J Hexagon Knowledge Graph */
    .graph-stage {
      position: relative;
      flex: 1;
      min-height: 520px;
      background: radial-gradient(circle at center, #161b22 0%, #0d1117 100%);
      border-radius: 8px;
      border: 1px solid var(--border-muted);
      overflow: hidden;
      display: flex;
    }
    #graphCanvas {
      width: 100%;
      height: 100%;
      display: block;
      cursor: grab;
    }
    #graphCanvas:active {
      cursor: grabbing;
    }
    .graph-toolbar {
      position: absolute;
      top: 12px;
      left: 12px;
      display: flex;
      gap: 6px;
      z-index: 10;
      background: rgba(13, 17, 23, 0.85);
      padding: 6px;
      border-radius: 6px;
      border: 1px solid var(--border-muted);
      backdrop-filter: blur(8px);
    }
    .graph-preset-pill {
      font-size: 11px;
      font-family: var(--font-mono);
      padding: 3px 8px;
      border-radius: 4px;
      background: var(--bg-sidebar);
      border: 1px solid var(--border-muted);
      color: var(--accent);
      cursor: pointer;
      transition: all 0.15s ease;
      user-select: none;
    }
    .graph-preset-pill:hover {
      background: var(--accent-subtle);
      border-color: var(--accent);
      transform: translateY(-1px);
    }
    .graph-inspector {
      position: absolute;
      top: 12px;
      right: 12px;
      width: 290px;
      max-height: calc(100% - 24px);
      background: rgba(22, 27, 34, 0.95);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 14px;
      box-shadow: 0 12px 32px rgba(0,0,0,0.6);
      backdrop-filter: blur(12px);
      z-index: 10;
      overflow-y: auto;
      font-size: 12px;
      display: none;
    }
    .graph-legend {
      position: absolute;
      bottom: 12px;
      left: 12px;
      display: flex;
      gap: 12px;
      background: rgba(13, 17, 23, 0.85);
      padding: 6px 12px;
      border-radius: 6px;
      border: 1px solid var(--border-muted);
      font-size: 11px;
      z-index: 10;
      pointer-events: none;
    }
    .legend-item {
      display: flex;
      align-items: center;
      gap: 6px;
      color: var(--text-secondary);
    }
    .legend-hex {
      width: 10px;
      height: 10px;
      clip-path: polygon(50% 0%, 100% 25%, 100% 75%, 50% 100%, 0% 75%, 0% 25%);
    }
  </style>
</head>
<body>

  <!-- SIDEBAR -->
  <div class="sidebar">
    <div class="brand-header">
      <div class="brand-logo">V</div>
      <div class="brand-title">
        <h1>VINOX</h1>
        <p>OpenVINO GenAI Web Studio</p>
      </div>
    </div>

    <div class="nav-section-title">STUDIO</div>
    <div class="nav-item active" onclick="switchView('chat')">💬 Chat & Inferenz</div>
    <div class="nav-item" onclick="switchView('search')">🔍 Hybrid Retrieval</div>
    <div class="nav-item" onclick="switchView('relations')">🕸️ Knowledge Graph</div>

    <div class="nav-section-title">SYSTEM</div>
    <div class="nav-item" onclick="switchView('system')">⚙️ Telemetrie & API</div>

    <!-- Capabilities Matrix Footer -->
    <div class="capabilities-card">
      <div class="cap-header">
        <span>VINOX CAPABILITIES</span>
        <span style="color: var(--success)">● ONLINE</span>
      </div>
      <div class="cap-row">
        <span class="cap-label">NPU Accel</span>
        <span class="cap-badge badge-active" style="background: rgba(16, 185, 129, 0.2); color: #10b981; border: 1px solid #10b981;">NPU · PRIORITY 🚀</span>
      </div>
      <div class="cap-row">
        <span class="cap-label">Generation</span>
        <span class="cap-badge badge-active" id="capGenDevice">NPU · READY</span>
      </div>
      <div class="cap-row">
        <span class="cap-label">Embedding</span>
        <span class="cap-badge badge-active" id="capEmbDevice">CPU · ACTIVE</span>
      </div>
      <div class="cap-row">
        <span class="cap-label">Storage</span>
        <span class="cap-badge badge-cached">NVMe · CACHED</span>
      </div>
      <div class="cap-row">
        <span class="cap-label">Tools</span>
        <span class="cap-badge badge-trusted">4 TRUSTED</span>
      </div>
    </div>
  </div>

  <!-- MAIN AREA -->
  <div class="main-area">
    <!-- Header -->
    <div class="header-bar">
      <div class="header-title" id="viewTitle">💬 CHAT & GENERATION STUDIO</div>
      <div class="telemetry-bar">
        <div class="pill">
          <span class="pill-label">Modell:</span>
          <span class="pill-val" id="pillModel">Lade...</span>
        </div>
        <div class="pill" id="pillDevice">
          <span class="pill-val" style="color: var(--success)">⚡ NPU (Priorität 🚀)</span>
        </div>
        <div class="pill">
          <span class="pill-label">Tempo:</span>
          <span class="pill-val" id="pillSpeed">0.0 tok/s</span>
        </div>
        <div class="pill">
          <span class="pill-label">Tokens:</span>
          <span class="pill-val" id="pillTokens" style="color: var(--success)">0</span>
        </div>
        <div class="pill">
          <span class="pill-val" style="color: var(--retrieval)">⚡ NVMe Active</span>
        </div>
      </div>
    </div>

    <!-- Container for Views -->
    <div class="view-container">

      <!-- VIEW 1: CHAT -->
      <div class="view-content active" id="viewChat">
        <!-- CONVERSATION SIDEBAR -->
        <div class="chat-sidebar">
          <div class="conv-sidebar-header">
            <span class="conv-header-title">💬 CHATS</span>
            <button class="btn btn-primary btn-sm" onclick="startNewChat()" title="Neuen Chat starten" style="padding: 3px 8px; font-size: 11px;">+ Neu</button>
          </div>
          <div class="conv-list" id="convList">
            <div style="text-align: center; color: var(--text-tertiary); padding: 20px 8px; font-size: 11px;">Lade Konversationen...</div>
          </div>
        </div>

        <!-- CHAT MAIN (CONTROLS + MESSAGES + COMPOSER) -->
        <div class="chat-main">
          <!-- Sampling Controls -->
          <div class="panel">
            <div class="controls-row">
              <label>🤖 Modell:</label>
              <select id="modelSelect" style="min-width: 180px;"></select>

              <label>⚡ Gerät:</label>
              <select id="deviceSelect" style="min-width: 140px;" onchange="handleDeviceChange(this.value)">
                <option value="NPU" selected>🚀 NPU (Intel AI Boost)</option>
                <option value="GPU">🎮 GPU (Intel Arc)</option>
                <option value="CPU">💻 CPU (Core Ultra)</option>
              </select>

              <label>⚙ Temp:</label>
              <input type="number" id="paramTemp" value="0.7" step="0.05" min="0" max="2" style="width: 55px;">

              <label>Top-P:</label>
              <input type="number" id="paramTopP" value="0.9" step="0.05" min="0" max="1" style="width: 55px;">

              <label>Max Tokens:</label>
              <input type="number" id="paramMaxTokens" value="512" step="32" min="16" style="width: 65px;">

              <div style="flex: 1"></div>
              <button class="btn" onclick="startNewChat()">+ Neuer Chat</button>
            </div>
          </div>

          <!-- Messages List -->
          <div class="chat-list" id="chatList">
            <div class="msg-group">
              <div class="msg-header">
                <span class="role-asst">ASSISTENT (VINOX LLM)</span>
                <button class="btn-copy" onclick="copyText(this, 'Willkommen bei VINOX! Der Server läuft vollständig auf OpenVINO 2026.3 mit nativer Tool-Governance und entkoppelten Embeddings.')">📋 Kopieren</button>
              </div>
              <div class="msg-bubble asst">Willkommen bei VINOX! Der Server läuft vollständig auf OpenVINO 2026.3 mit nativer Tool-Governance und entkoppelten Embeddings. Wie kann ich heute helfen?</div>
            </div>
          </div>

          <!-- Composer -->
          <div class="composer">
            <textarea id="promptInput" placeholder="Nachricht oder Frage eingeben... (Strg+Enter zum Senden)"></textarea>
            <button class="btn btn-primary" id="btnSend" onclick="handleSend()">➤ Senden</button>
          </div>
        </div>
      </div>

      <!-- VIEW 2: HYBRID RETRIEVAL -->
      <div class="view-content" id="viewSearch">
        <div class="panel">
          <div class="controls-row" style="margin-bottom: 8px;">
            <label>🔍 Suchanfrage:</label>
            <input type="text" id="searchInput" placeholder="Suchbegriff eingeben..." style="flex: 1;">
            <button class="btn btn-primary" onclick="executeSearch()">⚡ Suchen</button>
          </div>
          <div class="controls-row">
            <label>Retrieval Policy:</label>
            <label><input type="radio" name="searchPolicy" value="rrf" checked> Hybrid RRF (k=60)</label>
            <label><input type="radio" name="searchPolicy" value="weighted"> Hybrid Weighted (α=0.5)</label>
            <label><input type="radio" name="searchPolicy" value="bm25"> Keyword (BM25)</label>
            <label><input type="radio" name="searchPolicy" value="vector"> Dense Vector</label>
          </div>
        </div>

        <div id="searchResults" style="display: flex; flex-direction: column; gap: 10px; overflow-y: auto;">
          <div style="color: var(--text-tertiary); text-align: center; margin-top: 40px;">
            Noch keine Suchergebnisse. Gib oben einen Begriff ein.
          </div>
        </div>
      </div>

      <!-- VIEW 3: KNOWLEDGE GRAPH RELATIONS -->
      <div class="view-content" id="viewRelations">
        <div class="panel" style="display: flex; flex-direction: column; gap: 10px;">
          <div class="controls-row">
            <label style="font-weight: bold; color: var(--accent);">🕸️ Root Entity ID:</label>
            <input type="text" id="entityInput" value="VINOX" style="flex: 1;" placeholder="Entity ID eingeben...">
            <button class="btn btn-primary" onclick="queryRelations()">⚡ Query CTE Graph</button>
            <div style="display: flex; gap: 4px; background: var(--bg-app); padding: 3px; border-radius: 6px; border: 1px solid var(--border-muted);">
              <button class="btn btn-sm active" id="btnModeGraph" onclick="switchGraphMode('graph')">🕸️ Neo4J Hex-Graph</button>
              <button class="btn btn-sm" id="btnModeList" onclick="switchGraphMode('list')">📋 CTE Liste</button>
            </div>
          </div>
          <div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
            <span style="font-size: 11px; color: var(--text-tertiary);">Quick Presets:</span>
            <span class="graph-preset-pill" onclick="setEntityAndQuery('VINOX')">⚡ VINOX Core</span>
            <span class="graph-preset-pill" onclick="setEntityAndQuery('Intel_AI_Boost_NPU')">🚀 Intel AI Boost NPU</span>
            <span class="graph-preset-pill" onclick="setEntityAndQuery('Hybrid_Retrieval_Engine')">🔍 Hybrid Retrieval</span>
            <span class="graph-preset-pill" onclick="setEntityAndQuery('Reasoning_Channel')">🧠 Reasoning Channel</span>
            <span class="graph-preset-pill" onclick="setEntityAndQuery('MCP_Protocol')">🛠️ MCP Protocol</span>
          </div>
        </div>

        <!-- STAGE: Neo4j Hexagon Canvas View -->
        <div class="graph-stage" id="graphStageContainer">
          <canvas id="graphCanvas"></canvas>
          
          <div class="graph-toolbar">
            <button class="btn btn-sm" onclick="zoomGraph(1.2)" title="Heranzoomen">🔍 +</button>
            <button class="btn btn-sm" onclick="zoomGraph(0.8)" title="Herauszoomen">🔍 −</button>
            <button class="btn btn-sm" onclick="resetGraphView()" title="Ansicht zentrieren">⟲ Zentrieren</button>
            <button class="btn btn-sm" id="btnTogglePhysics" onclick="toggleGraphPhysics()" title="Physik pausieren/starten">⏸ Pause</button>
            <button class="btn btn-sm active" id="btnToggleCollision" onclick="toggleAntiCollision()" title="Anti-Kollision umschalten">🛡️ Anti-Collision: An</button>
          </div>

          <div class="graph-legend">
            <div class="legend-item"><div class="legend-hex" style="background: #f59e0b;"></div><span>Root (Tiefe 0)</span></div>
            <div class="legend-item"><div class="legend-hex" style="background: #0ea5e9;"></div><span>Tiefe 1</span></div>
            <div class="legend-item"><div class="legend-hex" style="background: #10b981;"></div><span>Tiefe 2</span></div>
            <div class="legend-item"><div class="legend-hex" style="background: #8b5cf6;"></div><span>Tiefe 3+</span></div>
          </div>

          <div class="graph-inspector" id="graphInspector">
            <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border-muted); padding-bottom: 8px;">
              <span style="font-weight: bold; color: var(--accent); font-size: 13px;">⬡ Entity Details</span>
              <button class="btn-copy" style="padding: 2px 6px;" onclick="closeInspector()">✕</button>
            </div>
            <div id="inspectorContent" style="margin-top: 8px; display: flex; flex-direction: column; gap: 8px;"></div>
            <button class="btn btn-primary btn-sm" id="btnInspectorRoot" style="margin-top: 10px; width: 100%;" onclick="querySelectedNodeAsRoot()">🔍 Als Root abfragen</button>
          </div>
        </div>

        <!-- Alternative: CTE List View -->
        <div id="relationsList" style="display: none; flex-direction: column; gap: 8px; overflow-y: auto;">
          <div style="color: var(--text-secondary); text-align: center; margin-top: 40px;">Geben Sie eine Entity ID ein, um den rekursiven CTE-Graphen abzufragen.</div>
        </div>
      </div>

      <!-- VIEW 4: SYSTEM & METRICS -->
      <div class="view-content" id="viewSystem">
        <div class="panel" style="display: flex; flex-direction: column; gap: 10px;">
          <h3 style="font-size: 14px; color: var(--text-primary);">System Status & Endpunkte</h3>
          <div class="controls-row">
            <button class="btn" onclick="checkHealth()">Gesundheitsprüfung (/health/live)</button>
            <button class="btn" onclick="openSpec()">OpenAPI Dokumentation (/openapi.yaml)</button>
          </div>
          <pre id="systemOutput" style="background: var(--bg-app); padding: 12px; border-radius: 6px; font-family: var(--font-mono); color: var(--accent); overflow: auto; max-height: 250px;">Klicke oben auf eine Aktion zur Abfrage...</pre>
        </div>
      </div>

    </div>
  </div>

  <script>
    let abortCtrl = null;
    let totalTokensCount = 0;

    // Switch active view
    function switchView(viewName) {
      document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.view-content').forEach(el => el.classList.remove('active'));

      const titles = {
        'chat': '💬 CHAT & GENERATION STUDIO',
        'search': '🔍 HYBRID RETRIEVAL STUDIO',
        'relations': '🕸️ KNOWLEDGE GRAPH & CTE RELATIONS',
        'system': '⚙️ TELEMETRIE & API SPEZIFIKATION'
      };
      document.getElementById('viewTitle').innerText = titles[viewName] || 'STUDIO';

      const map = { 'chat': 'viewChat', 'search': 'viewSearch', 'relations': 'viewRelations', 'system': 'viewSystem' };
      document.getElementById(map[viewName]).classList.add('active');

      const items = document.querySelectorAll('.nav-item');
      if (viewName === 'chat') items[0].classList.add('active');
      else if (viewName === 'search') items[1].classList.add('active');
      else if (viewName === 'relations') items[2].classList.add('active');
      else if (viewName === 'system') items[3].classList.add('active');
    }

    // Copy to clipboard helper
    function copyText(btn, text) {
      navigator.clipboard.writeText(text).then(() => {
        const old = btn.innerText;
        btn.innerText = '✓ Kopiert!';
        btn.classList.add('copied');
        setTimeout(() => {
          btn.innerText = old;
          btn.classList.remove('copied');
        }, 1500);
      });
    }

    // Fetch devices with strict NPU priority
    async function loadDevices() {
      try {
        const res = await fetch('/v1/devices');
        const data = await res.json();
        const sel = document.getElementById('deviceSelect');
        if (sel && data.devices && data.devices.length > 0) {
          sel.innerHTML = '';
          data.devices.forEach((d) => {
            const opt = document.createElement('option');
            opt.value = d.id;
            const isPrio = (d.priority === 1 || d.id === 'NPU');
            opt.innerText = (isPrio ? '🚀 ' : (d.id === 'GPU' ? '🎮 ' : '💻 ')) + d.id + ' (' + d.full_name + ')' + (isPrio ? ' [PRIORITÄT]' : '');
            if (d.is_active || d.id === data.active_device || (isPrio && !data.active_device)) {
              opt.selected = true;
            }
            sel.appendChild(opt);
          });
        }
        if (data.active_device) {
          updateDeviceBadge(data.active_device);
        } else if (data.prioritized_device) {
          updateDeviceBadge(data.prioritized_device);
        }
      } catch (e) {
        console.warn('Devices fetch error:', e);
      }
    }

    function updateDeviceBadge(dev) {
      const pillDev = document.getElementById('pillDevice');
      if (pillDev) {
        const isNpu = (dev === 'NPU');
        pillDev.innerHTML = `<span class="pill-val" style="color: ${isNpu ? 'var(--success)' : 'var(--accent)'}">⚡ ${dev} ${isNpu ? '(Priorität 🚀)' : 'Aktiv'}</span>`;
      }
      const capGen = document.getElementById('capGenDevice');
      if (capGen) {
        capGen.innerText = dev + ' · ACTIVE';
        capGen.className = 'cap-badge ' + (dev === 'NPU' ? 'badge-active' : 'badge-ready');
      }
    }

    async function handleDeviceChange(newDevice) {
      updateDeviceBadge(newDevice);
      const sel = document.getElementById('modelSelect');
      if (sel && sel.value) {
        await switchModel(sel.value, newDevice);
      }
    }

    // Fetch models on load
    async function loadModels() {
      try {
        const res = await fetch('/v1/models');
        const data = await res.json();
        const sel = document.getElementById('modelSelect');
        sel.innerHTML = '';
        if (data.data && data.data.length > 0) {
          let selectedModel = data.data[0].id;
          data.data.forEach((m) => {
            const opt = document.createElement('option');
            opt.value = m.id;
            opt.innerText = m.id + (m.is_loaded ? ' (Aktiv)' : '');
            if (m.is_loaded) {
              opt.selected = true;
              selectedModel = m.id;
            }
            sel.appendChild(opt);
          });
          document.getElementById('pillModel').innerText = selectedModel;
          sel.onchange = (e) => switchModel(e.target.value);
        } else {
          sel.innerHTML = '<option value="default">OpenVINO Default</option>';
          document.getElementById('pillModel').innerText = 'OpenVINO Default';
        }
      } catch (e) {
        document.getElementById('pillModel').innerText = 'Offline';
      }
    }

    async function switchModel(modelId, targetDevice) {
      const dev = targetDevice || (document.getElementById('deviceSelect')?.value || 'NPU');
      document.getElementById('pillModel').innerText = 'Lade ' + modelId + ' auf ' + dev + '...';
      try {
        const res = await fetch('/api/models/' + encodeURIComponent(modelId) + '/load', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ device: dev })
        });
        if (res.ok) {
          const loadResult = await res.json();
          document.getElementById('pillModel').innerText = modelId;
          updateDeviceBadge(loadResult.device || dev);
          await loadModels();
        } else {
          const err = await res.json().catch(() => ({}));
          alert('Fehler beim Laden des Modells: ' + (err.error?.message || 'Unbekannter Fehler'));
        }
      } catch (e) {
        alert('Netzwerkfehler: ' + e.message);
      }
    }

    // Chat SSE Streaming
    async function handleSend() {
      const btn = document.getElementById('btnSend');
      const input = document.getElementById('promptInput');
      const text = input.value.trim();

      if (abortCtrl) {
        // Abort currently running generation
        abortCtrl.abort();
        abortCtrl = null;
        btn.innerText = '➤ Senden';
        btn.classList.remove('btn-danger');
        return;
      }

      if (!text) return;
      input.value = '';

      // Auto-create conversation if none is active
      if (!activeConversationId) {
        try {
          const title = text.length > 36 ? (text.slice(0, 33) + '...') : text;
          const newConvRes = await fetch('/v1/conversations', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ title: title })
          });
          if (newConvRes.ok) {
            const newConvData = await newConvRes.json();
            activeConversationId = newConvData.id;
          }
        } catch (e) {
          console.warn('Auto-create conversation failed:', e);
        }
      }

      // Append user bubble
      appendMessage('user', text);

      // Prepare assistant bubble with reasoning container
      const asstHolder = appendMessage('asst', '');
      const reasonBox = asstHolder.reasonBox;
      const textBubble = asstHolder.textBubble;
      const copyBtn = asstHolder.copyBtn;

      btn.innerText = '⏹ Stopp';
      btn.classList.add('btn-danger');

      abortCtrl = new AbortController();
      const startTime = performance.now();
      let genTokens = 0;
      let fullText = '';
      let fullReasoning = '';

      try {
        const model = document.getElementById('modelSelect').value || 'default';
        const temp = parseFloat(document.getElementById('paramTemp').value) || 0.7;
        const top_p = parseFloat(document.getElementById('paramTopP').value) || 0.9;
        const max_tokens = parseInt(document.getElementById('paramMaxTokens').value) || 512;

        const res = await fetch('/v1/chat/completions', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          signal: abortCtrl.signal,
          body: JSON.stringify({
            model: model,
            messages: [{ role: 'user', content: text }],
            temperature: temp,
            top_p: top_p,
            max_tokens: max_tokens,
            stream: true,
            conversation_id: activeConversationId || undefined
          })
        });

        const reader = res.body.getReader();
        const decoder = new TextDecoder('utf-8');
        let buffer = '';

        while (true) {
          const { done, value } = await reader.read();
          if (done) break;

          buffer += decoder.decode(value, { stream: true });
          const lines = buffer.split('\n');
          buffer = lines.pop(); // keep remainder

          for (const line of lines) {
            const trimmed = line.trim();
            if (!trimmed || !trimmed.startsWith('data:')) continue;
            if (trimmed === 'data: [DONE]') break;

            try {
              const json = JSON.parse(trimmed.slice(5).trim());
              const delta = json.choices[0]?.delta || {};

              // Handle reasoning channel
              if (delta.reasoning_content) {
                fullReasoning += delta.reasoning_content;
              }

              // Handle regular content
              if (delta.content) {
                fullText += delta.content;
              }

              updateAssistantDisplay(asstHolder, fullText, fullReasoning);

              genTokens++;
              totalTokensCount++;
              document.getElementById('pillTokens').innerText = totalTokensCount;

              const elapsed = (performance.now() - startTime) / 1000;
              if (elapsed > 0.1) {
                document.getElementById('pillSpeed').innerText = (genTokens / elapsed).toFixed(1) + ' tok/s';
              }
            } catch (err) {}
          }
        }
      } catch (err) {
        if (err.name !== 'AbortError') {
          textBubble.innerText += '\n[Fehler: ' + err.message + ']';
        }
      } finally {
        btn.innerText = '➤ Senden';
        btn.classList.remove('btn-danger');
        abortCtrl = null;
        updateAssistantDisplay(asstHolder, fullText, fullReasoning);
        loadConversations();
      }
    }

    function updateAssistantDisplay(asstHolder, rawText, rawReasoning) {
      let combinedReasoning = rawReasoning || '';
      let mainText = rawText || '';

      // Parse inline <think> tags if present
      if (mainText.includes('<think>')) {
        const parts = mainText.split('<think>');
        const before = parts[0];
        const after = parts.slice(1).join('<think>');
        if (after.includes('</think>')) {
          const endParts = after.split('</think>');
          const thinkContent = endParts[0];
          const rest = endParts.slice(1).join('</think>');
          combinedReasoning = (combinedReasoning ? (combinedReasoning + '\n\n') : '') + thinkContent.trim();
          mainText = (before + rest).trim();
        } else {
          combinedReasoning = (combinedReasoning ? (combinedReasoning + '\n\n') : '') + after;
          mainText = before.trim();
        }
      }

      if (combinedReasoning.trim().length > 0) {
        asstHolder.reasonBox.style.display = 'flex';
        asstHolder.reasonBox.querySelector('.reason-text').innerText = combinedReasoning.trim();
        const tokenEstimate = Math.max(1, Math.round(combinedReasoning.length / 4));
        const headTitle = asstHolder.reasonBox.querySelector('.reason-title');
        if (headTitle) {
          headTitle.innerHTML = `🧠 Denken (<think>) <span class="reason-badge">· ${tokenEstimate} Tokens</span>`;
        }
      }

      asstHolder.textBubble.innerText = mainText || (combinedReasoning ? '...' : '');
      asstHolder.copyBtn.onclick = () => copyText(asstHolder.copyBtn, mainText || combinedReasoning);
    }

    function appendMessage(role, text) {
      const list = document.getElementById('chatList');
      const group = document.createElement('div');
      group.className = 'msg-group';

      const header = document.createElement('div');
      header.className = 'msg-header';

      const roleSpan = document.createElement('span');
      roleSpan.className = role === 'user' ? 'role-user' : 'role-asst';
      roleSpan.innerText = role === 'user' ? 'BENUTZER' : 'ASSISTENT (VINOX LLM)';

      const copyBtn = document.createElement('button');
      copyBtn.className = 'btn-copy';
      copyBtn.innerText = '📋 Kopieren';
      copyBtn.onclick = () => copyText(copyBtn, text);

      header.appendChild(roleSpan);
      header.appendChild(copyBtn);
      group.appendChild(header);

      let reasonBox = null;
      if (role === 'asst') {
        reasonBox = document.createElement('div');
        reasonBox.className = 'reasoning-card';
        reasonBox.style.display = 'none';
        reasonBox.innerHTML = `
          <div class="reason-head" onclick="toggleReason(this)">
            <span class="reason-title">🧠 Denken (<think>)</span>
            <span class="reason-chevron">▼</span>
          </div>
          <div class="reason-text" style="display: none;"></div>
        `;
        group.appendChild(reasonBox);
      }

      const bubble = document.createElement('div');
      bubble.className = 'msg-bubble ' + (role === 'user' ? 'user' : 'asst');
      bubble.innerText = text;
      group.appendChild(bubble);

      list.appendChild(group);
      list.scrollTop = list.scrollHeight;

      return { textBubble: bubble, reasonBox: reasonBox, copyBtn: copyBtn };
    }

    function toggleReason(header) {
      const card = header.closest('.reasoning-card');
      const text = card.querySelector('.reason-text');
      const chevron = header.querySelector('.reason-chevron');
      if (text.style.display === 'none') {
        text.style.display = 'block';
        if (chevron) chevron.innerText = '▲';
      } else {
        text.style.display = 'none';
        if (chevron) chevron.innerText = '▼';
      }
    }

    // Conversation Management
    let activeConversationId = null;

    function escapeHtml(str) {
      if (!str) return '';
      return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    function formatTime(ms) {
      if (!ms) return '';
      try {
        const d = new Date(ms);
        const now = new Date();
        if (d.toDateString() === now.toDateString()) {
          return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        }
        return d.toLocaleDateString([], { month: 'short', day: 'numeric' });
      } catch (e) {
        return '';
      }
    }

    async function loadConversations(autoSelectId = null) {
      const listEl = document.getElementById('convList');
      if (!listEl) return;
      try {
        const res = await fetch('/v1/conversations');
        if (!res.ok) throw new Error('Status ' + res.status);
        const data = await res.json();
        const convs = data.data || data.conversations || [];

        if (convs.length === 0) {
          listEl.innerHTML = `
            <div style="text-align: center; color: var(--text-tertiary); padding: 24px 8px; font-size: 11px; line-height: 1.5;">
              Keine Chats vorhanden.<br>
              <span style="color: var(--accent); cursor: pointer;" onclick="startNewChat()">+ Neuer Chat</span>
            </div>
          `;
          return;
        }

        listEl.innerHTML = '';
        convs.forEach((c) => {
          const item = document.createElement('div');
          const isActive = (c.id === activeConversationId);
          item.className = 'conv-item' + (isActive ? ' active' : '');
          item.dataset.id = c.id;

          const title = c.title || 'Neuer Chat';
          const msgCount = c.message_count || 0;
          const timeStr = formatTime(c.updated_at_ms || c.created_at_ms);

          item.innerHTML = `
            <div class="conv-info">
              <span class="conv-title" title="${escapeHtml(title)}">${escapeHtml(title)}</span>
              <div class="conv-sub">
                <span class="conv-badge">${msgCount} Msg</span>
                ${timeStr ? `<span>${timeStr}</span>` : ''}
              </div>
            </div>
            <button class="conv-del-btn" title="Chat löschen" onclick="deleteConversation(event, '${escapeHtml(c.id)}')">🗑️</button>
          `;

          item.onclick = (e) => {
            if (e.target.closest('.conv-del-btn')) return;
            selectConversation(c.id);
          };

          listEl.appendChild(item);
        });

        if (autoSelectId) {
          selectConversation(autoSelectId);
        } else if (!activeConversationId && convs.length > 0) {
          selectConversation(convs[0].id);
        }
      } catch (e) {
        listEl.innerHTML = `<div style="color: var(--danger); padding: 12px; font-size: 11px;">Fehler beim Laden: ${escapeHtml(e.message)}</div>`;
      }
    }

    async function selectConversation(convId) {
      if (abortCtrl) {
        abortCtrl.abort();
        abortCtrl = null;
      }
      activeConversationId = convId;

      document.querySelectorAll('.conv-item').forEach(el => {
        el.classList.toggle('active', el.dataset.id === convId);
      });

      const chatList = document.getElementById('chatList');
      chatList.innerHTML = '<div style="color: var(--text-tertiary); text-align: center; margin-top: 20px;">Lade Chatverlauf...</div>';

      try {
        const res = await fetch('/v1/conversations/' + encodeURIComponent(convId) + '/messages');
        if (!res.ok) throw new Error('Status ' + res.status);
        const data = await res.json();
        const msgs = data.data || data.messages || [];

        chatList.innerHTML = '';
        if (msgs.length === 0) {
          appendMessage('asst', 'Dieser Chat ist noch leer. Stelle eine Frage!');
          return;
        }

        msgs.forEach(m => {
          const role = (m.role === 'assistant' || m.role === 'asst') ? 'asst' : 'user';
          const asstHolder = appendMessage(role, m.content);
          if (role === 'asst') {
            updateAssistantDisplay(asstHolder, m.content, m.reasoning_content || '');
          }
        });
      } catch (e) {
        chatList.innerHTML = `<div style="color: var(--danger); text-align: center; margin-top: 20px;">Fehler: ${escapeHtml(e.message)}</div>`;
      }
    }

    function startNewChat() {
      if (abortCtrl) {
        abortCtrl.abort();
        abortCtrl = null;
      }
      activeConversationId = null;
      document.querySelectorAll('.conv-item').forEach(el => el.classList.remove('active'));

      const chatList = document.getElementById('chatList');
      chatList.innerHTML = '';
      appendMessage('asst', 'Willkommen bei VINOX! Der Server läuft vollständig auf OpenVINO 2026.3 mit nativer Tool-Governance und entkoppelten Embeddings. Wie kann ich heute helfen?');
      document.getElementById('pillTokens').innerText = '0';
      document.getElementById('pillSpeed').innerText = '0.0 tok/s';
      document.getElementById('promptInput').focus();
    }

    async function deleteConversation(event, convId) {
      if (event) event.stopPropagation();
      if (!confirm('Möchtest du diesen Chat wirklich löschen?')) return;

      try {
        const res = await fetch('/v1/conversations/' + encodeURIComponent(convId), {
          method: 'DELETE'
        });
        if (!res.ok) throw new Error('Löschen fehlgeschlagen');

        if (activeConversationId === convId) {
          startNewChat();
        }
        await loadConversations();
      } catch (e) {
        alert('Fehler beim Löschen: ' + e.message);
      }
    }

    function clearChat() {
      startNewChat();
    }

    // Search
    async function executeSearch() {
      const q = document.getElementById('searchInput').value.trim();
      const container = document.getElementById('searchResults');
      if (!q) return;

      container.innerHTML = '<div style="color: var(--text-secondary)">Suche läuft...</div>';
      try {
        const policy = document.querySelector('input[name="searchPolicy"]:checked').value;
        const alpha = policy === 'bm25' ? 0.0 : (policy === 'vector' ? 1.0 : 0.5);

        const res = await fetch('/v1/search', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ query: q, alpha: alpha, limit: 5 })
        });
        const data = await res.json();
        container.innerHTML = '';

        const hits = data.results || data.matches || [];
        if (hits.length > 0) {
          hits.forEach((hit, idx) => {
            const card = document.createElement('div');
            card.className = 'hit-card';
            const scoreVal = hit.score !== undefined ? hit.score : (hit.hybrid_score || 0);
            const titleVal = hit.title || hit.message_id || 'Treffer';
            const snippetVal = hit.snippet || hit.content || ('ID: ' + (hit.message_id || ''));
            const safeSnippet = snippetVal.replace(/'/g, "\\'").replace(/\n/g, ' ');
            card.innerHTML = `
              <div class="hit-header">
                <span class="hit-title">#${idx + 1} ${titleVal}</span>
                <span class="hit-badge" style="background: var(--accent-subtle); color: var(--accent);">Score: ${Number(scoreVal).toFixed(4)}</span>
                <button class="btn-copy" onclick="copyText(this, '${safeSnippet}')">📋 Kopieren</button>
              </div>
              <div class="hit-snippet">${snippetVal}</div>
            `;
            container.appendChild(card);
          });
        } else {
          container.innerHTML = '<div style="color: var(--text-secondary)">Keine Treffer gefunden.</div>';
        }
      } catch (err) {
        container.innerHTML = '<div style="color: var(--danger)">Fehler: ' + err.message + '</div>';
      }
    }

    // =========================================================================
    // Neo4J-Style Hexagon Knowledge Graph Engine
    // =========================================================================
    let graphNodes = new Map();
    let graphEdges = [];
    let graphAnimId = null;
    let graphPhysicsRunning = true;
    let graphPhysicsTicks = 0;
    let graphZoom = 1.0;
    let graphPan = { x: 0, y: 0 };
    let isPanning = false;
    let startPan = { x: 0, y: 0 };
    let draggedNode = null;
    let hoveredNode = null;
    let selectedNode = null;
    let activeGraphDisplayMode = 'graph';

    function switchGraphMode(mode) {
      activeGraphDisplayMode = mode;
      document.getElementById('btnModeGraph').classList.toggle('active', mode === 'graph');
      document.getElementById('btnModeList').classList.toggle('active', mode === 'list');
      document.getElementById('graphStageContainer').style.display = mode === 'graph' ? 'flex' : 'none';
      document.getElementById('relationsList').style.display = mode === 'list' ? 'flex' : 'none';
      if (mode === 'graph') {
        resizeGraphCanvas();
        wakeGraphPhysics();
      }
    }

    function setEntityAndQuery(entityId) {
      document.getElementById('entityInput').value = entityId;
      queryRelations();
    }

    function zoomGraph(factor) {
      const canvas = document.getElementById('graphCanvas');
      if (!canvas) return;
      const cx = canvas.clientWidth / 2;
      const cy = canvas.clientHeight / 2;
      const newZoom = Math.max(0.25, Math.min(4.0, graphZoom * factor));
      graphPan.x = cx - (cx - graphPan.x) * (newZoom / graphZoom);
      graphPan.y = cy - (cy - graphPan.y) * (newZoom / graphZoom);
      graphZoom = newZoom;
      drawGraph();
    }

    function resetGraphView() {
      graphZoom = 1.0;
      const canvas = document.getElementById('graphCanvas');
      if (canvas) {
        graphPan = { x: canvas.clientWidth / 2, y: canvas.clientHeight / 2 };
      } else {
        graphPan = { x: 0, y: 0 };
      }
      wakeGraphPhysics();
    }

    let graphAntiCollision = true;

    function toggleGraphPhysics() {
      graphPhysicsRunning = !graphPhysicsRunning;
      const btn = document.getElementById('btnTogglePhysics');
      if (btn) btn.innerText = graphPhysicsRunning ? '⏸ Pause' : '▶ Start';
      if (graphPhysicsRunning) wakeGraphPhysics();
    }

    function toggleAntiCollision() {
      graphAntiCollision = !graphAntiCollision;
      const btn = document.getElementById('btnToggleCollision');
      if (btn) {
        btn.innerText = graphAntiCollision ? '🛡️ Anti-Collision: An' : '🛡️ Anti-Collision: Aus';
        btn.classList.toggle('active', graphAntiCollision);
      }
      wakeGraphPhysics();
    }

    function closeInspector() {
      document.getElementById('graphInspector').style.display = 'none';
      selectedNode = null;
      drawGraph();
    }

    function querySelectedNodeAsRoot() {
      if (selectedNode) {
        setEntityAndQuery(selectedNode.id);
        closeInspector();
      }
    }

    function drawRoundedHexagon(ctx, cx, cy, r, cr) {
      const angle = Math.PI / 3;
      const offset = Math.PI / 6; // 30 deg offset for flat-top
      const pts = [];
      for (let i = 0; i < 6; i++) {
        const a = offset + i * angle;
        pts.push({ x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) });
      }
      ctx.beginPath();
      ctx.moveTo((pts[0].x + pts[5].x) / 2, (pts[0].y + pts[5].y) / 2);
      for (let i = 0; i < 6; i++) {
        const next = pts[(i + 1) % 6];
        ctx.arcTo(pts[i].x, pts[i].y, next.x, next.y, cr);
      }
      ctx.closePath();
    }

    function formatNodeLabel(id) {
      if (!id) return [''];
      if (id.length <= 11) return [id];
      const parts = id.split(/[_]/);
      if (parts.length >= 2) {
        if (parts.length === 2) {
          return [truncateLabel(parts[0], 12), truncateLabel(parts[1], 12)];
        }
        const mid = Math.ceil(parts.length / 2);
        const l1 = parts.slice(0, mid).join('_');
        const l2 = parts.slice(mid).join('_');
        return [truncateLabel(l1, 12), truncateLabel(l2, 12)];
      }
      return [id.substring(0, 10), truncateLabel(id.substring(10), 10)];
    }

    function truncateLabel(s, max) {
      return s.length > max ? s.substring(0, max - 1) + '…' : s;
    }

    function getNodePalette(depth) {
      if (depth === 0) {
        return {
          fillStart: '#f59e0b', fillEnd: '#b45309', border: '#fef08a', glow: 'rgba(245, 158, 11, 0.65)', text: '#ffffff'
        };
      } else if (depth === 1) {
        return {
          fillStart: '#0ea5e9', fillEnd: '#0369a1', border: '#bae6fd', glow: 'rgba(14, 165, 233, 0.65)', text: '#ffffff'
        };
      } else if (depth === 2) {
        return {
          fillStart: '#10b981', fillEnd: '#047857', border: '#a7f3d0', glow: 'rgba(16, 185, 129, 0.65)', text: '#ffffff'
        };
      } else {
        return {
          fillStart: '#8b5cf6', fillEnd: '#6d28d9', border: '#ddd6fe', glow: 'rgba(139, 92, 246, 0.65)', text: '#ffffff'
        };
      }
    }

    function getNodeIcon(id) {
      const s = (id || '').toLowerCase();
      if (s.includes('vinox')) return '⚡';
      if (s.includes('npu') || s.includes('boost')) return '🚀';
      if (s.includes('gpu') || s.includes('arc')) return '🎮';
      if (s.includes('cpu') || s.includes('ultra')) return '🖥️';
      if (s.includes('retrieval') || s.includes('search') || s.includes('bm25') || s.includes('vec')) return '🔍';
      if (s.includes('reason') || s.includes('think')) return '🧠';
      if (s.includes('mcp') || s.includes('tool')) return '🛠️';
      if (s.includes('quant') || s.includes('int4') || s.includes('int8') || s.includes('fp16')) return '📐';
      if (s.includes('model') || s.includes('qwen') || s.includes('deepseek')) return '🤖';
      return '⬡';
    }

    function resizeGraphCanvas() {
      const canvas = document.getElementById('graphCanvas');
      const container = document.getElementById('graphStageContainer');
      if (!canvas || !container) return;
      const dpr = window.devicePixelRatio || 1;
      const w = container.clientWidth;
      const h = container.clientHeight;
      if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
        canvas.width = w * dpr;
        canvas.height = h * dpr;
        canvas.style.width = w + 'px';
        canvas.style.height = h + 'px';
        if (graphPan.x === 0 && graphPan.y === 0) {
          graphPan = { x: w / 2, y: h / 2 };
        }
      }
      drawGraph();
    }

    function wakeGraphPhysics() {
      graphPhysicsTicks = 0;
      if (!graphAnimId) {
        graphAnimId = requestAnimationFrame(graphPhysicsLoop);
      }
    }

    function graphPhysicsLoop() {
      if (graphPhysicsRunning && graphNodes.size > 0 && graphPhysicsTicks < 300) {
        updateGraphPhysics();
        graphPhysicsTicks++;
      }
      drawGraph();
      if (graphPhysicsRunning && graphPhysicsTicks < 300) {
        graphAnimId = requestAnimationFrame(graphPhysicsLoop);
      } else {
        graphAnimId = null;
      }
    }

    function updateGraphPhysics() {
      const nodes = Array.from(graphNodes.values());
      const kRep = 4500;
      const kSpring = 0.05;
      const targetDist = 135;

      // 1. Repulsion between all nodes
      for (let i = 0; i < nodes.length; i++) {
        for (let j = i + 1; j < nodes.length; j++) {
          const u = nodes[i];
          const v = nodes[j];
          let dx = v.x - u.x;
          let dy = v.y - u.y;
          let d = Math.sqrt(dx * dx + dy * dy);
          if (d < 1) { dx = (Math.random() - 0.5); dy = (Math.random() - 0.5); d = 1; }
          if (d < 450) {
            const f = kRep / (d * d);
            const fx = (dx / d) * f;
            const fy = (dy / d) * f;
            if (u !== draggedNode) { u.vx -= fx; u.vy -= fy; }
            if (v !== draggedNode) { v.vx += fx; v.vy += fy; }
          }
        }
      }

      // 2. Spring attraction along edges
      for (const e of graphEdges) {
        const u = graphNodes.get(e.source);
        const v = graphNodes.get(e.target);
        if (u && v) {
          let dx = v.x - u.x;
          let dy = v.y - u.y;
          let d = Math.sqrt(dx * dx + dy * dy);
          if (d < 1) d = 1;
          const force = (d - targetDist) * kSpring;
          const fx = (dx / d) * force;
          const fy = (dy / d) * force;
          if (u !== draggedNode) { u.vx += fx; u.vy += fy; }
          if (v !== draggedNode) { v.vx -= fx; v.vy -= fy; }
        }
      }

      // 3. Central gravity & velocity damping
      const gravity = 0.02;
      for (const node of nodes) {
        if (node === draggedNode) continue;
        node.vx -= node.x * gravity;
        node.vy -= node.y * gravity;
        node.vx *= 0.85;
        node.vy *= 0.85;
        node.x += node.vx;
        node.y += node.vy;
      }

      // 4. Elastic Anti-Collision Solver (Guarantees zero hexagon overlapping)
      if (graphAntiCollision) {
        const collisionPadding = 22; // clearance buffer between hexagon outer boundaries
        const collisionPasses = 3;   // multi-pass iterative relaxation
        for (let pass = 0; pass < collisionPasses; pass++) {
          for (let i = 0; i < nodes.length; i++) {
            for (let j = i + 1; j < nodes.length; j++) {
              const u = nodes[i];
              const v = nodes[j];
              const minDist = (u.radius || 38) + (v.radius || 38) + collisionPadding;
              let dx = v.x - u.x;
              let dy = v.y - u.y;
              let dist = Math.sqrt(dx * dx + dy * dy);
              if (dist < minDist) {
                if (dist < 0.01) {
                  dx = (Math.random() - 0.5) * 2;
                  dy = (Math.random() - 0.5) * 2;
                  dist = Math.sqrt(dx * dx + dy * dy) || 1;
                }
                const overlap = (minDist - dist);
                const nx = dx / dist;
                const ny = dy / dist;

                if (u === draggedNode) {
                  v.x += nx * overlap;
                  v.y += ny * overlap;
                  v.vx += nx * overlap * 0.25;
                  v.vy += ny * overlap * 0.25;
                } else if (v === draggedNode) {
                  u.x -= nx * overlap;
                  u.y -= ny * overlap;
                  u.vx -= nx * overlap * 0.25;
                  u.vy -= ny * overlap * 0.25;
                } else {
                  const half = overlap * 0.5;
                  u.x -= nx * half;
                  u.y -= ny * half;
                  v.x += nx * half;
                  v.y += ny * half;

                  // Elastic impulse along collision normal
                  const dvx = v.vx - u.vx;
                  const dvy = v.vy - u.vy;
                  const vn = dvx * nx + dvy * ny;
                  if (vn < 0) {
                    const restitution = 0.35;
                    const impulse = -(1 + restitution) * vn * 0.5;
                    u.vx -= nx * impulse;
                    u.vy -= ny * impulse;
                    v.vx += nx * impulse;
                    v.vy += ny * impulse;
                  }
                }
              }
            }
          }
        }
      }
    }

    function drawGraph() {
      const canvas = document.getElementById('graphCanvas');
      if (!canvas) return;
      const ctx = canvas.getContext('2d');
      const dpr = window.devicePixelRatio || 1;
      ctx.save();
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.scale(dpr, dpr);

      // Grid Pattern
      ctx.save();
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.03)';
      ctx.lineWidth = 1;
      const gridSize = 40 * graphZoom;
      const startX = (graphPan.x % gridSize);
      const startY = (graphPan.y % gridSize);
      for (let x = startX; x < canvas.clientWidth; x += gridSize) {
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.clientHeight); ctx.stroke();
      }
      for (let y = startY; y < canvas.clientHeight; y += gridSize) {
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.clientWidth, y); ctx.stroke();
      }
      ctx.restore();

      // Camera Transform
      ctx.save();
      ctx.translate(graphPan.x, graphPan.y);
      ctx.scale(graphZoom, graphZoom);

      // 1. Draw Edges
      for (const e of graphEdges) {
        const u = graphNodes.get(e.source);
        const v = graphNodes.get(e.target);
        if (!u || !v) continue;

        const isHighlighted = (hoveredNode && (hoveredNode.id === u.id || hoveredNode.id === v.id)) ||
                              (selectedNode && (selectedNode.id === u.id || selectedNode.id === v.id));

        const dx = v.x - u.x;
        const dy = v.y - u.y;
        const dist = Math.sqrt(dx * dx + dy * dy) || 1;
        const ux = dx / dist;
        const uy = dy / dist;

        // Cut off at node radius
        const startX = u.x + ux * u.radius;
        const startY = u.y + uy * u.radius;
        const endX = v.x - ux * (v.radius + 6);
        const endY = v.y - uy * (v.radius + 6);

        // Curved line via slight normal displacement
        const midX = (startX + endX) / 2 + (-uy) * 16;
        const midY = (startY + endY) / 2 + (ux) * 16;

        ctx.save();
        ctx.beginPath();
        ctx.moveTo(startX, startY);
        ctx.quadraticCurveTo(midX, midY, endX, endY);
        ctx.strokeStyle = isHighlighted ? '#38bdf8' : 'rgba(148, 163, 184, 0.35)';
        ctx.lineWidth = isHighlighted ? 2.5 : 1.5;
        if (isHighlighted) {
          ctx.shadowColor = '#38bdf8';
          ctx.shadowBlur = 8;
        }
        ctx.stroke();

        // Arrowhead at target
        const headLen = isHighlighted ? 10 : 8;
        const arrowAngle = Math.atan2(endY - midY, endX - midX);
        ctx.beginPath();
        ctx.moveTo(endX, endY);
        ctx.lineTo(endX - headLen * Math.cos(arrowAngle - Math.PI / 7), endY - headLen * Math.sin(arrowAngle - Math.PI / 7));
        ctx.lineTo(endX - headLen * Math.cos(arrowAngle + Math.PI / 7), endY - headLen * Math.sin(arrowAngle + Math.PI / 7));
        ctx.closePath();
        ctx.fillStyle = isHighlighted ? '#38bdf8' : 'rgba(148, 163, 184, 0.7)';
        ctx.fill();

        // Relationship Type Pill Label
        const label = (e.type || 'rel').toUpperCase();
        ctx.font = 'bold 9px var(--font-mono, monospace)';
        const textWidth = ctx.measureText(label).width;
        const pillW = textWidth + 10;
        const pillH = 14;

        ctx.fillStyle = isHighlighted ? 'rgba(14, 165, 233, 0.95)' : 'rgba(22, 27, 34, 0.88)';
        ctx.strokeStyle = isHighlighted ? '#38bdf8' : 'rgba(148, 163, 184, 0.4)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(midX - pillW / 2, midY - pillH / 2, pillW, pillH, 3);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = isHighlighted ? '#ffffff' : 'var(--text-secondary, #cbd5e1)';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(label, midX, midY);
        ctx.restore();
      }

      // 2. Draw Hexagon Nodes
      for (const node of graphNodes.values()) {
        const isHovered = (hoveredNode && hoveredNode.id === node.id);
        const isSelected = (selectedNode && selectedNode.id === node.id);
        const palette = getNodePalette(node.depth);
        const r = node.radius + (isHovered || isSelected ? 4 : 0);
        const cr = 8; // rounded corner radius

        ctx.save();
        // Glow effect
        if (isHovered || isSelected) {
          ctx.shadowColor = palette.glow;
          ctx.shadowBlur = 22;
        } else {
          ctx.shadowColor = palette.glow;
          ctx.shadowBlur = 8;
        }

        // Hexagon Path
        drawRoundedHexagon(ctx, node.x, node.y, r, cr);

        // Gradient Fill
        const grad = ctx.createLinearGradient(node.x, node.y - r, node.x, node.y + r);
        grad.addColorStop(0, palette.fillStart);
        grad.addColorStop(1, palette.fillEnd);
        ctx.fillStyle = grad;
        ctx.fill();

        // Border
        ctx.strokeStyle = (isSelected ? '#ffffff' : palette.border);
        ctx.lineWidth = (isSelected ? 3.5 : (isHovered ? 2.5 : 1.8));
        ctx.stroke();
        ctx.shadowBlur = 0;

        // Dynamic 1-or-2 Line Entity Label
        const lines = formatNodeLabel(node.id);
        ctx.fillStyle = '#ffffff';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        if (lines.length === 1) {
          ctx.font = (node.depth === 0 ? '16px' : '13px') + ' sans-serif';
          ctx.fillText(getNodeIcon(node.id), node.x, node.y - 7);

          ctx.font = 'bold 9.5px var(--font-mono, monospace)';
          ctx.fillText(lines[0], node.x, node.y + 8);
        } else {
          ctx.font = (node.depth === 0 ? '14px' : '12px') + ' sans-serif';
          ctx.fillText(getNodeIcon(node.id), node.x, node.y - 11);

          ctx.font = 'bold 8.8px var(--font-mono, monospace)';
          ctx.fillText(lines[0], node.x, node.y + 3);
          ctx.fillText(lines[1], node.x, node.y + 14);
        }

        // Depth badge pill
        if (node.depth >= 0) {
          const depthTag = 'D' + node.depth;
          ctx.font = 'bold 7.5px sans-serif';
          ctx.fillStyle = 'rgba(15, 23, 42, 0.85)';
          ctx.beginPath();
          ctx.roundRect(node.x - 9, node.y - r + 3, 18, 9, 3);
          ctx.fill();
          ctx.fillStyle = palette.border;
          ctx.fillText(depthTag, node.x, node.y - r + 8);
        }

        ctx.restore();
      }

      ctx.restore();

      // 3. Screen-Space Hover Tooltip
      if (hoveredNode && !draggedNode) {
        const sx = graphPan.x + hoveredNode.x * graphZoom;
        const sy = graphPan.y + hoveredNode.y * graphZoom;
        const palette = getNodePalette(hoveredNode.depth);
        const nodeRadiusScreen = (hoveredNode.radius + 4) * graphZoom;
        
        ctx.save();
        const fullTitle = hoveredNode.id;
        const hintText = '⚡ Doppelklick: Als Root • Klick: Details';

        ctx.font = 'bold 11.5px var(--font-mono, monospace)';
        const titleWidth = ctx.measureText(fullTitle).width;
        ctx.font = '9.5px var(--font-ui, sans-serif)';
        const hintWidth = ctx.measureText(hintText).width;
        
        const tooltipW = Math.max(titleWidth + 70, hintWidth + 24, 180);
        const tooltipH = 46;
        let tipX = sx - tooltipW / 2;
        let tipY = sy - nodeRadiusScreen - tooltipH - 12;

        if (tipX < 10) tipX = 10;
        if (tipX + tooltipW > canvas.clientWidth - 10) tipX = canvas.clientWidth - tooltipW - 10;
        if (tipY < 10) tipY = sy + nodeRadiusScreen + 14;

        ctx.shadowColor = 'rgba(0, 0, 0, 0.75)';
        ctx.shadowBlur = 14;
        ctx.shadowOffsetY = 4;

        ctx.fillStyle = 'rgba(15, 23, 42, 0.96)';
        ctx.strokeStyle = palette.border;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.roundRect(tipX, tipY, tooltipW, tooltipH, 7);
        ctx.fill();
        ctx.stroke();

        ctx.shadowColor = 'transparent';
        ctx.shadowBlur = 0;
        ctx.shadowOffsetY = 0;

        // Icon + Full Title
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.font = '12px sans-serif';
        ctx.fillText(getNodeIcon(hoveredNode.id), tipX + 10, tipY + 15);

        ctx.font = 'bold 11px var(--font-mono, monospace)';
        ctx.fillStyle = '#ffffff';
        ctx.fillText(fullTitle, tipX + 28, tipY + 15);

        // Depth Badge Pill
        ctx.font = 'bold 8.5px var(--font-ui, sans-serif)';
        const badgeW = 42;
        const badgeH = 15;
        const badgeX = tipX + tooltipW - badgeW - 8;
        const badgeY = tipY + 7;
        ctx.fillStyle = palette.fillEnd || 'rgba(30, 41, 59, 0.8)';
        ctx.strokeStyle = palette.border;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(badgeX, badgeY, badgeW, badgeH, 3);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#ffffff';
        ctx.textAlign = 'center';
        ctx.fillText('Tiefe ' + hoveredNode.depth, badgeX + badgeW / 2, badgeY + badgeH / 2);

        // Hint Line
        ctx.textAlign = 'left';
        ctx.font = '9px var(--font-ui, sans-serif)';
        ctx.fillStyle = 'rgba(148, 163, 184, 0.95)';
        ctx.fillText(hintText, tipX + 10, tipY + 34);

        ctx.restore();
      }

      ctx.restore();
    }

    // Canvas Interaction Setup
    function setupGraphInteraction() {
      const canvas = document.getElementById('graphCanvas');
      if (!canvas) return;

      function getGraphPos(e) {
        const rect = canvas.getBoundingClientRect();
        const clientX = e.clientX - rect.left;
        const clientY = e.clientY - rect.top;
        return {
          gx: (clientX - graphPan.x) / graphZoom,
          gy: (clientY - graphPan.y) / graphZoom,
          cx: clientX,
          cy: clientY
        };
      }

      function findNodeAt(gx, gy) {
        for (const node of graphNodes.values()) {
          const dx = node.x - gx;
          const dy = node.y - gy;
          if (dx * dx + dy * dy <= (node.radius + 6) * (node.radius + 6)) {
            return node;
          }
        }
        return null;
      }

      canvas.onmousedown = (e) => {
        const pos = getGraphPos(e);
        const hit = findNodeAt(pos.gx, pos.gy);
        if (hit) {
          draggedNode = hit;
          selectedNode = hit;
          openInspector(hit);
          wakeGraphPhysics();
        } else {
          isPanning = true;
          startPan = { x: e.clientX - graphPan.x, y: e.clientY - graphPan.y };
        }
      };

      window.addEventListener('mousemove', (e) => {
        if (activeGraphDisplayMode !== 'graph') return;
        const pos = getGraphPos(e);

        if (draggedNode) {
          draggedNode.x = pos.gx;
          draggedNode.y = pos.gy;
          draggedNode.vx = 0;
          draggedNode.vy = 0;
          if (graphAntiCollision) updateGraphPhysics();
          wakeGraphPhysics();
          drawGraph();
        } else if (isPanning) {
          graphPan.x = e.clientX - startPan.x;
          graphPan.y = e.clientY - startPan.y;
          drawGraph();
        } else {
          const hit = findNodeAt(pos.gx, pos.gy);
          if (hit !== hoveredNode) {
            hoveredNode = hit;
            canvas.style.cursor = hit ? 'pointer' : 'grab';
            drawGraph();
          }
        }
      });

      window.addEventListener('mouseup', () => {
        draggedNode = null;
        isPanning = false;
      });

      canvas.onwheel = (e) => {
        e.preventDefault();
        const rect = canvas.getBoundingClientRect();
        const mouseX = e.clientX - rect.left;
        const mouseY = e.clientY - rect.top;
        const zoomDelta = e.deltaY < 0 ? 1.15 : 0.87;
        const newZoom = Math.max(0.25, Math.min(4.0, graphZoom * zoomDelta));

        graphPan.x = mouseX - (mouseX - graphPan.x) * (newZoom / graphZoom);
        graphPan.y = mouseY - (mouseY - graphPan.y) * (newZoom / graphZoom);
        graphZoom = newZoom;
        drawGraph();
      };

      canvas.ondblclick = (e) => {
        const pos = getGraphPos(e);
        const hit = findNodeAt(pos.gx, pos.gy);
        if (hit) {
          setEntityAndQuery(hit.id);
        }
      };

      window.addEventListener('resize', resizeGraphCanvas);
    }

    function openInspector(node) {
      const inspector = document.getElementById('graphInspector');
      const content = document.getElementById('inspectorContent');
      if (!inspector || !content) return;

      const outEdges = graphEdges.filter(e => e.source === node.id);
      const inEdges = graphEdges.filter(e => e.target === node.id);

      let html = `
        <div style="display: flex; align-items: center; gap: 8px;">
          <span style="font-size: 20px;">${getNodeIcon(node.id)}</span>
          <div>
            <div style="font-weight: bold; color: var(--text-primary); font-size: 13px;">${node.id}</div>
            <div style="color: var(--text-tertiary); font-size: 11px;">CTE Tiefe: ${node.depth}</div>
          </div>
        </div>
        <div style="margin-top: 6px; border-top: 1px solid var(--border-muted); padding-top: 6px;">
          <div style="font-weight: bold; color: var(--accent); margin-bottom: 4px;">Ausgehende Relationen (${outEdges.length}):</div>
          ${outEdges.length === 0 ? '<div style="color: var(--text-tertiary);">Keine</div>' : ''}
          ${outEdges.map(e => `
            <div style="display: flex; justify-content: space-between; background: var(--bg-app); padding: 4px 6px; border-radius: 4px; font-family: var(--font-mono); font-size: 11px; margin-bottom: 3px;">
              <span style="color: var(--warning);">${e.type}</span>
              <span style="color: var(--success);">${e.target}</span>
            </div>
          `).join('')}
        </div>
        <div style="margin-top: 4px; border-top: 1px solid var(--border-muted); padding-top: 6px;">
          <div style="font-weight: bold; color: var(--accent); margin-bottom: 4px;">Eingehende Relationen (${inEdges.length}):</div>
          ${inEdges.length === 0 ? '<div style="color: var(--text-tertiary);">Keine</div>' : ''}
          ${inEdges.map(e => `
            <div style="display: flex; justify-content: space-between; background: var(--bg-app); padding: 4px 6px; border-radius: 4px; font-family: var(--font-mono); font-size: 11px; margin-bottom: 3px;">
              <span style="color: var(--accent);">${e.source}</span>
              <span style="color: var(--warning);">${e.type}</span>
            </div>
          `).join('')}
        </div>
      `;
      content.innerHTML = html;
      inspector.style.display = 'flex';
    }

    // Query CTE Graph and populate both Neo4j Hexagon Canvas and List View
    async function queryRelations() {
      const id = document.getElementById('entityInput').value.trim();
      const list = document.getElementById('relationsList');
      if (!id) return;

      try {
        const res = await fetch('/v1/relations?source_id=' + encodeURIComponent(id));
        const data = await res.json();
        list.innerHTML = '';
        const rels = Array.isArray(data) ? data : (data.relations || []);

        // 1. Populate List View
        if (rels.length > 0) {
          rels.forEach(r => {
            const item = document.createElement('div');
            item.className = 'rel-item';
            item.innerHTML = `
              <span style="color: var(--accent); font-weight: bold;">${r.source_id}</span>
              <span class="rel-arrow">──[${r.relation_type}]──▶</span>
              <span style="color: var(--success); font-weight: bold;">${r.target_id}</span>
              <div style="flex: 1"></div>
              <span style="color: var(--text-tertiary);">Tiefe ${r.depth || 1}</span>
            `;
            list.appendChild(item);
          });
        } else {
          list.innerHTML = '<div style="color: var(--text-secondary); text-align: center; margin-top: 40px;">Keine Relationen für ' + id + ' gefunden.</div>';
        }

        // 2. Build Graph Nodes & Edges for Neo4j Hexagon Visualizer
        graphNodes.clear();
        graphEdges = [];

        // Add Root Node
        graphNodes.set(id, {
          id: id,
          depth: 0,
          radius: 44,
          x: 0,
          y: 0,
          vx: 0,
          vy: 0
        });

        // Add Connected Nodes & Edges
        rels.forEach((r, idx) => {
          const depth = r.depth || 1;
          const radius = depth === 1 ? 40 : 36;

          if (!graphNodes.has(r.source_id)) {
            const angle = (idx / (rels.length || 1)) * Math.PI * 2;
            const dist = 100 + depth * 50;
            graphNodes.set(r.source_id, {
              id: r.source_id,
              depth: depth,
              radius: radius,
              x: Math.cos(angle) * dist + (Math.random() - 0.5) * 20,
              y: Math.sin(angle) * dist + (Math.random() - 0.5) * 20,
              vx: 0,
              vy: 0
            });
          }

          if (!graphNodes.has(r.target_id)) {
            const angle = ((idx + 0.5) / (rels.length || 1)) * Math.PI * 2;
            const dist = 120 + depth * 60;
            graphNodes.set(r.target_id, {
              id: r.target_id,
              depth: depth,
              radius: radius,
              x: Math.cos(angle) * dist + (Math.random() - 0.5) * 20,
              y: Math.sin(angle) * dist + (Math.random() - 0.5) * 20,
              vx: 0,
              vy: 0
            });
          }

          graphEdges.push({
            source: r.source_id,
            target: r.target_id,
            type: r.relation_type,
            depth: depth
          });
        });

        resetGraphView();
        resizeGraphCanvas();
        wakeGraphPhysics();

      } catch (err) {
        list.innerHTML = '<div style="color: var(--danger)">Fehler: ' + err.message + '</div>';
      }
    }

    // Auto-setup graph interaction once DOM is loaded
    setTimeout(() => {
      setupGraphInteraction();
      resizeGraphCanvas();
      queryRelations(); // Initial load with VINOX
    }, 100);

    // Health / System
    async function checkHealth() {
      const out = document.getElementById('systemOutput');
      try {
        const res = await fetch('/health/live');
        const data = await res.json();
        out.innerText = JSON.stringify(data, null, 2);
      } catch (err) {
        out.innerText = 'Fehler: ' + err.message;
      }
    }

    function openSpec() {
      window.open('/openapi.yaml', '_blank');
    }

    // Keyboard Shortcuts
    document.getElementById('promptInput').addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        handleSend();
      }
    });

    // Init
    loadDevices();
    loadModels();
    loadConversations();
  </script>
</body>
</html>
)rawliteral";

} // namespace vinox::server
