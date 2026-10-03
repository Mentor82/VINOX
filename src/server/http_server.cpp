#include "http_server.hpp"

#include <iostream>

#include "openai_handlers.hpp"
#include "agent_handlers.hpp"
#include "web_ui.hpp"

namespace vinox::server {

HttpServer::HttpServer(ServerContext& ctx)
    : ctx_(ctx), server_(std::make_unique<httplib::Server>()) {
    setup_middleware();
    setup_routes();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setup_middleware() {
    // 16 MB max payload
    server_->set_payload_max_length(16 * 1024 * 1024);

    // Keep-alive and read timeouts
    server_->set_read_timeout(60, 0);
    server_->set_write_timeout(60, 0);

    // CORS preflight and headers
    if (ctx_.cors_enabled) {
        server_->Options(".*", [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", ctx_.cors_origin);
            res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            res.status = 204;
        });

        server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", ctx_.cors_origin);

            // API key check if configured
            if (!ctx_.api_key.empty() && req.path != "/health/live" && req.method != "OPTIONS") {
                if (!req.has_header("Authorization")) {
                    res.status = 401;
                    res.set_content("{\"error\":{\"message\":\"Missing Authorization header\",\"type\":\"auth_error\"}}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                std::string auth = req.get_header_value("Authorization");
                std::string expected = "Bearer " + ctx_.api_key;
                if (auth != expected) {
                    res.status = 401;
                    res.set_content("{\"error\":{\"message\":\"Invalid Bearer token\",\"type\":\"auth_error\"}}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });
    } else {
        if (!ctx_.api_key.empty()) {
            server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
                if (req.path != "/health/live") {
                    if (!req.has_header("Authorization")) {
                        res.status = 401;
                        res.set_content("{\"error\":{\"message\":\"Missing Authorization header\",\"type\":\"auth_error\"}}", "application/json");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                    std::string auth = req.get_header_value("Authorization");
                    std::string expected = "Bearer " + ctx_.api_key;
                    if (auth != expected) {
                        res.status = 401;
                        res.set_content("{\"error\":{\"message\":\"Invalid Bearer token\",\"type\":\"auth_error\"}}", "application/json");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                }
                return httplib::Server::HandlerResponse::Unhandled;
            });
        }
    }
}

void HttpServer::setup_routes() {
    // VINOX Embedded Web Studio Single-Page Application
    server_->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(VINOX_WEB_STUDIO_HTML, "text/html; charset=utf-8");
    });
    server_->Get("/web", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(VINOX_WEB_STUDIO_HTML, "text/html; charset=utf-8");
    });

    // Health & System
    server_->Get("/health/live", [this](const httplib::Request& req, httplib::Response& res) {
        handle_health_live(req, res, ctx_);
    });
    server_->Get("/health/ready", [this](const httplib::Request& req, httplib::Response& res) {
        handle_health_ready(req, res, ctx_);
    });
    server_->Get("/metrics", [this](const httplib::Request& req, httplib::Response& res) {
        handle_metrics(req, res, ctx_);
    });
    server_->Get("/openapi.yaml", [this](const httplib::Request& req, httplib::Response& res) {
        handle_openapi_yaml(req, res, ctx_);
    });

    // OpenAI Models & Devices
    server_->Get("/v1/models", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_models(req, res, ctx_);
    });
    server_->Get("/v1/devices", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_devices(req, res, ctx_);
    });
    server_->Post("/api/models/:id/load", [this](const httplib::Request& req, httplib::Response& res) {
        handle_model_load(req, res, ctx_);
    });
    server_->Post("/api/models/:id/unload", [this](const httplib::Request& req, httplib::Response& res) {
        handle_model_unload(req, res, ctx_);
    });

    // OpenAI Chat & Completions
    server_->Post("/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
        handle_chat_completions(req, res, ctx_);
    });
    server_->Post("/v1/completions", [this](const httplib::Request& req, httplib::Response& res) {
        handle_completions(req, res, ctx_);
    });
    server_->Post("/v1/embeddings", [this](const httplib::Request& req, httplib::Response& res) {
        handle_embeddings(req, res, ctx_);
    });

    // Tokenizer
    server_->Post("/tokenize", [this](const httplib::Request& req, httplib::Response& res) {
        handle_tokenize(req, res, ctx_);
    });
    server_->Post("/detokenize", [this](const httplib::Request& req, httplib::Response& res) {
        handle_detokenize(req, res, ctx_);
    });

    // Storage & Search
    server_->Get("/v1/conversations", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_conversations(req, res, ctx_);
    });
    server_->Post("/v1/conversations", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create_conversation(req, res, ctx_);
    });
    server_->Get("/v1/conversations/:id", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_conversation(req, res, ctx_);
    });
    server_->Delete("/v1/conversations/:id", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete_conversation(req, res, ctx_);
    });
    server_->Get("/v1/conversations/:id/messages", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_conversation_messages(req, res, ctx_);
    });
    server_->Post("/v1/conversations/:id/messages", [this](const httplib::Request& req, httplib::Response& res) {
        handle_add_conversation_message(req, res, ctx_);
    });
    server_->Post("/v1/search", [this](const httplib::Request& req, httplib::Response& res) {
        handle_search(req, res, ctx_);
    });
    server_->Post("/v1/documents", [this](const httplib::Request& req, httplib::Response& res) {
        handle_documents_ingest(req, res, ctx_);
    });

    // Relations CTE
    server_->Get("/v1/relations", [this](const httplib::Request& req, httplib::Response& res) {
        handle_relations_query(req, res, ctx_);
    });
    server_->Post("/v1/relations", [this](const httplib::Request& req, httplib::Response& res) {
        handle_relations_create(req, res, ctx_);
    });
    server_->Delete("/v1/relations", [this](const httplib::Request& req, httplib::Response& res) {
        handle_relations_delete(req, res, ctx_);
    });

    // Plans
    server_->Post("/v1/plans", [this](const httplib::Request& req, httplib::Response& res) {
        handle_plans_create(req, res, ctx_);
    });
    server_->Get("/v1/plans/:id", [this](const httplib::Request& req, httplib::Response& res) {
        handle_plans_get(req, res, ctx_);
    });
    server_->Post("/v1/plans/:id/approve", [this](const httplib::Request& req, httplib::Response& res) {
        handle_plans_approve(req, res, ctx_);
    });

    // Agent Runs
    server_->Post("/v1/agent/runs", [this](const httplib::Request& req, httplib::Response& res) {
        handle_agent_runs_create(req, res, ctx_);
    });
    server_->Get("/v1/agent/runs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        handle_agent_runs_get(req, res, ctx_);
    });
    server_->Post("/v1/agent/runs/:id/cancel", [this](const httplib::Request& req, httplib::Response& res) {
        handle_agent_runs_cancel(req, res, ctx_);
    });
    server_->Get("/v1/agent/runs/:id/events", [this](const httplib::Request& req, httplib::Response& res) {
        handle_agent_runs_events(req, res, ctx_);
    });

    // Tool Execution
    server_->Post("/v1/tools/execute", [this](const httplib::Request& req, httplib::Response& res) {
        handle_tool_execute(req, res, ctx_);
    });
}

bool HttpServer::start() {
    is_running_ = true;
    std::cout << "[VINOX-SERVER] Listening on http://" << ctx_.host << ":" << ctx_.port << "\n";
    return server_->listen(ctx_.host, ctx_.port);
}

void HttpServer::stop() {
    if (is_running_) {
        is_running_ = false;
        if (server_) {
            server_->stop();
        }
    }
}

bool HttpServer::is_running() const {
    return is_running_.load();
}

} // namespace vinox::server
