#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VINOX Realistic Sandbox Seeder
Erstellt eine realistische Entwicklungsumgebung im Ordner 'sandbox/' für
sichere Tool-Execution-Tests (fs.read, fs.list, fs.write) und Agent-Training.
"""

from pathlib import Path
import json

def seed_sandbox():
    base = Path("sandbox")
    base.mkdir(parents=True, exist_ok=True)

    # 1. Subdirectories
    (base / "config").mkdir(parents=True, exist_ok=True)
    (base / "src" / "utils").mkdir(parents=True, exist_ok=True)
    (base / "docs").mkdir(parents=True, exist_ok=True)
    (base / "data").mkdir(parents=True, exist_ok=True)
    (base / "logs").mkdir(parents=True, exist_ok=True)

    # 2. sandbox/README.md
    (base / "README.md").write_text("""# VINOX Isolated Sandbox Workspace

Dieses Verzeichnis dient als isolierte und deterministische Sandbox für autonome VINOX-Agenten.
Hier können Datei-Operationen (`fs.read`, `fs.list`, `fs.write`) sicher und ohne Einfluss auf das Projekt-Root ausgeführt werden.

## Verzeichnisstruktur
- `config/`: Konfigurationsdateien im JSON- und YAML-Format
- `src/`: Python-Microservice Quellcode und Hilfsmodule
- `docs/`: Architekturübersicht und API-Spezifikationen
- `data/`: CSV- und JSON-Messdaten / Beispieldatensätze
- `logs/`: Protokolldateien für Session- und Fehleranalysen
""", encoding="utf-8")

    # 3. sandbox/config/app_settings.json
    app_settings = {
        "service_name": "vinox-sandbox-agent",
        "environment": "staging",
        "debug": True,
        "api_gateway": {
            "bind_host": "127.0.0.1",
            "port": 9090,
            "ssl_enabled": False
        },
        "rate_limiting": {
            "requests_per_minute": 120,
            "burst_limit": 30
        },
        "features": {
            "hybrid_retrieval": True,
            "npu_offloading": True,
            "sandbox_quarantine": True
        }
    }
    (base / "config" / "app_settings.json").write_text(json.dumps(app_settings, indent=2), encoding="utf-8")

    # 4. sandbox/config/database.yaml
    (base / "config" / "database.yaml").write_text("""# Database Configuration for VINOX Sandbox
connection:
  driver: sqlite3
  database_file: "sandbox/data/agent_state.db"
  timeout_seconds: 30
  journal_mode: "WAL"

pool:
  min_connections: 2
  max_connections: 10
  idle_timeout_seconds: 300

security:
  read_only_mode: false
  enforce_foreign_keys: true
""", encoding="utf-8")

    # 5. sandbox/src/main.py
    (base / "src" / "main.py").write_text('''#!/usr/bin/env python3
"""
VINOX Sandbox Microservice
Demonstriert einen einfachen API-Endpunkt mit Health-Check und Rechenoperationen.
"""

import sys
from utils.math_helpers import calculate_growth_rate, compute_hypotenuse

def health_check():
    return {"status": "healthy", "service": "vinox-sandbox-worker", "version": "1.0.0"}

def main():
    print("Starte VINOX Sandbox Worker...")
    status = health_check()
    print(f"Status: {status}")
    
    # Beispielberechnungen
    hyp = compute_hypotenuse(12, 16)
    print(f"Test-Hypotenuse (12, 16): {hyp}")
    
    growth = calculate_growth_rate(1000, 1500, 5)
    print(f"Wachstumsrate: {growth:.2f}%")

if __name__ == "__main__":
    main()
''', encoding="utf-8")

    # 6. sandbox/src/utils/math_helpers.py
    (base / "src" / "utils" / "math_helpers.py").write_text('''"""
Mathematische Hilfsfunktionen für den VINOX Sandbox Microservice.
"""

import math

def compute_hypotenuse(a: float, b: float) -> float:
    """Berechnet die Hypotenuse nach dem Satz des Pythagoras."""
    return math.sqrt(a**2 + b**2)

def calculate_growth_rate(start_val: float, end_val: float, periods: int) -> float:
    """Berechnet die durchschnittliche Wachstumsrate pro Periode in Prozent."""
    if start_val <= 0 or periods <= 0:
        return 0.0
    return ((end_val / start_val) ** (1.0 / periods) - 1.0) * 100.0

def kinetic_energy(mass_kg: float, velocity_ms: float) -> float:
    """Berechnet die kinetische Energie E_k = 0.5 * m * v^2 in Joule."""
    return 0.5 * mass_kg * (velocity_ms ** 2)
''', encoding="utf-8")

    # 7. sandbox/src/utils/file_parser.py
    (base / "src" / "utils" / "file_parser.py").write_text('''"""
Sichere Datei-Parsing-Funktionen innerhalb der Sandbox.
"""

import json
from pathlib import Path

def parse_json_safely(file_path: str):
    p = Path(file_path)
    if not p.exists():
        raise FileNotFoundError(f"Datei {file_path} existiert nicht.")
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)
''', encoding="utf-8")

    # 8. sandbox/docs/system_overview.md
    (base / "docs" / "system_overview.md").write_text("""# Systemübersicht: VINOX Agent Pipeline

Die VINOX Agent Pipeline kombiniert lokale OpenVINO-Inferenz mit deterministischen Werkzeugen.

## Architekturkomponenten
1. **Inference Engine**: Führt quantisierte Modelle (INT4 / FP16) auf Intel NPU / CPU aus.
2. **C-ABI Plugin Host**: Lädt dynamische DLLs (`vinox_plugin_*.dll`) zur Laufzeit.
3. **Sandbox Controller**: Schirmt Dateioperationen (`fs.read`, `fs.write`, `fs.list`) gegen Sandbox-Escapes ab.
4. **Hybrid Retrieval**: Kombiniert FTS5-Volltextsuche mit 1024-dim Vektoren (`sqlite-vec`).
""", encoding="utf-8")

    # 9. sandbox/docs/api_spec.json
    api_spec = {
        "openapi": "3.1.0",
        "info": {
            "title": "VINOX Sandbox Service API",
            "version": "1.0.0"
        },
        "paths": {
            "/v1/health": {
                "get": {
                    "summary": "Health status probe",
                    "responses": {"200": {"description": "Service is operational"}}
                }
            },
            "/v1/calculate": {
                "post": {
                    "summary": "Execute bounded math expression",
                    "parameters": ["expression"]
                }
            }
        }
    }
    (base / "docs" / "api_spec.json").write_text(json.dumps(api_spec, indent=2), encoding="utf-8")

    # 10. sandbox/data/sales_metrics.csv
    (base / "data" / "sales_metrics.csv").write_text("""quarter,revenue_eur,growth_percent,active_users
2025-Q1,125000,12.5,1420
2025-Q2,148000,18.4,1890
2025-Q3,172000,16.2,2310
2025-Q4,210000,22.1,2950
2026-Q1,245000,16.7,3400
""", encoding="utf-8")

    # 11. sandbox/data/benchmark_npu.json
    npu_bench = {
        "device": "Intel AI Boost NPU",
        "model": "Qwen2.5-1.5B-Instruct-int4-ov",
        "tokens_per_second": 48.6,
        "first_token_latency_ms": 112.4,
        "power_consumption_watts": 8.5,
        "test_timestamp": "2026-09-06T18:40:00Z"
    }
    (base / "data" / "benchmark_npu.json").write_text(json.dumps(npu_bench, indent=2), encoding="utf-8")

    # 12. sandbox/logs/agent_session.log
    (base / "logs" / "agent_session.log").write_text("""[2026-09-06T18:30:10Z] [INFO] Agent runtime initialized on Intel NPU.
[2026-09-06T18:30:12Z] [INFO] Dynamic plugins loaded: time, math, fs, retrieval.
[2026-09-06T18:30:15Z] [DEBUG] Workspace sandbox bounded to root: 'sandbox/'.
[2026-09-06T18:32:00Z] [INFO] Tool call executed: fs.read {'path': 'sandbox/config/app_settings.json'} -> 200 OK.
[2026-09-06T18:35:40Z] [INFO] Session audit completed. Integrity check passed.
""", encoding="utf-8")

    # 13. sandbox/notes.txt
    (base / "notes.txt").write_text("""VINOX Sandbox Scratchpad
- TODO: Check Intel AI Boost NPU driver update
- TODO: Benchmark sqlite-vec hybrid retrieval on 50k documents
- NOTE: All fs.* operations within this folder are sandboxed.
""", encoding="utf-8")

    print(f"Sandbox erfolgreich initialisiert in '{base.resolve()}':")
    count = 0
    for p in base.rglob("*"):
        if p.is_file():
            count += 1
            print(f"  - {p.relative_to(base.parent)}")
    print(f"Gesamt: {count} reelle Dateien angelegt.")

if __name__ == "__main__":
    seed_sandbox()
