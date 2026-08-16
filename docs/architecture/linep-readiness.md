# VINOX LiNeP / LiNeP-SL Readiness Contract

## Purpose

VINOX should be prepared to integrate LiNeP and LiNeP-SL as protocol/transport adapters without introducing LiNeP-specific business logic into Core, Serving, Storage, Tools, Agent, CLI, Server, or GUI.

The immediate goal is **readiness, not a hard dependency**: Phase 8 and Phase 9 must preserve protocol-neutral runtime contracts so LiNeP can later be added as another transport/peer adapter alongside local DLL calls, HTTP/SSE and MCP.

## Architectural invariant

> VINOX runtime semantics are transport-neutral. Session/correlation identity, sequencing, cancellation, capability metadata, bounded payload semantics, error states, governance and audit identity must be representable without depending on HTTP-, SSE-, MCP- or LiNeP-specific concepts.

A transport adapter may carry or transform canonical VINOX messages/events, but it must not become the owner of Agent, Tool, Policy, Approval or execution semantics.

## Canonical identity and sequencing

Transport-facing operations should preserve stable identifiers where applicable:

- `request_id`
- `session_id`
- `correlation_id`
- `run_id`
- `operation_id`
- event/chunk sequence number

Sequence scope must be explicit per logical stream. Reconnect/retry behavior must never merge unrelated session/correlation streams.

## Cancellation and terminal outcomes

Cancellation is a canonical runtime state, not a transport-specific event. LiNeP/LiNeP-SL integration must be able to represent at least:

- completed
- failed
- blocked
- cancelled
- timeout
- disconnected
- indeterminate / outcome unknown where a side effect cannot be proven cancelled

A disconnect must not silently become cancellation or success. Cancellation should propagate through Agent -> Tool/MCP -> Sandbox/Worker where supported.

## Capability and peer metadata

VINOX should expose protocol-neutral capability metadata suitable for later peer scheduling. Candidate metadata includes:

- supported models/backends
- CPU/GPU/NPU availability
- supported task classes
- context and payload limits
- current load/capacity
- Tool/MCP/Agent capabilities
- security-class ceiling / allowed execution scope
- supported streaming/cancellation semantics

Hardware type is metadata, not a scheduling policy embedded into the protocol. A future LiNeP fabric may therefore treat NPU-capable systems as worker resources without coupling VINOX Core or LiNeP wire semantics to one accelerator vendor.

## Governance boundary

LiNeP transport must never widen permissions.

- remote capability advertisement is untrusted input until validated
- Tool and Agent execution still passes the canonical VINOX registry/schema/policy/governance boundary
- delegation may preserve or reduce rights, never increase them implicitly
- transport metadata cannot approve a plan, tool call, workspace takeover or policy transition
- audit evidence remains linked to the canonical operation/run identities

## Bounded payload and fragmentation readiness

Canonical messages/events must have explicit size limits independent of transport framing. If LiNeP/LiNeP-SL fragmentation is used later:

- fragmentation/reassembly belongs to the transport layer
- logical payload identity remains stable across fragments
- fragment sequence is scoped to the exact logical stream
- malformed, duplicate, missing or out-of-order fragments fail deterministically
- reassembly limits prevent unbounded memory growth
- a partial/truncated payload can never be reported as successful complete data

## Phase 8 implication

Phase 8 CLI hardening should not introduce CLI- or HTTP-specific state into Core/Agent contracts. CLI is an adapter only. Process-level CLI tests should validate canonical statuses/events rather than inventing a second state model.

## Phase 9 implication

Before the HTTP/SSE server becomes the dominant remote adapter, define or preserve a small protocol-neutral transport/event abstraction for:

- request/response identity
- streaming chunks/events
- sequence/reconnect metadata
- cancellation
- capability advertisement
- typed terminal status
- bounded payloads

HTTP/SSE maps onto this abstraction; it is not the abstraction itself.

## Future LiNeP adapter

A future VINOX LiNeP adapter can then map:

```text
VINOX Core / Agent / Tools
          |
          v
Canonical Transport/Event Contract
      |        |        |
      v        v        v
   HTTP/SSE   MCP    LiNeP / LiNeP-SL
```

This allows VINOX to become a real reference peer for LiNeP and enables later cross-node workloads such as inference, Agent tasks, Tool execution and artifact/event streaming without either project depending on the internal architecture of the other.

## Acceptance invariant

> PASS/Verified may certify only interoperability and behavior actually exercised by the verification path. A locally correct event shape is not proof of LiNeP wire interoperability; real LiNeP support begins only when VINOX exchanges and validates real LiNeP/LiNeP-SL traffic end-to-end.
