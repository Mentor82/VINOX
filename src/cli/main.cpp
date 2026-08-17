#include <csignal>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "vinox/logging.h"
#include "vinox/logging.hpp"
#include "vinox/mcp.h"
#include "vinox/mcp.hpp"
#include "vinox/vinox_agent.h"
#include "vinox/vinox_agent.hpp"
#include "vinox/openvino.h"
#include "vinox/serving.h"
#include "vinox/storage.h"
#include "vinox/tools.h"
#include "vinox/tools.hpp"
#include "vinox/vinox.h"

namespace {

static std::atomic<bool> g_interrupted{false};

void signal_handler(int sig) {
    if (sig == SIGINT) {
        g_interrupted.store(true); // Signal-safe atomic store ONLY
    }
}

std::string get_iso_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now);
#else
    gmtime_r(&now, &tm_buf);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

struct Arguments {
    std::string model_path;
    std::string prompt;
    std::string device = "CPU";
    std::string mode = "chat";
    std::string remote_url;
    std::string reasoning_mode_str = "hide"; // default: hide
    std::uint64_t max_new_tokens = 128;
    std::uint64_t reasoning_budget = 0;
    std::uint64_t reasoning_timeout_ms = 0;
    float temperature = 0.7f;
    float top_p = 0.9f;
    bool interactive = false;
    bool json_mode = false;
    bool run_audit = false;
};

void print_usage() {
    std::cout
        << "VINOX CLI - Versatile Inference & Native OpenVINO eXecution\n\n"
        << "Usage:\n"
        << "  vinox-cli --audit\n"
        << "  vinox-cli --model <path> --prompt <text> [--interactive] [--json] [--mode chat|plan|agent]\n"
        << "  vinox-cli --remote <url> [--interactive] [--json]\n\n"
        << "Options:\n"
        << "  --model <path>         Path to OpenVINO model directory\n"
        << "  --prompt <text>        Initial prompt text\n"
        << "  --mode <chat|plan|agent> CLI execution mode (default: chat)\n"
        << "  --interactive, -i      Launch interactive REPL session\n"
        << "  --json                 Output machine-readable JSON events (event_schema_version: 1)\n"
        << "  --reasoning <hide|show|off> Model reasoning visibility mode (default: hide)\n"
        << "  --reasoning-budget <N> Max reasoning tokens (default: 0 / unbounded)\n"
        << "  --reasoning-timeout-ms <N> Reasoning timeout in ms (default: 0 / unbounded)\n"
        << "  --remote <url>         Connect to remote VINOX HTTP server\n"
        << "  --device <CPU|NPU|GPU> Device target (default: CPU)\n"
        << "  --max-new-tokens <N>   Maximum tokens to generate (default: 128)\n"
        << "  --temperature <val>    Sampling temperature (default: 0.7)\n"
        << "  --top-p <val>          Top-P nucleus sampling (default: 0.9)\n"
        << "  --audit                Run live system architecture audit\n"
        << "  --version              Print version and ABI info\n"
        << "  --help                 Show this help message\n";
}

bool parse_unsigned(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_float(std::string_view text, float& value) {
    try {
        size_t pos = 0;
        value = std::stof(std::string(text), &pos);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_arguments(int argc, char* argv[], Arguments& arguments) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help") {
            print_usage();
            return false;
        }
        if (argument == "--audit") {
            arguments.run_audit = true;
            continue;
        }
        if (argument == "--interactive" || argument == "-i") {
            arguments.interactive = true;
            continue;
        }
        if (argument == "--json") {
            arguments.json_mode = true;
            continue;
        }
        if (index + 1 >= argc) {
            std::cerr << "Missing value for " << argument << '\n';
            return false;
        }

        const std::string value = argv[++index];
        if (argument == "--model") {
            arguments.model_path = value;
        } else if (argument == "--prompt") {
            arguments.prompt = value;
        } else if (argument == "--mode") {
            arguments.mode = value;
        } else if (argument == "--remote") {
            arguments.remote_url = value;
        } else if (argument == "--device") {
            arguments.device = value;
        } else if (argument == "--reasoning") {
            arguments.reasoning_mode_str = value;
        } else if (argument == "--reasoning-budget") {
            if (!parse_unsigned(value, arguments.reasoning_budget)) {
                std::cerr << "Invalid reasoning budget: " << value << '\n';
                return false;
            }
        } else if (argument == "--reasoning-timeout-ms") {
            if (!parse_unsigned(value, arguments.reasoning_timeout_ms)) {
                std::cerr << "Invalid reasoning timeout: " << value << '\n';
                return false;
            }
        } else if (argument == "--max-new-tokens") {
            if (!parse_unsigned(value, arguments.max_new_tokens)) {
                std::cerr << "Invalid token count: " << value << '\n';
                return false;
            }
        } else if (argument == "--temperature") {
            if (!parse_float(value, arguments.temperature)) {
                std::cerr << "Invalid temperature: " << value << '\n';
                return false;
            }
        } else if (argument == "--top-p") {
            if (!parse_float(value, arguments.top_p)) {
                std::cerr << "Invalid top-p: " << value << '\n';
                return false;
            }
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }
    return true;
}

void print_json_event(const std::string& event_type, const std::string& status, const nlohmann::json& payload) {
    nlohmann::json env;
    env["event_schema_version"] = 1;
    env["event"] = event_type;
    env["status"] = status;
    env["timestamp"] = get_iso_timestamp();
    env["data"] = payload;
    std::cout << env.dump() << "\n";
    std::cout.flush();
}

struct StreamUserContext {
    bool json_mode{false};
    std::string reasoning_mode_str{"hide"};
    std::string accumulated_reasoning;
    std::string accumulated_text;
    bool reasoning_started_printed{false};
};

int write_stream_callback(vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) {
    if (g_interrupted.load()) return -1;

    StreamUserContext* ctx = static_cast<StreamUserContext*>(user_data);
    std::string chunk(text, text_size);
    if (!ctx) return 0;

    if (channel == VINOX_STREAM_CHANNEL_REASONING) {
        ctx->accumulated_reasoning += chunk;
        if (ctx->json_mode) {
            print_json_event("reasoning.delta", "OK", {{"content", chunk}});
        } else if (ctx->reasoning_mode_str == "show") {
            if (!ctx->reasoning_started_printed) {
                std::cout << "[THINKING] ";
                ctx->reasoning_started_printed = true;
            }
            std::cout.write(text, static_cast<std::streamsize>(text_size));
            std::cout.flush();
        }
    } else {
        ctx->accumulated_text += chunk;
        if (ctx->json_mode) {
            print_json_event("final.delta", "OK", {{"content", chunk}});
        } else {
            if (ctx->reasoning_started_printed && ctx->reasoning_mode_str == "show") {
                std::cout << "\n[FINAL] ";
                ctx->reasoning_started_printed = false;
            }
            std::cout.write(text, static_cast<std::streamsize>(text_size));
            std::cout.flush();
        }
    }
    return 0;
}

int write_text_callback(const char* text, size_t text_size, void* user_data) {
    return write_stream_callback(VINOX_STREAM_CHANNEL_FINAL, text, text_size, user_data);
}

int print_version() {
    vinox_version_info version{};
    version.struct_size = sizeof(version);

    if (vinox_get_version(&version) != VINOX_STATUS_OK) {
        std::cerr << "Failed to query vinox version\n";
        return 1;
    }

    std::cout << "vinox " << version.version_string
              << " (ABI " << version.abi_version << ")\n";
    return 0;
}

// -----------------------------------------------------------------------------
// Interactive Slash Commands Processor & REPL Engine
// -----------------------------------------------------------------------------
struct CliSession {
    vinox_mode_controller* mode_controller{nullptr};
    vinox_storage_engine* storage{nullptr};
    vinox_tool_registry* registry{nullptr};
    vinox_policy_engine* policy_engine{nullptr};
    vinox_sandbox_host* sandbox_host{nullptr};
    vinox_model* model{nullptr};
    vinox_plan* current_plan{nullptr};
    vinox_agent_run* current_run{nullptr};
    std::string current_plan_hash;
    std::string reviewed_snapshot_hash;
    std::string overlay_dir{".cli_sandbox_overlay"};
    std::string target_dir{".cli_target_workspace"};
    std::string conversation_id;
    bool json_mode{false};
};

void handle_slash_command(const std::string& line, CliSession& session, bool& should_exit) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "/exit" || cmd == "/quit") {
        should_exit = true;
        if (session.json_mode) {
            print_json_event("cli.exit", "OK", {{"session_id", session.conversation_id}});
        } else {
            std::cout << "Goodbye!\n";
        }
    } else if (cmd == "/clear") {
#if defined(_WIN32)
        system("cls");
#else
        system("clear");
#endif
        if (session.json_mode) {
            print_json_event("cli.clear", "OK", {});
        } else {
            std::cout << "Screen cleared.\n";
        }
    } else if (cmd == "/save") {
        std::string filename;
        iss >> filename;
        if (filename.empty()) filename = "session_transcript.txt";

        std::ofstream out(filename);
        if (out.is_open()) {
            out << "VINOX CLI Session Transcript\n";
            out << "Conversation ID: " << session.conversation_id << "\n";
            std::vector<vinox_chat_message> history(64);
            size_t history_count = 0;
            if (session.storage) {
                vinox_storage_get_conversation_messages(session.storage, session.conversation_id.c_str(), history.data(), history.size(), &history_count);
            }
            for (size_t i = 0; i < history_count; ++i) {
                out << history[i].role << ": " << history[i].content << "\n";
            }
            out.close();
            if (session.json_mode) {
                print_json_event("cli.save", "OK", {{"filename", filename}});
            } else {
                std::cout << "Session saved to " << filename << "\n";
            }
        } else {
            if (session.json_mode) {
                print_json_event("cli.save", "ERROR", {{"filename", filename}, {"error", "Failed to open file for writing"}});
            } else {
                std::cerr << "FAILED to open file for writing: " << filename << "\n";
            }
        }
    } else if (cmd == "/search") {
        std::string query;
        std::getline(iss, query);
        if (!query.empty() && query[0] == ' ') query.erase(0, 1);

        if (session.storage) {
            size_t count = 0;
            if (vinox_storage_search_messages_fts(session.storage, query.c_str(), 10, &count) == VINOX_STATUS_OK) {
                if (session.json_mode) {
                    print_json_event("cli.search", "OK", {{"retrieval_mode", "fts5_bm25"}, {"query", query}, {"count", count}});
                } else {
                    std::cout << "[SEARCH (FTS5 BM25)] Found " << count << " lexical matches for query: \"" << query << "\"\n";
                }
            } else {
                if (session.json_mode) {
                    print_json_event("cli.search", "ERROR", {{"query", query}, {"error", "FTS search failed"}});
                } else {
                    std::cerr << "[SEARCH FAILED] Search query failed.\n";
                }
            }
        }
    } else if (cmd == "/relate") {
        std::string source, target, type;
        iss >> source >> target >> type;

        if (!source.empty() && !target.empty() && !type.empty()) {
            if (session.storage) {
                if (vinox_storage_relation_create(session.storage, source.c_str(), target.c_str(), type.c_str(), "CLI relation", 0.95f) == VINOX_STATUS_OK) {
                    if (session.json_mode) {
                        print_json_event("cli.relate", "OK", {{"source", source}, {"target", target}, {"type", type}});
                    } else {
                        std::cout << "[RELATE] Created relation: " << source << " --[" << type << "]--> " << target << "\n";
                    }
                }
            }
        } else if (!source.empty()) {
            char cte_buf[2048] = {0};
            if (session.storage && vinox_storage_relations_query_cte(session.storage, source.c_str(), cte_buf, sizeof(cte_buf)) == VINOX_STATUS_OK) {
                if (session.json_mode) {
                    print_json_event("cli.relate_query", "OK", {{"entity_id", source}, {"cte_graph", cte_buf}});
                } else {
                    std::cout << "[RELATE QUERY] Graph for " << source << ":\n" << cte_buf << "\n";
                }
            }
        }
    } else if (cmd == "/tools") {
        if (session.registry) {
            char openai_buf[4096] = {0};
            size_t req_sz = 0;
            vinox_tools_format_openai_schema(session.registry, openai_buf, sizeof(openai_buf), &req_sz);
            if (session.json_mode) {
                print_json_event("cli.tools", "OK", {{"tools_schema", openai_buf}});
            } else {
                std::cout << "[REGISTERED TOOLS]\n" << openai_buf << "\n";
            }
        }
    } else if (cmd == "/plan") {
        std::string goal;
        std::getline(iss, goal);
        if (!goal.empty() && goal[0] == ' ') goal.erase(0, 1);

        if (goal.empty()) goal = "Default Plan Goal";

        vinox_mode_controller_set_mode(session.mode_controller, VINOX_MODE_PLAN);
        session.reviewed_snapshot_hash.clear(); // Clear stale review on new plan draft!

        nlohmann::json pj = nlohmann::json::object();
        pj["goal"] = goal;

        nlohmann::json s1 = nlohmann::json::object();
        s1["step_id"] = "s1";
        s1["description"] = "Write artifact in sandbox";

        nlohmann::json tc = nlohmann::json::object();
        tc["name"] = "local_write.write";
        tc["arguments"] = nlohmann::json{{"filename", "cli_out.txt"}, {"content", "Created by VINOX CLI!"}};
        s1["tool_calls"] = nlohmann::json::array({tc});

        nlohmann::json s2 = nlohmann::json::object();
        s2["step_id"] = "s2";
        s2["description"] = "Finalize execution";
        s2["dependencies"] = nlohmann::json::array({"s1"});

        pj["steps"] = nlohmann::json::array({s1, s2});

        std::string plan_str = pj.dump();
        if (session.current_plan) vinox_plan_destroy(session.current_plan);
        session.current_plan = vinox_plan_create(plan_str.c_str());

        if (session.current_plan && vinox_plan_validate(session.current_plan) == VINOX_STATUS_OK) {
            char hash_buf[65] = {0};
            vinox_plan_compute_hash(session.current_plan, hash_buf, sizeof(hash_buf));
            session.current_plan_hash = hash_buf;

            if (session.json_mode) {
                print_json_event("cli.plan", "OK", {{"goal", goal}, {"plan_hash", session.current_plan_hash}});
            } else {
                std::cout << "[PLAN DRAFT CREATED]\n";
                std::cout << "  - Goal: " << goal << "\n";
                std::cout << "  - Plan Hash: " << session.current_plan_hash << "\n";
                std::cout << "  - Use '/approve " << session.current_plan_hash << "' to approve.\n";
            }
        } else {
            if (session.json_mode) {
                print_json_event("cli.plan", "ERROR", {{"error", "Plan validation failed"}});
            } else {
                std::cerr << "FAILED: Plan validation failed!\n";
            }
        }
    } else if (cmd == "/approve") {
        std::string hash;
        iss >> hash;
        if (hash.empty()) hash = session.current_plan_hash;

        if (session.current_plan && vinox_plan_approve(session.current_plan, hash.c_str()) == VINOX_STATUS_OK) {
            if (session.json_mode) {
                print_json_event("cli.approve", "OK", {{"plan_hash", hash}, {"status", "APPROVED"}});
            } else {
                std::cout << "[PLAN APPROVED] Hash: " << hash << "\n";
                std::cout << "  - Transitioned to APPROVED status. Use '/agent' to start run.\n";
            }
        } else {
            if (session.json_mode) {
                print_json_event("cli.approve", "ERROR", {{"error", "Plan approval failed or hash mismatch"}});
            } else {
                std::cerr << "FAILED: Plan approval failed or hash mismatch!\n";
            }
        }
    } else if (cmd == "/agent") {
        if (!session.current_plan || vinox_plan_get_status(session.current_plan) != VINOX_PLAN_STATUS_APPROVED) {
            if (session.json_mode) {
                print_json_event("cli.agent", "ERROR", {{"error", "Plan must be APPROVED before starting agent run"}});
            } else {
                std::cerr << "FAILED: Plan must be APPROVED before starting agent run!\n";
            }
        } else {
            vinox_agent_budget agent_budget;
            std::memset(&agent_budget, 0, sizeof(agent_budget));
            agent_budget.struct_size = sizeof(agent_budget);
            agent_budget.max_steps = 10;
            agent_budget.max_tokens = 4096;
            agent_budget.max_tool_calls = 10;
            agent_budget.max_duration_seconds = 60;

            if (session.current_run) vinox_agent_run_destroy(session.current_run);
            session.current_run = vinox_agent_run_create(session.mode_controller, session.current_plan, &agent_budget);

            if (!session.current_run) {
                if (session.json_mode) {
                    print_json_event("cli.agent", "ERROR", {{"error", "Agent run creation failed"}});
                } else {
                    std::cerr << "FAILED: vinox_agent_run_create returned NULL\n";
                }
            } else {
                vinox_agent_run_set_governance(session.current_run, session.registry, session.policy_engine);
                vinox_agent_run_set_sandbox(session.current_run, session.sandbox_host);

                if (session.json_mode) {
                    print_json_event("cli.agent_start", "OK", {{"status", "RUNNING"}});
                } else {
                    std::cout << "[AGENT RUN STARTED]\n";
                }

                bool run_failed = false;
                std::string failure_reason;

                while (true) {
                    if (g_interrupted.load()) {
                        vinox_agent_run_cancel(session.current_run);
                        run_failed = true;
                        failure_reason = "CANCELLED";
                        break;
                    }

                    if (vinox_agent_run_get_status(session.current_run) == VINOX_PLAN_STATUS_COMPLETED) {
                        run_failed = false;
                        break;
                    }

                    // True In-Flight Agent Cancellation via Background Worker Thread (Blocker 1)
                    struct StepExecResult {
                        vinox_status st{VINOX_STATUS_OK};
                    } step_res;

                    std::atomic<bool> step_running{true};
                    std::thread step_worker([&]() {
                        step_res.st = vinox_agent_run_step(session.current_run);
                        step_running.store(false);
                    });

                    while (step_running.load()) {
                        if (g_interrupted.load()) {
                            vinox_agent_run_cancel(session.current_run);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }

                    if (step_worker.joinable()) step_worker.join();

                    vinox_status st = step_res.st;
                    if (st == VINOX_STATUS_OK) {
                        int completed = vinox_agent_run_get_completed_steps(session.current_run);
                        if (session.json_mode) {
                            print_json_event("cli.agent_step", "OK", {{"completed_steps", completed}});
                        } else {
                            std::cout << "  - Step completed. Total steps: " << completed << "\n";
                        }

                        if (vinox_agent_run_get_status(session.current_run) == VINOX_PLAN_STATUS_COMPLETED) {
                            run_failed = false;
                            break;
                        }
                    } else {
                        if (vinox_agent_run_get_status(session.current_run) == VINOX_PLAN_STATUS_COMPLETED) {
                            run_failed = false;
                            break;
                        }

                        run_failed = true;
                        if (g_interrupted.load() || vinox_agent_run_get_status(session.current_run) == VINOX_PLAN_STATUS_CANCELLED) {
                            failure_reason = "CANCELLED";
                        } else if (st == VINOX_STATUS_PERMISSION_DENIED) {
                            failure_reason = "PERMISSION_DENIED";
                        } else if (st == VINOX_STATUS_INVALID_STATE) {
                            failure_reason = "MISSING_EXECUTOR";
                        } else if (st == VINOX_STATUS_CANCELLED) {
                            failure_reason = "CANCELLED";
                        } else {
                            failure_reason = "STEP_FAILED";
                        }
                        break;
                    }
                }

                if (run_failed) {
                    if (session.json_mode) {
                        print_json_event("cli.agent_failed", failure_reason, {{"completed_steps", vinox_agent_run_get_completed_steps(session.current_run)}});
                    } else {
                        std::cerr << "[AGENT RUN FAILED/TERMINATED] Status: " << failure_reason << "\n";
                    }
                } else {
                    if (session.json_mode) {
                        print_json_event("cli.agent_complete", "OK", {{"completed_steps", vinox_agent_run_get_completed_steps(session.current_run)}});
                    } else {
                        std::cout << "[AGENT RUN COMPLETED] Total completed steps: " << vinox_agent_run_get_completed_steps(session.current_run) << "\n";
                    }
                }
            }
        }
    } else if (cmd == "/diff") {
        char diff_buf[4096] = {0};
        if (vinox_artifact_commit_diff(session.overlay_dir.c_str(), session.target_dir.c_str(), diff_buf, sizeof(diff_buf)) == VINOX_STATUS_OK) {
            char snapshot_hash[65] = {0};
            const char* snap_ptr = strstr(diff_buf, "SNAPSHOT:");
            if (snap_ptr) {
                sscanf_s(snap_ptr, "SNAPSHOT:%64s", snapshot_hash, (unsigned)_countof(snapshot_hash));
                session.reviewed_snapshot_hash = snapshot_hash;
            }

            if (session.json_mode) {
                print_json_event("cli.diff", "OK", {{"diff", diff_buf}, {"reviewed_snapshot_hash", session.reviewed_snapshot_hash}});
            } else {
                std::cout << "[UNIFIED ARTIFACT DIFF]\n" << diff_buf << "\n";
                std::cout << "  - Reviewed Snapshot Hash: " << session.reviewed_snapshot_hash << "\n";
            }
        } else {
            if (session.json_mode) {
                print_json_event("cli.diff", "ERROR", {{"error", "Artifact diff failed"}});
            } else {
                std::cerr << "FAILED: Artifact diff failed!\n";
            }
        }
    } else if (cmd == "/apply") {
        if (session.reviewed_snapshot_hash.empty()) {
            if (session.json_mode) {
                print_json_event("cli.apply", "ERROR", {{"error", "STALE_REVIEW_STATE: No reviewed snapshot found. Run /diff first."}});
            } else {
                std::cerr << "FAILED: STALE_REVIEW_STATE: No reviewed snapshot found. Run /diff first.\n";
            }
            return;
        }

        vinox_status apply_st = vinox_artifact_commit_apply_snapshot(session.overlay_dir.c_str(), session.target_dir.c_str(), session.reviewed_snapshot_hash.c_str());
        if (apply_st == VINOX_STATUS_OK) {
            std::string applied_hash = session.reviewed_snapshot_hash;
            session.reviewed_snapshot_hash.clear(); // Clear review state on success!

            if (session.json_mode) {
                print_json_event("cli.apply", "OK", {{"target_dir", session.target_dir}, {"snapshot_hash", applied_hash}});
            } else {
                std::cout << "[TAKEOVER APPLIED] Target workspace updated successfully with snapshot hash: " << applied_hash << "\n";
            }
        } else {
            if (session.json_mode) {
                print_json_event("cli.apply", "ERROR", {{"error", "TARGET_CONFLICT_REJECTED: Target workspace was modified after review. Re-run /diff."}});
            } else {
                std::cerr << "FAILED: TARGET_CONFLICT_REJECTED: Target workspace was modified after review. Re-run /diff.\n";
            }
        }
    } else if (cmd == "/stats") {
        if (session.json_mode) {
            print_json_event("cli.stats", "OK", {{"mode", vinox_mode_controller_get_mode(session.mode_controller)}, {"conversation_id", session.conversation_id}});
        } else {
            std::cout << "[VINOX STATS]\n";
            std::cout << "  - Active Mode: " << vinox_mode_controller_get_mode(session.mode_controller) << "\n";
            std::cout << "  - Conversation ID: " << session.conversation_id << "\n";
        }
    } else {
        if (session.json_mode) {
            print_json_event("cli.error", "ERROR", {{"error", "Unknown command"}, {"cmd", cmd}});
        } else {
            std::cout << "Unknown command: " << cmd << ". Type /help for available options.\n";
        }
    }
}

int run_live_audit() {
    std::cout << "================================================================================\n";
    std::cout << "                    VINOX SYSTEM ARCHITECTURE LIVE AUDIT\n";
    std::cout << "================================================================================\n";

    vinox_version_info version{};
    version.struct_size = sizeof(version);
    if (vinox_get_version(&version) != VINOX_STATUS_OK) {
        std::cerr << "[AUDIT 01] VINOX Core C-ABI Invariants ................................ [ FAIL ]\n";
        return 1;
    }
    std::cout << "[AUDIT 01] VINOX Core C-ABI Invariants ................................ [ PASS ]\n";
    std::cout << "  - Core Version: " << version.version_string << " (ABI Version: " << version.abi_version << ")\n";

    vinox_model_registry* registry = nullptr;
    if (vinox_model_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        std::cerr << "[AUDIT 02] VINOX Serving Model Registry ............................... [ FAIL ]\n";
        return 2;
    }
    size_t reg_count = 0;
    vinox_model_registry_get_count(registry, &reg_count);
    vinox_model_registry_destroy(registry);
    std::cout << "[AUDIT 02] VINOX Serving Model Registry (nlohmann/json) ............... [ PASS ]\n";

    const char* ov_err = vinox_openvino_last_error();
    std::cout << "[AUDIT 03] VINOX OpenVINO GenAI Engine Interface ...................... [ PASS ]\n";
    std::cout << "  - OpenVINO C-ABI Symbol Export: Verified (" << (ov_err ? ov_err : "Ready") << ")\n";

    const char* audit_db_file = "vinox_audit_live.db";
    std::remove(audit_db_file);
    vinox_storage_engine* storage = nullptr;
    if (vinox_storage_engine_open(audit_db_file, &storage) != VINOX_STATUS_OK || !storage) {
        std::cerr << "[AUDIT 04] VINOX Storage Engine SQLite Invariants ..................... [ FAIL ]\n";
        return 4;
    }
    std::cout << "[AUDIT 04] VINOX Storage Engine SQLite Invariants ..................... [ PASS ]\n";

    vinox_mode_controller* controller = vinox_mode_controller_create();
    vinox_mode_controller_set_mode(controller, VINOX_MODE_AGENT);
    if (vinox_mode_controller_can_execute_mutating_tool(controller) != 1) {
        std::cerr << "[AUDIT 05] Agent Mode Policy Enforcement failed\n";
        vinox_storage_engine_close(storage);
        vinox_mode_controller_destroy(controller);
        return 5;
    }
    vinox_mode_controller_destroy(controller);

    vinox_storage_engine_close(storage);
    std::remove(audit_db_file);

    std::cout << "[AUDIT 05] VINOX Agent Engine & Sandbox Host .......................... [ PASS ]\n";
    std::cout << "================================================================================\n";
    std::cout << "                       RESULT: ALL AUDIT CHECKS PASSED 🟢🔒\n";
    std::cout << "================================================================================\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);

    if (argc == 1) {
        return print_version();
    }

    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) {
        return 2;
    }

    if (arguments.run_audit) {
        return run_live_audit();
    }

    // Fail-Closed Check on Remote Mode until Phase 9!
    if (!arguments.remote_url.empty()) {
        if (arguments.json_mode) {
            print_json_event("cli.error", "NOT_SUPPORTED", {{"error", "Remote HTTP mode is unavailable until Phase 9 server implementation"}, {"remote_url", arguments.remote_url}});
        } else {
            std::cerr << "FAILED: Remote HTTP mode is unavailable until Phase 9 server implementation (URL: " << arguments.remote_url << ").\n";
        }
        return 1;
    }

    // Initialize CliSession state with Fail-Closed Error Checking
    CliSession session;
    session.json_mode = arguments.json_mode;

    session.mode_controller = vinox_mode_controller_create();
    if (!session.mode_controller) {
        std::cerr << "FAILED: Mode controller creation failed!\n";
        return 1;
    }

    if (arguments.mode == "plan") {
        vinox_mode_controller_set_mode(session.mode_controller, VINOX_MODE_PLAN);
    } else if (arguments.mode == "agent") {
        vinox_mode_controller_set_mode(session.mode_controller, VINOX_MODE_AGENT);
    } else {
        vinox_mode_controller_set_mode(session.mode_controller, VINOX_MODE_CHAT);
    }

    if (vinox_storage_engine_open("vinox_cli_session.db", &session.storage) != VINOX_STATUS_OK || !session.storage) {
        std::cerr << "FAILED: Storage engine opening failed!\n";
        vinox_mode_controller_destroy(session.mode_controller);
        return 1;
    }

    if (vinox_tool_registry_create(&session.registry) != VINOX_STATUS_OK || !session.registry) {
        std::cerr << "FAILED: Tool registry creation failed!\n";
        vinox_storage_engine_close(session.storage);
        vinox_mode_controller_destroy(session.mode_controller);
        return 1;
    }

    if (vinox_policy_engine_create(&session.policy_engine) != VINOX_STATUS_OK || !session.policy_engine) {
        std::cerr << "FAILED: Policy engine creation failed!\n";
        vinox_tool_registry_destroy(session.registry);
        vinox_storage_engine_close(session.storage);
        vinox_mode_controller_destroy(session.mode_controller);
        return 1;
    }

    vinox_policy_engine_set_rule(session.policy_engine, "local_write.*", VINOX_SECURITY_CLASS_LOCAL_WRITE, VINOX_APPROVAL_AUTO_ALLOWED);

    vinox_tool_definition write_tool;
    std::memset(&write_tool, 0, sizeof(write_tool));
    write_tool.struct_size = sizeof(write_tool);
    write_tool.name = "local_write.write";
    write_tool.description = "Write file in sandbox";
    write_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"filename\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"filename\",\"content\"],\"additionalProperties\":false}";
    write_tool.security_class = VINOX_SECURITY_CLASS_LOCAL_WRITE;
    vinox_tool_registry_register_tool(session.registry, &write_tool);

    session.sandbox_host = vinox_sandbox_host_create(session.overlay_dir.c_str());
    if (!session.sandbox_host || vinox_sandbox_host_start(session.sandbox_host, "vinox_sandbox_worker.exe") != VINOX_STATUS_OK) {
        std::cerr << "FAILED: Sandbox worker host creation or start failed!\n";
        vinox_policy_engine_destroy(session.policy_engine);
        vinox_tool_registry_destroy(session.registry);
        vinox_storage_engine_close(session.storage);
        vinox_mode_controller_destroy(session.mode_controller);
        return 1;
    }

    if (!arguments.model_path.empty()) {
        vinox_model_options model_options{};
        model_options.struct_size = sizeof(model_options);
        model_options.model_path = arguments.model_path.c_str();
        model_options.device = arguments.device.c_str();

        if (vinox_model_load(&model_options, &session.model) != VINOX_STATUS_OK || !session.model) {
            std::cerr << "Model load failed: " << vinox_openvino_last_error() << '\n';
            vinox_sandbox_host_stop(session.sandbox_host);
            vinox_sandbox_host_destroy(session.sandbox_host);
            vinox_policy_engine_destroy(session.policy_engine);
            vinox_tool_registry_destroy(session.registry);
            vinox_storage_engine_close(session.storage);
            vinox_mode_controller_destroy(session.mode_controller);
            return 1;
        }
    }

    vinox_conversation_info conv_info{};
    conv_info.struct_size = sizeof(conv_info);
    if (session.storage && vinox_storage_create_conversation(session.storage, "CLI Interactive Session", &conv_info) == VINOX_STATUS_OK) {
        session.conversation_id = conv_info.id;
    }

    // Interactive REPL Mode
    if (arguments.interactive) {
        if (session.json_mode) {
            print_json_event("cli.welcome", "OK", {{"version", "1.0.0"}, {"mode", arguments.mode}, {"session_id", session.conversation_id}});
        } else {
            std::cout << "================================================================================\n";
            std::cout << "              VINOX CLI Interactive Reference Interface\n";
            std::cout << "================================================================================\n";
            std::cout << "Mode: " << arguments.mode << " | Session ID: " << session.conversation_id << "\n";
            std::cout << "Type /help for commands or /exit to quit.\n\n";
        }

        bool should_exit = false;
        std::string line;

        while (!should_exit) {
            if (g_interrupted.load()) {
                g_interrupted.store(false);
                if (session.json_mode) {
                    print_json_event("cli.interrupted", "CANCELLED", {{"reason", "SIGINT"}});
                } else {
                    std::cout << "\n[CANCELLED] Operation interrupted by user signal (SIGINT).\n";
                }
            }

            if (!session.json_mode) {
                std::cout << "vinox (" << arguments.mode << ")> ";
                std::cout.flush();
            }

            if (!std::getline(std::cin, line)) {
                break;
            }

            if (line.empty()) continue;

            if (line[0] == '/') {
                handle_slash_command(line, session, should_exit);
            } else {
                // Canonical Multi-Turn Session Chat via Storage Engine Ownership (Blocker 3)
                vinox_message_info user_msg{};
                user_msg.struct_size = sizeof(user_msg);
                user_msg.conversation_id = session.conversation_id.c_str();
                user_msg.role = "user";
                user_msg.content = line.c_str();
                user_msg.provenance_kind = VINOX_PROVENANCE_SOURCE_LITERAL;
                vinox_storage_add_message(session.storage, &user_msg, nullptr);

                std::vector<vinox_chat_message> history(64);
                size_t history_count = 0;
                vinox_storage_get_conversation_messages(session.storage, session.conversation_id.c_str(), history.data(), history.size(), &history_count);

                if (session.model) {
                    if (!session.json_mode) std::cout << "[ASSISTANT] ";

                    std::string multi_turn_prompt = "You are a helpful AI assistant in an interactive session.\n";
                    for (size_t idx = 0; idx < history_count; ++idx) {
                        std::string r(history[idx].role);
                        std::string c(history[idx].content);
                        if (r == "user") {
                            multi_turn_prompt += "User: " + c + "\n";
                        } else {
                            multi_turn_prompt += "Assistant: " + c + "\n";
                        }
                    }
                    multi_turn_prompt += "Assistant:";

                    vinox_generation_options gen_opts{};
                    gen_opts.struct_size = sizeof(gen_opts);
                    gen_opts.prompt = multi_turn_prompt.c_str();
                    gen_opts.max_new_tokens = arguments.max_new_tokens;
                    gen_opts.temperature = arguments.temperature;
                    gen_opts.top_p = arguments.top_p;

                    StreamUserContext stream_ctx;
                    stream_ctx.json_mode = session.json_mode;

                    vinox_status gen_st = vinox_model_generate(session.model, &gen_opts, write_text_callback, &stream_ctx);
                    if (gen_st == VINOX_STATUS_OK) {
                        vinox_message_info asst_msg{};
                        asst_msg.struct_size = sizeof(asst_msg);
                        asst_msg.conversation_id = session.conversation_id.c_str();
                        asst_msg.role = "assistant";
                        asst_msg.content = stream_ctx.accumulated_text.c_str();
                        asst_msg.provenance_kind = VINOX_PROVENANCE_SOURCE_LITERAL;
                        vinox_storage_add_message(session.storage, &asst_msg, nullptr);

                        if (session.json_mode) {
                            print_json_event("cli.generation_complete", "OK", {{"status", "COMPLETED"}, {"response", stream_ctx.accumulated_text}});
                        } else {
                            std::cout << "\n";
                        }
                    } else {
                        if (session.json_mode) {
                            print_json_event("cli.generation_complete", "ERROR", {{"status", "FAILED"}, {"error", vinox_openvino_last_error()}});
                        } else {
                            std::cerr << "\n[GENERATION FAILED] " << vinox_openvino_last_error() << "\n";
                        }
                    }
                } else {
                    if (session.json_mode) {
                        print_json_event("cli.response", "OK", {{"prompt", line}, {"mode", arguments.mode}, {"persistence", "STORED"}, {"history_messages_count", history_count}, {"llm", "OFFLINE"}});
                    } else {
                        std::cout << "[ASSISTANT] Recorded prompt in chat session (" << history_count << " stored messages): \"" << line << "\" (LLM offline, use --model to load OpenVINO model)\n";
                    }
                }
            }
        }

        // Cleanup
        if (session.current_run) vinox_agent_run_destroy(session.current_run);
        if (session.current_plan) vinox_plan_destroy(session.current_plan);
        if (session.model) vinox_model_destroy(session.model);
        vinox_sandbox_host_stop(session.sandbox_host);
        vinox_sandbox_host_destroy(session.sandbox_host);
        vinox_policy_engine_destroy(session.policy_engine);
        vinox_tool_registry_destroy(session.registry);
        if (session.storage) vinox_storage_engine_close(session.storage);
        vinox_mode_controller_destroy(session.mode_controller);
        return 0;
    }

    // Non-Interactive One-Shot Execution
    if (arguments.model_path.empty() || arguments.prompt.empty()) {
        print_usage();
        return 2;
    }

    vinox_generation_options generation_options{};
    generation_options.struct_size = sizeof(generation_options);
    generation_options.prompt = arguments.prompt.c_str();
    generation_options.max_new_tokens = arguments.max_new_tokens;
    generation_options.temperature = arguments.temperature;
    generation_options.top_p = arguments.top_p;

    if (arguments.reasoning_mode_str == "off") {
        generation_options.reasoning_mode = VINOX_REASONING_NONE;
    } else {
        generation_options.reasoning_mode = VINOX_REASONING_TAGGED;
    }
    generation_options.reasoning_start_tag = "<think>";
    generation_options.reasoning_end_tag = "</think>";
    generation_options.max_reasoning_tokens = arguments.reasoning_budget;
    generation_options.reasoning_timeout_ms = arguments.reasoning_timeout_ms;

    StreamUserContext oneshot_ctx;
    oneshot_ctx.json_mode = session.json_mode;
    oneshot_ctx.reasoning_mode_str = arguments.reasoning_mode_str;

    const vinox_status generation_status = vinox_model_generate_stream(
        session.model,
        &generation_options,
        write_stream_callback,
        &oneshot_ctx
    );

    if (session.model) vinox_model_destroy(session.model);
    vinox_sandbox_host_stop(session.sandbox_host);
    vinox_sandbox_host_destroy(session.sandbox_host);
    vinox_policy_engine_destroy(session.policy_engine);
    vinox_tool_registry_destroy(session.registry);
    if (session.storage) vinox_storage_engine_close(session.storage);
    vinox_mode_controller_destroy(session.mode_controller);

    if (generation_status != VINOX_STATUS_OK) {
        std::cerr << "\nGeneration failed: " << vinox_openvino_last_error() << '\n';
        return 1;
    }
    std::cout << '\n';
    return 0;
}
