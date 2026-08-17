// Template/Protocol Evidence Probe (Issue #21 / Issue #20)
//
// Runs vinox_model_protocol_compile() + vinox_model_protocol_encode_prompt()
// against the real chat_template.jinja of each model package in the 7-model
// evidence fixture set, and reports what actually comes out. This is the
// bridge asset between Issue #21 (template rendering) and Issue #20 (protocol
// characterization): once the rendering layer is replaced by a real
// ITemplateRuntime, re-running this file unmodified against the same 7
// packages is how we tell "renderer fixed" apart from "characterizer still
// broken".
//
// Requires the model packages to be present locally under C:\ai\models\OpenVINO
// (same assumption as tests/realtime_evidence_harness.cpp).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const char* reasoning_mode_name(vinox_reasoning_mode m) {
    switch (m) {
        case VINOX_REASONING_NONE: return "NONE";
        case VINOX_REASONING_TAGGED: return "TAGGED";
        case VINOX_REASONING_NATIVE: return "NATIVE";
        default: return "?";
    }
}

const char* start_policy_name(vinox_reasoning_start_policy p) {
    switch (p) {
        case VINOX_REASONING_START_EXPLICIT: return "EXPLICIT";
        case VINOX_REASONING_START_PREFILLED: return "PREFILLED";
        case VINOX_REASONING_START_IMPLICIT: return "IMPLICIT";
        default: return "?";
    }
}

const char* tool_format_name(vinox_tool_format_mode f) {
    switch (f) {
        case VINOX_TOOL_FORMAT_CANONICAL_JSON: return "CANONICAL_JSON";
        case VINOX_TOOL_FORMAT_NATIVE_TEMPLATE: return "NATIVE_TEMPLATE";
        case VINOX_TOOL_FORMAT_NATIVE_CHANNEL: return "NATIVE_CHANNEL";
        default: return "?";
    }
}

struct ProbeOutcome {
    vinox_status compile_status = VINOX_STATUS_RUNTIME_ERROR;
    vinox_model_protocol_contract contract{};
    vinox_status encode_status = VINOX_STATUS_RUNTIME_ERROR;
    std::string rendered_prompt;
};

ProbeOutcome run_probe(const std::string& template_path) {
    ProbeOutcome outcome;
    std::string tpl = slurp(template_path);

    outcome.contract.struct_size = sizeof(outcome.contract);
    outcome.compile_status = vinox_model_protocol_compile(tpl.c_str(), nullptr, &outcome.contract);
    if (outcome.compile_status != VINOX_STATUS_OK) {
        return outcome;
    }

    char prompt_buf[8192] = {0};
    size_t written = 0;
    const char* tools_json = "[{\"name\": \"get_weather\", \"description\": \"Get weather\", \"parameters\": {}}]";
    outcome.encode_status = vinox_model_protocol_encode_prompt(
        &outcome.contract, "You are a helpful assistant.", "What is 2+2?", tools_json,
        prompt_buf, sizeof(prompt_buf), &written);
    if (outcome.encode_status == VINOX_STATUS_OK) {
        outcome.rendered_prompt.assign(prompt_buf, written);
    }
    return outcome;
}

size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return 0;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    return count;
}

void report(const char* label, const ProbeOutcome& outcome) {
    std::printf("--- %s ---\n", label);
    std::printf("compile_status = %d\n", static_cast<int>(outcome.compile_status));
    if (outcome.compile_status != VINOX_STATUS_OK) {
        std::printf("(compile did not succeed; no render evidence for this package)\n\n");
        return;
    }
    std::printf("reasoning_mode=%s start_policy=%s tool_format=%s\n",
        reasoning_mode_name(outcome.contract.reasoning_mode),
        start_policy_name(outcome.contract.reasoning_start_policy),
        tool_format_name(outcome.contract.tool_format));
    std::printf("encode_status = %d\n", static_cast<int>(outcome.encode_status));
    if (outcome.encode_status == VINOX_STATUS_OK) {
        size_t sys_dupes = count_occurrences(outcome.rendered_prompt, "helpful assistant");
        bool has_user_text = outcome.rendered_prompt.find("What is 2+2?") != std::string::npos;
        std::printf("system-phrase occurrences = %zu, user text present = %s\n",
            sys_dupes, has_user_text ? "yes" : "no");
    }
    std::printf("\n");
}

}  // namespace

int main() {
    std::printf("================================================================================\n");
    std::printf("  VINOX Template/Protocol Evidence Probe -- 7-Model Fixture Set (Issue #21/#20)  \n");
    std::printf("================================================================================\n\n");

    const std::string root = "C:\\ai\\models\\OpenVINO\\";
    const std::vector<std::pair<std::string, std::string>> fixtures = {
        {"DeepSeek-R1-Distill-Llama-3.2-1B-ov", root + "DeepSeek-R1-Distill-Llama-3.2-1B-ov\\chat_template.jinja"},
        {"DeepSeek-R1-Distill-Llama-8B-ov", root + "DeepSeek-R1-Distill-Llama-8B-ov\\chat_template.jinja"},
        {"Llama3.3-8B-Instruct-Thinking-ov", root + "Llama3.3-8B-Instruct-Thinking-ov\\chat_template.jinja"},
        {"Qwen2.5-1.5B-Instruct-ov", root + "Qwen2.5-1.5B-Instruct-ov\\chat_template.jinja"},
        {"Phi-4-mini-instruct-ov", root + "Phi-4-mini-instruct-ov\\chat_template.jinja"},
        {"DeepSeek-R1-Distill-Qwen-1.5B-ov", root + "DeepSeek-R1-Distill-Qwen-1.5B-ov\\chat_template.jinja"},
        {"SmolLM3-3B-ov", root + "SmolLM3-3B-ov\\chat_template.jinja"},
    };

    for (const auto& [label, path] : fixtures) {
        ProbeOutcome outcome = run_probe(path);
        report(label.c_str(), outcome);
    }

    // Pinned negative regression case (flagged during the #20/#21 review,
    // 2026-08-17): Llama3.3-8B-Instruct-Thinking-ov currently drops the
    // user's question entirely from the rendered prompt, and misidentifies
    // the generic Llama role-header token `<|start_header_id|>` (present in
    // every message, not tool-specific) as a native tool boundary marker.
    // This assertion intentionally pins the CURRENT BROKEN behavior so that
    // replacing PackageTemplateExecutor with a real ITemplateRuntime (#21)
    // trips this check -- at which point it must be rewritten to assert the
    // correct behavior (user text present, no false tool-marker detection).
    {
        std::printf("[REGRESSION PIN] Llama3.3-8B-Instruct-Thinking-ov known-broken render ... ");
        ProbeOutcome outcome = run_probe(root + "Llama3.3-8B-Instruct-Thinking-ov\\chat_template.jinja");
        if (outcome.compile_status != VINOX_STATUS_OK || outcome.encode_status != VINOX_STATUS_OK) {
            std::printf("[ SKIP ] (compile/encode did not succeed the way it did during triage)\n");
        } else {
            bool has_user_text = outcome.rendered_prompt.find("What is 2+2?") != std::string::npos;
            bool bogus_tool_marker = std::strcmp(outcome.contract.tool_begin_marker, "<|start_header_id|>") == 0;
            if (!has_user_text && bogus_tool_marker) {
                std::printf("[ PASS ] (still broken as triaged: user text dropped, bogus tool marker) - fix pending #21\n");
            } else {
                std::printf("[ CHANGED ] behavior differs from triage baseline -- update this pin to assert correctness\n");
                return 1;
            }
        }
    }

    std::printf("================================================================================\n");
    std::printf("   Evidence probe complete. Re-run unmodified after #21 lands.\n");
    std::printf("================================================================================\n");
    return 0;
}
