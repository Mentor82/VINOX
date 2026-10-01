#pragma once

#include "src/thirdparty/httplib/httplib.h"
#include "server_context.hpp"

namespace vinox::server {

void handle_list_conversations(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_get_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_create_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_delete_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_get_conversation_messages(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_add_conversation_message(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_search(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_documents_ingest(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_relations_query(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_relations_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_relations_delete(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_plans_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_plans_get(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_plans_approve(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_agent_runs_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_agent_runs_get(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_agent_runs_cancel(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);
void handle_agent_runs_events(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

void handle_tool_execute(const httplib::Request& req, httplib::Response& res, ServerContext& ctx);

} // namespace vinox::server
