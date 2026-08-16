#include "vinox/vinox_agent.h"
#include <string>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace fs = std::filesystem;

struct vinox_sandbox_host {
    std::string overlay_dir;
#if defined(_WIN32)
    HANDLE hProcess{NULL};
    HANDLE hThread{NULL};
    HANDLE hChildStdinWrite{NULL};
    HANDLE hChildStdoutRead{NULL};
    HANDLE hJob{NULL};
#endif
    int next_id{1};
};

extern "C" {

VINOX_API vinox_sandbox_host* VINOX_CALL vinox_sandbox_host_create(const char* overlay_dir) {
    auto host = new vinox_sandbox_host();
    host->overlay_dir = overlay_dir ? overlay_dir : ".sandbox_overlay";
    fs::create_directories(host->overlay_dir);
    return host;
}

VINOX_API void VINOX_CALL vinox_sandbox_host_destroy(vinox_sandbox_host* host) {
    if (host) {
        vinox_sandbox_host_stop(host);
        delete host;
    }
}

VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_start(vinox_sandbox_host* host, const char* worker_exe_path) {
    if (!host) return VINOX_STATUS_INVALID_ARGUMENT;

#if defined(_WIN32)
    std::string exe_path = worker_exe_path ? worker_exe_path : "vinox_sandbox_worker.exe";

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStdoutWrite = NULL;
    HANDLE hChildStdinRead = NULL;

    if (!CreatePipe(&host->hChildStdoutRead, &hChildStdoutWrite, &saAttr, 0) ||
        !SetHandleInformation(host->hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    if (!CreatePipe(&hChildStdinRead, &host->hChildStdinWrite, &saAttr, 0) ||
        !SetHandleInformation(host->hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(host->hChildStdoutRead);
        CloseHandle(hChildStdoutWrite);
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = hChildStdoutWrite;
    si.hStdInput = hChildStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    std::string cmd = exe_path + " " + host->overlay_dir;
    char cmd_buf[512];
    strncpy_s(cmd_buf, sizeof(cmd_buf), cmd.c_str(), _TRUNCATE);

    if (!CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(host->hChildStdoutRead);
        CloseHandle(hChildStdoutWrite);
        CloseHandle(hChildStdinRead);
        CloseHandle(host->hChildStdinWrite);
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    CloseHandle(hChildStdoutWrite);
    CloseHandle(hChildStdinRead);

    host->hProcess = pi.hProcess;
    host->hThread = pi.hThread;

    // Nephy Finding 6: Attach to Job Object - MUST fail closed if Job Object setup fails!
    host->hJob = CreateJobObjectA(NULL, NULL);
    if (!host->hJob) {
        TerminateProcess(host->hProcess, 1);
        CloseHandle(host->hProcess);
        CloseHandle(host->hThread);
        CloseHandle(host->hChildStdoutRead);
        CloseHandle(host->hChildStdinWrite);
        host->hProcess = NULL;
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(host->hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)) ||
        !AssignProcessToJobObject(host->hJob, host->hProcess)) {
        CloseHandle(host->hJob);
        TerminateProcess(host->hProcess, 1);
        CloseHandle(host->hProcess);
        CloseHandle(host->hThread);
        CloseHandle(host->hChildStdoutRead);
        CloseHandle(host->hChildStdinWrite);
        host->hProcess = NULL;
        host->hJob = NULL;
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    return VINOX_STATUS_OK;
#else
    return VINOX_STATUS_NOT_SUPPORTED;
#endif
}

VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_exec_tool(vinox_sandbox_host* host, const char* tool_name, const char* args_json, char* out_buf, size_t out_buf_sz) {
    if (!host || !tool_name || !out_buf || out_buf_sz < 1) return VINOX_STATUS_INVALID_ARGUMENT;

#if defined(_WIN32)
    if (!host->hChildStdinWrite || !host->hChildStdoutRead) return VINOX_STATUS_INVALID_STATE;

    int current_req_id = host->next_id++;
    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = current_req_id;
    req["method"] = "sandbox/exec_tool";
    req["params"]["name"] = tool_name;

    // Nephy Finding D.2: Malformed args_json must fail closed (do NOT default to empty object)
    if (args_json && strlen(args_json) > 0) {
        try {
            req["params"]["arguments"] = nlohmann::json::parse(args_json);
        } catch (...) {
            std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Malformed arguments JSON\"}";
            if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
            strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
            return VINOX_STATUS_INVALID_ARGUMENT;
        }
    } else {
        req["params"]["arguments"] = nlohmann::json::object();
    }

    std::string req_str = req.dump() + "\n";

    // Bounded request size check (max 256 KB)
    if (req_str.length() > 262144) {
        std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Oversized request payload (>256 KB)\"}";
        if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
        strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
        return VINOX_STATUS_OUT_OF_RANGE;
    }

    DWORD written = 0;
    if (!WriteFile(host->hChildStdinWrite, req_str.c_str(), (DWORD)req_str.length(), &written, NULL)) {
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    // Nephy Finding 5: Host-owned pipe read deadline using PeekNamedPipe
    std::string res_line;
    auto start_wait = std::chrono::steady_clock::now();
    bool got_newline = false;

    while (!got_newline) {
        DWORD avail = 0;
        if (!PeekNamedPipe(host->hChildStdoutRead, NULL, 0, NULL, &avail, NULL)) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        if (avail > 0) {
            char ch = 0;
            DWORD read_bytes = 0;
            if (ReadFile(host->hChildStdoutRead, &ch, 1, &read_bytes, NULL) && read_bytes > 0) {
                if (ch == '\n') {
                    got_newline = true;
                    break;
                }
                if (ch != '\r') res_line.push_back(ch);
                if (res_line.length() > 262144) {
                    std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Oversized response payload (>256 KB)\"}";
                    if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
                    strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
                    return VINOX_STATUS_OUT_OF_RANGE;
                }
            }
        } else {
            // Deadline check (5000ms limit)
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_wait).count();
            if (elapsed > 5000) {
                // Terminate runaway worker process to prevent background mutating execution
                TerminateProcess(host->hProcess, 1);
                CloseHandle(host->hProcess);
                CloseHandle(host->hThread);
                host->hProcess = NULL;
                host->hThread = NULL;

                std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"INDETERMINATE_OUTCOME_MUTATION_CANCELLED: Host pipe read execution deadline timeout (5000 ms). Subprocess terminated.\"}";
                if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
                strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);

                // Restart fresh worker process for subsequent calls
                vinox_sandbox_host_start(host, "vinox_sandbox_worker.exe");

                return VINOX_STATUS_CANCELLED;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    if (res_line.empty()) {
        std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Worker process EOF or process death\"}";
        if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
        strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    try {
        auto res_j = nlohmann::json::parse(res_line);

        // Nephy Finding 5: Validate jsonrpc == "2.0"
        if (!res_j.contains("jsonrpc") || res_j["jsonrpc"].get<std::string>() != "2.0") {
            std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Invalid JSON-RPC version envelope\"}";
            if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
            strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        // Nephy Finding 5: Validate matching response id (missing id is rejected)
        if (!res_j.contains("id") || !res_j["id"].is_number_integer() || res_j["id"].get<int>() != current_req_id) {
            std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"JSON-RPC request/response ID mismatch or missing ID\"}";
            if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
            strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        // Nephy Finding D.1: Handle worker error response
        if (res_j.contains("error")) {
            std::string err_out = res_j["error"].dump();
            std::string err_msg = "{\"status\":\"ERROR\",\"error\":" + err_out + "}";
            if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
            strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        if (res_j.contains("result")) {
            std::string result_str = res_j["result"].dump();
            if (result_str.length() >= out_buf_sz) {
                return VINOX_STATUS_OUT_OF_RANGE; // Return error instead of silent truncation!
            }
            strncpy_s(out_buf, out_buf_sz, result_str.c_str(), _TRUNCATE);
            return VINOX_STATUS_OK;
        }
    } catch (...) {
        std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Invalid JSON-RPC response envelope\"}";
        if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
        strncpy_s(out_buf, out_buf_sz, err_msg.c_str(), _TRUNCATE);
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    if (res_line.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
    strncpy_s(out_buf, out_buf_sz, res_line.c_str(), _TRUNCATE);
    return VINOX_STATUS_OK;
#else
    std::string err_msg = "{\"status\":\"ERROR\",\"error\":\"Sandbox host worker not supported on non-Windows platforms\"}";
    if (err_msg.length() >= out_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;
    strncpy(out_buf, err_msg.c_str(), out_buf_sz - 1);
    out_buf[out_buf_sz - 1] = '\0';
    return VINOX_STATUS_NOT_FOUND;
#endif
}

VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_stop(vinox_sandbox_host* host) {
    if (!host) return VINOX_STATUS_INVALID_ARGUMENT;

#if defined(_WIN32)
    if (host->hChildStdinWrite) {
        nlohmann::json req;
        req["jsonrpc"] = "2.0";
        req["id"] = host->next_id++;
        req["method"] = "sandbox/shutdown";
        std::string req_str = req.dump() + "\n";
        DWORD written = 0;
        WriteFile(host->hChildStdinWrite, req_str.c_str(), (DWORD)req_str.length(), &written, NULL);
    }

    if (host->hProcess) {
        WaitForSingleObject(host->hProcess, 1000);
        TerminateProcess(host->hProcess, 0);
        CloseHandle(host->hProcess);
        host->hProcess = NULL;
    }

    if (host->hThread) {
        CloseHandle(host->hThread);
        host->hThread = NULL;
    }

    if (host->hChildStdinWrite) {
        CloseHandle(host->hChildStdinWrite);
        host->hChildStdinWrite = NULL;
    }

    if (host->hChildStdoutRead) {
        CloseHandle(host->hChildStdoutRead);
        host->hChildStdoutRead = NULL;
    }

    if (host->hJob) {
        CloseHandle(host->hJob);
        host->hJob = NULL;
    }
#endif

    return VINOX_STATUS_OK;
}

} // extern "C"
