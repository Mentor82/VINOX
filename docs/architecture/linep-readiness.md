# LiNeP & LiNeP-SL Protocol-Neutral Transport Readiness Contract

## Overview

This specification establishes the architectural invariants required to ensure VINOX Core, Serving, Storage, Tool, MCP, and Agent contracts remain strictly protocol-neutral and ready for future LiNeP (Lightweight Network Protocol) and LiNeP-SL (Lightweight Network Protocol - Stream Layer) transport adapters without altering runtime execution or governance.

---

## 1. Protocol-Neutral Context & Identity Propagation

All VINOX execution envelopes and events must maintain transport-neutral identity fields:

- `request_id`: Cryptographic or UUID string identifying the single invocation request.
- `session_id`: Canonical session/conversation identifier (`vinox_conversation_info.id`).
- `correlation_id`: End-to-end tracing identifier spanning across CLI, HTTP, MCP, and LiNeP process boundaries.
- `run_id` / `operation_id`: Unique identifier for Agent runs and long-running background tasks.

**Invariant**: Neither Core logic nor Governance Policy Engines may depend on HTTP headers (`X-Request-ID`, `Authorization`), MCP JSON-RPC headers, or LiNeP frame headers to establish identity. Identifiers are extracted by transport adapters into the canonical `vinox_correlation_context` structure.

---

## 2. Canonical Typed Terminal States

All execution paths (Inference, Tools, Agent Steps, Sandbox Actions) emit canonical, transport-neutral terminal outcomes:

| Terminal Status | Description |
| :--- | :--- |
| `COMPLETED` | Execution completed successfully meeting all schema, policy, and step invariants. |
| `FAILED` | Execution failed due to runtime error, exception, or non-zero worker status. |
| `BLOCKED` | Execution blocked due to missing governance handles or pending user approval. |
| `PERMISSION_DENIED` | Policy engine explicitly denied tool or capability access (Default-Deny). |
| `CANCELLED` | Execution interrupted via `SIGINT`, client disconnect, or budget cancellation. |
| `TIMED_OUT` | Execution exceeded maximum duration deadline or host read timeout. |
| `INDETERMINATE` | State uncertain due to unexpected process termination during mutating operation. |

**Invariant**: CLI, HTTP/SSE, MCP, and LiNeP adapters map these canonical statuses to their respective wire representations. No adapter may mask `FAILED`, `CANCELLED`, or `INDETERMINATE` as a successful completion.

---

## 3. Transport-Neutral Cancellation Architecture

Cancellation is a runtime execution contract, not a UI or transport event:

- Interrupt signals (`SIGINT`, `Ctrl+C`), HTTP connection drops, MCP request cancellations, or LiNeP `CANCEL` frames trigger cancellation on the active `vinox_agent_run` or `vinox_model_generate` context.
- Cancellation propagates fail-closed down to model generation pipelines, MCP client transports, and Windows Sandbox worker subprocesses (`TerminateProcess`).
- Cancellation outcome is recorded as `CANCELLED` or `INDETERMINATE_OUTCOME_MUTATION_CANCELLED`.

---

## 4. Generic Capability & Resource Metadata

Capability discovery and hardware resource reporting must use generic metadata structures:

```json
{
  "engine_version": "1.0.0",
  "abi_version": 1,
  "backends": ["CPU", "NPU", "GPU"],
  "security_classes": ["READ_ONLY", "LOCAL_READ", "LOCAL_WRITE", "NETWORK_CLIENT", "SYSTEM_EXEC"],
  "limits": {
    "max_bounded_payload_bytes": 262144,
    "max_array_elements": 1024,
    "pipe_timeout_ms": 5000
  }
}
```

**Invariant**: Capability metadata provides uniform advertising across local CLI, HTTP endpoints, and future LiNeP channels without coupling scheduling to vendor-specific accelerator APIs.

---

## 5. Payload Bounding vs. Transport Fragmentation

- **Payload Bounding**: VINOX enforces a strict 256 KB (262,144 bytes) payload limit on tool argument JSON payloads and step outputs at the governance boundary (`vinox_tool_registry_validate_arguments`).
- **Transport Fragmentation**: LiNeP-SL stream layer framing or HTTP chunking operates strictly below the governance boundary. Reassembled frames are validated against the 256 KB limit before reaching execution engines.
- **Invariant**: Transport-level fragmentation/reassembly NEVER alters canonical operation identity, payload bounds, or policy evaluation.

---

## 6. Governance & Permissions Preservation

- Remote transport adapters (HTTP, MCP, LiNeP) cannot widen, override, or bypass VINOX tool registries, schema validators, or policy engines.
- All remote operations execute under the same immutable Mode Controller policies (`CHAT`, `PLAN`, `AGENT`) and default-deny policy rules as local CLI calls.

---

## 7. Protocol Adapter Independence

VINOX exposes a minimal protocol-neutral request/response and streaming event interface:

```c
typedef struct vinox_event_envelope {
    uint32_t event_schema_version;
    const char* event_type;
    const char* status;
    const char* timestamp_iso;
    const char* correlation_id;
    const char* payload_json;
} vinox_event_envelope;
```

Adapters for:
1. **Local CLI**: Formats envelopes for stdout or structured `--json`.
2. **HTTP/SSE (Phase 9)**: Maps envelopes to `text/event-stream` SSE events and OpenAI DTOs.
3. **MCP (Phase 6)**: Maps envelopes to JSON-RPC 2.0 notifications/responses.
4. **LiNeP / LiNeP-SL (Future)**: Maps envelopes to binary LiNeP stream frames.

**Invariant**: LiNeP/LiNeP-SL support remains an optional transport adapter without introducing hard build or runtime dependencies into Phase 8 or Phase 9 core binaries.
