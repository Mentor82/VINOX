# VINOX PLAN.md Architecture Review — 2026-08-16

Review scope: consistency of `PLAN.md` with the implemented Phase 4 / Phase 5.1 / Phase 5.2 architecture and the current repository direction.

## Executive summary

`PLAN.md` remains structurally strong, but it now mixes three different kinds of information:

1. durable architecture invariants,
2. implementation roadmap/status,
3. historical/local environment notes.

After the Phase 4 and Phase 5 hardening work, several statements are stale or too broad. The main recommendation is to keep the durable architecture, but reconcile the execution roadmap with what is actually implemented and split Phase 5 into explicit sub-phases.

## High-priority inconsistencies

### 1. Phase 4 still names the removed custom JSON parser

The Phase 4 checklist still describes an autonomous `json.hpp` parser as completed. The implemented architecture was intentionally moved to `nlohmann/json` after architecture review.

Recommended replacement:

- `nlohmann/json` as the single production JSON foundation for manifests and later protocol surfaces.
- The former custom parser remains research/experimental material outside the VINOX production path.

### 2. Phase 5 is too coarse for the actual implementation state

The current single "Phase 5: Storage, Embeddings und Retrieval" contains relational persistence, chat branches, embeddings, hybrid retrieval, relations, delete/export/import/backup.

The repository has already reached distinct milestones that should be represented explicitly:

- **Phase 5.1 — SQLite persistence & FTS foundation**: migrations, WAL/FK invariants, C ABI, conversation/message persistence, FTS5.
- **Phase 5.2 — Embeddings & hybrid retrieval**: 1024-dim embedding profile, L2 normalization, `sqlite-vec`, real FTS5 BM25, vector backend contract, deterministic fusion/audit evidence.
- **Phase 5.3 — Documents, chunks & typed semantic relations**: document/chunk schema, relation evidence, recursive relation queries, relation-assisted retrieval.
- **Phase 5.4 — Storage lifecycle & portability**: delete/re-index, export/import, backup, recovery and migration compatibility.

This avoids marking all of Phase 5 complete while large PLAN-defined storage capabilities are still intentionally future work.

### 3. `sqlite-vec` is still listed as an open pre-Phase-1 decision

`PLAN.md` still lists the exact `sqlite-vec` version and static-vs-loadable integration as an open decision. That is now decided by implementation: VINOX vendors `sqlite-vec` v0.1.6 and statically integrates/initializes it for the production local vector path.

Move this item from "open" to "decided" and reference the canonical retrieval backend contract.

### 4. Embedding metadata contract is stronger than the current Phase 5.2 persistence

The plan states that model ID, dimension, pooling and normalization are stored with every embedding so that model changes can trigger targeted re-indexing.

The present Phase 5.2 implementation establishes a 1024-dimensional production index, but the full persisted embedding-profile metadata contract should remain an explicit requirement for multi-model/multi-profile evolution rather than being treated as already complete.

Recommendation: make the current 1024-dim profile explicit for Phase 5.2 and schedule persistent model/profile metadata before multiple embedding models or dimensions are permitted.

### 5. Hybrid retrieval architecture should link to an explicit scoring/backend contract

The semantic-search section correctly says that candidates are normalized and weighted, but it does not specify the production backend/fallback/audit semantics that became necessary during Issue #6.

Canonical companion document:

- `docs/architecture/retrieval-backend-contract.md`

The PLAN should reference it and summarize these invariants:

- production local vector backend = versioned `sqlite-vec`/`vec0`,
- backend selection fixed per engine instance,
- no silent operation-level fallback,
- real FTS5 BM25 source,
- documented score normalization/fusion,
- live audit proves the executed path.

## Medium-priority cleanup

### 6. "Offene Entscheidungen vor Phase 1" is historically stale

Phase 1 is long past. Rename to something like `Offene Architektur- und Produktentscheidungen` and separate:

- unresolved decisions,
- decided architecture,
- historical development-environment notes.

### 7. Planned project structure no longer fully matches the implementation layout

The plan shows `src/storage/sqlite/`, while the current storage implementation uses `src/storage/db.cpp`, `embeddings.cpp`, `vector_index.cpp`, and vendored sqlite-vec sources.

Either label the tree explicitly as a target layout or update it to the current modular structure.

### 8. Storage schema section describes the target schema, not the current Phase 5.1 schema

The relational schema includes workspaces, message parts, documents, chunks, embedding models, nodes, relations, tool/agent tables and more. This is useful as a target architecture, but it should be labeled as such so it cannot be mistaken for the current migration state.

Recommended split:

- `Current storage baseline`
- `Target relational schema`

### 9. Atomic transaction statement needs a milestone boundary

The storage architecture says message, embedding and relation updates are atomic. The current public API exposes these as separate operations.

Keep the invariant for workflows that require a compound commit, but assign its implementation to the phase where relation/document ingestion is added. Do not imply Phase 5.1/5.2 already provides a general cross-operation transaction contract.

### 10. Retrieval currently specifies three signals, while Phase 5.2 implements two

The target semantic-search section lists vector, FTS5 and relation signals. Phase 5.2 intentionally delivers vector + FTS5. Relation-assisted retrieval belongs to Phase 5.3.

This should be explicit in the roadmap.

## Durable strengths worth preserving

The following PLAN areas are internally consistent and should remain architectural anchors:

- C ABI boundary rules and opaque handles.
- strict separation of backend/protocol/UI.
- relocatable standalone stage and no silent global runtime fallback.
- forward-only versioned SQL migrations.
- default-deny tool/MCP security model.
- mode separation between Chat, Plan and Agent.
- sandboxed side effects and explicit apply step.
- evidence-based agent/reviewer model.
- capability matrices instead of pretending compatibility.

## Recommended PLAN status language

Use three explicit markers consistently:

- `Target architecture` — required end-state, not necessarily implemented.
- `Implemented` — present in code and covered by evidence/tests.
- `Deferred` — intentionally scheduled for a named later phase.

Avoid marking a broad phase complete when only a subset of its target architecture is present.

## Suggested immediate PLAN changes

1. Replace the stale Phase-4 `json.hpp` completion item with `nlohmann/json` production integration.
2. Split Phase 5 into 5.1 / 5.2 / 5.3 / 5.4.
3. Mark Phase 5.1 and 5.2 implemented/hardened.
4. Reference `docs/architecture/retrieval-backend-contract.md` from Technical Basis and Semantic Search.
5. Move sqlite-vec v0.1.6 static integration into `Bereits entschieden`.
6. Remove it from the open-decision list.
7. Label the large relational schema as target schema.
8. Preserve persistent embedding-profile metadata as a required evolution invariant.
9. Rename the open-decisions section so it no longer claims to be "before Phase 1".
10. Add acceptance evidence for the production vector backend and truthful live-audit semantics.

---

Architecture review signum: **Nephy 🔎**
