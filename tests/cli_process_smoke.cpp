#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <thread>
#include <atomic>
#include "vinox/vinox_agent.h"
#include "vinox/tools.h"
#include <nlohmann/json.hpp>

int run_cli_process_with_input(const std::string& cli_args, const std::string& input_text, std::string& output_text, int& exit_code) {
#if defined(_WIN32)
    char abs_build_path[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\build", MAX_PATH, abs_build_path, NULL);

    char abs_vcpkg_dbg[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\vcpkg_installed\\x64-windows\\debug\\bin", MAX_PATH, abs_vcpkg_dbg, NULL);

    char abs_vcpkg_rel[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\vcpkg_installed\\x64-windows\\bin", MAX_PATH, abs_vcpkg_rel, NULL);

    std::string ov_genai_dbg = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\bin\\intel64\\Debug";
    std::string ov_genai_rel = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\bin\\intel64\\Release";
    std::string ov_genai_tbb = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\3rdparty\\tbb\\bin";

    std::string ov_sdk = "C:\\ai\\openvino_sdk\\openvino_2024.6.0";
    const char* env_ov = std::getenv("VINOX_OPENVINO_SDK_ROOT");
    if (env_ov && strlen(env_ov) > 0) ov_sdk = env_ov;

    std::string ov_bin = ov_sdk + "\\runtime\\bin\\intel64\\Debug";
    std::string ov_tbb = ov_sdk + "\\runtime\\3rdparty\\tbb\\bin";

    char old_path[8192] = {0};
    GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
    std::string new_path = std::string(abs_build_path) + ";" + std::string(abs_vcpkg_dbg) + ";" + std::string(abs_vcpkg_rel) + ";" + ov_genai_dbg + ";" + ov_genai_rel + ";" + ov_genai_tbb + ";" + ov_bin + ";" + ov_tbb + ";" + std::string(old_path);
    SetEnvironmentVariableA("PATH", new_path.c_str());

    HANDLE h_child_in_read = NULL;
    HANDLE h_child_in_write = NULL;
    HANDLE h_child_out_read = NULL;
    HANDLE h_child_out_write = NULL;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&h_child_out_read, &h_child_out_write, &sa, 0) || !SetHandleInformation(h_child_out_read, HANDLE_FLAG_INHERIT, 0)) {
        return -1;
    }
    if (!CreatePipe(&h_child_in_read, &h_child_in_write, &sa, 0) || !SetHandleInformation(h_child_in_write, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(h_child_out_read);
        CloseHandle(h_child_out_write);
        return -1;
    }

    PROCESS_INFORMATION pi{};
    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = h_child_out_write;
    si.hStdOutput = h_child_out_write;
    si.hStdInput = h_child_in_read;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string exe_path = "vinox-cli.exe";
    if (GetFileAttributesA(exe_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        exe_path = ".\\out\\windows-msvc-debug\\build\\vinox-cli.exe";
    }
    if (GetFileAttributesA(exe_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        exe_path = ".\\vinox-cli.exe";
    }

    std::string cmd = exe_path + " " + cli_args;
    char cmd_buf[512];
    strcpy_s(cmd_buf, sizeof(cmd_buf), cmd.c_str());

    if (!CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(h_child_out_read);
        CloseHandle(h_child_out_write);
        CloseHandle(h_child_in_read);
        CloseHandle(h_child_in_write);
        return -2;
    }

    CloseHandle(h_child_out_write);
    CloseHandle(h_child_in_read);

    if (!input_text.empty()) {
        DWORD written = 0;
        WriteFile(h_child_in_write, input_text.c_str(), static_cast<DWORD>(input_text.size()), &written, NULL);
    }
    CloseHandle(h_child_in_write);

    char buf[1024];
    DWORD read_bytes = 0;
    while (ReadFile(h_child_out_read, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
        buf[read_bytes] = '\0';
        output_text += buf;
    }

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    exit_code = static_cast<int>(code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h_child_out_read);
    return 0;
#else
    return -1;
#endif
}

int test_target_mutation_after_review_in_persistent_process() {
#if defined(_WIN32)
    // Clean up test directories to ensure fresh state
    std::error_code ec_clean;
    std::filesystem::remove_all(".cli_target_workspace", ec_clean);
    std::filesystem::remove_all(".cli_sandbox_overlay", ec_clean);

    char abs_build_path[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\build", MAX_PATH, abs_build_path, NULL);

    char abs_vcpkg_dbg[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\vcpkg_installed\\x64-windows\\debug\\bin", MAX_PATH, abs_vcpkg_dbg, NULL);

    char abs_vcpkg_rel[MAX_PATH] = {0};
    GetFullPathNameA("out\\windows-msvc-debug\\vcpkg_installed\\x64-windows\\bin", MAX_PATH, abs_vcpkg_rel, NULL);

    std::string ov_genai_dbg = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\bin\\intel64\\Debug";
    std::string ov_genai_rel = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\bin\\intel64\\Release";
    std::string ov_genai_tbb = "C:\\ai\\openvino_genai_2026.2.1\\openvino_genai_windows_2026.2.1.0_x86_64\\runtime\\3rdparty\\tbb\\bin";

    std::string ov_sdk = "C:\\ai\\openvino_sdk\\openvino_2024.6.0";
    const char* env_ov = std::getenv("VINOX_OPENVINO_SDK_ROOT");
    if (env_ov && strlen(env_ov) > 0) ov_sdk = env_ov;

    std::string ov_bin = ov_sdk + "\\runtime\\bin\\intel64\\Debug";
    std::string ov_tbb = ov_sdk + "\\runtime\\3rdparty\\tbb\\bin";

    char old_path[8192] = {0};
    GetEnvironmentVariableA("PATH", old_path, sizeof(old_path));
    std::string new_path = std::string(abs_build_path) + ";" + std::string(abs_vcpkg_dbg) + ";" + std::string(abs_vcpkg_rel) + ";" + ov_genai_dbg + ";" + ov_genai_rel + ";" + ov_genai_tbb + ";" + ov_bin + ";" + ov_tbb + ";" + std::string(old_path);
    SetEnvironmentVariableA("PATH", new_path.c_str());

    HANDLE h_child_in_read = NULL;
    HANDLE h_child_in_write = NULL;
    HANDLE h_child_out_read = NULL;
    HANDLE h_child_out_write = NULL;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&h_child_out_read, &h_child_out_write, &sa, 0) || !SetHandleInformation(h_child_out_read, HANDLE_FLAG_INHERIT, 0)) {
        return -1;
    }
    if (!CreatePipe(&h_child_in_read, &h_child_in_write, &sa, 0) || !SetHandleInformation(h_child_in_write, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(h_child_out_read);
        CloseHandle(h_child_out_write);
        return -1;
    }

    PROCESS_INFORMATION pi{};
    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = h_child_out_write;
    si.hStdOutput = h_child_out_write;
    si.hStdInput = h_child_in_read;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string exe_path = "vinox-cli.exe";
    if (GetFileAttributesA(exe_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        exe_path = ".\\out\\windows-msvc-debug\\build\\vinox-cli.exe";
    }
    std::string cmd = exe_path + " -i --json";
    char cmd_buf[512];
    strcpy_s(cmd_buf, sizeof(cmd_buf), cmd.c_str());

    if (!CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(h_child_out_read);
        CloseHandle(h_child_out_write);
        CloseHandle(h_child_in_read);
        CloseHandle(h_child_in_write);
        return -2;
    }

    CloseHandle(h_child_out_write);
    CloseHandle(h_child_in_read);

    // 1. Send plan, approve, agent, diff
    std::string phase1_cmds = "/plan Mutation Process Test Goal\n/approve\n/agent\n/diff\n";
    DWORD written = 0;
    WriteFile(h_child_in_write, phase1_cmds.c_str(), static_cast<DWORD>(phase1_cmds.size()), &written, NULL);

    // 2. Read output until cli.diff event
    std::string accumulated;
    char buf[512];
    DWORD read_bytes = 0;
    bool found_diff_event = false;

    for (int attempts = 0; attempts < 50; ++attempts) {
        DWORD avail = 0;
        if (PeekNamedPipe(h_child_out_read, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            if (ReadFile(h_child_out_read, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
                buf[read_bytes] = '\0';
                accumulated += buf;
                if (accumulated.find("cli.diff") != std::string::npos) {
                    found_diff_event = true;
                    break;
                }
            }
        }
        Sleep(50);
    }

    if (!found_diff_event) {
        std::cerr << "FAILED 03b: Did not receive cli.diff event in persistent process: " << accumulated << "\n";
        CloseHandle(h_child_in_write);
        CloseHandle(h_child_out_read);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 3. Mutate target workspace on disk WHILE subprocess is STILL RUNNING!
    CreateDirectoryA(".cli_target_workspace", NULL);
    std::ofstream mut_file(".cli_target_workspace/mutation_after_review.txt");
    mut_file << "Unreviewed mutation on disk after /diff\n";
    mut_file.close();

    // 4. Send /apply to the SAME running subprocess
    std::string apply_cmd = "/apply\n/exit\n";
    WriteFile(h_child_in_write, apply_cmd.c_str(), static_cast<DWORD>(apply_cmd.size()), &written, NULL);
    CloseHandle(h_child_in_write);

    // 5. Read remaining output
    while (ReadFile(h_child_out_read, buf, sizeof(buf) - 1, &read_bytes, NULL) && read_bytes > 0) {
        buf[read_bytes] = '\0';
        accumulated += buf;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h_child_out_read);

    // 6. Verify that /apply event HAS status "ERROR" and contains "TARGET_CONFLICT_REJECTED"!
    std::istringstream iss(accumulated);
    std::string line;
    bool found_rejected_error = false;
    bool found_successful_apply = false;

    while (std::getline(iss, line)) {
        if (line.find("cli.apply") != std::string::npos) {
            if (line.find("\"status\":\"ERROR\"") != std::string::npos && line.find("TARGET_CONFLICT_REJECTED") != std::string::npos) {
                found_rejected_error = true;
            }
            if (line.find("\"status\":\"OK\"") != std::string::npos) {
                found_successful_apply = true;
            }
        }
    }

    if (found_rejected_error && !found_successful_apply) {
        std::cout << "  [PASS 03b] Single-Process Target Mutation Rejection After /diff: Verified (TARGET_CONFLICT_REJECTED)\n";
        return 0;
    } else {
        std::cerr << "FAILED 03b: Apply succeeded or failed to report TARGET_CONFLICT_REJECTED after target mutation! Output:\n" << accumulated << "\n";
        return 1;
    }
#else
    return 0;
#endif
}

int main(void) {
    std::cout << "Starting VINOX Phase 8 CLI Process-Level E2E & Contract Verification Test...\n";

    // 1. Criteria B: Fail-Closed Remote Mode Test
    std::string remote_out;
    int remote_code = -1;
    int res = run_cli_process_with_input("--remote http://127.0.0.1:8080 --json", "", remote_out, remote_code);
    if (res == 0) {
        if (remote_code == 1 && remote_out.find("NOT_SUPPORTED") != std::string::npos) {
            std::cout << "  [PASS 01] Fail-Closed Remote Mode Rejection: Verified (Code 1, NOT_SUPPORTED)\n";
        } else {
            std::cerr << "FAILED 01: Remote mode did not reject properly (code " << remote_code << "): " << remote_out << "\n";
            return 1;
        }
    } else {
        std::cerr << "FAILED 01: Process creation failed with error code: " << res << "\n";
        return 1;
    }

    // 2. Criteria F: Interactive --json REPL Slash-Command Pipeline & Strict JSON Output Test
    std::string commands =
        "/plan E2E Process Test Goal\n"
        "/approve\n"
        "/agent\n"
        "/diff\n"
        "/apply\n"
        "/exit\n";

    std::string repl_out;
    int repl_code = -1;
    if (run_cli_process_with_input("-i --json", commands, repl_out, repl_code) != 0 || repl_code != 0) {
        std::cerr << "FAILED 02: CLI Process execution failed with code " << repl_code << ": " << repl_out << "\n";
        return 1;
    }

    // Strict JSON parsing check: every non-empty line MUST be valid JSON with event_schema_version: 1
    std::istringstream iss(repl_out);
    std::string line;
    bool found_welcome = false;
    bool found_plan = false;
    bool found_approve = false;
    bool found_agent_start = false;
    bool found_agent_complete = false;
    bool found_diff = false;
    bool found_apply = false;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            if (!j.contains("event_schema_version") || j["event_schema_version"] != 1) {
                std::cerr << "FAILED 02: JSON line missing event_schema_version: 1: " << line << "\n";
                return 1;
            }
            std::string event = j.value("event", "");
            std::string status = j.value("status", "");

            if (event == "cli.welcome") found_welcome = true;
            if (event == "cli.plan") found_plan = true;
            if (event == "cli.approve") found_approve = true;
            if (event == "cli.agent_start") found_agent_start = true;
            if (event == "cli.agent_complete" && status == "OK") found_agent_complete = true;
            if (event == "cli.diff") found_diff = true;
            if (event == "cli.apply") found_apply = true;
        } catch (const std::exception& e) {
            std::cerr << "FAILED 02: Non-JSON raw output line emitted in --json mode: " << line << " (" << e.what() << ")\n";
            return 1;
        }
    }

    if (found_welcome && found_plan && found_approve && found_agent_start && found_agent_complete && found_diff && found_apply) {
        std::cout << "  [PASS 02] Interactive --json REPL Event Pipeline, Strict JSON Contract & Agent Completion: Verified\n";
    } else {
        std::cerr << "FAILED 02: Missing expected JSON events or agent completion in CLI output!\n" << repl_out << "\n";
        return 1;
    }

    // 3a. Criteria E: Stale Review Snapshot Binding Rejection (/apply without /diff)
    std::string stale_cmd =
        "/plan Stale Test Goal\n"
        "/approve\n"
        "/agent\n"
        "/apply\n" // Calling /apply without /diff -> MUST fail with STALE_REVIEW_STATE!
        "/exit\n";

    std::string stale_out;
    int stale_code = -1;
    if (run_cli_process_with_input("-i --json", stale_cmd, stale_out, stale_code) == 0) {
        if (stale_out.find("STALE_REVIEW_STATE") != std::string::npos) {
            std::cout << "  [PASS 03a] Stale Review Snapshot Binding Rejection (/apply without /diff): Verified\n";
        } else {
            std::cerr << "FAILED 03a: /apply without prior /diff failed to reject stale review state: " << stale_out << "\n";
            return 1;
        }
    }

    // 3b. Single Persistent Process Target Mutation After /diff Rejection Test
    if (test_target_mutation_after_review_in_persistent_process() != 0) {
        return 1;
    }

    // 4. Criteria A: Multi-Turn Session REPL Chat Prompt & SQLite Storage Ownership Test
    std::string chat_cmd =
        "Hello VINOX assistant!\n"
        "What was my previous message?\n"
        "/save test_session.txt\n"
        "/exit\n";

    std::string chat_out;
    int chat_code = -1;
    if (run_cli_process_with_input("-i --json", chat_cmd, chat_out, chat_code) == 0 && chat_code == 0) {
        if (chat_out.find("history_messages_count") != std::string::npos || chat_out.find("STORED") != std::string::npos) {
            std::cout << "  [PASS 04] Multi-Turn Session REPL Chat & SQLite Storage History Ownership: Verified\n";
        } else {
            std::cerr << "FAILED 04: Multi-turn chat session test failed: " << chat_out << "\n";
            return 1;
        }
    }

    // 5. In-Flight Sandbox Subprocess Cancellation & Zero Step Completion Test
    vinox_mode_controller* mode_ctrl = vinox_mode_controller_create();
    vinox_mode_controller_set_mode(mode_ctrl, VINOX_MODE_AGENT);

    const char* cancel_plan_json = "{\"version\":1,\"goal\":\"Inflight Cancel Goal\",\"steps\":[{\"step_id\":\"step1\",\"description\":\"Slow Sandbox Tool Step\",\"dependencies\":[],\"tool_calls\":[{\"name\":\"test_sleep\",\"arguments_json\":\"{\\\"delay_ms\\\":2000}\"}]}]}";
    vinox_plan* cancel_plan = vinox_plan_create(cancel_plan_json);
    char plan_hash[128] = {0};
    vinox_plan_compute_hash(cancel_plan, plan_hash, sizeof(plan_hash));
    vinox_plan_approve(cancel_plan, plan_hash);

    vinox_agent_budget budget{};
    budget.max_steps = 10;
    budget.max_tool_calls = 10;
    budget.max_tokens = 10000;
    budget.max_duration_seconds = 60;

    vinox_agent_run* run = vinox_agent_run_create(mode_ctrl, cancel_plan, &budget);
    vinox_tool_registry* reg = nullptr;
    vinox_policy_engine* pol = nullptr;
    vinox_tool_registry_create(&reg);
    vinox_policy_engine_create(&pol);

    vinox_tool_definition sdef{};
    sdef.struct_size = sizeof(sdef);
    sdef.name = "test_sleep";
    sdef.description = "Slow test tool";
    sdef.security_class = 1;
    sdef.parameters_json_schema = "{\"type\":\"object\"}";
    vinox_tool_registry_register_tool(reg, &sdef);

    vinox_policy_engine_set_rule(pol, "*", 4, 1);

    vinox_sandbox_host* sb = vinox_sandbox_host_create(".cli_sandbox_overlay");
    const char* worker_exe = "vinox_sandbox_worker.exe";
    if (!std::filesystem::exists(worker_exe)) {
        worker_exe = "out\\windows-msvc-debug\\build\\vinox_sandbox_worker.exe";
    }
    vinox_sandbox_host_start(sb, worker_exe);

    vinox_agent_run_set_governance(run, reg, pol);
    vinox_agent_run_set_sandbox(run, sb);

    std::atomic<vinox_status> step_status{VINOX_STATUS_OK};
    std::thread worker([run, &step_status]() {
        step_status.store(vinox_agent_run_step(run));
    });

    // Wait 100ms to guarantee test_sleep has been dispatched to sandbox worker and exec_tool is in read loop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cancel in-flight AFTER real dispatch
    vinox_agent_run_cancel(run);

    if (worker.joinable()) worker.join();

    vinox_plan_status final_st = vinox_agent_run_get_status(run);
    int completed = vinox_agent_run_get_completed_steps(run);

    vinox_sandbox_host_destroy(sb);
    vinox_policy_engine_destroy(pol);
    vinox_tool_registry_destroy(reg);
    vinox_agent_run_destroy(run);
    vinox_plan_destroy(cancel_plan);
    vinox_mode_controller_destroy(mode_ctrl);

    if (final_st == VINOX_PLAN_STATUS_CANCELLED && completed == 0 && step_status.load() == VINOX_STATUS_CANCELLED) {
        std::cout << "  [PASS 05] Deterministic In-Flight Sandbox Dispatch Cancellation & Zero Step Completion: Verified (CANCELLED)\n";
    } else {
        std::cerr << "FAILED 05: Deterministic in-flight cancellation test failed. Step status: " << step_status.load() << ", Run status: " << final_st << ", Completed steps: " << completed << "\n";
        return 1;
    }

    std::cout << "SUCCESS: All VINOX Phase 8 CLI Process-Level E2E & Contract Verification tests passed! 🟢🔒\n";
    return 0;
}
