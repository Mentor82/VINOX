# VINOX Isolated Sandbox Workspace

Dieses Verzeichnis dient als isolierte und deterministische Sandbox für autonome VINOX-Agenten.
Hier können Datei-Operationen (`fs.read`, `fs.list`, `fs.write`) sicher und ohne Einfluss auf das Projekt-Root ausgeführt werden.

## Verzeichnisstruktur
- `config/`: Konfigurationsdateien im JSON- und YAML-Format
- `src/`: Python-Microservice Quellcode und Hilfsmodule
- `docs/`: Architekturübersicht und API-Spezifikationen
- `data/`: CSV- und JSON-Messdaten / Beispieldatensätze
- `logs/`: Protokolldateien für Session- und Fehleranalysen
