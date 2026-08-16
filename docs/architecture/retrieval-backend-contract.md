# VINOX Retrieval Backend Contract

Status: Canonical architecture contract for Phase 5.2 retrieval backends.

## Production backend

`sqlite-vec` is the required, versioned production vector backend for the local VINOX storage path.

- VINOX vendors and builds an explicitly pinned `sqlite-vec` version as part of `vinox_storage`.
- The production path MUST initialize the `vec0` backend on the active SQLite connection before the storage engine is reported ready.
- Failure to initialize the required production backend is fail-closed for the production storage configuration.
- A successful production storage audit MUST prove that the active backend kind is `VINOX_VECTOR_BACKEND_SQLITE_VEC` and MUST execute at least one query through that backend.

## Backend selection and fallback

Backend selection is fixed per storage-engine instance.

- `VINOX_VECTOR_BACKEND_SQLITE_VEC` means vector writes and vector queries are actually executed through the `sqlite-vec`/`vec0` path.
- A brute-force implementation may exist only as an explicitly selected reference/development backend.
- A reference backend MUST report `VINOX_VECTOR_BACKEND_BRUTE_FORCE_REF` and MUST NOT be presented as production `sqlite-vec` retrieval.
- A write or query failure on the active production backend MUST NOT silently fall back per operation to a different persistence or retrieval path.
- Any future configurable fallback must perform an explicit engine-level backend transition with observable state and corresponding audit evidence.

## Dimensions and embedding profiles

The current Phase 5.2 production index uses the 1024-dimensional embedding profile associated with the VINOX embedding reference model.

- Query/index dimension mismatch is an explicit error.
- Future support for multiple dimensions or embedding models requires a persisted embedding/index profile containing at least model identity, dimension, pooling strategy and normalization strategy.
- Re-indexing after an incompatible embedding-profile change must be explicit and reproducible.

## Hybrid ranking

Hybrid retrieval combines independently meaningful text and vector signals.

- Text relevance originates from the real SQLite FTS5 `bm25(...)` function.
- Vector relevance originates from the active vector backend.
- Score direction and normalization are defined by VINOX before fusion.
- `alpha` is constrained to `[0.0, 1.0]`.
- Equal hybrid scores use deterministic secondary ordering.
- Any public field named `bm25_score`, `vector_score` or `hybrid_score` must represent the documented scoring path rather than a proxy or placeholder value.

## Audit evidence

Audit output is evidence, not decoration.

A PASS statement may only claim an invariant that was exercised by the current audit execution. In particular, the Phase 5.2 audit must verify:

1. the active vector backend kind,
2. successful indexed vector write and retrieval,
3. real BM25 score variation across differently relevant candidates,
4. deterministic hybrid ranking,
5. invalid-alpha rejection,
6. embedding-dimension mismatch rejection.

Regression tests may provide additional evidence, but a live-audit line labeled `Verified` must correspond to a check performed by that live audit.

---

Architecture review signum: **Nephy 🔎**
