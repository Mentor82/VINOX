# VINOX - Versatile Inference & Native OpenVINO eXecution

> **Tagline:** Standalone C++ GenAI infrastructure powered by OpenVINO.

## Projektplan

## 1. Ziel

Entwicklung einer modularen Chat-Plattform in C++20 auf Basis von OpenVINO und
OpenVINO GenAI. Die Plattform stellt dieselbe Inferenzlogik ueber vier Wege zur
Verfuegung:

- stabile, wiederverwendbare DLL-Schnittstelle
- OpenAI-kompatibler HTTP-Server mit OpenAPI-3.1-Beschreibung
- interaktive CLI nach dem Vorbild von llama.cpp
- native Desktop-GUI

Die Inferenzlogik liegt in Bibliotheken und wird nicht in den Anwendungen
dupliziert. Der erste Zielhost ist Windows; portable Kernkomponenten sollen auch
unter Linux baubar bleiben.

## 2. Architektur

```text
CLI / GUI / Server / externe Apps
                     |
                     v
          C++ Wrapper / C-ABI DLL
                     |
                     v
         Chat Core / Mode Controller
             |          |          |
             v          v          v
         Serving    Storage    Tools / MCP
             |          |          |
             v          v          v
        OpenVINO     SQLite   Sandbox Worker
```

### Architekturregeln

- EXE-Dateien enthalten nur Anwendungscode und verwenden dieselben DLLs.
- Die oeffentliche ABI verwendet C-Typen, opaque Handles und Fehlercodes.
- STL-Typen und Exceptions ueberschreiten keine DLL-Grenze.
- Token-Streaming erfolgt ueber Callbacks und ist abbrechbar.
- Backend, Protokoll und Benutzeroberflaechen bleiben getrennte Module.
- Persistenz und Retrieval liegen hinter austauschbaren Repository-Schnittstellen.
- Chat, Plan und Agent besitzen unveraenderliche, technisch erzwungene Policies.
- Toolausfuehrung im Agent-Modus erfolgt ausserhalb des Hostprozesses.
- Konfiguration und Fehlerbehandlung sind fuer CLI, Server und GUI konsistent.
- Build und Laufzeit verwenden ausschliesslich versionierte Abhaengigkeiten aus
    dem Buildbaum beziehungsweise dem erzeugten Standalone-Stage.
- Eine installierte Qt-, OpenVINO-, vcpkg- oder Modellumgebung ist zur Laufzeit
    nicht erforderlich und wird nicht als stiller Fallback verwendet.

## 3. Vorgesehene Projektstruktur

```text
openvino-chat/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg.json
|-- vcpkg-configuration.json
|-- cmake/
|   |-- dependencies/
|   |-- install/
|   `-- packaging/
|-- include/
|   `-- vinox/
|       |-- vinox.h
|       `-- vinox.hpp
|-- src/
|   |-- core/
|   |-- api/
|   |-- model_registry/
|   |-- serving/
|   |-- backends/openvino/
|   |-- storage/sqlite/
|   |-- retrieval/
|   |-- tools/
|   |-- mcp/
|   |-- agent/
|   |-- sandbox/
|   |-- server/
|   |-- cli/
|   `-- gui/
|-- schemas/
|   |-- openapi.yaml
|   |-- model-manifest.schema.json
|   |-- agent-plan.schema.json
|   |-- agent-profile.schema.json
|   `-- database/
|-- tests/
|   |-- unit/
|   |-- integration/
|   `-- api/
|-- examples/
|-- docs/
|-- models/
|-- scripts/
`-- out/
    `-- <preset>/
        |-- build/
        |-- vcpkg_installed/
        `-- stage/
```

## 4. Auslieferungsartefakte

- `vinox_core.dll`: Sessions, Promptaufbau und Generationssteuerung
- `vinox_openvino.dll`: OpenVINO-/OpenVINO-GenAI-Backend
- `vinox_serving.dll`: Modellregistry, Scheduler und Request-Lifecycle
- `vinox_storage.dll`: SQLite-Persistenz, Migrationen und Retrieval
- `vinox_tools.dll`: Tool Registry, Schema-Validierung und Ausfuehrungspolicy
- `vinox_mcp.dll`: MCP-Client, Transports und Protokolladapter
- `vinox_mcp_server.exe`: eigener MCP-Server fuer freigegebene vinox-Funktionen
- `vinox_agent.dll`: Mode Controller, Planmodell und Agent-Orchestrierung
- `vinox_sandbox_worker.exe`: isolierter Prozess fuer Agent-Aktionen
- `vinox-server.exe`: REST- und SSE-Server
- `vinox-cli.exe`: lokale oder entfernte Kommandozeilenanwendung
- `vinox-gui.exe`: native Qt-Desktopanwendung
- `vinox.h`: versionierte C-API
- `vinox.hpp`: RAII-basierter C++-Wrapper
- OpenAPI-Schema, Beispiele und CMake-Paketdateien

Statische Bibliotheken koennen spaeter optional angeboten werden. Die DLLs sind
der verbindliche Integrationsweg fuer die erste Version.

### Standalone-Build und Laufzeitlayout

`out/<preset>/stage` ist die kanonische Installations- und Testwurzel. Alle EXEs
werden nur aus diesem Verzeichnis gestartet und das Release-Paket wird ohne
manuelle Umbauten direkt daraus erzeugt:

```text
stage/
|-- bin/                     EXEs, vinox-DLLs und redistributable Runtime-DLLs
|-- lib/                     Importbibliotheken und CMake-Paket fuer Entwickler
|-- include/vinox/          oeffentliche C- und C++-Header
|-- plugins/
|   |-- openvino/            OpenVINO-Plugins und zugehoerige Konfiguration
|   |-- qt/                  Qt-Plattform-, TLS- und weitere benoetigte Plugins
|   `-- vinox/              optionale vinox-Backend-Plugins
|-- qml/                     ausschliesslich verwendete Qt-QML-Module
|-- share/vinox/
|   |-- schemas/
|   |-- migrations/
|   |-- prompts/
|   `-- config/
|-- models/                  optionale, lizenzgepruefte Modell-Bundles
|-- licenses/                Projekt- und Drittanbieter-Lizenztexte
|-- qt.conf                  relative Qt-Plugin- und QML-Pfade
|-- THIRD_PARTY_NOTICES.txt
`-- sbom.spdx.json
```

CMake bezieht C++-Abhaengigkeiten reproduzierbar ueber den vcpkg Manifest Mode
mit festem Baseline-Commit. Heruntergeladene Quellen und Pakete liegen im
Build-/Cache-Bereich; benoetigte Laufzeitdateien werden durch
`cmake --install` komponentengenau in `stage` kopiert. Absolute Entwicklerpfade
duerfen weder in Binaries noch Konfigurationen oder CMake-Exporten verbleiben.
OpenVINO und Qt werden als explizit versionierte SDK-Eingaben importiert; nur die
tatsaechlich benoetigten und lizenzrechtlich erlaubten Runtime-Dateien werden
gestaged. Ein Manifest mit SHA-256 prueft jede gebuendelte Datei.

Unter Windows setzt der Launcher sichere DLL-Suchregeln und registriert nur
relative Verzeichnisse unterhalb von `stage`; eine Suche ueber aktuelles
Arbeitsverzeichnis oder globales `PATH` ist gesperrt. Qt verwendet `qt.conf`,
lokale Pluginpfade und lokale QML-Imports. OpenVINO erhaelt seinen Pluginpfad aus
dem Stage-Manifest. Unter Linux verwenden Binaries relative RPATHs auf Basis von
`$ORIGIN`. Der Prozess bricht mit einer klaren Diagnose ab, wenn eine deklarierte
Datei fehlt oder von ausserhalb der Stage-Wurzel geladen werden soll.

Compiler, CMake, Ninja, Windows SDK und Debugger sind Buildwerkzeuge und nicht
Bestandteil des Standalone-Pakets. Modelle koennen wegen Groesse und separater
Lizenz als eigenes, aber gleich strukturiertes Bundle installiert werden. Ein
vollstaendiges Offline-Paket enthaelt mindestens ein freigegebenes Chatmodell;
ohne Modellbundle startet die Anwendung weiterhin, meldet aber `no model
installed`, statt globale Modellpfade zu durchsuchen.

## 5. Technische Basis

- C++20
- CMake mit CMake Presets
- vcpkg im Manifest Mode
- OpenVINO Runtime und OpenVINO GenAI 2026.2.1
- SQLite mit Foreign Keys, WAL und FTS5
- `sqlite-vec` als primaerer lokaler Vektorindex, hinter einer eigenen Schnittstelle
- Qt 6.10.1 mit Qt Quick/QML fuer die GUI
- `nlohmann/json` fuer JSON
- JSON Schema fuer Toolargumente, Ergebnisse und strukturierte Modellantworten
- `spdlog` / vinox_logging fuer strukturierte Protokollierung
- Strukturierte Events, Korrelations-Propagation und strikte No-Content/No-Secret Privacy Policy (dokumentiert in [`docs/architecture/logging-audit-telemetry-contract.md`](docs/architecture/logging-audit-telemetry-contract.md))
- Catch2 oder GoogleTest fuer Tests
- Doxygen und Markdown fuer API- und Entwicklerdokumentation
- HTTP-Bibliothek mit robustem SSE- und Abbruch-Support, Auswahl nach Spike

### Logging, Audit & Telemetry Contract

VINOX unterscheidet vier getrennte Beobachtbarkeits-Konzepte:

1. **Operational Logging:** Diagnose- und Laufzeit-Ereignisse (best-effort, fail-safe).
2. **Audit Evidence:** Dauerhafte, nachvollziehbare Evidenz fuer sicherheits- und governance-relevante Aktionen (Tools, MCP, Sandbox, Agent).
3. **Metrics / Telemetry:** Aggregierbare Messwerte (Request-Count, Latenz, TTFT, TPOT, Durchsatz) unabhaengig von Log-Prosa.
4. **Active Audit Verification (`vinox-cli --audit`):** Reale System-Architektur-Pruefung.

**Kanonische Regeln:**
- Versionierter strukturierter Event-Envelope (`event_schema_version: 1`) mit stabilen Event-IDs.
- Default No-Content & No-Secret Policy: Prompts, Antworten, API-Keys, Bearer-Tokens und Passwoerter werden NIEMALS standardmaessig geloggt.
- Zentrale Redaktions-Engine fuer Log-Sinks und C-ABI `last_error()` Diagnosemeldungen.
- Ende-zu-Ende Korrelations-Propagation (`request_id`, `session_id`, `run_id`) ueber DLL- und Prozess-Grenzen.
- Detaillierter Vertrag dokumentiert in [`docs/architecture/logging-audit-telemetry-contract.md`](docs/architecture/logging-audit-telemetry-contract.md).

### Lizenztyp und Distribution

Der originale Projektcode, die C-/C++-Header, CMake-Dateien, Schemas, Beispiele
und Dokumentation werden unter der **Apache License 2.0** veroeffentlicht
(`SPDX-License-Identifier: Apache-2.0`). Sie passt zum OpenVINO-Oekosystem,
erlaubt private und kommerzielle Nutzung und enthaelt im Gegensatz zur MIT-Lizenz
eine ausdrueckliche Patentlizenz. Beitraege werden ohne separate CLA unter
denselben Bedingungen angenommen, sofern spaeter nichts anderes festgelegt wird.

Die Projektlizenz ersetzt keine Drittanbieter- oder Modelllizenz. Fuer jede
Release-Version werden deshalb `LICENSE`, `NOTICE` und
`THIRD_PARTY_NOTICES.txt` erzeugt und zusammen mit einer maschinenlesbaren SBOM
ausgeliefert. Versions-, Quell-, Lizenz- und Hashangaben werden aus dem
Abhaengigkeits-Lockfile und den Modellmanifesten abgeleitet.

Aktuelle Einordnung:

- OpenVINO- und OpenVINO-GenAI-Quellcode beziehungsweise Header: Apache-2.0.
- Die lokal installierten OpenVINO-Runtime-Binaries unter `runtime/bin/*` stehen
    laut mitgelieferter Dokumentation unter der Intel OpenVINO Distribution
    License. Nur die in `docs/licensing/redist.txt` genannten Dateien werden
    zusammen mit EULA, Redistributables-Liste und Third-Party Notices verteilt.
- Qt Quick/QML: dynamische Verknuepfung unter LGPL-3.0 oder eine kommerzielle
    Qt-Lizenz. Bei LGPL-Nutzung bleiben Qt-DLLs austauschbar; Lizenztext,
    Copyright-Hinweise, Qt-Quellcode-Angebot beziehungsweise Bezugsweg und die
    Erlaubnis zum Reverse Engineering fuer das Debugging eigener Qt-Aenderungen
    werden dokumentiert. LGPL verlangt bei sauberer dynamischer Verknuepfung nicht
    die Offenlegung des Apache-lizenzierten Anwendungscodes. Statische Qt-Builds
    sind ohne kommerzielle Lizenz oder vorab geprueften Relinking-Prozess gesperrt.
- SQLite ist Public Domain; `sqlite-vec` wird in der gewaehlten MIT- oder
    Apache-2.0-Variante dokumentiert. `nlohmann/json` und `spdlog` sind MIT,
    Catch2 ist BSL-1.0 und GoogleTest BSD-3-Clause.
- Die GitHub-Projekte aus der Multiagent-Recherche sind Konzeptquellen und keine
    Laufzeitabhaengigkeiten. Ihre Lizenztexte werden nur aufgenommen, falls
    spaeter tatsaechlich Code oder Assets uebernommen werden.
- Modellgewichte, Tokenizer und Modellkarten sind separate Werke und werden nie
    durch die Apache-2.0-Projektlizenz abgedeckt. Das lokale
    `Qwen3-Embedding-0.6B-fp16-ov` weist Apache-2.0 in seiner Model Card aus. Dem
    lokalen `Qwen2.5-1B-Instruct-fp16-test-ov` fehlen Model Card und Lizenzdatei;
    es ist daher ein reines Entwicklungsmodell und darf bis zum Nachweis von
    Originalmodell, Konvertierung, Lizenz und Hash nicht weiterverteilt werden.

Neue Produktionsabhaengigkeiten muessen eine dokumentierte Lizenzpruefung
bestehen. GPL-/AGPL-Abhaengigkeiten, GPL-only-Qt-Module und nicht eindeutig
lizenzierte Assets oder Modelle sind standardmaessig gesperrt. Die noch offene
HTTP- und MCP-Bibliotheksauswahl bevorzugt Apache-2.0, MIT, BSD oder BSL.

### Festgelegte lokale Entwicklungsumgebung

#### Toolchain-Bestand

| Komponente | Gefunden | Verwendung |
|---|---|---|
| Visual Studio Community 2026 | 18.8.3, MSVC 19.51 / Toolset 14.51 | verfuegbarer Fallback, zunaechst nicht Referenztoolchain |
| Visual Studio Build Tools 2022 | 17.14.38, Toolset 14.44 | primaerer C++20-Compiler fuer Windows x64 |
| Kitware CMake | 4.2.1 | primaeres CMake |
| Qt CMake | 3.30.5 | verfuegbarer Fallback |
| Strawberry CMake | 3.29.2 | nicht fuer dieses Projekt verwenden |
| Qt Ninja | 1.12.1 | primaerer Build Runner |
| Qt | 6.10.1 `mingw_64` | installiert, aber nicht ABI-kompatibel mit den OpenVINO-MSVC-Bibliotheken |
| Qt MinGW | GCC 13.1.0 | nur fuer das vorhandene Qt-MinGW-Kit, nicht fuer OpenVINO-Ziele |

Die Windows-Referenztoolchain ist **MSVC 2022 x64 + Ninja + Kitware CMake**.
OpenVINO liefert `openvino.lib`, `openvino_genai.lib` und zugehoerige
MSVC-Importbibliotheken. Das vorhandene Qt-Kit ist dagegen mit `win32-g++`
gebaut. Diese ABIs werden nicht in einem Prozess gemischt.

Fuer den lokalen GUI-Build wird daher ueber den Qt Maintenance Tool noch das zu
Qt 6.10.1 gehoerende Desktop-Kit `msvc2022_64` installiert. Danach zeigt
`Qt6_DIR` auf:

```text
C:\Qt\6.10.1\msvc2022_64\lib\cmake\Qt6
```

Core, DLL, CLI, Server und GUI werden anschliessend mit demselben MSVC-2022-Preset
gebaut. Visual Studio 2026 bleibt eine spaetere, getrennt zu validierende
Toolchain. Das auf `PATH` zuerst gefundene Strawberry-CMake und Strawberry-GCC
werden durch absolute Toolpfade oder die Preset-Umgebung ausgeschlossen.

#### SDK und Modelle

```text
SDK:
C:\ai\openvino_genai_2026.2.1\openvino_genai_windows_2026.2.1.0_x86_64

Modelle:
C:\ai\models\OpenVINO

Primaeres Referenzmodell:
C:\ai\models\OpenVINO\Qwen2.5-1B-Instruct-fp16-test-ov

Embedding-Referenzmodell:
C:\ai\models\OpenVINO\Qwen3-Embedding-0.6B-fp16-ov
```

Das SDK meldet die Version `2026.2.1-21919-ede283a88e3-releases/2026/2`.
Sein CMake-Paket liegt unter `runtime\cmake` und stellt das bestaetigte Target
`openvino::genai` bereit. Vor einem manuellen Build kann die Umgebung so geladen
werden:

```powershell
& "C:\ai\openvino_genai_2026.2.1\openvino_genai_windows_2026.2.1.0_x86_64\setupvars.ps1"
```

Alternativ werden beim Konfigurieren mindestens diese CMake-Variablen gesetzt:

```powershell
cmake --preset windows-msvc `
    -DOpenVINO_DIR="C:\ai\openvino_genai_2026.2.1\openvino_genai_windows_2026.2.1.0_x86_64\runtime\cmake" `
    -DOpenVINOGenAI_DIR="C:\ai\openvino_genai_2026.2.1\openvino_genai_windows_2026.2.1.0_x86_64\runtime\cmake" `
    -DVINOX_MODELS_DIR="C:\ai\models\OpenVINO"
```

Absolute lokale Pfade werden nicht in C++-Quellcode festgeschrieben. Sie gelangen
ueber Umgebungsvariablen, CMake-Cache-Variablen oder ein lokales User Preset in
den Build. Ein eincheckbares Preset enthaelt nur portable Standardwerte.

Das Referenzmodell enthaelt OpenVINO-IR, Tokenizer, Detokenizer,
Generationskonfiguration und `chat_template.jinja`. Als Instruct-Modell eignet es
sich besser fuer Chat und strukturierte JSON-Ausgaben als das kleinere
Coder-Modell. Es wird fuer den ersten Text-, JSON- und Streaming-Test verwendet.
Der vorhandene Ordner
`Llama-3.2-1B-fp16-ov` ist aktuell leer und wird nicht als Testmodell verwendet.

Das Embedding-Modell ist mit OpenVINO 2026 kompatibel und erzeugt Vektoren mit
Dimension 1024. Queries erhalten eine kurze Retrieval-Instruktion, Dokumente und
Chattexte nicht. Verwendet werden Last-Token-Pooling anhand der Attention Mask
und anschliessende L2-Normalisierung. Modell-ID, Dimension, Pooling und
Normalisierung werden mit jedem Embedding gespeichert, damit ein Modellwechsel
gezielt neu indiziert werden kann.

## 6. DLL-API

Die erste C-ABI deckt mindestens folgende Funktionen ab:

- Runtime initialisieren und herunterfahren
- verfuegbare OpenVINO-Geraete abfragen
- Modell laden und entladen
- Chat-Session erzeugen, zuruecksetzen und zerstoeren
- Nachrichten und System-Prompt setzen
- Generationsparameter setzen
- synchrone Generierung starten
- Tokens per Callback streamen
- laufende Generierung abbrechen
- Fehlertext, Tokenanzahl und Laufzeitstatistiken abfragen

Die ABI wird explizit versioniert. Speicher wird immer von dem Modul freigegeben,
das ihn alloziert hat.

## 7. Modellverwaltung und Serving

Dieser Bereich uebernimmt bewaehrte Konzepte aus llama.cpp, Ollama und vLLM,
bleibt aber an die tatsaechlichen OpenVINO-GenAI-Faehigkeiten gebunden.

### Modellmanifest und Registry

Jedes Modell erhaelt ein validiertes `model-manifest.json`. Es enthaelt:

- stabile Modell-ID, Anzeigename und optionale Aliasse
- lokalen Pfad, Modelltyp und SHA-256-Pruefsummen
- Quelle, Lizenz und Konvertierungsinformationen
- Tokenizer- und Chat-Template-Pfad samt Hash
- Kontextlaenge und Standard-Generationsparameter
- Standardgeraet und erlaubte OpenVINO-Plugin-Properties
- Faehigkeiten wie `chat`, `structured_output`, `tools`, `embeddings` oder `vision`

Die Registry scannt nur konfigurierte Modellwurzeln und vertraut weder Manifest
noch Modellpfad ungeprueft. Zustaende sind `unloaded`, `loading`, `ready`,
`draining` und `error`. Der MVP unterstuetzt ein aktives Chatmodell plus ein
Embedding-Modell, Lazy Load, Warmup, konfigurierbares `keep_alive` und explizites
Entladen. Vollstaendiges Multi-Model-Serving folgt spaeter.

Ein Ollama-artiger Remote-Pull ist nicht Teil des MVP. Der erste Import ist lokal
und prueft Pfad, Manifest, Hash und Lizenzhinweis. Remote-Downloads benoetigen
spaeter eine eigene Supply-Chain-Policy, fortsetzbare Downloads und
Signaturpruefung.

### Tokenizer, Chat Template und Kontextbudget

- Tokenanzahl wird vor Admission mit dem Tokenizer des geladenen Modells bestimmt.
- Systemtext, Verlauf, Toolschemas, Retrieval-Kontext und Antwortreserve zaehlen
    gemeinsam gegen das Kontextfenster.
- Ueberlaufstrategien sind `reject`, `truncate_oldest` und `summarize`; der MVP
    implementiert mindestens `reject` und `truncate_oldest` deterministisch.
- Chat Templates werden aus dem Modell geladen, gehasht und pro Request eindeutig
    protokolliert; ein validiertes Override ist moeglich.
- Persistierter Chatverlauf und fluechtiger KV-/Prefix-Cache bleiben getrennte
    Konzepte. Ein Cacheverlust darf niemals Verlauf verlieren oder veraendern.

Eigene Tokenize-/Detokenize-Funktionen stehen in DLL, CLI und HTTP zur Verfuegung.
Sie sind fuer Kontextvorschau, Tests und OpenAI-kompatible Usage-Werte notwendig.

### Generierung und strukturierte Ausgabe

Die oeffentliche Generationskonfiguration bildet die vom SDK bestaetigten
Parameter ab: `max_tokens`, `temperature`, `top_p`, `top_k`, `min_p`, `seed`,
`stop`, `repetition_penalty`, `presence_penalty`, `frequency_penalty`, Beam Search
und begrenzte `logprobs`. Parameterkombinationen werden vor dem Start validiert.

`response_format` unterstuetzt Text, JSON Object und JSON Schema. Intern werden
die OpenVINO-GenAI-Backends fuer JSON Schema, Regex, EBNF und Structural Tags
gekapselt. Strukturierte Ausgabe ist damit ein Engine-Feature und kein
nachtraegliches JSON-Reparieren. Das SDK liefert derzeit nur den jeweils besten
Token-Logprob verlaesslich; diese Einschraenkung wird in der API ausgewiesen.

### Scheduler, Cache und Backpressure

Der Server nutzt die bestaetigte Continuous-Batching-Pipeline mit
`SchedulerConfig`. Konfigurierbar sind mindestens:

- `max_num_batched_tokens` und `max_num_seqs`
- dynamische Prompt-Aufteilung mit `dynamic_split_fuse`
- KV-Cache-Groesse beziehungsweise Blockzahl
- Prefix Caching und optional Cache Eviction
- Request-Queue, Prioritaet, Deadline und maximales Queue-Alter

Admission Control prueft Kontextlaenge, Queue-Limit und Speicherbudget vor dem
Start. Ueberlast liefert reproduzierbare `429`- oder `503`-Fehler mit
Retry-Hinweis. Disconnect, Timeout und Benutzerabbruch entfernen Requests auch
aus Scheduler und KV-Cache. Fairness verhindert, dass lange Prompts kurze
interaktive Requests dauerhaft blockieren.

### Beobachtbarkeit

Pro Request werden Request-ID, Modellrevision, Finish Reason, Input-/Outputtokens,
Queue-Zeit, Ladezeit, TTFT, TPOT, Durchsatz, Tokenisierung, Detokenisierung und
Grammar-Compile-Zeit erfasst. Inhalte bleiben standardmaessig ausgeschlossen.
Aggregierte Metriken stehen in einem Prometheus-kompatiblen Format bereit.

Liveness zeigt nur den Prozesszustand; Readiness beruecksichtigt Modellstatus,
Scheduler und Datenbank. Ein Warmup-Request laeuft nach dem Laden, bevor ein
Modell `ready` wird.

### Bewusst spaeter

- LoRA-Adapter mit versionierter Adapter-Registry
- speculative decoding und prompt lookup decoding nach Benchmarks
- mehrere gleichzeitig geladene Chatmodelle und verteiltes Serving
- multimodale Chatmodelle, Audio und Bildgenerierung
- Reranker-Modell fuer eine zweite Retrieval-Stufe
- OpenAI Responses API als Adapter nach Stabilisierung des Chat-/Tool-Streams

Die OpenVINO-GenAI-2026.2.1-Header bieten fuer LoRA, speculative decoding und
weitere Pipelines bereits Erweiterungspunkte. Diese werden nicht in den MVP
gezogen, bevor Speicherbedarf, Qualitaet und Durchsatz gemessen sind.

## 8. Datenbank, Semantik und Relationen

### Storage-Architektur

SQLite ist die Standarddatenbank fuer lokale GUI, CLI und Einzelserver. Sie wird
nicht direkt aus UI- oder HTTP-Code angesprochen. Der Core verwendet getrennte
Schnittstellen fuer Chatverlauf, semantischen Index und Relationen. Damit kann ein
spaeterer Mehrbenutzer-Server PostgreSQL oder einen externen Vektorstore anbinden,
ohne die Chatlogik zu aendern.

Standardkonfiguration:

- `PRAGMA foreign_keys = ON`
- WAL-Modus mit einem Writer und kurzlebigen Read-Transaktionen
- vorbereitete Statements und gebundene Parameter
- versionierte, vorwaerts laufende SQL-Migrationen
- atomare Transaktionen fuer Nachricht, Embedding und Relationen
- Backup ueber die SQLite Backup API
- optionale Verschluesselung spaeter ueber SQLCipher

### Relationales Kernschema

- `workspaces`: oberste Isolations- und spaetere Tenant-Grenze
- `conversations`: Titel, Modell, System-Prompt und Zeitstempel
- `messages`: Rolle, Inhalt, Status, Tokenzahlen und `parent_message_id`
- `message_parts`: Text, Tool Call, Tool Result oder Anhang
- `documents`: Quelle, URI, Hash, Metadaten und Indexstatus
- `chunks`: Dokumentabschnitte mit Position und Tokenbereich
- `embedding_models`: Modell-ID, Dimension, Pooling und Normalisierung
- `embeddings`: Vektor pro indizierbarem Objekt und Embedding-Modell
- `nodes`: kanonische IDs fuer graphfaehige Objekte
- `relations`: gerichtete, typisierte Kanten zwischen zwei Knoten
- `relation_evidence`: Nachrichten, Chunks oder Textspannen als Beleg
- `tool_servers`: MCP-Konfiguration ohne Geheimnisse
- `tool_definitions`: versionierter Snapshot erkannter Tools und Schemas
- `tool_executions`: Auditdaten, Status, Dauer und begrenzte Ergebnisreferenz
- `tool_approvals`: benutzergebundene, widerrufbare Freigaben
- `plans`: versionierte Planartefakte mit Ziel, Policy-Hash und Freigabestatus
- `plan_steps`: Abhaengigkeiten, erwartete Ergebnisse, Risiken und Validierung
- `agent_runs`: Modus, Budget, Sandbox-ID, Zustand und Abschlussgrund
- `agent_checkpoints`: wiederaufnehmbare Zustands- und Artefaktreferenzen
- `agent_profiles`: Rolle, Prompt-Hash, Modell, Tools, Policy und Capabilities
- `agent_events`: append-only Action-, Observation-, Status- und Handoff-Events
- `agent_tasks`: Besitzer, Lease, Zustand, Idempotency Key und Ergebnisreferenz
- `agent_task_dependencies`: gerichtete Kanten des ausfuehrbaren Task-DAG
- `resource_locks`: exklusive Pfad- oder Ressourcen-Leases mit Ablaufzeit
- `artifacts`: Diffs, Reports und erzeugte Dateien samt Hash und Herkunft
- `schema_migrations`: reproduzierbarer Datenbankstand

> **Implementierungsstatus Schema (Phase 5.1 & 5.2):**  
> Aktuell implementiert und verifiziert sind `schema_migrations`, `conversations`, `messages`, `messages_fts` (FTS5 External Content Virtual Table), `message_embeddings` sowie `message_embeddings_vec` (`sqlite-vec` `vec0` Virtual Table).  
> Die uebrigen oben gelisteten Tabellen (`documents`, `chunks`, `typed_relations`, `evidence`, Agent-/Tool-Tabellen etc.) bilden die Ziel-Architektur fuer Phase 5.3, Phase 5.4 sowie spaetere Ausbaustufen (Phase 6–7).

Nachrichten bleiben unveraenderlich; Bearbeiten oder Regenerieren erzeugt einen
neuen Zweig ueber `parent_message_id`. Dadurch lassen sich Chatvarianten sauber
darstellen, ohne Verlauf zu ueberschreiben.

### Semantische Suche

Retrieval kombiniert drei Signale:

1. Vektoraehnlichkeit ueber `sqlite-vec`,
2. lexikalische Suche ueber FTS5 (`bm25(...)`),
3. Relationssignale wie Zitate, gleiche Entitaeten oder direkte Nachbarschaft (Phase 5.3).

`sqlite-vec` v0.1.6 ist das erforderliche, versionierte Produktions-Vektor-Backend fuer das aktuelle Phase-5.2 Vektorprofil und ist vendored/statisch gebunden. Die Backend-Auswahl ist fuer eine Storage-Engine Instanz fixiert. Das Fehlschlagen der Initialisierung des erforderlichen Produktions-Backends ist fail-closed. Ein Referenz-Backend darf nur als explizit gewaehlter Entwicklungs-/Referenzpfad existieren und darf niemals als `SQLITE_VEC` gemeldet werden; es gibt keinen stillen Per-Operation-Fallback.

`bm25_score` muss von FTS5 BM25 stammen, Vektor-Scores vom gewaehlten Vektor-Backend, und Audit-Evidenz darf keinen Pfad bescheinigen, den sie nicht selbst ausgefuehrt hat. Detaillierter Vertrag: [`docs/architecture/retrieval-backend-contract.md`](docs/architecture/retrieval-backend-contract.md).

Die Kandidaten werden normalisiert, gewichtet zusammengefuehrt und mit Quelle, Score und Textspanne zurueckgegeben. Embeddings werden asynchron in Batches erzeugt. Loeschen und Re-Embedding bleiben transaktional nachvollziehbar.

### Relationsmodell

Strukturelle Beziehungen mit Integritaetsanforderung bleiben normale Foreign
Keys, beispielsweise Conversation zu Message oder Document zu Chunk. Die
generische Relationstabelle ist nur fuer domanenuebergreifende und semantische
Beziehungen vorgesehen. So wird die Datenbank nicht zu einem schwer pruefbaren
Entity-Attribute-Value-Modell.

Erste Knotentypen:

- `conversation`, `message`, `document`, `chunk`
- `entity`, `concept`, `artifact`, `tool_result`

Erste Relationstypen:

- `replies_to`, `cites`, `mentions`, `derived_from`
- `related_to`, `contradicts`, `supersedes`, `belongs_to`

Jede Relation speichert Richtung, Typ, Gewicht beziehungsweise Konfidenz,
Erzeuger (`user`, `model`, `extractor`, `system`), Zeitstempel und optionale
Gueltigkeit. Automatisch erkannte Beziehungen benoetigen Evidenz und koennen vom
Benutzer bestaetigt oder verworfen werden. Rekursive CTEs reichen zunaechst fuer
Nachbarschaft, Pfade und Kontextaufbau; ein separater Graphserver wird erst bei
gemessenen Skalierungsproblemen eingefuehrt.

### Aufbewahrung und Datenschutz

- Verlaufsspeicherung ist konfigurierbar und in der GUI sichtbar steuerbar.
- Einzelne Chats, Dokumente und der gesamte Workspace sind loeschbar.
- Abgeleitete Chunks, Embeddings und Relationen werden kaskadierend entfernt.
- Export und Import verwenden ein versioniertes JSON-Format.
- Logs enthalten standardmaessig keine Prompts oder Dokumentinhalte.

## 9. Tools und MCP

### Tool-Orchestrierung

Der Core besitzt eine zentrale Tool Registry. Native Tools und ueber MCP erkannte
Tools werden in dasselbe interne Format aus Name, Beschreibung, JSON-Schema und
Sicherheitsklasse uebersetzt. Der Ablauf ist immer:

1. Modell erzeugt einen strukturierten Tool Call.
2. Argumente werden strikt gegen das registrierte JSON-Schema validiert.
3. Policy und gegebenenfalls Benutzerfreigabe werden geprueft.
4. Das Tool laeuft mit Timeout, Abbruch und Ausgabelimit.
5. Das Ergebnis wird als typisierter Tool-Result-Nachrichtenteil gespeichert.
6. Das Modell erhaelt das begrenzte Ergebnis fuer den naechsten Dialogschritt.

Die Anzahl automatischer Tool-Runden ist begrenzt. Unbekannte Tools, zusaetzliche
Argumente und ungueltige JSON-Antworten werden nicht ausgefuehrt. VINOX verwendet eine
explizit begrenzte JSON-Schema-Subset-Validierung (`Bounded JSON Schema Subset`: `type`, `required`,
`enum`, `additionalProperties: false`). Nicht unterstuetzte sicherheitsrelevante Schema-Features
duerfen nicht stillschweigend als validiert gelten. Das Qwen2.5-Instruct-Modell wird im Spike auf
Toolauswahl und Argumenttreue getestet; strukturierte Generierung gegen JSON Schema dient als
kontrollierter Fallback.

### OpenAI-kompatibles Tool Calling

`/v1/chat/completions` unterstuetzt die Felder `tools`, `tool_choice` und
`parallel_tool_calls` sowie `tool_calls` in Assistant-Antworten und Nachrichten
mit Rolle `tool`. Parallelitaet wird nur fuer voneinander unabhaengige,
read-only klassifizierte Tools automatisch erlaubt.

### MCP-Client

Der MCP-Host unterstuetzt Capability Negotiation und mindestens:

- Tools: Erkennung, Schemaaktualisierung und Aufruf
- Resources: Auflisten, Lesen und optionale Subscriptions
- Prompts: Auflisten und Abrufen als explizite Vorlagen
- Transports: `stdio` und Streamable HTTP

Die unterstuetzte MCP-Protokollversion wird im Build festgelegt und in
Kompatibilitaetstests dokumentiert. Legacy-SSE wird nur bei konkretem Bedarf als
separater Adapter vorgesehen. Toolnamen werden als `<server>.<tool>`
namespaced, damit mehrere Server keine Namenskollisionen verursachen.

Konfiguration umfasst Kommando beziehungsweise URL, Transport, erlaubte
Capabilities, Arbeitsverzeichnis, Umgebungsvariablen-Whitelist und Policy. Bei
Windows-`stdio`-Servern werden notwendige Systemvariablen wie `SystemRoot`,
`WINDIR`, `ProgramFiles`, `ProgramData` und `USERPROFILE` kontrolliert
weitergereicht. Zugangsdaten liegen im Windows Credential Manager und niemals in
Prompts, Logs oder der SQLite-Datenbank.

### Eigener MCP-Server

Der separate `vinox_mcp_server` stellt nur explizit freigegebene Funktionen zur
Verfuegung. Erste Tools:

- `vinox.search`: hybride Suche mit Quellen
- `vinox.conversation_get`: Verlauf oder einzelnen Zweig laden
- `vinox.document_ingest`: Dokument aufnehmen und indizieren
- `vinox.relations_query`: Nachbarn und Pfade abfragen
- `vinox.relation_create`: belegte Relation anlegen

Conversations und Documents koennen zusaetzlich als adressierbare MCP Resources
angeboten werden. Schreibende Funktionen bleiben standardmaessig deaktiviert und
werden pro Workspace freigegeben. Interne Admin- oder Geheimnisfunktionen werden
nicht ueber MCP exponiert.

### Sicherheitsmodell

- Sicherheitsklassen: `read_only`, `local_write`, `process`, `network`, `admin`
- Default-Deny und Allowlists pro Server, Tool und Workspace
- Freigabeoptionen: einmalig, fuer die Session oder dauerhaft widerrufbar
- Prozessisolation mit festem Arbeitsverzeichnis und minimaler Umgebung
- Netzwerk-, Dateisystem-, Laufzeit-, Groessen- und Parallelitaetslimits
- MCP-Beschreibungen, Resources und Toolausgaben gelten als nicht vertrauenswuerdig
- Toolinhalte koennen niemals selbst eine Freigabe erteilen oder Policy aendern
- Auditspur speichert Entscheidung und Metadaten, sensible Inhalte nur opt-in
- Abbruch des Chats beendet auch laufende Tool- und MCP-Aufrufe

## 10. Chat, Plan und Agent

Der Modus wird ausserhalb des Prompts durch den Mode Controller gesetzt. Weder
Modelltext noch Toolausgabe oder MCP-Resource kann den Modus wechseln oder
Berechtigungen erweitern.

### Modusmatrix

| Modus | Zweck | Standardrechte | Ergebnis |
|---|---|---|---|
| Chat | Fragen, Dialog und Retrieval | keine Seiteneffekte; optionale explizit aktivierte Read-only-Tools | Antwort und Quellen |
| Plan | Ziel untersuchen und Vorgehen entwerfen | nur Read-only-Inspektion, keine mutierenden Kommandos | versioniertes Planartefakt |
| Agent | genehmigten Plan autonom ausfuehren | begrenzte Tools ausschliesslich ueber Sandbox und Policy | Artefakte, Diff und Validierungsbericht |

Chat bleibt ein einzelner kontrollierter Antwortzyklus und startet keine
autonome Toolschleife. Plan darf Dateien, Symbole, Metadaten und Suchergebnisse
lesen, aber weder Dateien schreiben noch Builds oder sonstige potenziell
mutierende Prozesse starten. Agent arbeitet in begrenzten Schritten und kann bei
Unsicherheit, Policy-Verletzung oder veraendertem Scope pausieren.

### Planartefakt und Uebergaenge

Ein Plan ist strukturiert und gegen `agent-plan.schema.json` validiert. Er
enthaelt Ziel, Annahmen, Schritte, Abhaengigkeiten, benoetigte Capabilities,
Risiken, erwartete Artefakte, Validierungen und Budgets. Freitext allein ist kein
ausfuehrbarer Plan.

Zulaessige Uebergaenge sind `chat -> plan`, `plan -> agent` und jederzeit zurueck
zu Chat. Der Wechsel zu Agent benoetigt eine Benutzerfreigabe fuer Plan-Hash,
Policy, Workspace und Budget. Der Agent kann sich nicht selbst hochstufen.
Aendert sich Scope oder ein sicherheitsrelevanter Schritt, wird pausiert und ein
neuer Plan-Hash muss freigegeben werden.

Statuswerte sind `draft`, `ready`, `approved`, `running`, `paused`, `blocked`,
`completed`, `failed` und `cancelled`. Verborgene Gedankengaenge werden weder
angezeigt noch gespeichert; sichtbar sind Ziel, kurze Aktionszusammenfassungen,
Tool Calls, Ergebnisse, Diffs und Pruefungen.

### Agent-Orchestrierung

- feste Limits fuer Schritte, Tool Calls, Tokens, Laufzeit und Ausgabegroesse
- Checkpoint nach jedem abgeschlossenen Schritt und vor externer Seitenauswirkung
- idempotente Wiederaufnahme nur bei unveraendertem Plan- und Workspace-Hash
- Validierung ist eigener Schritt und kann nicht durch Erfolgsbehauptung ersetzt werden
- parallele Schritte nur bei explizit fehlenden Abhaengigkeiten und getrennten Pfaden
- Abbruch propagiert an Tools, MCP, Prozesse und Sandbox
- Stuck Detection stoppt Wiederholungen ohne neuen Zustand oder Fortschritt
- der Einzelagent ist MVP; Multiagent bleibt eine optionale, messbare Ausbaustufe

### Optionale Multiagent-Ausbaustufe

Multiagent wird nicht als freier Gruppenchat gebaut. Ein Supervisor besitzt Ziel,
Plan, Gesamtbudget und Benutzerkommunikation. Er zerlegt den genehmigten Plan in
einen Task-DAG und delegiert nur klar abgegrenzte Aufgaben an spezialisierte
Worker. Erste Profile sind `researcher`, `implementer` und `reviewer`.

Regeln:

- jeder Task hat genau einen aktiven Besitzer und eine ablaufende Lease
- Worker erhalten nur den benoetigten Kontext und ein Teilbudget
- Berechtigungen koennen bei Delegation nur gleich bleiben oder kleiner werden
- kein Agent darf Freigaben erteilen, Policies aendern oder weitere Rechte vergeben
- jeder schreibende Worker arbeitet in eigenem Overlay beziehungsweise Worktree
- Worker kommunizieren ueber typisierte Events und Artefakte, nicht ueber
    unstrukturierten gemeinsamen Promptzustand
- paralleles Fan-out ist nur fuer unabhaengige Tasks und getrennte Ressourcen erlaubt
- ein deterministischer Merger prueft Hashes, Diffs und Konflikte
- der Reviewer besitzt keine Schreibtools und validiert Claims gegen Evidenz
- der Supervisor allein erzeugt die abschliessende Benutzerantwort

Das Eventmodell folgt `Action -> Observation` und ist append-only. Jedes Event
enthaelt mindestens Event-, Run-, Task- und Agent-ID, Parent-ID, Sequenz,
Zeitstempel, Typ, Payload-Schema-Version und Hash. Der aktuelle Zustand wird mit
deterministischen Reducern aufgebaut und kann aus Events plus Checkpoint
rekonstruiert werden.

Kombinierbare Abbruchbedingungen stoppen bei abgeschlossenem DAG, Benutzerabbruch,
Budget-, Token- oder Zeitlimit, zu vielen Runden, wiederholten Fehlern oder
`max_stalls`. Pause und Freigabe sind explizite Interrupt-Events. Resume darf
bereits bestaetigte Task-Ergebnisse nicht doppelt ausfuehren; Idempotency Keys,
Leases und Resource Locks sichern das ab.

Multiagent bleibt per Feature-Flag deaktiviert, bis ein Evaluationssatz zeigt,
dass Supervisor plus Worker bei Qualitaet oder Laufzeit besser sind als derselbe
Einzelagent. Gemessen werden Erfolgsquote, Validierungsfehler, Tokens, Laufzeit,
Konflikte und notwendige Benutzereingriffe.

### MCP und A2A

MCP verbindet den Agenten mit Tools, Prompts und Resources. A2A verbindet
eigenstaendige Agentendienste ueber Tasks, Messages, Status und Artifacts. Ein
MCP-Server wird deshalb nicht automatisch als Agent behandelt.

Interne Worker verwenden zunaechst das lokale typisierte Event-/Taskmodell. Eine
spaetere A2A-1.0-Erweiterung kann Remote Agents ueber Agent Cards entdecken,
Tasks streamen und abbrechen sowie Artefakte empfangen. Remote Agent Cards und
Capabilities werden nicht ungeprueft vertraut; Signatur, Authentifizierung,
Policy und Artifact-Scan liegen vor jeder Delegation. A2A ist kein MVP-Bestandteil.

### GitHub-Referenzen und uebernommene Muster

Stand der Recherche: 2026-08-15. Es werden Konzepte neu implementiert und keine
Codefragmente kopiert.

- [Microsoft AutoGen](https://github.com/microsoft/autogen): typisierte
    Nachrichten, Agent Runtime, Supervisor/Worker, Handoffs, Ledger und
    kombinierbare Termination Conditions. Code MIT, Dokumentation CC BY 4.0.
- [LangGraph](https://github.com/langchain-ai/langgraph): zustandsbehafteter
    Ausfuehrungsgraph, Checkpoints, Interrupt/Resume, Subgraphs, Replay und
    deterministische Reducer. MIT.
- [OpenHands Agent SDK](https://github.com/OpenHands/software-agent-sdk):
    Action/Observation-Events, Task-Delegation, Confirmation Policy,
    Security Analyzer, Leases, Resource Locks und Stuck Detection. MIT.
- [Magentic-UI](https://github.com/microsoft/magentic-ui): getrennte
    `allow`/`require_approval`/`deny`-Entscheidungen, kritische Punkte,
    Sandbox-Erkennung und Vorher-/Nachher-Verifikation. MIT.
- [A2A](https://github.com/a2aproject/A2A): Agent Cards, Task-Lifecycle,
    getrennte Messages und Artifacts, Streaming, Cancel und Auth-Schemata.
    Apache-2.0.

### Sandbox-Architektur

Die Modellinferenz bleibt auf dem Host, damit GPU/NPU und Modellcache nicht in
eine Wegwerf-Umgebung dupliziert werden. Nur Tool- und Prozessausfuehrung laeuft
im separaten Sandbox Worker ueber eine kleine, versionierte RPC-Schnittstelle.

Mindestanforderungen:

- Arbeitskopie oder Copy-on-Write-Workspace; Original ist read-only
- kein Zugriff auf Host-Credentials, SSH-Schluessel oder Credential Manager
- Netzwerk standardmaessig aus; Ziele nur per expliziter Allowlist
- kanonische Pfadpruefung inklusive Symlinks und Windows Reparse Points
- CPU-, Speicher-, Prozess-, Laufzeit- und Ausgabelimits
- minimaler Satz an Umgebungsvariablen und ausfuehrbaren Programmen
- deterministisches Beenden des gesamten Prozessbaums und Aufraeumen
- Rueckgabe nur deklarierter Artefakte mit Hash, Diff und Validierungsstatus

Windows Job Objects begrenzen Ressourcen und Prozesslebensdauer, sind allein aber
keine Sicherheitsgrenze fuer Dateisystem oder Netzwerk. Der Spike entscheidet
deshalb zwischen AppContainer/restricted token und einer Container- oder
Windows-Sandbox-Loesung. Bis diese Grenze nachweislich funktioniert, darf der
Agent-Modus nicht als sicher beworben werden.

Ein MCP-Server kann ausserhalb der lokalen Sandbox eigene Seiteneffekte haben.
Agent-Aufrufe laufen daher immer ueber den Host-Proxy mit derselben Policy und
Freigabe; `sandboxed` wird niemals allein aufgrund des MCP-Transports angenommen.

### Uebernahme in den Workspace

Der Agent schreibt nicht direkt in das Original. Nach erfolgreicher Validierung
zeigt CLI oder GUI Dateiliste, Diff, Tests und Warnungen. Erst eine explizite
Uebernahme wendet ausgewaehlte Artefakte atomar an. Konflikte durch zwischenzeitlich
geaenderte Hostdateien blockieren die Uebernahme und fuehren nicht zu einem
automatischen Ueberschreiben.

## 11. Server und Protokoll

### MVP-Endpunkte

- `GET /health/live`
- `GET /health/ready`
- `GET /v1/models`
- `POST /v1/chat/completions`
- `POST /v1/completions`
- `POST /v1/embeddings`
- `POST /tokenize`
- `POST /detokenize`
- `POST /api/models/{id}/load`
- `POST /api/models/{id}/unload`
- `GET /metrics`
- `GET/POST /v1/conversations`
- `POST /v1/search`
- `GET/POST/DELETE /v1/relations`
- `POST /v1/plans`
- `GET /v1/plans/{id}`
- `POST /v1/plans/{id}/approve`
- `POST /v1/agent/runs`
- `GET /v1/agent/runs/{id}`
- `POST /v1/agent/runs/{id}/cancel`
- `GET /v1/agent/runs/{id}/events`
- `GET /openapi.yaml`
- optional `GET /docs` fuer Swagger UI

`/v1/chat/completions` unterstuetzt normale JSON-Antworten und Streaming ueber
Server-Sent Events. Das Format orientiert sich an der OpenAI API. Die formale
OpenAPI-3.1-Datei dokumentiert alle tatsaechlich implementierten Felder.

### MVP-Parameter

- `model`
- `messages`
- `temperature`
- `top_p`
- `top_k`
- `min_p`
- `max_tokens`
- `stop`
- `seed`
- `repetition_penalty`
- `presence_penalty`
- `frequency_penalty`
- `response_format`
- `logprobs`
- `stream`
- `tools`
- `tool_choice`
- `parallel_tool_calls`

Nicht unterstuetzte Felder werden mit strukturierten Fehlerantworten gemeldet und
nicht stillschweigend ignoriert. Request-Limits, Timeouts, Abbruch bei
Client-Trennung und kontrolliertes Herunterfahren gehoeren zum Server-MVP.

Streaming definiert stabile Chunk-IDs, Finish Reason, Tool-Call-Deltas, optional
Usage im letzten Chunk und einen eindeutigen Abschlussmarker. Fehler nach Beginn
des Streams werden als strukturierte Stream-Events gemeldet. Die API gibt eine
Kompatibilitaetsmatrix aus, statt nicht implementierte OpenAI-Felder vorzutaeuschen.

### Sichere Betriebsgrenzen

- Standardbindung ist ausschliesslich `127.0.0.1`; externe Interfaces erfordern
    eine explizite Konfiguration.
- Optionaler Bearer-Token-Schutz gilt einheitlich fuer REST, Streaming und MCP.
- CORS ist standardmaessig deaktiviert und verwendet bei Aktivierung eine
    Origin-Allowlist ohne Wildcard und Credentials-Kombination.
- Request-Body-, Header-, Verbindungs- und Rate-Limits greifen vor der Inferenz.
- Produktions-TLS endet zunaechst an einem dokumentierten Reverse Proxy; native
    TLS-Unterstuetzung ist kein verdeckter Eigenbau.
- Konfigurationsrangfolge ist Defaults, Konfigurationsdatei, Umgebung, CLI; die
    effektive Konfiguration kann ohne Geheimnisse ausgegeben werden.

### Kompatibilitaetsgrenzen

- Primaeres Protokoll ist OpenAI-kompatibel; Ollamas `/api/*` wird im MVP nicht
    nachgebaut.
- Primaeres Modellformat ist OpenVINO IR. GGUF wird nicht direkt geladen, sondern
    benoetigt einen dokumentierten Konvertierungsweg, sofern technisch moeglich.
- Tensor-/Pipeline-Parallelismus und verteiltes Serving nach vLLM-Vorbild sind
    kein Ziel des lokalen MVP.
- Eine Capability-Matrix nennt pro Modell und Endpoint exakt unterstuetzte Felder.

## 12. CLI

Beispiele:

```powershell
vinox-cli --model .\models\qwen --device GPU
vinox-cli --server http://localhost:8080
vinox-cli --model .\models\qwen --prompt "Erklaere OpenVINO"
```

Funktionen:

- interaktiver Chat und Einmal-Prompt
- lokaler DLL-Modus und entfernter Servermodus
- Token-Streaming und Abbruch
- System-Prompt und Sampling-Parameter
- Modell- und Geraeteauswahl
- Modelle anzeigen, laden, warmhalten und entladen
- Tokenzaehler und Kontextbudget vor der Generierung
- JSON-Ausgabe fuer Skripte
- speicherbarer Gespraechsverlauf
- semantische Suche mit Quellenanzeige
- MCP-Server verbinden, trennen und deren Tools auflisten
- interaktive Freigabe sicherheitsrelevanter Tool Calls
- Moduswahl `chat`, `plan` oder `agent`
- Plan pruefen, freigeben und als Agent-Run starten
- Agent-Ereignisse, Budget, Diffs und Validierungen anzeigen
- ausgewaehlte Sandbox-Artefakte in den Workspace uebernehmen
- Kommandos wie `/clear`, `/save`, `/search`, `/relate`, `/tools`, `/mcp`,
    `/plan`, `/agent`, `/approve`, `/diff`, `/apply`, `/stats` und `/exit`

## 13. GUI

Die GUI verwendet die DLL lokal oder verbindet sich mit einem Server. Sie bietet:

- Chat-Verlauf mit laufender Tokenanzeige
- Start, Stopp und erneute Generierung
- Modell- und Geraeteauswahl
- Modellstatus, Warmup, Keep-Alive und Kontextauslastung
- lokale und entfernte Verbindung
- mehrere Chat-Sessions
- durchsuchbarer Verlauf und Quellenansicht
- Relationsansicht fuer verknuepfte Nachrichten, Dokumente und Entitaeten
- MCP-Serververwaltung und Tool-Browser
- klarer Freigabedialog mit Server, Tool, Argumenten und Risikoklasse
- Auditansicht fuer laufende und abgeschlossene Tool Calls
- klarer Modusumschalter fuer Chat, Plan und Agent
- Planansicht mit Schritten, Risiken, Capabilities und Freigabe
- Agent-Zeitleiste mit Aktionen, Budget, Checkpoints und Abbruch
- Diff-/Artefaktansicht mit selektiver Uebernahme
- Sampling-Einstellungen
- Tokenrate, Latenz und Modellstatus
- persistente, benutzerspezifische Einstellungen

Die GUI implementiert keine eigene Inferenzlogik.

## 14. Umsetzungsetappen

### Phase 1: Grundgeruest

- Qt 6.10.1 Desktop-Kit `msvc2022_64` ueber den Maintenance Tool nachinstallieren
- CMake-Projekt und Presets anlegen
- Preset auf MSVC 2022 x64, Kitware CMake und Qt Ninja festlegen
- Abhaengigkeiten mit vcpkg-Baseline und Hashes deklarieren
- `build`, `vcpkg_installed` und kanonischen `stage` pro Preset einrichten
- komponentenbasierte CMake-Installregeln fuer alle Runtime-Dateien anlegen
- `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.txt`, SBOM und Lizenzpruefung anlegen
- DLL-Exportregeln und Logging einrichten
- Build fuer Debug, Release und RelWithDebInfo verifizieren
- minimale CI fuer Windows vorbereiten

**Ergebnis:** Alle leeren Zielmodule lassen sich reproduzierbar bauen.

### Phase 2: Inferenz-Spike

- `Qwen2.5-1B-Instruct-fp16-test-ov` als Referenzmodell laden
- CPU- und verfuegbare GPU-Geraete erkennen
- synchrone Textgenerierung ausfuehren
- strukturierte JSON-Ausgabe gegen ein Schema pruefen
- Samplingparameter, Logprobs und Finish Reasons pruefen
- Continuous Batching, Prefix Cache und Scheduler-Limits pruefen
- Token-Streaming und Abbruch pruefen
- Speicherbedarf und Startzeit dokumentieren

**Ergebnis:** OpenVINO GenAI ist fuer den vorgesehenen Kernpfad technisch
verifiziert; die HTTP-Bibliothek ist anhand von SSE und Abbruch gewaehlt.

### Phase 3: Core und stabile DLL

- Modell-, Session- und Generationsabstraktionen implementieren
- versionierte C-ABI definieren
- C++-Wrapper erstellen
- Fehler-, Thread- und Speicherregeln testen
- minimales C-Beispiel gegen die DLL bauen

**Ergebnis:** Ein externer C- oder C++-Prozess kann ein Modell laden und Tokens
streamen, ohne interne Implementierungsdetails zu kennen.

### Phase 4: Modellregistry und Serving (🟢 Abgeschlossen & Gehärtet)

- JSON-Schema fuer Modellmanifeste und lokale Registry implementieren (🟢)
- Modellzustandsmaschine, Lazy Load, Warmup, Keep-Alive und Unload implementieren (🟢)
- Tokenize/Detokenize, Chat Templates und Kontextbudget kapseln (🟢)
- Generationsparameter und `response_format` vollstaendig validieren (🟢)
- `nlohmann/json` als kanonische JSON-Grundlage fuer Manifeste und spaetere JSON-basierte Schnittstellen; VINOX-spezifische strikte Manifest-/Schema-Validierung bleibt in der Serving-Schicht erhalten (🟢)
- Continuous Batching, Admission Control, Backpressure und Fairness implementieren (🟢)
- Usage-, TTFT-/TPOT- und Prometheus-Metriken bereitstellen

**Ergebnis:** `vinox_serving.dll` ist stabil, zeiger- und evolutionssicher C-ABI-zertifiziert und verwaltet Modell-Manifeste reproduzierbar.
### Phase 5: Storage, Embeddings und Retrieval

#### Phase 5.1 — SQLite-Persistenz & FTS-Grundlage — 🟢 abgeschlossen/gehaertet

- Kanonische versionierte SQL-Migrationen (`001_init.sql`)
- WAL-Modus + Foreign-Key Invarianten (fail-closed verifiziert)
- Conversation-/Message-Persistenz und Branching (`parent_id`)
- FTS5 External-Content Virtual Table und Trigger-Synchronisation
- C-ABI/Storage Lifetime und Zero-Side-Effects bei Validierungs-Fehlern
- Reopen-, Persistenz-, FK- und Migrations-Regressionstest-Abdeckung

#### Phase 5.2 — Embeddings & Hybrides Retrieval — 🟢 abgeschlossen/gehaertet

- Qwen3 Embedding Profil, 1024 Dimensionen, L2-Normalisierung
- `sqlite-vec` v0.1.6 als erforderliches Produktions-Vektor-Backend (vendored/statisch gebunden)
- Backend-Auswahl fixiert pro Storage-Engine Instanz
- Initialisierung fail-closed wenn `vec0` nicht etabliert werden kann; keine stillen Per-Operation-Fallbacks
- Echter FTS5 `bm25(...)` Signal-Abruf
- Deterministische logistische/normalisierte Text/Vektor-Fusion und Tie-Breaking
- Alpha- und Dimensions-Validierung mit deterministischer Fehler-Abweisung (`VINOX_STATUS_INVALID_ARGUMENT`)
- Live-Audit weist das aktive Backend nach und fuehrt gezeigte Eigenschaften live aus
- Detaillierter Backend-Vertrag dokumentiert in [`docs/architecture/retrieval-backend-contract.md`](docs/architecture/retrieval-backend-contract.md)

#### Phase 5.3 — Dokumente & Semantische Relationen — 🟢 abgeschlossen

- Dokumente und Chunks aufnehmen und indizieren (`vinox_storage_document_ingest`)
- Typisierte semantische Relationen und Evidenz speichern (`typed_relations`, `evidence`)
- Rekursive Relations-Abfragen (CTEs) implementieren (`vinox_storage_relations_query_cte`)
- Relations-Signal als drittes Retrieval-Signal integrieren
- Persistente Embedding-Modell/Profil Metadaten fuer gezielte Re-Indexierung bei Modell-Wechseln (`embedding_profiles`)

#### Phase 5.4 — Storage-Lebenszyklus & Portabilitaet — 🟢 abgeschlossen

- Loeschungs- und Re-Index Lebenszyklus (`ON DELETE CASCADE`)
- Export und Import in versioniertem Format (`vinox_storage_export_json`, `vinox_storage_import_json`)
- Online-Backup ueber die SQLite Backup API (`vinox_storage_backup_online`)
- Recovery und Vorwaerts-Migrations-Kompatibilitaet (`002_documents_relations.sql`)
- Kaskadierendes Aufraeumen fuer abgeleitete Chunks, Embeddings und Relationen

**Ergebnis:** Phase 5 (SQLite, `sqlite-vec`, Dokumente, Chunks, CTE-Relationen, Online-Backup, Export/Import) ist vollstaendig umgesetzt und mit 12/12 CTest-Tests verifiziert.

### Phase 6: Tools und MCP — 🟢 gehärtet & umgesetzt (Eval Harness 🟢, Issue #16 in Review)

- [x] interne Tool Registry und JSON-Schema-Validierung implementieren
- [x] Policy Engine, Freigaben, Limits, Audit und Abbruch implementieren
- [x] MCP-Client fuer `stdio` und Streamable HTTP anbinden
- [x] Capability Negotiation fuer Tools, Resources und Prompts testen
- [x] eigenen MCP-Server fuer Suche, Verlauf und Relationen erstellen (real backend + fail-closed governance)
- [x] OpenAI-Toolformat in das interne Format und zurueck abbilden (0% semantic drift verified)
- [x] Phase 6.4 Control Loop, Timeouts/Deadlines, Cancellation, Bounded Output (256 KB) & Fail-Closed Registry Injection
- [x] Qwen2.5-Instruct Toolauswahl- und Argumentevaluierungsharness mit 16-Fälle-Corpus, N=5 Multi-Trial Engine, Raw-Output-Scoring, SHA256-Metadaten & CTest SKIP-Safeguard umgesetzt

**Ergebnis:** Phase 6 (Tools, Policy Engine, MCP Client, MCP Server, Governance Control Loop, JSON Schema Validierung & Gehärteter Qwen2.5 Evaluation Harness) ist vollständig umgesetzt und mit 13 CTest-Targets verifiziert.

### Phase 7: Agent und Sandbox

- Mode Controller und unveraenderliche Policies implementieren
- Plan-Schema, Zustandsmaschine, Hash und Freigabeprozess implementieren
- Agent-Loop mit Budgets, Checkpoints, Pause, Resume und Abbruch implementieren
- versioniertes Host-Worker-RPC und isolierten Workspace implementieren
- Windows-Sandbox-Backends gegen Datei-, Netzwerk- und Credential-Flucht testen
- Diff-, Artefakt-, Validierungs- und konfliktfeste Uebernahme implementieren
- MCP-Aufrufe im Agent-Modus ueber den Policy-Proxy erzwingen

**Ergebnis:** Ein freigegebener Plan laeuft begrenzt in einer nachweislich
isolierten Umgebung und kann nur gepruefte Artefakte zur Uebernahme vorschlagen.

### Phase 8: CLI

- lokales Chatten ueber DLL
- Servermodus ueber HTTP
- Streaming, Abbruch und Parameter
- Sitzungsverlauf und JSON-Ausgabe

**Ergebnis:** Vollstaendig nutzbarer Terminal-Chat fuer Entwicklung und Tests.

### Phase 9: Server

- OpenAI-kompatible DTOs und Fehlerobjekte
- Tool Calling und Tool-Result-Nachrichten
- Endpunkte und SSE implementieren
- Health, Model Lifecycle, Tokenizer und Metrikendpunkte implementieren
- Plan- und Agent-Run-Endpunkte mit Ereignisstream implementieren
- OpenAPI-Schema bereitstellen
- Parallelitaet, Limits und Shutdown absichern
- API-Vertragstests schreiben

**Ergebnis:** Standardclients koennen Chat-Completions synchron oder streamend
abrufen.

### Phase 10: GUI

- QML-Oberflaeche und View Models
- lokaler und entfernter Modus
- Sessions, Parameter und Statistiken
- responsive Desktop-Layouts und Fehlerzustaende

**Ergebnis:** Alltagstaugliche native Chat-Anwendung ohne duplizierte
Inferenzlogik.

### Phase 11: Optionale Multiagent-Ausbaustufe

- versionierte Agent Profiles und typisierte Action/Observation-Events erstellen
- Supervisor, Task-DAG, Handoffs und kombinierbare Abbruchbedingungen implementieren
- Task-Leases, Idempotency Keys, Resource Locks und Stuck Detection implementieren
- isolierte Worker-Overlays und deterministischen Merge implementieren
- read-only Reviewer und evidenzbasierte Abschlusspruefung implementieren
- Einzelagent gegen Multiagent auf einem festen Evaluationssatz vergleichen
- A2A-1.0-Client/-Server nur als separaten Folgespike bewerten

**Ergebnis:** Multiagent kann nur aktiviert werden, wenn Isolation, Korrektheit
und ein messbarer Vorteil gegenueber dem Einzelagenten nachgewiesen sind.

### Phase 12: Haertung und Distribution

- Last-, Abbruch- und Parallelitaetstests
- ABI-Kompatibilitaetspruefung
- Standalone-Stage, Installationslayout und relocatable CMake-Paket
- Windows-Paket mit EXE, DLL, Headern und Dokumentation
- Qt-, QML-, OpenVINO- und VC-Runtime-Abhaengigkeiten automatisiert stagen
- sichere Windows-DLL-Suche, `qt.conf` und Linux-`$ORIGIN`-RPATH pruefen
- Redistributables gegen OpenVINO-`redist.txt` und Qt-LGPL-Pflichten pruefen
- Third-Party Notices, Modellmanifeste, SBOM und Lizenzreport paketieren
- Linux-Build des portablen Kerns pruefen

**Ergebnis:** Reproduzierbares, dokumentiertes Release-Paket.

## 15. Teststrategie

- Unit-Tests fuer Promptaufbau, Sessions und Konfiguration
- ABI-Smoke-Test aus reinem C
- API-Vertragstests fuer JSON, Fehler und SSE
- Integrationstests mit einem kleinen Modell
- Manifest-, Modellzustands- und Hashvalidierungstests
- Tokenbudget- und Chat-Template-Golden-Tests
- Structured-Output-Tests fuer JSON Schema, Regex und EBNF
- Scheduler-Tests fuer Batching, Fairness, Backpressure und Abbruch
- Cache-Tests fuer Prefix-Hit, Eviction und Trennung vom Chatverlauf
- Lasttests mit TTFT-, TPOT-, Durchsatz- und Queue-Zielen
- Migrationstests von jeder unterstuetzten Schema-Version
- Retrieval-Tests fuer FTS, Vektorsuche und hybrides Ranking
- Integritaetstests fuer Relationen, Evidenz und kaskadierendes Loeschen
- Backup-/Restore- und konkurrierende Lese-/Schreibtests
- Tool-Schematests mit ungueltigen und zusaetzlichen Argumenten
- MCP-Vertragstests fuer Lifecycle, Capabilities und beide Transports
- Sicherheits- und Prompt-Injection-Tests fuer Policies und Freigaben
- Bindungs-, Auth-, CORS-, Rate-Limit- und Request-Groessentests
- Timeout-, Abbruch-, Ausgabelimit- und Serverausfalltests
- OpenAI-Vertragstests fuer Tool Calls und Tool-Result-Nachrichten
- Modus-Isolationstests: Chat und Plan koennen keine Schreibrechte erlangen
- Plan-Schema-, Hash-, Freigabe- und Scope-Aenderungstests
- Sandbox-Escape-Tests fuer Pfade, Reparse Points, Prozesse, Netzwerk und Secrets
- Budget-, Checkpoint-, Resume-, Cancel- und Crash-Recovery-Tests
- Diff-Uebernahme- und Konflikttests gegen zwischenzeitliche Hostaenderungen
- Tests, dass externe MCP-Seiteneffekte nicht als sandboxed klassifiziert werden
- Event-Replay-Tests fuer identischen Zustand aus Log und Checkpoint
- Multiagent-Tests fuer Leases, doppelte Zustellung, Handoffs und Abbruchbedingungen
- Race- und Konflikttests fuer parallele Worker und Resource Locks
- Vergleichstests Einzelagent gegen Multiagent mit Qualitaets- und Kostenbudgets
- A2A-Vertragstests erst bei Aktivierung der optionalen Erweiterung
- CI-Lizenztest fuer Abhaengigkeiten, Qt-Module, Modelle und Paketinhalt
- Test, dass nur erlaubte OpenVINO-Redistributables im Release-Paket liegen
- Test, dass jedes gebuendelte Modell Manifest, Herkunft, Lizenz und Hash besitzt
- Standalone-Smoke-Tests in einer sauberen Umgebung ohne Qt/OpenVINO auf `PATH`
- Loader-Audit: jede geladene Nicht-System-DLL stammt aus der Stage-Wurzel
- Relocation-Test nach Verschieben des gesamten `stage`-Verzeichnisses
- Negativtest fuer fehlende, manipulierte oder global gefundene Runtime-Dateien
- Pakettest auf absolute Build-, SDK-, Benutzer- und Modellpfade
- CLI-Tests fuer lokalen und entfernten Modus
- Server-Tests fuer Parallelitaet, Disconnect und Shutdown
- GUI-Tests fuer zentrale Bedienablaeufe
- Sanitizer unter Linux und geeignete Windows-Diagnosewerkzeuge

Langsame Modelltests werden separat markiert. Der normale Testlauf bleibt klein
und kann ohne grosses Modell ausgefuehrt werden.

## 16. Dokumentation

- Build- und Installationsanleitung
- Modellkonvertierung und unterstuetzte Modellformate
- Modellmanifest, Registry, Chat Templates und Kontextstrategien
- Scheduler, Cache, Kapazitaetsplanung und Metriken
- DLL-API mit Lebensdauer- und Threading-Regeln
- Datenbankschema, Migrationen, Retrieval und Relationsmodell
- Toolentwicklung, Sicherheitsklassen und Freigabemodell
- MCP-Serverkonfiguration, Transports und Troubleshooting
- Chat-/Plan-/Agent-Modi, Planformat und Freigabeprozess
- Sandbox-Garantien, Grenzen, Backends und Bedrohungsmodell
- Agent Profiles, Eventmodell, Task-DAG, Handoffs und Multiagent-Evaluation
- Abgrenzung MCP zu A2A und Vertrauensmodell fuer Remote Agents
- Serverkonfiguration und OpenAI-Kompatibilitaet
- sichere Netzwerkfreigabe, Reverse-Proxy-TLS und Kompatibilitaetsgrenzen
- CLI-Referenz
- GUI-Bedienung
- Architekturentscheidungen als kurze ADRs
- Projektlizenz, Third-Party Notices, SBOM und Redistributionsanleitung
- Qt-LGPL-Compliance und OpenVINO-Binary-Redistributables
- getrennte Modelllizenzen, Herkunftsnachweise und Weiterverteilungsregeln
- Standalone-Layout, Offline-Installation und Runtime-Loader-Diagnose
- Beispiele fuer C, C++, PowerShell und HTTP

## 17. MVP-Abnahmekriterien

Der MVP ist erreicht, wenn:

1. ein unterstuetztes Modell ueber die DLL geladen werden kann,
2. ein reines C-Beispiel Tokens streamend empfaengt und abbrechen kann,
3. die CLI lokal einen Mehrfachrunden-Chat ausfuehrt,
4. `/v1/chat/completions` synchron und per SSE funktioniert,
5. ein OpenAI-kompatibler Client den Server verwenden kann,
6. das OpenAPI-Schema dem implementierten Verhalten entspricht,
7. Chatverlaeufe mit Zweigen dauerhaft gespeichert und geloescht werden koennen,
8. hybride Suche relevante Nachrichten und Dokumentabschnitte mit Quellen findet,
9. typisierte Relationen samt Evidenz angelegt und abgefragt werden koennen,
10. OpenAI-kompatible Tool Calls mit validierten Argumenten funktionieren,
11. mindestens ein externer MCP-Server ueber `stdio` und Streamable HTTP
    verbunden und kontrolliert aufgerufen werden kann,
12. der eigene MCP-Server Suche und Relationsabfragen bereitstellt,
13. schreibende oder riskante Tools ohne passende Freigabe blockiert werden,
14. Modellmanifest, Hash, Warmup, Keep-Alive und Unload funktionieren,
15. Kontextueberlauf vor Generierung reproduzierbar behandelt wird,
16. JSON-Schema-Ausgabe ohne nachtraegliche Reparatur gueltiges JSON liefert,
17. parallele Requests ueber Admission Control und Continuous Batching laufen,
18. Usage, TTFT, TPOT, Queue-Zeit und Durchsatz messbar sind,
19. der Server ohne explizite Freigabe nur an `127.0.0.1` bindet,
20. Auth-, CORS-, Rate- und Groessenlimits vor Inferenz durchgesetzt werden,
21. Chat und Plan nachweislich keine mutierenden Tools ausfuehren koennen,
22. ein Plan nur mit passendem Hash, Workspace und Policy freigegeben wird,
23. Agent-Prozesse ohne Host-Credentials und ohne direkten Schreibzugriff auf den
    Original-Workspace laufen,
24. Netzwerk, Ressourcen, Prozessbaum und Laufzeit der Sandbox begrenzt sind,
25. nur gepruefte Diffs und Artefakte nach expliziter Freigabe uebernommen werden,
26. Build, Tests und Paketierung reproduzierbar dokumentiert sind,
27. Release-Paket, SBOM und Third-Party Notices lizenzgeprueft sind und keine
    unfreigegebenen Runtime-Dateien oder Modelle enthalten,
28. CLI, Server, GUI und Sandbox Worker aus einer verschobenen Stage-Wurzel ohne
    globale Qt-, OpenVINO-, vcpkg- oder Modellinstallation starten,
29. jede geladene Nicht-System-Runtimebibliothek und jedes Plugin nachweislich
    aus der Stage-Wurzel stammt.

Optionale Multiagent-Abnahme:

1. Worker koennen Rechte und Budgets bei Delegation nicht erweitern,
2. jeder Task besitzt hoechstens einen gueltigen Lease-Inhaber,
3. Event-Replay erzeugt denselben Zustand wie der Live-Lauf,
4. parallele Worker koennen nicht unbemerkt dieselbe Ressource veraendern,
5. Abbruchbedingungen beenden Stalls, Schleifen und Budgetueberschreitungen,
6. der Reviewer kann pruefen, aber keine Arbeitsartefakte veraendern,
7. ein fester Benchmark weist den Nutzen gegenueber dem Einzelagenten nach.

GUI, erweiterte Authentifizierung und Multi-Model-Serving bauen auf diesem MVP
auf. Die GUI folgt direkt nach der Stabilisierung von DLL, CLI, Storage, Tools
und Server; die uebrigen Erweiterungen sind nicht Teil des ersten MVP.

## 18. Offene Entscheidungen vor Phase 1

- HTTP-Bibliothek nach dem SSE-Spike
- Catch2 oder GoogleTest gemaess Abhaengigkeitsaufwand
- Umfang der ersten Linux-Unterstuetzung
- Paketformat fuer Windows, beispielsweise ZIP und optional MSI
- Ranking-Gewichte und Mindestscore nach einem Retrieval-Evaluationssatz
- C++-MCP-Bibliothek oder schlanke eigene Protokollschicht nach einem
    Kompatibilitaets-Spike
- festzulegende MCP-Protokollversion und Umfang eines optionalen Legacy-SSE-Adapters
- Defaultwerte fuer Queue, Scheduler, KV-Cache, Prefix Caching und Keep-Alive nach
    CPU-/GPU-Benchmarks
- genaue OpenAI-Kompatibilitaetsmatrix und Zeitpunkt fuer `/v1/responses`
- Manifestformat fuer spaetere LoRA-, Multimodal- und Reranker-Erweiterungen
- Windows-Sandbox-Backend nach Escape-, Performance- und Deployment-Spike:
    AppContainer/restricted token oder Container/Windows Sandbox
- Copy-on-Write-Technik und atomarer Apply-Mechanismus auf NTFS
- Standardbudgets und welche Read-only-Tools im Plan-Modus verfuegbar sind
- Agent-Profile und Multiagent-Evaluationssatz
- Schwellwerte, ab denen Multiagent gegenueber Einzelagent aktiviert werden darf
- Zeitpunkt und Umfang einer optionalen A2A-1.0-Implementierung
- kommerzielle Qt-Lizenz oder dynamische LGPL-3.0-Distribution
- finale HTTP- und MCP-Bibliotheken nach Funktions- und Lizenzspike

Bereits entschieden:

- OpenVINO Runtime und OpenVINO GenAI `2026.2.1`
- primaeres Referenzmodell `Qwen2.5-1B-Instruct-fp16-test-ov`
- Embedding-Referenzmodell `Qwen3-Embedding-0.6B-fp16-ov`
- SQLite als lokaler Store mit FTS5 und austauschbarem Vektorindex
- `sqlite-vec` v0.1.6, vendort/statische Produktions-Integration fuer das aktuelle lokale Retrieval-Profil; fail-closed Initialisierung und kein stiller Per-Operation-Fallback; Backend-/Evidenz-Vertrag dokumentiert in [`docs/architecture/retrieval-backend-contract.md`](docs/architecture/retrieval-backend-contract.md)
- strukturelle Foreign Keys plus typisierte semantische Kanten mit Evidenz
- zentrale Tool Registry mit JSON-Schema-Validierung und Default-Deny-Policy
- MCP-Client fuer `stdio` und Streamable HTTP sowie separater eigener MCP-Server
- lokales JSON-Modellmanifest mit Registry, Hashpruefung und Capability-Angaben
- Continuous Batching, Admission Control und Prometheus-kompatible Metriken
- drei technisch getrennte Modi Chat, Plan und Agent
- Agent-Ausfuehrung nur ueber separaten Sandbox Worker und expliziten Apply-Schritt
- Multiagent nur als Supervisor/Worker-Task-DAG mit typisierten Events und Leases
- Apache-2.0 fuer den eigenen Projektcode; Drittanbieter und Modelle separat
- ein relocatable Standalone-Stage ist die einzige unterstuetzte Laufzeitwurzel
- Windows-Referenztoolchain MSVC 2022 x64, CMake 4.2.1 und Ninja 1.12.1
- Qt 6.10.1 mit dem noch zu installierenden Kit `msvc2022_64`
- CMake-Integration ueber `find_package(OpenVINOGenAI REQUIRED)` und
    `openvino::genai`