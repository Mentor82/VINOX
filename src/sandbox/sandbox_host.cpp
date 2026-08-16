#include "vinox/vinox_agent.h"
#include <string>
#include <filesystem>
#include <sstream>
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

    // Attach to Job Object for fail-closed process tree resource limits
    host->hJob = CreateJobObjectA(NULL, NULL);
    if (host->hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(host->hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(host->hJob, host->hProcess);
    }
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_exec_tool(vinox_sandbox_host* host, const char* tool_name, const char* args_json, char* out_buf, size_t out_buf_sz) {
    if (!host || !tool_name || !out_buf || out_buf_sz < 1) return VINOX_STATUS_INVALID_ARGUMENT;

#if defined(_WIN32)
    if (!host->hChildStdinWrite || !host->hChildStdoutRead) return VINOX_STATUS_INVALID_STATE;

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = host->next_id++;
    req["method"] = "sandbox/exec_tool";
    req["params"]["name"] = tool_name;

    try {
        if (args_json && strlen(args_json) > 0) {
            req["params"]["arguments"] = nlohmann::json::parse(args_json);
        } else {
            req["params"]["arguments"] = nlohmann::json::object();
        }
    } catch (...) {
        req["params"]["arguments"] = nlohmann::json::object();
    }

    std::string req_str = req.dump() + "\n";
    DWORD written = 0;
    if (!WriteFile(host->hChildStdinWrite, req_str.c_str(), (DWORD)req_str.length(), &written, NULL)) {
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    std::string res_line;
    char ch = 0;
    DWORD read_bytes = 0;
    while (ReadFile(host->hChildStdoutRead, &ch, 1, &read_bytes, NULL) && read_bytes > 0) {
        if (ch == '\n') break;
        if (ch != '\r') res_line.push_back(ch);
    }

    if (res_line.empty()) return VINOX_STATUS_RUNTIME_ERROR;

    strncpy_s(out_buf, out_buf_sz, res_line.c_str(), _TRUNCATE);
#else
    strncpy(out_buf, "{}", out_buf_sz - 1);
    out_buf[out_buf_sz - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
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
