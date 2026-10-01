"""
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
