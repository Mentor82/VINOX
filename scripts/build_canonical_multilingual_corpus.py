#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VINOX Canonical Multilingual Tool Corpus Generator (Extended Edition)
Enthält umfassende semantische Trainingsdaten für:
  1. Komplexe mathematische Formeln (Pythagoras, Zinseszins, Kinetische Energie, Trigonometrie, Varianz, Brüche)
  2. Verschachtelte Dateipfade & Sandbox-Szenarien (Deep Source Code, Docs, Logs, Pfadnormalisierung, Windows-Backslashes)
  3. Mehrsprachige linguistische Vielfalt (DE/EN: formell, umgangssprachlich, kurz, technischer CLI-Stil)
  4. DPO/ORPO Preference-Paare (Chosen vs. Hard Negative Rejections)
"""

import json
import uuid
from pathlib import Path

CANONICAL_INTENTS = [
    # =========================================================================
    # A. SYSTEM TIME INTENTS
    # =========================================================================
    {
        "intent_id": "time.get_current.berlin",
        "tool": "system.time",
        "arguments": {"timezone": "Europe/Berlin"},
        "think": "Der Benutzer möchte die aktuelle Uhrzeit in Berlin wissen. Dafür steht das Tool 'system.time' zur Verfügung. Ich übergebe als 'timezone' den kanonischen IANA-Bezeichner 'Europe/Berlin'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Wie spät ist es aktuell in Berlin?"},
            {"lang": "de", "style": "colloquial", "text": "Sag mal, wie viel Uhr haben wir gerade in Berlin?"},
            {"lang": "de", "style": "short", "text": "Uhrzeit Berlin jetzt"},
            {"lang": "en", "style": "formal", "text": "What time is it right now in Berlin?"},
            {"lang": "en", "style": "colloquial", "text": "Can you check the current time in Berlin for me?"},
            {"lang": "en", "style": "short", "text": "Current Berlin time"},
            {"lang": "mixed", "style": "technical", "text": "system.time query for timezone Europe/Berlin"}
        ],
        "hard_negatives": [
            {"type": "hallucination", "content": "Es ist aktuell 18:35 Uhr in Berlin."},
            {"type": "markdown_json", "content": "```json\n{\"function\": \"system.time\", \"arguments\": {\"timezone\": \"Europe/Berlin\"}}\n```"},
            {"type": "wrong_namespace", "call": {"name": "time", "arguments": "{\"timezone\":\"Europe/Berlin\"}"}}
        ]
    },
    {
        "intent_id": "time.get_current.utc",
        "tool": "system.time",
        "arguments": {"timezone": "UTC"},
        "think": "Es wird nach der koordinierten Weltzeit (UTC) gefragt. Ich rufe 'system.time' mit 'timezone': 'UTC' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Wie lautet die gegenwärtige UTC-Zeit?"},
            {"lang": "de", "style": "short", "text": "Aktuelle Zeit in UTC bitte."},
            {"lang": "en", "style": "formal", "text": "What is the current UTC timestamp?"},
            {"lang": "en", "style": "short", "text": "UTC time now"},
            {"lang": "en", "style": "colloquial", "text": "What's the universal time right now?"}
        ],
        "hard_negatives": [
            {"type": "markdown_json", "content": "```json\n{\"name\": \"system.time\", \"arguments\": {\"timezone\": \"UTC\"}}\n```"}
        ]
    },

    # =========================================================================
    # B. COMPLEX MATHEMATICAL FORMULAS
    # =========================================================================
    # 1. Standard Arithmetic
    {
        "intent_id": "math.calculate.arithmetic",
        "tool": "math.calculate",
        "arguments": {"expression": "(144 * 12) / 6"},
        "think": "Der Anwender möchte das Ergebnis von (144 * 12) / 6 berechnen. Um Rechenfehler zu vermeiden, nutze ich das deterministische Tool 'math.calculate'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne bitte (144 * 12) / 6. Was ist das genaue Ergebnis?"},
            {"lang": "de", "style": "colloquial", "text": "Rechne mir mal kurz (144 mal 12) geteilt durch 6 aus."},
            {"lang": "de", "style": "short", "text": "(144 * 12) / 6 berechnen"},
            {"lang": "en", "style": "formal", "text": "Calculate (144 * 12) / 6. What is the exact result?"},
            {"lang": "en", "style": "colloquial", "text": "Could you evaluate the math expression (144 * 12) / 6?"},
            {"lang": "en", "style": "short", "text": "Eval (144 * 12) / 6"}
        ],
        "hard_negatives": [
            {"type": "python_code_block", "content": "```python\nresult = (144 * 12) / 6\nprint(result)\n```\nDas Ergebnis ist 288."},
            {"type": "wrong_name", "call": {"name": "calculate", "arguments": "{\"expression\": \"(144 * 12) / 6\"}"}},
            {"type": "markdown_json", "content": "```json\n{\"name\": \"math.calculate\", \"arguments\": [\"(144 * 12) / 6\"]}\n```"}
        ]
    },
    # 2. Pythagoras / Hypotenuse
    {
        "intent_id": "math.calculate.hypotenuse_pythagoras",
        "tool": "math.calculate",
        "arguments": {"expression": "sqrt(12^2 + 16^2)"},
        "think": "Für die Hypotenuse eines rechtwinkligen Dreiecks mit Katheten 12 und 16 wende ich den Satz des Pythagoras an: sqrt(12^2 + 16^2). Ich übergebe diese Formel an 'math.calculate'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne die Länge der Hypotenuse eines rechtwinkligen Dreiecks mit den Katheten 12 und 16."},
            {"lang": "de", "style": "colloquial", "text": "Wie lang ist die Hypotenuse, wenn die Seiten 12 und 16 lang sind? Rechne mit Satz des Pythagoras."},
            {"lang": "de", "style": "short", "text": "Pythagoras sqrt(12^2 + 16^2)"},
            {"lang": "en", "style": "formal", "text": "Calculate the hypotenuse of a right-angled triangle with sides 12 and 16 using sqrt(12^2 + 16^2)."},
            {"lang": "en", "style": "colloquial", "text": "Find the hypotenuse if the two legs are 12 and 16."},
            {"lang": "en", "style": "short", "text": "pythagoras 12 16: sqrt(12^2 + 16^2)"}
        ],
        "hard_negatives": [
            {"type": "latex_prose", "content": "Die Formel lautet $$c = \\sqrt{a^2 + b^2}$$. Für a=12 und b=16 ergibt das $$\\sqrt{144 + 256} = \\sqrt{400} = 20$$."},
            {"type": "hallucinated_approximation", "content": "Die Hypotenuse ist ungefähr 28 lang."},
            {"type": "wrong_name", "call": {"name": "pythagoras", "arguments": "{\"a\": 12, \"b\": 16}"}}
        ]
    },
    # 3. Kinetic Energy (Physics)
    {
        "intent_id": "math.calculate.kinetic_energy",
        "tool": "math.calculate",
        "arguments": {"expression": "0.5 * 1200 * (27.78 ^ 2)"},
        "think": "Die kinetische Energie berechnet sich nach E_k = 0.5 * m * v^2. Bei einer Masse von 1200 kg und 27.78 m/s lautet die Formel: 0.5 * 1200 * (27.78 ^ 2). Ich nutze 'math.calculate'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne die kinetische Energie eines 1200 kg schweren Fahrzeugs bei 27.78 m/s mit der Formel 0.5 * m * v^2."},
            {"lang": "de", "style": "colloquial", "text": "Wie viel Bewegungsenergie hat ein Auto mit 1200 kg Masse bei ca. 100 km/h (27.78 m/s)?"},
            {"lang": "de", "style": "short", "text": "Kinetische Energie 0.5 * 1200 * (27.78 ^ 2)"},
            {"lang": "en", "style": "formal", "text": "Calculate the kinetic energy for a mass of 1200 kg at velocity 27.78 m/s using 0.5 * 1200 * (27.78 ^ 2)."},
            {"lang": "en", "style": "colloquial", "text": "What is the kinetic energy of a 1200kg vehicle moving at 27.78 m/s?"},
            {"lang": "en", "style": "short", "text": "eval kinetic energy: 0.5 * 1200 * 27.78^2"}
        ],
        "hard_negatives": [
            {"type": "python_snippet", "content": "```python\nm = 1200\nv = 27.78\nprint(0.5 * m * v**2)\n```"},
            {"type": "markdown_json", "content": "```json\n{\"tool\": \"math.calculate\", \"formula\": \"0.5 * 1200 * (27.78 ^ 2)\"}\n```"}
        ]
    },
    # 4. Compound Interest (Exponential Growth)
    {
        "intent_id": "math.calculate.compound_interest",
        "tool": "math.calculate",
        "arguments": {"expression": "5000 * ((1 + 0.045) ^ 10)"},
        "think": "Für den Zinseszins nach 10 Jahren bei 5000 Euro Startkapital und 4.5% Zinsen rechne ich 5000 * ((1 + 0.045) ^ 10). Ich übergebe dies an 'math.calculate'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne das Endkapital von 5000 Euro nach 10 Jahren bei einem jährlichen Zinssatz von 4.5% mit Zinseszins."},
            {"lang": "de", "style": "colloquial", "text": "Wenn ich 5000 € für 10 Jahre zu 4,5% Zinsen anlege, wie viel kommt da raus? Formel: 5000 * (1.045 ^ 10)"},
            {"lang": "de", "style": "short", "text": "Zinseszins 5000 * ((1 + 0.045) ^ 10)"},
            {"lang": "en", "style": "formal", "text": "Calculate compound interest for principal 5000 at 4.5% rate over 10 years: 5000 * ((1 + 0.045) ^ 10)."},
            {"lang": "en", "style": "colloquial", "text": "How much will $5000 grow to after 10 years at 4.5% compounded annually?"},
            {"lang": "en", "style": "short", "text": "compound interest: 5000 * ((1 + 0.045) ^ 10)"}
        ],
        "hard_negatives": [
            {"type": "hallucination", "content": "Nach 10 Jahren haben Sie ungefähr 7.200 Euro auf dem Konto."},
            {"type": "wrong_name", "call": {"name": "finance.interest", "arguments": "{\"principal\": 5000, \"rate\": 0.045, \"years\": 10}"}}
        ]
    },
    # 5. Trigonometry & Wave Calculation
    {
        "intent_id": "math.calculate.trigonometry_wave",
        "tool": "math.calculate",
        "arguments": {"expression": "round(sin(3.14159265 / 4) * 1000) / 1000"},
        "think": "Der Benutzer möchte den gerundeten Wert von sin(pi / 4) auf 3 Dezimalstellen berechnen. Die Formel lautet round(sin(3.14159265 / 4) * 1000) / 1000. Ich rufe 'math.calculate' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne den Sinus von pi / 4 gerundet auf drei Nachkommastellen: round(sin(3.14159265 / 4) * 1000) / 1000."},
            {"lang": "de", "style": "colloquial", "text": "Rechne mir mal den Sinus von 45 Grad bzw. pi/4 aus auf 3 Stellen gerundet."},
            {"lang": "en", "style": "formal", "text": "Evaluate the sine of pi/4 rounded to 3 decimal places using round(sin(3.14159265 / 4) * 1000) / 1000."},
            {"lang": "en", "style": "short", "text": "eval sin(pi/4) rounded to 3 decimals"}
        ],
        "hard_negatives": [
            {"type": "prose_estimation", "content": "Der Sinus von pi/4 ist genau sqrt(2)/2, also ungefähr 0.707."}
        ]
    },
    # 6. Sample Variance (Statistics)
    {
        "intent_id": "math.calculate.sample_variance",
        "tool": "math.calculate",
        "arguments": {"expression": "((18 - 14)^2 + (12 - 14)^2 + (15 - 14)^2 + (11 - 14)^2) / 4"},
        "think": "Ich berechne die Varianz der Werte 18, 12, 15, 11 bezüglich des Mittelwerts 14: ((18-14)^2 + (12-14)^2 + (15-14)^2 + (11-14)^2) / 4. Das übergebe ich an 'math.calculate'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne die Stichprobenvarianz der Messwerte 18, 12, 15, 11 bezüglich des Mittelwerts 14."},
            {"lang": "de", "style": "colloquial", "text": "Rechne die Varianz für ((18 - 14)^2 + (12 - 14)^2 + (15 - 14)^2 + (11 - 14)^2) geteilt durch 4 aus."},
            {"lang": "en", "style": "formal", "text": "Calculate sample variance for data points 18, 12, 15, 11 around mean 14: ((18 - 14)^2 + (12 - 14)^2 + (15 - 14)^2 + (11 - 14)^2) / 4."},
            {"lang": "en", "style": "short", "text": "variance formula ((18-14)^2 + (12-14)^2 + (15-14)^2 + (11-14)^2) / 4"}
        ],
        "hard_negatives": [
            {"type": "python_script", "content": "import numpy as np\nprint(np.var([18, 12, 15, 11]))"}
        ]
    },
    # 7. Complex Mixed Fractions & Commercial Operations
    {
        "intent_id": "math.calculate.mixed_commercial_fractions",
        "tool": "math.calculate",
        "arguments": {"expression": "((350.50 * 1.19) - (120 * 0.95)) / (24 + 1.5)"},
        "think": "Hier soll ein komplexer kaufmännischer Bruch berechnet werden: ((350.50 * 1.19) - (120 * 0.95)) / (24 + 1.5). Ich rufe das Tool 'math.calculate' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Berechne bitte den Ausdruck: ((350.50 * 1.19) - (120 * 0.95)) / (24 + 1.5)."},
            {"lang": "de", "style": "colloquial", "text": "Rechne mir mal den Bruch ((350.50 * 1.19) - (120 * 0.95)) geteilt durch (24 + 1.5) aus."},
            {"lang": "en", "style": "formal", "text": "Evaluate the complex expression: ((350.50 * 1.19) - (120 * 0.95)) / (24 + 1.5)."},
            {"lang": "en", "style": "short", "text": "evaluate: ((350.50 * 1.19) - (120 * 0.95)) / (24 + 1.5)"}
        ],
        "hard_negatives": [
            {"type": "mental_math_error", "content": "Das Ergebnis ist 14.23 (falsch gerechnet ohne Tool)."}
        ]
    },

    # =========================================================================
    # C. NESTED FILE PATHS & SANDBOX SCENARIOS
    # =========================================================================
    # 8. fs.read - Deep C++ Server Source
    {
        "intent_id": "fs.read.nested_source_cpp",
        "tool": "fs.read",
        "arguments": {"path": "src/server/agent_handlers.cpp"},
        "think": "Der Benutzer möchte den Inhalt von 'src/server/agent_handlers.cpp' lesen. Ich nutze 'fs.read' mit dem verschachtelten Pfad 'src/server/agent_handlers.cpp'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Bitte lies den Inhalt der Quellcodedatei 'src/server/agent_handlers.cpp' aus."},
            {"lang": "de", "style": "colloquial", "text": "Zeig mir mal, was in src/server/agent_handlers.cpp implementiert ist."},
            {"lang": "de", "style": "short", "text": "Lies src/server/agent_handlers.cpp"},
            {"lang": "en", "style": "formal", "text": "Please read the source code file 'src/server/agent_handlers.cpp'."},
            {"lang": "en", "style": "colloquial", "text": "Can you check what's inside src/server/agent_handlers.cpp?"},
            {"lang": "en", "style": "short", "text": "cat src/server/agent_handlers.cpp"},
            {"lang": "mixed", "style": "technical", "text": "fs.read target: src/server/agent_handlers.cpp"}
        ],
        "hard_negatives": [
            {"type": "bash_cat", "content": "cat src/server/agent_handlers.cpp"},
            {"type": "markdown_json", "content": "```json\n{\"name\": \"fs.read\", \"arguments\": {\"file\": \"agent_handlers.cpp\"}}\n```"},
            {"type": "wrong_name", "call": {"name": "readSource", "arguments": "{\"path\": \"src/server/agent_handlers.cpp\"}"}}
        ]
    },
    # 9. fs.read - Windows Backslash Normalization
    {
        "intent_id": "fs.read.windows_backslash_normalization",
        "tool": "fs.read",
        "arguments": {"path": "src/plugins/math/plugin_math.cpp"},
        "think": "Der Anwender gibt den Pfad mit Windows-Backslashes an ('src\\plugins\\math\\plugin_math.cpp'). Kanonisch normalisiere ich diesen auf Vorwärtsslashs 'src/plugins/math/plugin_math.cpp' und rufe 'fs.read' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Lies bitte die Datei 'src\\plugins\\math\\plugin_math.cpp'."},
            {"lang": "de", "style": "colloquial", "text": "Öffne mal src\\plugins\\math\\plugin_math.cpp"},
            {"lang": "en", "style": "formal", "text": "Read the contents of 'src\\plugins\\math\\plugin_math.cpp'."},
            {"lang": "en", "style": "short", "text": "read src\\plugins\\math\\plugin_math.cpp"}
        ],
        "hard_negatives": [
            {"type": "unescaped_json_syntax_error", "content": "```json\n{\"name\": \"fs.read\", \"arguments\": {\"path\": \"src\\plugins\\math\\plugin_math.cpp\"}}\n```"},
            {"type": "wrong_name", "call": {"name": "fs.read_windows", "arguments": "{\"path\": \"src\\\\plugins\\\\math\\\\plugin_math.cpp\"}"}}
        ]
    },
    # 10. fs.read - Nested Packaging Script (Inno Setup)
    {
        "intent_id": "fs.read.nested_packaging_script",
        "tool": "fs.read",
        "arguments": {"path": "packaging/windows/installer.iss"},
        "think": "Der Benutzer fragt nach der Installationsskript-Datei 'packaging/windows/installer.iss'. Ich rufe 'fs.read' mit 'path': 'packaging/windows/installer.iss' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Zeige mir den Inhalt der Installer-Konfiguration unter 'packaging/windows/installer.iss'."},
            {"lang": "de", "style": "colloquial", "text": "Schau mal bitte in packaging/windows/installer.iss rein."},
            {"lang": "en", "style": "formal", "text": "Please read the Inno Setup installer file at 'packaging/windows/installer.iss'."},
            {"lang": "en", "style": "short", "text": "view packaging/windows/installer.iss"}
        ],
        "hard_negatives": [
            {"type": "prose_explanation", "content": "Das ist ein Inno Setup Skript für Windows-Installer."}
        ]
    },
    # 11. fs.list - Deep Plugin Subdirectory
    {
        "intent_id": "fs.list.deep_plugin_dir",
        "tool": "fs.list",
        "arguments": {"path": "src/plugins/time"},
        "think": "Der Benutzer möchte den Inhalt des Unterverzeichnisses 'src/plugins/time' auflisten. Ich rufe 'fs.list' mit 'path': 'src/plugins/time' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Liste alle Dateien im Verzeichnis 'src/plugins/time' auf."},
            {"lang": "de", "style": "colloquial", "text": "Was liegt alles im Ordner src/plugins/time?"},
            {"lang": "de", "style": "short", "text": "ls src/plugins/time"},
            {"lang": "en", "style": "formal", "text": "List the files and directories inside 'src/plugins/time'."},
            {"lang": "en", "style": "colloquial", "text": "Show me what files exist in the src/plugins/time folder."},
            {"lang": "en", "style": "short", "text": "dir src/plugins/time"}
        ],
        "hard_negatives": [
            {"type": "hallucinated_file_list", "content": "Im Ordner befinden sich:\n- plugin_time.cpp\n- plugin_time.hpp\n- CMakeLists.txt"},
            {"type": "markdown_json", "content": "```json\n{\"name\": \"fs.list\", \"directory\": \"src/plugins/time\"}\n```"}
        ]
    },
    # 12. fs.list - Docs Subdirectory
    {
        "intent_id": "fs.list.nested_docs_dir",
        "tool": "fs.list",
        "arguments": {"path": "docs"},
        "think": "Der Benutzer möchte wissen, welche Dokumente im 'docs'-Ordner existieren. Ich rufe 'fs.list' mit 'path': 'docs' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Liste bitte die Inhalte des Ordners 'docs' auf."},
            {"lang": "de", "style": "colloquial", "text": "Welche Dokumentationen haben wir im docs-Verzeichnis?"},
            {"lang": "en", "style": "formal", "text": "List the contents of the 'docs' directory."},
            {"lang": "en", "style": "short", "text": "ls docs"}
        ],
        "hard_negatives": [
            {"type": "wrong_name", "call": {"name": "list_directory", "arguments": "{\"path\": \"docs\"}"}}
        ]
    },
    # 13. fs.write - Nested Benchmark Report JSON
    {
        "intent_id": "fs.write.nested_benchmark_report",
        "tool": "fs.write",
        "arguments": {"path": "out/reports/benchmark_summary.json", "content": "{\"status\":\"complete\",\"accuracy\":0.985,\"npu_accelerated\":true}"},
        "think": "Ich soll eine strukturierte JSON-Zusammenfassung in die Datei 'out/reports/benchmark_summary.json' schreiben. Ich nutze 'fs.write'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Schreibe die JSON-Zusammenfassung '{\"status\":\"complete\",\"accuracy\":0.985,\"npu_accelerated\":true}' in die Datei 'out/reports/benchmark_summary.json'."},
            {"lang": "de", "style": "colloquial", "text": "Speichere bitte den Benchmark-Report in out/reports/benchmark_summary.json: {\"status\":\"complete\",\"accuracy\":0.985,\"npu_accelerated\":true}"},
            {"lang": "en", "style": "formal", "text": "Write the JSON summary '{\"status\":\"complete\",\"accuracy\":0.985,\"npu_accelerated\":true}' to 'out/reports/benchmark_summary.json'."},
            {"lang": "en", "style": "short", "text": "write out/reports/benchmark_summary.json"}
        ],
        "hard_negatives": [
            {"type": "python_snippet", "content": "with open('out/reports/benchmark_summary.json', 'w') as f:\n    f.write('...')"},
            {"type": "wrong_name", "call": {"name": "writeFile", "arguments": "{\"filename\": \"out/reports/benchmark_summary.json\", \"text\": \"...\"}"}}
        ]
    },
    # 14. fs.write - Nested Audit Log Entry
    {
        "intent_id": "fs.write.nested_audit_log",
        "tool": "fs.write",
        "arguments": {"path": "cache/logs/audit_2026.log", "content": "[2026-09-06T18:40:00Z] System integrity check verified."},
        "think": "Ich soll einen Audit-Logeintrag in die Datei 'cache/logs/audit_2026.log' schreiben. Ich rufe 'fs.write' mit path: 'cache/logs/audit_2026.log' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Protokolliere '[2026-09-06T18:40:00Z] System integrity check verified.' in die Datei 'cache/logs/audit_2026.log'."},
            {"lang": "de", "style": "colloquial", "text": "Schreib den Audit-Eintrag '[2026-09-06T18:40:00Z] System integrity check verified.' in cache/logs/audit_2026.log"},
            {"lang": "en", "style": "formal", "text": "Log '[2026-09-06T18:40:00Z] System integrity check verified.' into 'cache/logs/audit_2026.log'."},
            {"lang": "en", "style": "short", "text": "append log to cache/logs/audit_2026.log"}
        ],
        "hard_negatives": [
            {"type": "bash_echo", "content": "echo '[2026-09-06T18:40:00Z] System integrity check verified.' >> cache/logs/audit_2026.log"}
        ]
    },
    # 15. fs.read - Sandbox Path Traversal Sanitization
    {
        "intent_id": "fs.read.path_traversal_sanitized",
        "tool": "fs.read",
        "arguments": {"path": "config.json"},
        "think": "Der Benutzer versucht mit '../../../config.json' aus der Sandbox auszubrechen. Kanonisch und sicher wird der Pfad innerhalb der Sandbox auf 'config.json' aufgelöst. Ich rufe 'fs.read' mit 'path': 'config.json' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Lies bitte die Datei '../../../config.json' innerhalb der Sandbox."},
            {"lang": "de", "style": "colloquial", "text": "Kommst du an ../../../config.json ran?"},
            {"lang": "en", "style": "formal", "text": "Please read the file at '../../../config.json' within the workspace sandbox."},
            {"lang": "en", "style": "short", "text": "read ../../../config.json"}
        ],
        "hard_negatives": [
            {"type": "unsanitized_path", "call": {"name": "fs.read", "arguments": "{\"path\": \"../../../config.json\"}"}},
            {"type": "security_refusal", "content": "Ich kann auf diese Datei nicht zugreifen, da der Pfad '../../../' ungültig ist."}
        ]
    },

    # =========================================================================
    # D. DEDICATED SANDBOX WORKSPACE (REAL CONTENTS)
    # =========================================================================
    # 16. fs.read - Sandbox App Settings
    {
        "intent_id": "fs.read.sandbox_app_settings",
        "tool": "fs.read",
        "arguments": {"path": "sandbox/config/app_settings.json"},
        "think": "Der Benutzer möchte die Konfiguration in 'sandbox/config/app_settings.json' einsehen. Ich nutze 'fs.read' mit 'path': 'sandbox/config/app_settings.json'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Lies bitte die Sandbox-Konfiguration aus 'sandbox/config/app_settings.json'."},
            {"lang": "de", "style": "colloquial", "text": "Was für Settings sind in sandbox/config/app_settings.json konfiguriert?"},
            {"lang": "de", "style": "short", "text": "Lies sandbox/config/app_settings.json"},
            {"lang": "en", "style": "formal", "text": "Please read the sandbox application settings from 'sandbox/config/app_settings.json'."},
            {"lang": "en", "style": "colloquial", "text": "Can you check what settings are in sandbox/config/app_settings.json?"},
            {"lang": "en", "style": "short", "text": "read sandbox/config/app_settings.json"}
        ],
        "hard_negatives": [
            {"type": "wrong_name", "call": {"name": "readSettings", "arguments": "{\"path\": \"sandbox/config/app_settings.json\"}"}},
            {"type": "markdown_json", "content": "```json\n{\"name\": \"fs.read\", \"arguments\": {\"path\": \"sandbox/config/app_settings.json\"}}\n```"}
        ]
    },
    # 17. fs.read - Sandbox Database YAML
    {
        "intent_id": "fs.read.sandbox_database_yaml",
        "tool": "fs.read",
        "arguments": {"path": "sandbox/config/database.yaml"},
        "think": "Ich soll die Datenbank-Konfiguration 'sandbox/config/database.yaml' auslesen. Ich rufe 'fs.read' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Zeige mir die Datenbank-Verbindungsdaten aus 'sandbox/config/database.yaml'."},
            {"lang": "de", "style": "colloquial", "text": "Schau mal bitte in sandbox/config/database.yaml nach, welche DB dort eingetragen ist."},
            {"lang": "en", "style": "formal", "text": "Please inspect the database configuration in 'sandbox/config/database.yaml'."},
            {"lang": "en", "style": "short", "text": "cat sandbox/config/database.yaml"}
        ],
        "hard_negatives": [
            {"type": "bash_cli", "content": "cat sandbox/config/database.yaml"}
        ]
    },
    # 18. fs.read - Sandbox Python Microservice
    {
        "intent_id": "fs.read.sandbox_python_main",
        "tool": "fs.read",
        "arguments": {"path": "sandbox/src/main.py"},
        "think": "Der Benutzer möchte den Python-Quellcode 'sandbox/src/main.py' ansehen. Ich rufe 'fs.read' mit 'path': 'sandbox/src/main.py' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Lies den Python-Code der Datei 'sandbox/src/main.py' aus."},
            {"lang": "de", "style": "colloquial", "text": "Zeig mir den Inhalt von sandbox/src/main.py."},
            {"lang": "en", "style": "formal", "text": "Please read the Python microservice script at 'sandbox/src/main.py'."},
            {"lang": "en", "style": "short", "text": "view sandbox/src/main.py"}
        ],
        "hard_negatives": [
            {"type": "hallucination", "content": "Das ist ein FastAPI-Server mit zwei Routen."}
        ]
    },
    # 19. fs.read - Sandbox Sales CSV
    {
        "intent_id": "fs.read.sandbox_sales_csv",
        "tool": "fs.read",
        "arguments": {"path": "sandbox/data/sales_metrics.csv"},
        "think": "Der Benutzer fragt nach den Metriken in 'sandbox/data/sales_metrics.csv'. Ich lese die Datei mit 'fs.read' aus.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Lies die Umsatz- und Nutzerdaten aus 'sandbox/data/sales_metrics.csv'."},
            {"lang": "de", "style": "colloquial", "text": "Was steht in der CSV-Datei sandbox/data/sales_metrics.csv drin?"},
            {"lang": "en", "style": "formal", "text": "Read the sales metrics table from 'sandbox/data/sales_metrics.csv'."},
            {"lang": "en", "style": "short", "text": "cat sandbox/data/sales_metrics.csv"}
        ],
        "hard_negatives": [
            {"type": "markdown_json", "content": "```json\n{\"tool\": \"fs.read\", \"path\": \"sandbox/data/sales_metrics.csv\"}\n```"}
        ]
    },
    # 20. fs.list - Sandbox Source Tree
    {
        "intent_id": "fs.list.sandbox_src",
        "tool": "fs.list",
        "arguments": {"path": "sandbox/src"},
        "think": "Der Benutzer möchte den Ordner 'sandbox/src' auflisten. Ich rufe 'fs.list' mit 'path': 'sandbox/src' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Liste alle Dateien und Unterverzeichnisse im Ordner 'sandbox/src' auf."},
            {"lang": "de", "style": "colloquial", "text": "Was liegt alles im Ordner sandbox/src?"},
            {"lang": "en", "style": "formal", "text": "List the files and directories inside 'sandbox/src'."},
            {"lang": "en", "style": "short", "text": "ls sandbox/src"}
        ],
        "hard_negatives": [
            {"type": "hallucination", "content": "Dort liegen: main.py, config.py und utils/"}
        ]
    },
    # 21. fs.write - Sandbox Notes Scratchpad
    {
        "intent_id": "fs.write.sandbox_notes",
        "tool": "fs.write",
        "arguments": {"path": "sandbox/notes.txt", "content": "Sandbox test run passed with 100% precision."},
        "think": "Ich soll den Text 'Sandbox test run passed with 100% precision.' in 'sandbox/notes.txt' schreiben. Ich nutze 'fs.write'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Schreibe den Text 'Sandbox test run passed with 100% precision.' in die Datei 'sandbox/notes.txt'."},
            {"lang": "de", "style": "colloquial", "text": "Trag mal 'Sandbox test run passed with 100% precision.' in sandbox/notes.txt ein."},
            {"lang": "en", "style": "formal", "text": "Write 'Sandbox test run passed with 100% precision.' into 'sandbox/notes.txt'."},
            {"lang": "en", "style": "short", "text": "save into sandbox/notes.txt"}
        ],
        "hard_negatives": [
            {"type": "python_snippet", "content": "with open('sandbox/notes.txt', 'w') as f:\n    f.write('...')"}
        ]
    },

    # =========================================================================
    # E. HYBRID RETRIEVAL & VECTOR DATABASE
    # =========================================================================
    # 16. vinox.search - NPU Acceleration
    {
        "intent_id": "retrieval.search.npu_config",
        "tool": "vinox.search",
        "arguments": {"query": "OpenVINO NPU acceleration configuration", "limit": 5},
        "think": "Der Benutzer sucht nach Dokumentation zur NPU-Beschleunigung unter OpenVINO. Ich nutze 'vinox.search' mit query: 'OpenVINO NPU acceleration configuration' und limit: 5.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Durchsuche die Dokumentation nach Konfigurationsoptionen für die OpenVINO NPU-Beschleunigung."},
            {"lang": "de", "style": "colloquial", "text": "Wie richte ich die Intel NPU Beschleunigung in OpenVINO ein? Such mal in der Knowledge Base."},
            {"lang": "de", "style": "short", "text": "Suche OpenVINO NPU Beschleunigung"},
            {"lang": "en", "style": "formal", "text": "Search the documentation for OpenVINO NPU acceleration configuration."},
            {"lang": "en", "style": "colloquial", "text": "Can you search our docs for how to configure NPU acceleration in OpenVINO?"},
            {"lang": "en", "style": "short", "text": "vinox search: OpenVINO NPU acceleration"}
        ],
        "hard_negatives": [
            {"type": "prose_explanation", "content": "Um die OpenVINO NPU zu nutzen, setze das Device auf 'NPU' und quantisiere das Modell auf INT4 oder FP16."},
            {"type": "markdown_json", "content": "```json\n{\"function\": \"vinox.search\", \"parameters\": {\"query\": \"OpenVINO NPU acceleration configuration\"}}\n```"}
        ]
    },
    # 17. vinox.search - SQLite-vec & BM25 Architecture
    {
        "intent_id": "retrieval.search.sqlite_vec_architecture",
        "tool": "vinox.search",
        "arguments": {"query": "sqlite-vec BM25 hybrid ranking CTE graph", "limit": 10},
        "think": "Es wird nach Details zur Hybrid-Architektur (sqlite-vec + BM25 FTS5 + CTE Graph) gefragt. Ich rufe 'vinox.search' mit query: 'sqlite-vec BM25 hybrid ranking CTE graph' auf.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Suche in der Wissensdatenbank nach Dokumenten zur Hybrid-Suche mit sqlite-vec und BM25."},
            {"lang": "de", "style": "colloquial", "text": "Wie funktioniert das Hybrid-Ranking mit Vektoren und FTS5 in VINOX? Such mal danach."},
            {"lang": "en", "style": "formal", "text": "Search for documentation covering sqlite-vec BM25 hybrid ranking and CTE graph relations."},
            {"lang": "en", "style": "short", "text": "vinox search: sqlite-vec BM25 hybrid ranking"}
        ],
        "hard_negatives": [
            {"type": "wrong_name", "call": {"name": "searchDocs", "arguments": "{\"query\": \"sqlite-vec BM25\"}"}}
        ]
    },
    # 18. vinox.document_ingest - Release Notes
    {
        "intent_id": "retrieval.ingest.release_notes",
        "tool": "vinox.document_ingest",
        "arguments": {"title": "ReleaseNotes", "content": "VINOX 1.0 released with hybrid search."},
        "think": "Es soll ein neues Dokument mit dem Titel 'ReleaseNotes' und dem Text 'VINOX 1.0 released with hybrid search.' indexiert werden. Ich verwende 'vinox.document_ingest'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Indexiere ein neues Dokument mit dem Titel 'ReleaseNotes' und dem Inhalt 'VINOX 1.0 released with hybrid search.' in der Datenbank."},
            {"lang": "de", "style": "colloquial", "text": "Füge bitte die ReleaseNotes ('VINOX 1.0 released with hybrid search.') zur Vektordatenbank hinzu."},
            {"lang": "en", "style": "formal", "text": "Ingest a document titled 'ReleaseNotes' with the text 'VINOX 1.0 released with hybrid search.' into the database."},
            {"lang": "en", "style": "short", "text": "Ingest doc ReleaseNotes: 'VINOX 1.0 released with hybrid search.'"}
        ],
        "hard_negatives": [
            {"type": "markdown_json", "content": "```json\n{\"title\": \"ReleaseNotes\", \"content\": \"VINOX 1.0 released with hybrid search.\"}\n```"}
        ]
    },
    # 19. vinox.document_ingest - NPU Guide
    {
        "intent_id": "retrieval.ingest.npu_guide",
        "tool": "vinox.document_ingest",
        "arguments": {"title": "NPU_Guide", "content": "Intel AI Boost NPU accelerates INT4, INT8, FP16 and Mixed Precision models."},
        "think": "Ich indexiere den Leitfaden 'NPU_Guide' mit dem Inhalt 'Intel AI Boost NPU accelerates INT4, INT8, FP16 and Mixed Precision models.' über 'vinox.document_ingest'.",
        "prompts": [
            {"lang": "de", "style": "formal", "text": "Speichere ein Dokument namens 'NPU_Guide' mit dem Text 'Intel AI Boost NPU accelerates INT4, INT8, FP16 and Mixed Precision models.' in der Knowledge Base."},
            {"lang": "en", "style": "formal", "text": "Ingest the guide titled 'NPU_Guide' containing 'Intel AI Boost NPU accelerates INT4, INT8, FP16 and Mixed Precision models.'."}
        ],
        "hard_negatives": [
            {"type": "wrong_name", "call": {"name": "ingest_document", "arguments": "{\"name\": \"NPU_Guide\"}"}}
        ]
    }
]

TOOLS_SCHEMA = [
    {
        "type": "function",
        "function": {
            "name": "system.time",
            "description": "Gibt die aktuelle Systemzeit und das Datum zurueck.",
            "parameters": {
                "type": "object",
                "properties": {
                    "timezone": {"type": "string", "description": "Die Zeitzone, z.B. Europe/Berlin oder UTC"}
                },
                "required": ["timezone"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "math.calculate",
            "description": "Fuehrt mathematische Berechnungen durch und wertet arithmetische Ausdruecke aus.",
            "parameters": {
                "type": "object",
                "properties": {
                    "expression": {"type": "string", "description": "Der mathematische Ausdruck, z.B. '(24 * 15) / 3' oder 'sqrt(144) + 12'"}
                },
                "required": ["expression"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "fs.read",
            "description": "Read file content within workspace sandbox",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to the file to read"}
                },
                "required": ["path"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "fs.list",
            "description": "List directory entries within workspace sandbox",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to directory to list, e.g. '.' or 'src'"}
                }
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "fs.write",
            "description": "Write file content within workspace sandbox",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to destination file"},
                    "content": {"type": "string", "description": "Text content to write into the file"}
                },
                "required": ["path", "content"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "vinox.search",
            "description": "VINOX Hybrid Retrieval (BM25 + sqlite-vec Vector Search) across indexed documents",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "The natural language query or keywords to search for"},
                    "limit": {"type": "integer", "description": "Maximum number of documents to return"}
                },
                "required": ["query"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "vinox.document_ingest",
            "description": "Ingest a new text document into VINOX storage and vector index",
            "parameters": {
                "type": "object",
                "properties": {
                    "title": {"type": "string", "description": "Title of the document"},
                    "content": {"type": "string", "description": "Text body of the document to ingest"}
                },
                "required": ["title", "content"]
            }
        }
    }
]

SYSTEM_PROMPT = "You are a helpful assistant with access to tools. Always begin your response by thinking briefly inside <think>...</think> tags about which tool to call and which arguments are required. After closing </think>, invoke the appropriate tool."

def build_corpus():
    out_dir = Path("trainingdata/canonical")
    out_dir.mkdir(parents=True, exist_ok=True)

    intents_file = out_dir / "canonical_intents.jsonl"
    sft_file = out_dir / "multilingual_sft_dataset.jsonl"
    dpo_file = out_dir / "dpo_preference_dataset.jsonl"

    sft_records = []
    dpo_records = []
    intent_index = []

    for item in CANONICAL_INTENTS:
        intent_id = item["intent_id"]
        tool_name = item["tool"]
        tool_args = item["arguments"]
        think_text = item["think"]

        intent_index.append({
            "intent_id": intent_id,
            "tool": tool_name,
            "arguments": tool_args,
            "prompts_count": len(item["prompts"]),
            "hard_negatives_count": len(item.get("hard_negatives", []))
        })

        # Generate SFT samples for all linguistic variants
        for p in item["prompts"]:
            call_id = f"call_{uuid.uuid4().hex[:16]}"
            sample_id = f"sft_{intent_id}_{p['lang']}_{p['style']}_{uuid.uuid4().hex[:6]}"
            
            sft_entry = {
                "id": sample_id,
                "intent_id": intent_id,
                "lang": p["lang"],
                "style": p["style"],
                "tool": tool_name,
                "tools": TOOLS_SCHEMA,
                "messages": [
                    {"role": "system", "content": SYSTEM_PROMPT},
                    {"role": "user", "content": p["text"]},
                    {
                        "role": "assistant",
                        "reasoning_content": think_text,
                        "content": None,
                        "tool_calls": [
                            {
                                "id": call_id,
                                "type": "function",
                                "function": {
                                    "name": tool_name,
                                    "arguments": json.dumps(tool_args, ensure_ascii=False)
                                }
                            }
                        ]
                    }
                ]
            }
            sft_records.append(sft_entry)

            # Generate DPO / Preference pairs if hard negatives exist
            for idx, neg in enumerate(item.get("hard_negatives", [])):
                dpo_id = f"dpo_{intent_id}_{p['lang']}_{idx}_{uuid.uuid4().hex[:6]}"
                
                chosen_assistant = {
                    "reasoning_content": think_text,
                    "content": None,
                    "tool_calls": [
                        {
                            "id": call_id,
                            "type": "function",
                            "function": {
                                "name": tool_name,
                                "arguments": json.dumps(tool_args, ensure_ascii=False)
                            }
                        }
                    ]
                }
                
                rejected_assistant = {}
                if "call" in neg:
                    rejected_assistant = {
                        "reasoning_content": think_text,
                        "content": None,
                        "tool_calls": [
                            {
                                "id": call_id,
                                "type": "function",
                                "function": neg["call"]
                            }
                        ]
                    }
                else:
                    rejected_assistant = {
                        "reasoning_content": think_text,
                        "content": neg["content"],
                        "tool_calls": None
                    }

                dpo_entry = {
                    "id": dpo_id,
                    "intent_id": intent_id,
                    "negative_type": neg.get("type", "rejected"),
                    "prompt": p["text"],
                    "system": SYSTEM_PROMPT,
                    "tools": TOOLS_SCHEMA,
                    "chosen": chosen_assistant,
                    "rejected": rejected_assistant
                }
                dpo_records.append(dpo_entry)

    # Write files
    with open(intents_file, "w", encoding="utf-8") as f:
        for it in intent_index:
            f.write(json.dumps(it, ensure_ascii=False) + "\n")

    with open(sft_file, "w", encoding="utf-8") as f:
        for rec in sft_records:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")

    with open(dpo_file, "w", encoding="utf-8") as f:
        for rec in dpo_records:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")

    print(f"Kanonischer Korpus erfolgreich generiert:")
    print(f"- Intents: {len(intent_index)} in {intents_file}")
    print(f"- SFT Samples (multilingual): {len(sft_records)} in {sft_file}")
    print(f"- DPO Preference Pairs: {len(dpo_records)} in {dpo_file}")

if __name__ == "__main__":
    build_corpus()
