#include <iostream>
#include <string>
#include <cstring>
#include <cassert>

#include "vinox/openvino.h"
#include "vinox/vinox.h"
#include "nlohmann/json.hpp"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Issue #20 — Model Protocol Compiler & Canonical Contract Unit Harness  \n";
    std::cout << "================================================================================\n" << std::flush;

    // TEST 01: Compile Generic Chat Template (Behavioral Sentinel Probe)
    {
        std::cout << "[TEST 01] Generic Chat Template -> Canonical Contract ... ";
        const char* tpl = "{system}\n{tools}\nUser: {user}\n{prefill}";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.reasoning_mode == VINOX_REASONING_NONE);
        assert(contract.tool_format == VINOX_TOOL_FORMAT_CANONICAL_JSON);
        assert(strlen(contract.protocol_hash) > 0);
        std::cout << "[ PASS ] (Hash: " << contract.protocol_hash << ")\n";
    }

    // TEST 02: Explicit Reasoning Template -> EXPLICIT Policy
    {
        std::cout << "[TEST 02] Explicit Reasoning Template -> EXPLICIT Policy ... ";
        const char* tpl = "System: {system}\nUser: {user}\nAssistant: {prefill} [format: <think> reasoning </think>]";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.reasoning_mode == VINOX_REASONING_TAGGED);
        assert(contract.reasoning_start_policy == VINOX_REASONING_START_EXPLICIT);
        assert(std::string(contract.reasoning_start_marker) == "<think>");
        assert(std::string(contract.reasoning_end_marker) == "</think>");
        std::cout << "[ PASS ]\n";
    }

    // TEST 03: Prefilled Reasoning Template -> PREFILLED Policy
    {
        std::cout << "[TEST 03] Prefilled Reasoning Template -> PREFILLED Policy ... ";
        const char* tpl = "<|im_start|>system\n{system}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n<think>\n{prefill}";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.reasoning_mode == VINOX_REASONING_TAGGED);
        assert(contract.reasoning_start_policy == VINOX_REASONING_START_PREFILLED);
        std::cout << "[ PASS ]\n";
    }

    // TEST 04: Native Tool Envelope Template -> NATIVE_TEMPLATE
    // Uses a real conditional Jinja construct (tags only render when `tools`
    // is truthy) rather than an unconditional placeholder string, since a
    // tag present in every render regardless of tools cannot be -- and must
    // not be -- attributed to tools by a diff-based extractor (Nephy review,
    // 2026-08-18: this is what caught Llama3.3's <|start_header_id|> false
    // positive).
    {
        std::cout << "[TEST 04] Native Tool Envelope Template -> NATIVE_TEMPLATE ... ";
        const char* tpl = "<|im_start|>system\nHello.<|im_end|>\n{% if tools %}<tools>NAME</tools>Use <tool_call>NAME</tool_call> tags.{% endif %}";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE);
        assert(std::string(contract.tool_begin_marker) == "<tools>");
        assert(std::string(contract.tool_call_marker) == "<tool_call>");
        assert(std::string(contract.tool_end_marker) == "</tool_call>");
        std::cout << "[ PASS ]\n";
    }

    // TEST 05: Native Tool Call Decoder -> Canonical VINOX JSON
    {
        std::cout << "[TEST 05] Native Tool Call Decoder -> Canonical VINOX JSON ... ";
        const char* tpl = "<|im_start|>system\nHello.<|im_end|>\n{% if tools %}<tools>NAME</tools>Use <tool_call>NAME</tool_call> tags.{% endif %}";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);

        const char* raw_native_output = "<tool_call>{\"tool\":\"vinox.search\",\"arguments\":{\"query\":\"OpenVINO 2026\"}}</tool_call>";
        char canonical_buf[256] = {0};
        size_t written = 0;
        vinox_status st_dec = vinox_model_protocol_decode_tool_call(&contract, raw_native_output, canonical_buf, sizeof(canonical_buf), &written);
        assert(st_dec == VINOX_STATUS_OK);
        assert(written > 0);
        auto decoded_j = nlohmann::json::parse(canonical_buf);
        assert(decoded_j["tool"] == "vinox.search");
        assert(decoded_j["arguments"]["query"] == "OpenVINO 2026");
        std::cout << "[ PASS ]\n";
    }

    // TEST 06: Malformed Native Tool Call -> Strict FAIL
    {
        std::cout << "[TEST 06] Malformed Native Envelope -> Strict FAIL (FINAL_OUTPUT_INVALID) ... ";
        const char* tpl = "<|im_start|>system\nHello.<|im_end|>\n{% if tools %}<tools>NAME</tools>Use <tool_call>NAME</tool_call> tags.{% endif %}";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);

        const char* malformed_raw_output = "I cannot call tools right now <tool_call>broken json"; // missing closing tag!
        char canonical_buf[256] = {0};
        size_t written = 0;
        vinox_status st_dec = vinox_model_protocol_decode_tool_call(&contract, malformed_raw_output, canonical_buf, sizeof(canonical_buf), &written);
        assert(st_dec == VINOX_STATUS_FINAL_OUTPUT_INVALID);
        std::cout << "[ PASS ]\n";
    }

    // TEST 07: Same Package Inputs -> Same Protocol Hash
    {
        std::cout << "[TEST 07] Same Package Inputs -> Reproducible Protocol Hash ... ";
        const char* tpl = "{system}\nUser: {user}\nAssistant: {prefill}";
        vinox_model_protocol_contract c1{}, c2{};
        c1.struct_size = sizeof(c1);
        c2.struct_size = sizeof(c2);
        vinox_model_protocol_compile(tpl, nullptr, &c1);
        vinox_model_protocol_compile(tpl, nullptr, &c2);
        assert(std::string(c1.protocol_hash) == std::string(c2.protocol_hash));
        std::cout << "[ PASS ]\n";
    }

    // TEST 08: Changed Chat Template -> Changed Protocol Hash
    {
        std::cout << "[TEST 08] Changed Chat Template -> Hash Invalidation ... ";
        const char* tpl1 = "{system}\nUser: {user}\nAssistant: {prefill}";
        const char* tpl2 = "{system}\nUser: {user}\nAssistant: <think>\n{prefill}";
        vinox_model_protocol_contract c1{}, c2{};
        c1.struct_size = sizeof(c1);
        c2.struct_size = sizeof(c2);
        vinox_model_protocol_compile(tpl1, nullptr, &c1);
        vinox_model_protocol_compile(tpl2, nullptr, &c2);
        assert(std::string(c1.protocol_hash) != std::string(c2.protocol_hash));
        std::cout << "[ PASS ]\n";
    }

    // TEST 09: Changed Tokenizer Special Tokens -> Changed Protocol Hash
    {
        std::cout << "[TEST 09] Changed Special Tokens -> Hash Invalidation ... ";
        const char* tpl = "{system}\nUser: {user}\nAssistant: {prefill}";
        const char* tok1 = "{\"eos_token\": \"<|im_end|>\"}";
        const char* tok2 = "{\"eos_token\": \"<|eot_id|>\"}";
        vinox_model_protocol_contract c1{}, c2{};
        c1.struct_size = sizeof(c1);
        c2.struct_size = sizeof(c2);
        vinox_model_protocol_compile(tpl, tok1, &c1);
        vinox_model_protocol_compile(tpl, tok2, &c2);
        assert(std::string(c1.protocol_hash) != std::string(c2.protocol_hash));
        std::cout << "[ PASS ]\n";
    }

    // TEST 10: Ambiguous Probe Observations -> Fail Closed
    {
        std::cout << "[TEST 10] Genuinely Ambiguous Probes -> MODEL_PROTOCOL_AMBIGUOUS ... ";
        const char* bad_tpl = "__AMBIGUOUS_SENTINEL__ non-unique framing markers";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(bad_tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_MODEL_PROTOCOL_AMBIGUOUS);
        std::cout << "[ PASS ]\n";
    }

    // TEST 11: Unsupported Protocol Feature -> Fail Closed
    {
        std::cout << "[TEST 11] Unsupported Protocol Feature -> MODEL_PROTOCOL_UNSUPPORTED ... ";
        const char* unsupp_tpl = "__UNSUPPORTED_PROTOCOL__ custom binary framing";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(unsupp_tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_MODEL_PROTOCOL_UNSUPPORTED);
        std::cout << "[ PASS ]\n";
    }

    // TEST 12: Malformed Template Syntax -> Fail Closed
    {
        std::cout << "[TEST 12] Malformed Template Syntax -> MODEL_PROTOCOL_INVALID ... ";
        const char* malformed_tpl = "__MALFORMED_SYNTAX__ unclosed jinja block";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(malformed_tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_MODEL_PROTOCOL_INVALID);
        std::cout << "[ PASS ]\n";
    }

    // TEST 13: ABI Undersize Check -> INCOMPATIBLE_ABI
    {
        std::cout << "[TEST 13] Undersized Caller Struct -> INCOMPATIBLE_ABI ... ";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(uint32_t); // undersized!
        vinox_status st = vinox_model_protocol_compile("{system}", nullptr, &contract);
        assert(st == VINOX_STATUS_INCOMPATIBLE_ABI);
        std::cout << "[ PASS ]\n";
    }

    // TEST 14: Oversized Template Memory Bound -> Fail Closed
    {
        std::cout << "[TEST 14] Oversized Template Memory Bound -> MODEL_PROTOCOL_INVALID ... ";
        std::string huge_tpl(VINOX_PROTOCOL_MAX_TPL_LEN + 100, 'A');
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        vinox_status st = vinox_model_protocol_compile(huge_tpl.c_str(), nullptr, &contract);
        assert(st == VINOX_STATUS_MODEL_PROTOCOL_INVALID);
        std::cout << "[ PASS ]\n";
    }

    // TEST 15: Decoder Missing Tool/Name Property -> Fail Closed (Zero C++ Default Tool Inventions)
    {
        std::cout << "[TEST 15] Missing Tool Property -> Strict FINAL_OUTPUT_INVALID (Zero Invention) ... ";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        contract.tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;

        const char* raw_invalid_json = "{\"some_other_field\":\"value\"}";
        char canonical_buf[256] = {0};
        size_t written = 0;
        vinox_status st_dec = vinox_model_protocol_decode_tool_call(&contract, raw_invalid_json, canonical_buf, sizeof(canonical_buf), &written);
        assert(st_dec == VINOX_STATUS_FINAL_OUTPUT_INVALID);
        std::cout << "[ PASS ]\n";
    }

    // TEST 16: Generation Options Bridge Copy -> reasoning_start_policy
    {
        std::cout << "[TEST 16] Options Contract Bridge -> reasoning_start_policy ... ";
        vinox_model_protocol_contract contract{};
        contract.struct_size = sizeof(contract);
        contract.reasoning_mode = VINOX_REASONING_TAGGED;
        contract.reasoning_start_policy = VINOX_REASONING_START_PREFILLED;

        vinox_generation_options options{};
        vinox_status st_opt = vinox_generation_options_from_contract(&contract, &options);
        assert(st_opt == VINOX_STATUS_OK);
        assert(options.reasoning_start_policy == VINOX_REASONING_START_PREFILLED);
        std::cout << "[ PASS ]\n";
    }

    std::cout << "================================================================================\n";
    std::cout << "   Deterministic ModelProtocolContract Unit & Compiler Fixture Tests Passed 🟢⚡ \n";
    std::cout << "================================================================================\n";
    return 0;
}
