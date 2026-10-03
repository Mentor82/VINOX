# Systemübersicht: VINOX Agent Pipeline

Die VINOX Agent Pipeline kombiniert lokale OpenVINO-Inferenz mit deterministischen Werkzeugen.

## Architekturkomponenten
1. **Inference Engine**: Führt quantisierte Modelle (INT4 / FP16) auf Intel NPU / CPU aus.
2. **C-ABI Plugin Host**: Lädt dynamische DLLs (`vinox_plugin_*.dll`) zur Laufzeit.
3. **Sandbox Controller**: Schirmt Dateioperationen (`fs.read`, `fs.write`, `fs.list`) gegen Sandbox-Escapes ab.
4. **Hybrid Retrieval**: Kombiniert FTS5-Volltextsuche mit 1024-dim Vektoren (`sqlite-vec`).
