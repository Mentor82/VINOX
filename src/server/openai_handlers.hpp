#pragma once

#include "src/thirdparty/httplib/httplib.h"
#include "server_context.hpp"

namespace vinox::server {

void handle_health_live(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_health_ready(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_metrics(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_list_models(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_list_devices(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_model_load(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_model_unload(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_chat_completions(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_completions(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_embeddings(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_tokenize(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_detokenize(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_openapi_yaml(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

} // namespace vinox::server
