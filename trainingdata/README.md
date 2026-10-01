# VINOX Agent & Tool Calling Training Dataset

Dieses Verzeichnis enthält ein dreischichtiges Datenset (`canonical/`, `positive/`, `negative/`) aus Ground-Truth-Referenzen, mehrsprachigen Paraphrasen und echten Live-Inferenz-Läufen diverser OpenVINO-Modelle auf CPU mit Reasoning-Traces (`<think>...</think>`).

## 3-Layer-Architektur

```text
trainingdata/
├── canonical/                        # Schicht 1: Ground Truth Intent -> ToolCall
│   ├── canonical_intents.jsonl       # 27 definierte kanonische Tool-Intents
│   ├── multilingual_sft_dataset.jsonl# 127 mehrsprachige SFT-Samples (DE/EN: formal, umgangssprachlich, kurz)
│   └── dpo_preference_dataset.jsonl  # 221 DPO/ORPO Preference-Paare (Chosen vs. Hard Negative)
├── positive/                         # Schicht 2: Valide Live-Modellantworten
│   └── (20 isolierte JSON-Files + positive_samples.jsonl mit 35 Multi-Turn SFT Samples)
└── negative/                         # Schicht 3: Reale Fehlversuche & Hard Negatives
    └── (62 isolierte JSON-Files + negative_samples.jsonl mit 125 Contrastive Samples)
```

## Evaluierte Modelle (Live Inferenz)

1. `DeepSeek-R1-Distill-Llama-3.2-1B-ov` — **0/7 Positiv** (0.0%)
2. `DeepSeek-R1-Distill-Qwen-1.5B-ov` — **2/14 Positiv** (14.3%)
3. `GroundTruth_Benchmark` — **3/3 Positiv** (100.0%)
4. `Phi-4-mini-instruct-ov` — **6/25 Positiv** (24.0%)
5. `Qwen-2.5-Coder-0.5B` — **4/17 Positiv** (23.5%)
6. `Qwen2.5-1.5B-Instruct-int4-ov` — **7/25 Positiv** (28.0%)
7. `SmolLM3-3B-ov` — **0/7 Positiv** (0.0%)
8. `deepseek-r1-distill-qwen-1.5b-sft-int4-ov` — **0/10 Positiv** (0.0%)
9. `deepseek-r1-tokenfix-epoch2-int4-ov` — **11/23 Positiv** (47.8%)
10. `ov_deepseek_tools_v4` — **0/14 Positiv** (0.0%)
11. `qwen-2.5-1.5b-sft-int4-ov` — **2/15 Positiv** (13.3%)

## Evaluierte Tools

| Tool Name | Beschreibung | Positiv | Negativ | Gesamt | Positiv-Quote |
| :--- | :--- | :---: | :---: | :---: | :---: |
| `fs.list` | Verzeichnisinhalte auflisten in Sandbox | 5 | 20 | 25 | 20.0% |
| `fs.read` | Sicheres Lesen von Dateien in Workspace-Sandbox | 7 | 28 | 35 | 20.0% |
| `fs.write` | Schreiben von Dateien in Workspace-Sandbox | 6 | 12 | 18 | 33.3% |
| `math.calculate` | Deterministischer mathematischer Parser & Rechner | 5 | 33 | 38 | 13.2% |
| `system.time` | Standard-Systemzeit & Zeitzonen | 5 | 8 | 13 | 38.5% |
| `vinox.document_ingest` | Dokumenten-Ingestion & Vektorindexierung | 4 | 8 | 12 | 33.3% |
| `vinox.search` | VINOX Hybrid Retrieval (BM25 + sqlite-vec) | 3 | 16 | 19 | 15.8% |
| **Gesamt** | | **35** | **125** | **160** | **21.9%** |

## Häufigste Negativ-Muster (ideal für DPO/ORPO Alignment)

1. **Markdown-JSON-Trap**: Modell generiert ```json { "name": "...", "arguments": [...] } ``` im Text-Content anstelle nativer `<tool_call>`-Tags.
2. **Namespace-Verkürzung**: Modell ruft z. B. `calculate` statt `math.calculate` oder `readConfig` statt `fs.read` auf.
3. **Array- statt Objekt-Argumente**: Parameter werden als Positions-Array statt als benannte Schlüssel-Werte (`key-value object`) serialisiert.
4. **Reasoning-Exhaustion**: Modell verbleibt im `<think>`-Block, analysiert die Berechnung oder Dateistruktur im Detail und beendet die Antwort ohne Funktionsaufruf.
5. **Erklär-Modus**: Modell erklärt dem Anwender in natürlicher Sprache die Syntax oder CLI-Optionen des Tools, statt die Ausführung selbst zu triggern.

## Format der JSONL-Einträge

Jeder Eintrag folgt dem standardisierten Chat-Completions / ShareGPT Schema:

```json
{
  "id": "system.time_Phi-4-mini-instruct-ov_20260906_153941_265287",
  "tool": "system.time",
  "model": "Phi-4-mini-instruct-ov",
  "label": "positive",
  "notes": "Valid tool call with reasoning trace",
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "system.time",
        "description": "Gibt die aktuelle Systemzeit und das Datum zurueck.",
        "parameters": {
          "type": "object",
          "properties": {
            "timezone": {
              "type": "string",
              "description": "Die Zeitzone, z.B. Europe/Berlin oder UTC"
            }
          },
          "required": [
            "timezone"
          ]
        }
      }
    }
  ],
  "messages": [
    {
      "role": "system",
      "content": "You are a helpful assistant with access to tools. Always begin your response by thinking briefly inside <think>...</think> tags about which tool to call and which arguments are required. After closing </think>, invoke the appropriate tool."
    },
    {
      "role": "user",
      "content": "What time is it right now in Berlin?"
    },
    {
      "role": "assistant",
      "reasoning_content": "To get the current system time for Berlin, I will use the system.time function with \"Europe/Berlin\" as the timezone parameter.",
      "content": null,
      "tool_calls": [
        {
          "id": "call_e65c7bf5360c3294",
          "type": "function",
          "function": {
            "name": "system.time",
            "arguments": "{\"timezone\":\"Europe/Berlin\"}"
          }
        }
      ]
    }
  ]
}
```
