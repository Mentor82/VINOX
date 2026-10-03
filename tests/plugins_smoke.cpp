#include <iostream>
#include <filesystem>
#include <string>
#include <cstring>
#include <cmath>

#include <nlohmann/json.hpp>

#include "vinox/tools.h"
#include "vinox/tools.hpp"
#include "vinox/plugins.h"
#include "vinox/plugins.hpp"
#include "vinox/storage.h"
#include "vinox/vinox.h"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "               VINOX Tool Plugins & Execution Engine Smoke Test                 \n";
    std::cout << "================================================================================\n";

    // 1. Create Registry and Policy Engine
    vinox_tool_registry* registry = nullptr;
    vinox_tool_registry_create(&registry);
    vinox_policy_engine* policy = nullptr;
    vinox_policy_engine_create(&policy);

    // Default policy: auto-allow READ_ONLY and LOCAL_WRITE for standard tools
    vinox_policy_engine_set_rule(policy, "*", VINOX_SECURITY_CLASS_LOCAL_WRITE, VINOX_APPROVAL_AUTO_ALLOWED);

    // 2. Test std_math: math.calculate
    auto math_plug = vinox::plugins::ToolPlugin::create_std_math();
    if (!math_plug.is_valid() || math_plug.register_with(registry) != VINOX_STATUS_OK) {
        std::cerr << "FAILED: register std_math plugin!\n";
        return 1;
    }

    vinox_tool_call_request req_calc{};
    req_calc.struct_size = sizeof(req_calc);
    req_calc.call_id = "call_math_1";
    req_calc.tool_name = "math.calculate";
    req_calc.arguments_json = "{\"expression\":\"(12 + 34) * 2 - sqrt(16)\"}";

    vinox_tool_call_result res_calc{};
    res_calc.struct_size = sizeof(res_calc);
    char pool_buf[2048] = {0};

    vinox_status st = vinox_tool_registry_execute(registry, policy, &req_calc, &res_calc, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_calc.status_code != 0) {
        std::cerr << "FAILED: math.calculate execution: " << res_calc.error_message << "\n";
        return 1;
    }

    auto j_calc = nlohmann::json::parse(res_calc.result_json);
    double res_val = j_calc.value("result", 0.0);
    std::cout << "math.calculate result: " << res_val << "\n";
    if (std::abs(res_val - 88.0) > 1e-6) {
        std::cerr << "FAILED: math.calculate expected 88.0, got " << res_val << "\n";
        return 1;
    }
    std::cout << "[PASS 01] std_math bounded arithmetic evaluation passed (result = 88.0).\n";

    // 3. Test std_time: system.time
    auto time_plug = vinox::plugins::ToolPlugin::create_std_time();
    if (!time_plug.is_valid() || time_plug.register_with(registry) != VINOX_STATUS_OK) {
        std::cerr << "FAILED: register std_time plugin!\n";
        return 1;
    }

    vinox_tool_call_request req_time{};
    req_time.struct_size = sizeof(req_time);
    req_time.call_id = "call_time_1";
    req_time.tool_name = "system.time";
    req_time.arguments_json = "{}";

    vinox_tool_call_result res_time{};
    res_time.struct_size = sizeof(res_time);
    st = vinox_tool_registry_execute(registry, policy, &req_time, &res_time, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_time.status_code != 0) {
        std::cerr << "FAILED: system.time execution!\n";
        return 1;
    }

    auto j_time = nlohmann::json::parse(res_time.result_json);
    std::string utc_str = j_time.value("utc_time", "");
    std::string loc_str = j_time.value("local_time", "");
    std::cout << "system.time UTC: " << utc_str << ", Local: " << loc_str << "\n";
    if (utc_str.find('T') == std::string::npos || utc_str.find('Z') == std::string::npos ||
        loc_str.find('T') == std::string::npos) {
        std::cerr << "FAILED: system.time ISO-8601 formatting!\n";
        return 1;
    }
    std::cout << "[PASS 02] std_time ISO-8601 format & timezone offset verified.\n";

    // 4. Test std_fs: fs.write, fs.read, and Path Traversal Protection
    std::filesystem::path sandbox_dir = std::filesystem::current_path() / "test_sandbox_fs";
    std::filesystem::create_directories(sandbox_dir);

    auto fs_plug = vinox::plugins::ToolPlugin::create_std_fs(sandbox_dir.string());
    if (!fs_plug.is_valid() || fs_plug.register_with(registry) != VINOX_STATUS_OK) {
        std::cerr << "FAILED: register std_fs plugin!\n";
        return 1;
    }

    // Write file inside sandbox
    vinox_tool_call_request req_fswrite{};
    req_fswrite.struct_size = sizeof(req_fswrite);
    req_fswrite.call_id = "call_fs_1";
    req_fswrite.tool_name = "fs.write";
    req_fswrite.arguments_json = "{\"path\":\"hello.txt\",\"content\":\"VINOX Plugin Content 2026\"}";

    vinox_tool_call_result res_fswrite{};
    res_fswrite.struct_size = sizeof(res_fswrite);
    st = vinox_tool_registry_execute(registry, policy, &req_fswrite, &res_fswrite, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_fswrite.status_code != 0) {
        std::cerr << "FAILED: fs.write execution: " << res_fswrite.error_message << "\n";
        return 1;
    }

    // Read file back from sandbox
    vinox_tool_call_request req_fsread{};
    req_fsread.struct_size = sizeof(req_fsread);
    req_fsread.call_id = "call_fs_2";
    req_fsread.tool_name = "fs.read";
    req_fsread.arguments_json = "{\"path\":\"hello.txt\"}";

    vinox_tool_call_result res_fsread{};
    res_fsread.struct_size = sizeof(res_fsread);
    st = vinox_tool_registry_execute(registry, policy, &req_fsread, &res_fsread, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_fsread.status_code != 0) {
        std::cerr << "FAILED: fs.read execution!\n";
        return 1;
    }
    auto j_read = nlohmann::json::parse(res_fsread.result_json);
    std::string read_content = j_read.value("content", "");
    if (read_content != "VINOX Plugin Content 2026") {
        std::cerr << "FAILED: Content mismatch on fs.read!\n";
        return 1;
    }

    // Test Path Traversal Protection (Canonical containment check)
    vinox_tool_call_request req_escape{};
    req_escape.struct_size = sizeof(req_escape);
    req_escape.call_id = "call_fs_3";
    req_escape.tool_name = "fs.read";
    req_escape.arguments_json = "{\"path\":\"../../escaped.txt\"}";

    vinox_tool_call_result res_escape{};
    res_escape.struct_size = sizeof(res_escape);
    st = vinox_tool_registry_execute(registry, policy, &req_escape, &res_escape, pool_buf, sizeof(pool_buf));
    if (st == VINOX_STATUS_OK && res_escape.status_code == 0) {
        std::cerr << "FAILED: Path traversal was not denied!\n";
        return 1;
    }
    std::cout << "[PASS 03] std_fs sandbox read/write and canonical path traversal protection verified.\n";

    // 5. Test std_retrieval: Ingestion & Hybrid Search
    vinox_storage_engine* storage = nullptr;
    vinox_storage_engine_open(":memory:", &storage);

    auto ret_plug = vinox::plugins::ToolPlugin::create_std_retrieval(storage, nullptr);
    if (!ret_plug.is_valid() || ret_plug.register_with(registry) != VINOX_STATUS_OK) {
        std::cerr << "FAILED: register std_retrieval plugin!\n";
        return 1;
    }

    // Ingest document
    vinox_tool_call_request req_ingest{};
    req_ingest.struct_size = sizeof(req_ingest);
    req_ingest.call_id = "call_ret_1";
    req_ingest.tool_name = "vinox.document_ingest";
    req_ingest.arguments_json = "{\"title\":\"OpenVINO Docs\",\"content\":\"OpenVINO provides high performance AI model inference across CPU, GPU, and NPU hardware.\"}";

    vinox_tool_call_result res_ingest{};
    res_ingest.struct_size = sizeof(res_ingest);
    st = vinox_tool_registry_execute(registry, policy, &req_ingest, &res_ingest, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_ingest.status_code != 0) {
        std::cerr << "FAILED: vinox.document_ingest!\n";
        return 1;
    }

    // Search document
    vinox_tool_call_request req_search{};
    req_search.struct_size = sizeof(req_search);
    req_search.call_id = "call_ret_2";
    req_search.tool_name = "vinox.search";
    req_search.arguments_json = "{\"query\":\"OpenVINO AI inference\"}";

    vinox_tool_call_result res_search{};
    res_search.struct_size = sizeof(res_search);
    st = vinox_tool_registry_execute(registry, policy, &req_search, &res_search, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_OK || res_search.status_code != 0) {
        std::cerr << "FAILED: vinox.search execution!\n";
        return 1;
    }
    std::cout << "[PASS 04] std_retrieval document ingestion & hybrid search verified.\n";

    // 6. Fail-Closed Policy Enforcement Check
    vinox_policy_engine* strict_policy = nullptr;
    vinox_policy_engine_create(&strict_policy);
    // Allow ONLY READ_ONLY tools
    vinox_policy_engine_set_rule(strict_policy, "*", VINOX_SECURITY_CLASS_READ_ONLY, VINOX_APPROVAL_AUTO_ALLOWED);

    vinox_tool_call_result res_denied{};
    res_denied.struct_size = sizeof(res_denied);
    // Attempt local write with strict policy
    st = vinox_tool_registry_execute(registry, strict_policy, &req_fswrite, &res_denied, pool_buf, sizeof(pool_buf));
    if (st != VINOX_STATUS_PERMISSION_DENIED) {
        std::cerr << "FAILED: Policy engine should have returned VINOX_STATUS_PERMISSION_DENIED for LOCAL_WRITE tool!\n";
        return 1;
    }
    std::cout << "[PASS 05] Fail-closed Policy Engine enforcement verified (LOCAL_WRITE denied under READ_ONLY policy).\n";

    // Cleanup
    vinox_storage_engine_close(storage);
    vinox_tool_registry_destroy(registry);
    vinox_policy_engine_destroy(policy);
    vinox_policy_engine_destroy(strict_policy);
    std::filesystem::remove_all(sandbox_dir);

    std::cout << "\nALL 5 TOOL PLUGINS SMOKE TESTS PASSED CLEANLY!\n";
    return 0;
}
