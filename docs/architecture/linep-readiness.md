# LiNeP & LiNeP-SL Protocol-Neutral Transport Readiness Contract

## Overview

This specification establishes the architectural invariants required to ensure VINOX Core, Serving, Storage, Tool, MCP, and Agent contracts remain strictly protocol-neutral and ready for future LiNeP (Lightweight Network Protocol) and LiNeP-SL (Lightweight Network Protocol - Stream Layer) integration without altering runtime execution or governance.

LiNeP integration is explicitly optional. VINOX remains fully functional as a standalone runtime without LiNeP or LiNeP-SL present.

---

## 0. Integration Boundary: Plugin vs. Codec

LiNeP is integrated into VINOX as an **optional transport plugin / protocol adapter**, not as a Core dependency and not merely as a codec.

The intended boundary is:

```text
VINOX Core / Serving / Agent / Tools
                |
                v
   Canonical Messages / Events
                |
                v
      Transport Adapter API
        |       |       |
        v       v       v
      Local   HTTP/SSE  MCP   ...
                         |
                         v
              optional LiNeP plugin
                         |
                         v
                  LiNeP-SL codec
```

A future implementation may use a package/artifact name such as `vinox_transport_linep` for the optional adapter.

**Definitions:**

- **LiNeP plugin / adapter**: owns protocol/session integration, identity mapping, capability exchange, cancellation mapping, transport evidence, connection lifecycle, and the bridge between VINOX canonical envelopes and LiNeP.
- **LiNeP-SL codec**: lives inside or below that adapter and owns wire framing, serialization/deserialization, fragmentation/reassembly, and stream-layer representation.
- **VINOX Core**: remains unaware of LiNeP frame formats, session headers, connection APIs, or codec implementation details.

**Invariant**: LiNeP may extend transport reach, but it may not become a second execution engine, governance layer, scheduler authority, or business-logic implementation.

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

A discovered or advertised hardware capability means only that a host **can** provide that capability. It does not mean the resource is currently free, schedulable, or authorized for remote use.

---

## 5. Payload Bounding vs. Transport Fragmentation

- **Payload Bounding**: VINOX enforces a strict 256 KB (262,144 bytes) payload limit on tool argument JSON payloads and step outputs at the governance boundary (`vinox_tool_registry_validate_arguments`).
- **Transport Fragmentation**: LiNeP-SL stream-layer framing or HTTP chunking operates strictly below the governance boundary. Reassembled frames are validated against the 256 KB limit before reaching execution engines.
- **Invariant**: Transport-level fragmentation/reassembly NEVER alters canonical operation identity, payload bounds, or policy evaluation.

LiNeP-SL therefore acts as the **codec / stream representation layer** beneath the optional LiNeP transport plugin. Fragmentation semantics are transport concerns and must not leak into VINOX business or governance contracts.

---

## 6. Governance & Permissions Preservation

- Remote transport adapters (HTTP, MCP, LiNeP) cannot widen, override, or bypass VINOX tool registries, schema validators, or policy engines.
- All remote operations execute under the same immutable Mode Controller policies (`CHAT`, `PLAN`, `AGENT`) and default-deny policy rules as local CLI calls.
- Remote discovery of a capability never grants permission to consume it.
- A LiNeP peer cannot raise host resource limits, change execution priority, or override local admission decisions.

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
4. **LiNeP (Future, optional plugin)**: Maps canonical envelopes to LiNeP protocol operations.
5. **LiNeP-SL (inside/below LiNeP plugin)**: Encodes/decodes stream frames and transport fragmentation without owning VINOX execution semantics.

**Invariant**: LiNeP/LiNeP-SL support remains optional and introduces no hard build or runtime dependency into VINOX Core, CLI, Server, GUI, Tool, or Agent binaries.

---

## 8. Host Resource Governance & Coexistence

Host resource governance becomes especially important when VINOX is not used standalone but participates as a LiNeP worker in a distributed fabric.

A remote workload must never infer "available" from "advertised". Local user interaction and host policy have precedence over remote work.

The intended scheduling boundary is:

```text
LiNeP remote workload
        |
        v
   Admission Control
        |
        v
     Host Policy
        |
        v
 Resource Governor
        |
        v
  Device Scheduler
   |     |     |
  CPU   GPU   NPU
```

The local host remains the final authority over:

- CPU, GPU, NPU, RAM/shared-memory, storage-I/O, network, thermal, and power budgets,
- whether remote workloads are allowed at all,
- which devices and models may be used,
- concurrency and queue depth,
- background/balanced/dedicated execution profiles,
- pause, defer, reject, backpressure, and cancellation decisions.

Suggested execution profiles:

- `background`: cooperative low-impact use; yields aggressively to interactive host activity.
- `balanced`: higher resource use while retaining explicit host headroom.
- `dedicated`: full-load mode for benchmark, maintenance, or explicitly dedicated systems.

**Critical LiNeP invariant**: LiNeP may discover and advertise host capabilities, but it may **never take ownership of local resource policy**. Remote scheduling is advisory until accepted by local Admission Control and Host Policy.

Where a requested workload cannot be admitted immediately, the adapter must surface a truthful typed result such as queued/deferred/busy/rejected semantics rather than silently oversubscribing the host.

---

## 9. Coexistence & Hardware Evidence

Future distributed deployment acceptance should include reproducible coexistence evidence, especially before VINOX is deployed to interactive workstations or employee laptops.

At minimum, benchmark:

- `VINOX off` baseline,
- `background`,
- `balanced`,
- `dedicated/full-load`.

Evidence should record hardware identity and configuration together with user-impact and workload metrics, including where available:

- CPU/GPU/NPU model and driver/plugin versions,
- RAM and shared-memory configuration,
- operating-system/build version,
- OpenVINO/OpenVINO GenAI version,
- explicit execution device (`CPU`, `GPU`, `NPU`; no unreported fallback),
- model identifier, precision, and model/IR hash,
- corpus/workload hash,
- CPU/GPU/NPU utilization,
- RAM/shared-memory pressure and paging,
- storage/network I/O,
- thermal/power throttling,
- interactive/user-facing latency,
- VINOX queue time, job latency, throughput, TTFT/TPOT where applicable.

**Evidence invariant**: A hardware-execution PASS proves only that the workload actually executed on the reported hardware/device. It does not by itself prove acceptable model quality, acceptable host coexistence, or suitability for distributed LiNeP deployment. Those claims require their own exercised verification paths.
