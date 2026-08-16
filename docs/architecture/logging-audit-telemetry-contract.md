# Logging, Audit & Telemetry Contract

## Architectural Separation

VINOX strictly distinguishes four distinct operational and observability concerns:

1. **Operational Logging** — Diagnostic and runtime events for operators and developers. Best-effort by default; logging failure must not fail normal inference or storage operations.
2. **Audit Evidence** — Durable, tamper-evident evidence for security- and governance-sensitive state transitions and actions (tool execution, policy approval, sandbox execution, MCP connections).
3. **Metrics / Telemetry** — Aggregatable quantitative measurements (request count, latency, TTFT, TPOT, throughput, queue time, error count). Metrics are represented independently and are not parsed from prose log messages.
4. **Active Audit Verification (`vinox-cli --audit`)** — Real-time system architecture testing evidence.

---

## Canonical Event Envelope

All VINOX operational log events follow a versioned, structured envelope schema (`event_schema_version: 1`):

| Field | Type | Description |
|---|---|---|
| `timestamp` | ISO-8601 UTC string | Event timestamp in UTC (e.g. `2026-08-16T02:20:00.123Z`) |
| `level` | Enum String | `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL` |
| `component` | String | Subsystem name (`core`, `serving`, `storage`, `tools`, `mcp`, `agent`, `sandbox`, `cli`) |
| `event` | String | Stable event identifier (e.g., `storage.message.add`, `serving.model.load`) |
| `event_schema_version` | uint32 | Schema version (always `1` for this baseline) |
| `request_id` | String / Null | Correlation identifier for client requests |
| `session_id` | String / Null | Correlation identifier for chat sessions |
| `run_id` | String / Null | Correlation identifier for agent runs |
| `task_id` | String / Null | Correlation identifier for agent tasks |
| `operation_id` | String / Null | Correlation identifier for sub-operations |
| `model_id` | String / Null | Loaded model revision or identifier |
| `backend` | String / Null | Active execution backend (`openvino`, `sqlite-vec`, etc.) |
| `duration_ms` | uint64 / Null | Duration of operation in milliseconds |
| `status` | String / Null | Result status (`OK`, `INVALID_ARGUMENT`, `RUNTIME_ERROR`) |
| `status_code` | uint32 / Null | Stable numeric status code |

---

## Default No-Content & No-Secret Privacy Invariants

### Excluded Content by Default
- Prompts, raw user queries, and full generated LLM text responses.
- Message, document, or chunk content.
- Tool arguments or execution results containing user data.
- Authorization headers, API keys, passwords, bearer tokens, cookies, or secrets.
- Raw embedding floating-point vectors.
- Process environment variables containing credentials.

### Centralized Redaction
- Automatic redaction is applied to text payloads before log emission:
  - `Authorization: Bearer <token>` $\rightarrow$ `Authorization: Bearer [REDACTED]`
  - `sk-[a-zA-Z0-9_-]{8,}` $\rightarrow$ `sk-[REDACTED]`
  - Key-value patterns matching `password=...`, `token=...`, `secret=...` $\rightarrow$ `[REDACTED]`
- Maximum field size limits prevent log amplification attacks.
- Diagnostic errors returned via `vinox_last_error()` follow the exact same secret/content redaction policy.

---

## Correlation Context Propagation

Correlation context survives across all VINOX process and DLL boundaries:

$$\text{CLI / GUI / Server} \longrightarrow \text{Core / Serving} \longrightarrow \text{Storage} \longrightarrow \text{Tools / MCP} \longrightarrow \text{Agent} \longrightarrow \text{Sandbox Worker}$$

- Correlation IDs are validated at ingress (bounded to $\le 64$ ASCII characters).
- C-ABI functions accept an optional `vinox_correlation_context` pointer.
- Child operations inherit parent IDs (`request_id`, `session_id`, `run_id`).
