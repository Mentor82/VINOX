# VINOX Architecture & Knowledge Graph Ingestion Script
$ErrorActionPreference = "Stop"

$baseUrl = "http://127.0.0.1:8080"
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "           VINOX Knowledge Ingestion & Graph Relationship Seeder               " -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# 1. Verify Server Connectivity
try {
    $health = Invoke-RestMethod -Uri "$baseUrl/health/live" -Method Get -TimeoutSec 5
    Write-Host "[1/4] Server is healthy: $($health.status)" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Server not reachable at $baseUrl" -ForegroundColor Red
    exit 1
}

# 2. Ingest Document 1: Hardware Hierarchy & NPU Execution
$doc1 = @{
    title = "VINOX Hardware Execution Hierarchy & Intel AI Boost NPU Multi-Precision Architecture"
    content = @"
VINOX is an ultra-high performance neural inference server optimized for Intel Core Ultra processors and OpenVINO 2026.3.
The execution engine enforces a strict 3-tier hardware priority hierarchy:
1. Intel AI Boost NPU (Priority 1): Neural Processing Unit dedicated to power-efficient, zero-latency inference across multiple neural precisions: INT4 low-bit quantization, INT8 integer weights and activations, and native FP16 half-precision floating-point tensor execution. The NPU accelerates both language models and multimodal workloads while maximizing battery life and compute efficiency.
2. Intel Arc Graphics iGPU (Priority 2): High-throughput integrated GPU providing acceleration fallback when model topologies, massive parameter counts, or tensor dimensions exceed NPU capabilities.
3. Intel Core Ultra 7 155H CPU (Priority 3): Robust host execution target utilizing AVX2/AVX-512 neural extensions for general-purpose execution and decoupled embedding generation.
VINOX ensures zero-overhead failover between hardware targets without interrupting active client HTTP and SSE streams.
"@
    relations = @(
        @{ source_id = "VINOX"; target_id = "Intel_AI_Boost_NPU"; type = "prioritizes_hardware"; evidence = "Strict Priority 1 execution target"; confidence = 1.0 },
        @{ source_id = "Intel_AI_Boost_NPU"; target_id = "INT4_Quantization"; type = "accelerates"; evidence = "High-efficiency INT4 quantized LLM pipeline"; confidence = 1.0 },
        @{ source_id = "Intel_AI_Boost_NPU"; target_id = "INT8_Quantization"; type = "accelerates"; evidence = "Integer 8-bit quantized weights and activations"; confidence = 1.0 },
        @{ source_id = "Intel_AI_Boost_NPU"; target_id = "FP16_Precision"; type = "accelerates"; evidence = "Native FP16 half-precision tensor execution"; confidence = 1.0 },
        @{ source_id = "Intel_AI_Boost_NPU"; target_id = "Mixed_Precision"; type = "accelerates"; evidence = "Dynamic hybrid quantization pipelines"; confidence = 0.98 },
        @{ source_id = "VINOX"; target_id = "Intel_Arc_GPU"; type = "fallback_tier_2"; evidence = "iGPU acceleration for large models"; confidence = 0.95 },
        @{ source_id = "VINOX"; target_id = "Intel_Core_Ultra_CPU"; type = "fallback_tier_3"; evidence = "Host CPU AVX execution"; confidence = 0.90 }
    )
}

Write-Host "[2/4] Ingesting Document 1 (Hardware Execution)..." -ForegroundColor Yellow
$json1 = $doc1 | ConvertTo-Json -Depth 5
$res1 = Invoke-RestMethod -Uri "$baseUrl/v1/documents" -Method Post -Body $json1 -ContentType "application/json; charset=utf-8"
Write-Host "       Ingested Document ID: $($res1.document_id) (Relations: $($res1.relations_created))" -ForegroundColor Green

# 3. Ingest Document 2: Hybrid Retrieval & Vector Search
$doc2 = @{
    title = "VINOX Hybrid Retrieval, sqlite-vec Dense Vectors & Graph CTE Engine"
    content = @"
VINOX integrates a zero-dependency hybrid retrieval engine combining lexical BM25 ranking via SQLite FTS5 with dense vector KNN search via the sqlite-vec extension and graph-based CTE relation weighting.
The system dynamically computes fusion scores using configurable alpha parameters:
- Pure Keyword Search (BM25): alpha = 0.0
- Balanced Hybrid Retrieval: alpha = 0.5 (Reciprocal Rank Fusion RRF k=60)
- Pure Semantic Vector Search: alpha = 1.0
Embeddings are generated using the decoupled Qwen3-Embedding-0.6B engine (1024 dimensions) operating on the host CPU. Documents are segmented into structured chunks with SHA-256 provenance hashes and stored in canonical WAL-mode SQLite databases.
"@
    relations = @(
        @{ source_id = "VINOX"; target_id = "Hybrid_Retrieval_Engine"; type = "integrates"; evidence = "BM25 + sqlite-vec vector engine"; confidence = 0.98 },
        @{ source_id = "Hybrid_Retrieval_Engine"; target_id = "BM25_FTS5"; type = "lexical_index"; evidence = "Full-text search ranking"; confidence = 1.0 },
        @{ source_id = "Hybrid_Retrieval_Engine"; target_id = "sqlite_vec"; type = "dense_vector_knn"; evidence = "Zero-dependency vector search"; confidence = 1.0 },
        @{ source_id = "Hybrid_Retrieval_Engine"; target_id = "Qwen3_Embedding"; type = "encodes_via"; evidence = "Decoupled 1024-dim embedding model"; confidence = 0.97 },
        @{ source_id = "Hybrid_Retrieval_Engine"; target_id = "Knowledge_Graph_CTE"; type = "graph_reranking"; evidence = "Recursive relation confidence boost"; confidence = 0.95 }
    )
}

Write-Host "[3/4] Ingesting Document 2 (Hybrid Retrieval & Vector Search)..." -ForegroundColor Yellow
$json2 = $doc2 | ConvertTo-Json -Depth 5
$res2 = Invoke-RestMethod -Uri "$baseUrl/v1/documents" -Method Post -Body $json2 -ContentType "application/json; charset=utf-8"
Write-Host "       Ingested Document ID: $($res2.document_id) (Relations: $($res2.relations_created))" -ForegroundColor Green

# 4. Ingest Document 3: Native Reasoning Channel & Agent Protocols
$doc3 = @{
    title = "VINOX Native Dual Reasoning Channel & Minja Template Protocol Contract"
    content = @"
In OpenVINO 2026.3, VINOX separates model output into distinct semantic streams: VINOX_STREAM_CHANNEL_REASONING and VINOX_STREAM_CHANNEL_FINAL. Rather than parsing synthetic <think> and </think> string markers, VINOX decodes the reasoning process natively at the generator layer.
The Qt6 desktop GUI and Web Studio route reasoning tokens directly to collapsible ReasoningCard components with real-time token throughput metrics and duration timing.
Minja-compiled template protocol contracts guarantee strict conformity with Jinja chat templates and HuggingFace tokenizer configurations.
External tools and autonomy are managed through the Model Context Protocol (MCP) server integration.
"@
    relations = @(
        @{ source_id = "VINOX"; target_id = "Reasoning_Channel"; type = "native_stream"; evidence = "Dual stream separation"; confidence = 1.0 },
        @{ source_id = "Reasoning_Channel"; target_id = "ReasoningCard_UI"; type = "renders_to"; evidence = "Collapsible reasoning card component"; confidence = 0.99 },
        @{ source_id = "VINOX"; target_id = "Minja_Protocol"; type = "compiles_contract"; evidence = "Jinja2 template protocol compilation"; confidence = 0.96 },
        @{ source_id = "VINOX"; target_id = "MCP_Protocol"; type = "dispatches_tools"; evidence = "JSON-RPC 2.0 tool execution"; confidence = 0.95 },
        @{ source_id = "MCP_Protocol"; target_id = "DeepSeek_Tools"; type = "powers_agent"; evidence = "Autonomous agent tool calling"; confidence = 0.94 }
    )
}

Write-Host "[4/4] Ingesting Document 3 (Reasoning Channel & MCP Protocol)..." -ForegroundColor Yellow
$json3 = $doc3 | ConvertTo-Json -Depth 5
$res3 = Invoke-RestMethod -Uri "$baseUrl/v1/documents" -Method Post -Body $json3 -ContentType "application/json; charset=utf-8"
Write-Host "       Ingested Document ID: $($res3.document_id) (Relations: $($res3.relations_created))" -ForegroundColor Green

Write-Host "`nKnowledge indexing and Graph CTE seeding completed successfully!" -ForegroundColor Green
