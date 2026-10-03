#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "src/server/server_context.hpp"
#include "src/server/http_server.hpp"
#include "src/thirdparty/httplib/httplib.h"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Phase 9 — OpenAI Compatible Server & Protocol Contract Smoke Test       \n";
    std::cout << "================================================================================\n";

    vinox::server::ServerContext ctx;
    ctx.host = "127.0.0.1";
    ctx.port = 18080;
    ctx.model_path = "mock";
    ctx.db_path = "test_server_smoke.db";

    // Initialize mock model and storage
    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = "mock";
    m_opts.device = "CPU";
    vinox_model_load(&m_opts, &ctx.model);

    vinox_storage_engine_open(ctx.db_path.c_str(), &ctx.storage);
    vinox_tool_registry_create(&ctx.tool_registry);
    ctx.mode_controller = vinox_mode_controller_create();

    vinox::server::HttpServer server(ctx);

    std::thread server_thread([&server]() {
        server.start();
    });

    // Give server time to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client client("127.0.0.1", 18080);
    client.set_read_timeout(5, 0);

    // TEST 01: Health Live Probe
    {
        std::cout << "[TEST 01] GET /health/live ... ";
        auto res = client.Get("/health/live");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["status"] == "ok");
        std::cout << "[ PASS ]\n";
    }

    // TEST 02: Health Ready Probe
    {
        std::cout << "[TEST 02] GET /health/ready ... ";
        auto res = client.Get("/health/ready");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["status"] == "ready");
        assert(j["model_loaded"] == true);
        assert(j["storage_ready"] == true);
        std::cout << "[ PASS ]\n";
    }

    // TEST 03: Prometheus Metrics
    {
        std::cout << "[TEST 03] GET /metrics ... ";
        auto res = client.Get("/metrics");
        assert(res && res->status == 200);
        assert(res->body.find("vinox_requests_total") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 04: OpenAPI Specification
    {
        std::cout << "[TEST 04] GET /openapi.yaml ... ";
        auto res = client.Get("/openapi.yaml");
        assert(res && res->status == 200);
        assert(res->body.find("openapi: 3.1.0") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 05: List Models
    {
        std::cout << "[TEST 05] GET /v1/models ... ";
        auto res = client.Get("/v1/models");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["object"] == "list");
        assert(!j["data"].empty());
        std::cout << "[ PASS ]\n";
    }

    // TEST 06: Synchronous Chat Completion
    {
        std::cout << "[TEST 06] POST /v1/chat/completions (synchronous) ... ";
        nlohmann::json req_body = {
            {"model", "mock"},
            {"messages", nlohmann::json::array({
                {{"role", "user"}, {"content", "Hello VINOX!"}}
            })},
            {"max_tokens", 32}
        };

        auto res = client.Post("/v1/chat/completions", req_body.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["object"] == "chat.completion");
        assert(!j["choices"].empty());
        assert(j["choices"][0]["message"]["role"] == "assistant");
        assert(j["usage"]["completion_tokens"] > 0);
        std::cout << "[ PASS ]\n";
    }

    // TEST 07: Streaming Chat Completion (SSE)
    {
        std::cout << "[TEST 07] POST /v1/chat/completions (streaming SSE) ... ";
        nlohmann::json req_body = {
            {"model", "mock"},
            {"stream", true},
            {"messages", nlohmann::json::array({
                {{"role", "user"}, {"content", "Stream me a response!"}}
            })},
            {"max_tokens", 16}
        };

        std::string sse_chunks;
        auto res = client.Post(
            "/v1/chat/completions",
            req_body.dump(),
            "application/json"
        );
        assert(res && res->status == 200);
        assert(res->body.find("data: {") != std::string::npos);
        assert(res->body.find("data: [DONE]") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 08: Embeddings Endpoint (1024-dim dense vectors)
    {
        std::cout << "[TEST 08] POST /v1/embeddings ... ";
        nlohmann::json req_body = {
            {"model", "qwen3-embedding"},
            {"input", nlohmann::json::array({"Vector search in VINOX", "Deep learning inference"})}
        };

        auto res = client.Post("/v1/embeddings", req_body.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["object"] == "list");
        assert(j["data"].size() == 2);
        assert(j["data"][0]["embedding"].size() == 1024);
        std::cout << "[ PASS ]\n";
    }

    // TEST 09: Tokenize & Detokenize
    {
        std::cout << "[TEST 09] POST /tokenize & /detokenize ... ";
        nlohmann::json tok_req = {{"text", "OpenVINO GenAI infrastructure"}};
        auto res = client.Post("/tokenize", tok_req.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j["count"] > 0);

        nlohmann::json detok_req = {{"tokens", j["tokens"]}};
        auto dres = client.Post("/detokenize", detok_req.dump(), "application/json");
        assert(dres && dres->status == 200);
        std::cout << "[ PASS ]\n";
    }

    // TEST 10: Conversations & Storage
    {
        std::cout << "[TEST 10] POST /v1/conversations ... ";
        nlohmann::json conv_req = {{"title", "API Test Conversation"}};
        auto res = client.Post("/v1/conversations", conv_req.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(!j["id"].empty());
        assert(j["title"] == "API Test Conversation");
        std::cout << "[ PASS ]\n";
    }

    // TEST 11: Hybrid Search
    {
        std::cout << "[TEST 11] POST /v1/search ... ";
        nlohmann::json s_req = {
            {"query", "test"},
            {"alpha", 0.5},
            {"limit", 5}
        };
        auto res = client.Post("/v1/search", s_req.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        assert(j.contains("matches"));
        std::cout << "[ PASS ]\n";
    }

    // TEST 12: Relations CTE
    {
        std::cout << "[TEST 12] POST /v1/relations & GET /v1/relations ... ";
        nlohmann::json r_req = {
            {"source_id", "ent_1"},
            {"target_id", "ent_2"},
            {"type", "cites"}
        };
        auto res = client.Post("/v1/relations", r_req.dump(), "application/json");
        assert(res && res->status == 200);

        auto qres = client.Get("/v1/relations?source_id=ent_1");
        assert(qres && qres->status == 200);
        std::cout << "[ PASS ]\n";
    }

    // TEST 13: Plan Creation & Hash-Bound Approval
    std::string plan_id;
    std::string plan_hash;
    {
        std::cout << "[TEST 13] POST /v1/plans & POST /v1/plans/{id}/approve ... ";
        nlohmann::json p_req = {{"task", "Build Standalone GenAI Server"}};
        auto res = client.Post("/v1/plans", p_req.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        plan_id = j["plan_id"];
        plan_hash = j["plan_hash"];
        assert(!plan_id.empty() && !plan_hash.empty());

        // Negative test: approve with wrong hash must return 409
        nlohmann::json bad_app = {{"plan_hash", "0000000000000000"}};
        auto bad_res = client.Post("/v1/plans/" + plan_id + "/approve", bad_app.dump(), "application/json");
        assert(bad_res && bad_res->status == 409);

        // Positive test: approve with valid hash
        nlohmann::json good_app = {{"plan_hash", plan_hash}};
        auto good_res = client.Post("/v1/plans/" + plan_id + "/approve", good_app.dump(), "application/json");
        assert(good_res && good_res->status == 200);
        std::cout << "[ PASS ]\n";
    }

    // TEST 14: Agent Run Lifecycle & SSE Events
    {
        std::cout << "[TEST 14] POST /v1/agent/runs & GET events ... ";
        nlohmann::json r_req = {
            {"plan_id", plan_id},
            {"plan_hash", plan_hash},
            {"workspace_dir", "."}
        };
        auto res = client.Post("/v1/agent/runs", r_req.dump(), "application/json");
        assert(res && res->status == 200);
        auto j = nlohmann::json::parse(res->body);
        std::string run_id = j["run_id"];
        assert(!run_id.empty());

        auto get_res = client.Get("/v1/agent/runs/" + run_id);
        assert(get_res && get_res->status == 200);

        auto cancel_res = client.Post("/v1/agent/runs/" + run_id + "/cancel", "{}", "application/json");
        assert(cancel_res && cancel_res->status == 200);

        auto ev_res = client.Get("/v1/agent/runs/" + run_id + "/events");
        assert(ev_res && ev_res->status == 200);
        assert(ev_res->body.find("data: {") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 15: Fail-Closed Unsupported Parameter Rejection
    {
        std::cout << "[TEST 15] Unsupported modality field returns 400 Bad Request ... ";
        nlohmann::json bad_req = {
            {"model", "mock"},
            {"messages", nlohmann::json::array({{{"role", "user"}, {"content", "Hi"}}})},
            {"audio", {{"voice", "alloy"}}} // Unsupported modality
        };
        auto res = client.Post("/v1/chat/completions", bad_req.dump(), "application/json");
        assert(res && res->status == 400);
        std::cout << "[ PASS ]\n";
    }

    // Stop server
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    // Clean up handles
    vinox_model_destroy(ctx.model);
    vinox_storage_engine_close(ctx.storage);
    vinox_tool_registry_destroy(ctx.tool_registry);
    vinox_mode_controller_destroy(ctx.mode_controller);

    std::cout << "\n================================================================================\n";
    std::cout << "  ALL 15 SERVER & OPENAI REST/SSE PROTOCOL CONTRACT TESTS PASSED!               \n";
    std::cout << "================================================================================\n";
    return 0;
}
