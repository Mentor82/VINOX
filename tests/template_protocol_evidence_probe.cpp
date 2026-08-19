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
    if (outcome.contract.tool_format != VINOX_TOOL_FORMAT_CANONICAL_JSON) {
        std::printf("tool_begin_marker='%s' tool_call_marker='%s' tool_end_marker='%s'\n",
            outcome.contract.tool_begin_marker, outcome.contract.tool_call_marker, outcome.contract.tool_end_marker);
    }
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
    // 2026-08-17): Llama3.3-8B-Instruct-Thinking-ov originally dropped the
    // user's question entirely from the rendered prompt, and misidentified
    // the generic Llama role-header token `<|start_header_id|>` (present in
    // every message, not tool-specific) as a native tool boundary marker.
    //
    // 2026-08-18 update #1, after #21 (real ITemplateRuntime/minja rendering)
    // landed: the dropped-user-text half is fixed -- rendering is now a
    // faithful execution of the package's own template, so this half is
    // re-pinned as a forward regression guard (must stay present).
    //
    // 2026-08-18 update #2, after the diff-region-scoped tool-marker
    // extraction landed: the OLD `<|start_header_id|>` false positive is
    // gone (that exact tag is common to every render regardless of tools,
    // so it's correctly excluded now). But Llama3.3's real native tool-call
    // *output* format is untagged bare JSON (`{"name":..,"parameters":..}`,
    // see its chat_template.jinja) with no delimiters at all -- the probe
    // suite only rendered system+user+tools, never an actual assistant
    // tool-call turn, so it had no way to see that yet.
    //
    // 2026-08-18 update #3, after the assistant-tool-call probe (task #10)
    // landed: fixed for real. The new probe renders an actual assistant turn
    // with a native tool_calls entry and diffs it against a plain-content
    // assistant turn, so it directly observes that Llama3.3's output has no
    // delimiting tags at all -- tool_format now correctly resolves to
    // CANONICAL_JSON (decode's generic brace-extraction + name/parameters
    // aliasing is the right fallback for an untagged native shape, not a
    // NATIVE_TEMPLATE with a marker that was never real). Re-pinned as a
    // forward regression guard against the false classification returning.
    {
        std::printf("[REGRESSION PIN] Llama3.3-8B-Instruct-Thinking-ov user text (fixed by #21) ... ");
        ProbeOutcome outcome = run_probe(root + "Llama3.3-8B-Instruct-Thinking-ov\\chat_template.jinja");
        if (outcome.compile_status != VINOX_STATUS_OK || outcome.encode_status != VINOX_STATUS_OK) {
            std::printf("[ SKIP ] (compile/encode did not succeed the way it did during triage)\n");
        } else {
            bool has_user_text = outcome.rendered_prompt.find("What is 2+2?") != std::string::npos;
            if (has_user_text) {
                std::printf("[ PASS ] (user text now present -- do not regress)\n");
            } else {
                std::printf("[ REGRESSED ] user text missing again -- #21 rendering has regressed\n");
                return 1;
            }

            std::printf("[REGRESSION PIN] Llama3.3-8B-Instruct-Thinking-ov correct CANONICAL_JSON (fixed by #20 task #10) ... ");
            bool correctly_canonical = (outcome.contract.tool_format == VINOX_TOOL_FORMAT_CANONICAL_JSON);
            if (correctly_canonical) {
                std::printf("[ PASS ] (no false NATIVE_TEMPLATE marker -- do not regress)\n");
            } else {
                std::printf("[ REGRESSED ] tool_format is no longer CANONICAL_JSON -- false-marker bug may be back\n");
                return 1;
            }
        }
    }

    // Pinned positive regression case (found + fixed 2026-08-18, #20 scope):
    // Phi-4-mini-instruct-ov advertises tools as a per-message field on the
    // system turn (`{% if 'tools' in message %}`), not the top-level `tools`
    // context variable most other packages use (`{% if tools %}`). VINOX only
    // ever set the latter, so this package's tools were silently never
    // rendered into the prompt at all -- confirmed live in
    // realtime_evidence_harness Package G, where the model answered a
    // calculator prompt conversationally because it was never told a
    // calculator tool existed. Fixed by attaching the same canonical tools
    // JSON verbatim to the system message too (MinjaTemplateRuntime's
    // build_messages_json), satisfying both conventions from one input.
    // Pinned forward: if this package's tools stop being detected, the fix
    // regressed.
    {
        std::printf("[REGRESSION PIN] Phi-4-mini-instruct-ov sees advertised tools (fixed 2026-08-18) ... ");
        ProbeOutcome outcome = run_probe(root + "Phi-4-mini-instruct-ov\\chat_template.jinja");
        if (outcome.compile_status != VINOX_STATUS_OK || outcome.encode_status != VINOX_STATUS_OK) {
            std::printf("[ SKIP ] (compile/encode did not succeed the way it did during triage)\n");
        } else {
            bool tools_visible = outcome.rendered_prompt.find("<|tool|>") != std::string::npos;
            if (tools_visible) {
                std::printf("[ PASS ] (tools now rendered into the prompt -- do not regress)\n");
            } else {
                std::printf("[ REGRESSED ] tools missing from rendered prompt again\n");
                return 1;
            }
        }
    }

    // Pinned case (found 2026-08-18 after task #10's assistant-tool-call
    // probe landed; fixed 2026-08-18, task #11): the probe correctly finds
    // DeepSeek's real *outer* native envelope markers
    // ("<｜tool▁calls▁begin｜>" / "<｜tool▁call▁end｜>"), so
    // vinox_model_protocol_decode_tool_call successfully locates and extracts
    // the content between them. But DeepSeek's *inner* envelope is not JSON
    // at all -- "<｜tool▁call▁begin｜>function<｜tool▁sep｜>NAME\n```json\n
    // ARGS\n```<｜tool▁call▁end｜>", a custom name/sep + markdown-fence shape
    // (see its chat_template.jinja) that VINOX has no generic (non-model-
    // specific) way to parse into a canonical call. Originally this fell
    // through decode's "retain original string if non-JSON" path and
    // returned VINOX_STATUS_OK with the raw, un-parsed envelope text as the
    // "canonical" JSON -- a silent pass-through of garbage. Fixed: decode now
    // distinguishes "no envelope located at all" (legitimately lenient, e.g.
    // a plain conversational CANONICAL_JSON response) from "envelope located
    // but its contents aren't valid JSON" (a real format mismatch) and fails
    // closed with FINAL_OUTPUT_INVALID for the latter, rather than
    // fabricating a canonical call VINOX never actually understood. Re-pinned
    // as a forward regression guard against the silent pass-through
    // returning.
    {
        std::printf("[REGRESSION PIN] DeepSeek-R1-Distill-Qwen-1.5B-ov native call fails closed (fixed by task #11) ... ");
        ProbeOutcome outcome = run_probe(root + "DeepSeek-R1-Distill-Qwen-1.5B-ov\\chat_template.jinja");
        if (outcome.compile_status != VINOX_STATUS_OK) {
            std::printf("[ SKIP ] (compile did not succeed the way it did during triage)\n");
        } else {
            const std::string raw_native =
                "<" "\xef" "\xbd" "\x9c" "tool" "\xe2" "\x96" "\x81" "calls" "\xe2" "\x96" "\x81" "begin" "\xef" "\xbd" "\x9c" ">"
                "<" "\xef" "\xbd" "\x9c" "tool" "\xe2" "\x96" "\x81" "call" "\xe2" "\x96" "\x81" "begin" "\xef" "\xbd" "\x9c" ">"
                "function" "<" "\xef" "\xbd" "\x9c" "tool" "\xe2" "\x96" "\x81" "sep" "\xef" "\xbd" "\x9c" ">"
                "get_weather\n```json\n{\"location\": \"Berlin\"}\n```"
                "<" "\xef" "\xbd" "\x9c" "tool" "\xe2" "\x96" "\x81" "call" "\xe2" "\x96" "\x81" "end" "\xef" "\xbd" "\x9c" ">"
                "<" "\xef" "\xbd" "\x9c" "tool" "\xe2" "\x96" "\x81" "calls" "\xe2" "\x96" "\x81" "end" "\xef" "\xbd" "\x9c" ">";
            char canonical_buf[512] = {0};
            size_t written = 0;
            vinox_status dst = vinox_model_protocol_decode_tool_call(
                &outcome.contract, raw_native.c_str(), canonical_buf, sizeof(canonical_buf), &written);
            bool silently_passed_through_garbage =
                (dst == VINOX_STATUS_OK) && std::string(canonical_buf).find("\"tool\"") == std::string::npos;
            if (silently_passed_through_garbage) {
                std::printf("[ REGRESSED ] decode silently returned non-JSON envelope text as OK again\n");
                return 1;
            } else if (dst == VINOX_STATUS_FINAL_OUTPUT_INVALID) {
                std::printf("[ PASS ] (fails closed on the unparseable native envelope -- do not regress)\n");
            } else {
                std::printf("[ CHANGED ] decode status is %d, neither the original bug nor the expected fail-closed fix -- update this pin\n", (int)dst);
                return 1;
            }
        }
    }

    std::printf("================================================================================\n");
    std::printf("   Evidence probe complete. Re-run unmodified after #21 lands.\n");
    std::printf("================================================================================\n");
    return 0;
}
