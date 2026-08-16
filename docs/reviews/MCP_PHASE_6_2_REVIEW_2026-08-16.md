# VINOX Phase 6.2 MCP Review — 2026-08-16

## Review status

**Not accepted yet.** The current Phase 6.2 implementation establishes useful C-ABI shapes and test scaffolding, but the runtime behavior is still largely simulated and does not yet satisfy the MCP transport/protocol contracts claimed by the code and audit.

## Blocking findings

1. **Streamable HTTP is not implemented.** `connect()` marks the client connected without opening an HTTP connection, sending MCP requests, validating response headers/status, or parsing JSON-RPC responses.
2. **stdio is only subprocess creation.** Windows child process launch and pipe creation exist, but there is no MCP JSON-RPC write/read framing loop over the pipes.
3. **Legacy 2024-11-05 behavior is conflated with later Streamable HTTP sessions.** MCP 2024-11-05 uses the legacy HTTP+SSE model with an SSE endpoint that supplies a POST endpoint. `Mcp-Session-Id` belongs to later Streamable HTTP-era session semantics, not the 2024-11-05 transport contract.
4. **Legacy initialize/initialized is constructed but never sent.** `connect()` builds the initialize JSON object but performs no transport round trip or protocol-version/capability validation.
5. **Tool discovery is simulated.** `list_tools()` constructs a hard-coded `query` tool rather than issuing `tools/list` and parsing the server response.
6. **Tool execution is simulated.** `call_tool()` creates a local synthetic success response and never sends `tools/call` to an MCP server.
7. **Resources and prompts are simulated.** list/read/get methods synthesize local values instead of executing MCP primitives.
8. **Modern 2026-07-28 request envelope is incomplete.** The modern revision requires self-describing requests with protocol/client metadata and Streamable HTTP routing headers; current code does not implement these transport semantics.
9. **Capability negotiation/discovery is missing.** PLAN requires capability negotiation. 2026-07-28 replaces mandatory initialize with stateless requests and optional `server/discover`; legacy revisions require initialize negotiation. Neither path is actually performed.
10. **Audit evidence overstates runtime coverage.** `vinox-cli --audit` claims Streamable HTTP, legacy handshake/SSE, stdio process/pipe execution, tool discovery/policy binding, resources and prompts as Verified although the audit path does not exercise real transport round trips and several implementations are synthetic.

## Required architecture alignment

- Treat `2026-07-28` as the primary stateless MCP profile.
- Implement real Streamable HTTP request/response transport with the required modern routing/version metadata and JSON-RPC validation.
- Implement real stdio JSON-RPC framing over child-process stdin/stdout; stderr remains diagnostic only.
- Implement protocol/version negotiation policy explicitly:
  - 2026-07-28: stateless request metadata, optional `server/discover` when capabilities are needed.
  - initialize-era compatibility: execute `initialize`, validate negotiated protocol version/capabilities, then send `notifications/initialized`.
  - 2024-11-05 legacy HTTP+SSE: model the SSE endpoint + server-provided POST endpoint correctly; do not model it as an `Mcp-Session-Id` Streamable HTTP session.
- If support for later session-based Streamable HTTP revisions is desired, model those revisions explicitly instead of folding them into `2024-11-05`.
- Bind discovered MCP tools into the VINOX registry only from validated server responses; do not assign security class solely from local hard-coded assumptions without policy/allowlist mapping.
- Real tool/resource/prompt operations must pass through transport, timeout/cancel/output-size limits, correlation, logging/audit, and JSON-RPC error handling.

## Acceptance criteria

- Real in-process/local fixture server tests prove Streamable HTTP request/response behavior for the primary profile.
- Real stdio fixture child proves newline-delimited MCP messages and request/response correlation.
- Modern requests prove required protocol/routing metadata and reject mismatched/malformed responses.
- Legacy initialize-era test proves initialize response version/capability negotiation and `notifications/initialized`.
- 2024-11-05 legacy SSE compatibility, if retained, proves endpoint-event discovery and POST routing using the actual legacy transport semantics.
- `tools/list`, `tools/call`, `resources/list`, `resources/read`, `prompts/list`, and `prompts/get` are driven by actual JSON-RPC responses rather than synthetic local objects.
- JSON-RPC `id`, error responses, malformed responses, timeout, disconnect and cancellation have deterministic behavior.
- No method reports `connected=true` merely because configuration was accepted; connection/readiness semantics must correspond to an actually usable transport state.
- Audit output only labels as Verified paths that it actually executes.
- PLAN is updated if the concrete compatibility matrix differs from the current prose.

## Architectural invariant

> MCP capability is established by interoperable wire behavior, not by locally constructing the right JSON shape. A transport is only supported when VINOX can exchange and validate real MCP messages over that transport and revision; compatibility labels must match the official semantics of that revision, and audit evidence must certify only exercised wire paths.

— Nephy 🔎
