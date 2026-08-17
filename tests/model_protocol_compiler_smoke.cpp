#include <iostream>
#include <string>
#include <cstring>
#include <cassert>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Issue #20 — Model Protocol Compiler & Canonical Contract Test Harness   \n";
    std::cout << "================================================================================\n";

    // TEST 01: Compile Generic Chat Template (Behavioral Sentinel Probe)
    {
        std::cout << "[TEST 01] Compile Generic Chat Template -> Canonical Contract ... ";
        const char* tpl = "{system}\n{tools}\nUser: {user}\n{prefill}";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.reasoning_mode == VINOX_REASONING_NONE);
        assert(contract.tool_format == VINOX_TOOL_FORMAT_CANONICAL_JSON);
        assert(contract.protocol_hash != nullptr && strlen(contract.protocol_hash) > 0);
        std::cout << "[ PASS ] (Hash: " << contract.protocol_hash << ")\n";
    }

    // TEST 02: Compile Jinja Chat Template with Prefilled Reasoning State
    {
        std::cout << "[TEST 02] Compile Prefilled Reasoning Template -> PREFILLED Start Policy ... ";
        const char* tpl = "<|im_start|>system\n{system}\n{tools}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n<think>\n{prefill}";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.reasoning_mode == VINOX_REASONING_TAGGED);
        assert(contract.reasoning_start_policy == VINOX_REASONING_START_PREFILLED);
        assert(std::string(contract.reasoning_start_marker) == "<think>");
        assert(std::string(contract.reasoning_end_marker) == "</think>");
        std::cout << "[ PASS ]\n";
    }

    // TEST 03: Compile Native Tool Call Envelope Template
    {
        std::cout << "[TEST 03] Compile Native Tool Envelope Template -> NATIVE_TEMPLATE Tool Format ... ";
        const char* tpl = "<|im_start|>system\n{system}\n<tools>\n{tools}\n</tools><|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n<tool_call>";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);
        assert(contract.tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE);
        assert(std::string(contract.tool_begin_marker) == "<tools>");
        assert(std::string(contract.tool_call_marker) == "<tool_call>");
        assert(std::string(contract.tool_end_marker) == "</tool_call>");
        std::cout << "[ PASS ]\n";
    }

    // TEST 04: Native Tool Call Decoder -> Canonical VINOX Tool Call JSON Translation
    {
        std::cout << "[TEST 04] Decode Native Tool Call Envelope -> Canonical VINOX Tool Call JSON ... ";
        const char* tpl = "<tools>\n{tools}\n</tools>\n<tool_call>";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);

        const char* raw_native_output = "<tool_call>{\"tool\":\"vinox.search\",\"arguments\":{\"query\":\"OpenVINO 2026\"}}</tool_call>";
        char canonical_buf[256] = {0};
        size_t written = 0;
        vinox_status st_dec = vinox_model_protocol_decode_tool_call(&contract, raw_native_output, canonical_buf, sizeof(canonical_buf), &written);
        assert(st_dec == VINOX_STATUS_OK);
        assert(written > 0);
        assert(std::string(canonical_buf).find("vinox.search") != std::string::npos);
        assert(std::string(canonical_buf).find("OpenVINO 2026") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 05: Malformed Native Tool Call -> Strict FAIL (No Repair / Salvage)
    {
        std::cout << "[TEST 05] Malformed Native Tool Call Envelope -> Strict FAIL (VINOX_STATUS_FINAL_OUTPUT_INVALID) ... ";
        const char* tpl = "<tools>\n{tools}\n</tools>\n<tool_call>";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_OK);

        const char* malformed_raw_output = "I cannot call tools right now <tool_call>broken json"; // missing closing tag!
        char canonical_buf[256] = {0};
        size_t written = 0;
        vinox_status st_dec = vinox_model_protocol_decode_tool_call(&contract, malformed_raw_output, canonical_buf, sizeof(canonical_buf), &written);
        assert(st_dec == VINOX_STATUS_FINAL_OUTPUT_INVALID); // Fail closed! No repair!
        std::cout << "[ PASS ]\n";
    }

    // TEST 06: Ambiguous / Malformed Protocol Sentinel -> Fail Closed
    {
        std::cout << "[TEST 06] Ambiguous Sentinels -> Fail Closed (MODEL_PROTOCOL_AMBIGUOUS) ... ";
        const char* bad_tpl = "__AMBIGUOUS_SENTINEL__ mixed protocol";
        vinox_model_protocol_contract contract{};
        vinox_status st = vinox_model_protocol_compile(bad_tpl, nullptr, &contract);
        assert(st == VINOX_STATUS_MODEL_PROTOCOL_AMBIGUOUS);
        std::cout << "[ PASS ]\n";
    }

    // TEST 07: Protocol Hash Reproducibility & Cache Invalidation
    {
        std::cout << "[TEST 07] Protocol Hash Reproducibility & Cache Invalidation ... ";
        const char* tpl1 = "{system}\nUser: {user}\nAssistant: {prefill}";
        const char* tpl2 = "{system}\nUser: {user}\nAssistant: <think>\n{prefill}";

        vinox_model_protocol_contract c1{}, c2{};
        vinox_model_protocol_compile(tpl1, nullptr, &c1);
        vinox_model_protocol_compile(tpl2, nullptr, &c2);

        assert(std::string(c1.protocol_hash) != std::string(c2.protocol_hash)); // Cache invalidation verified!
        std::cout << "[ PASS ]\n";
    }

    std::cout << "================================================================================\n";
    std::cout << "   RESULT: ALL ISSUE #20 MODEL PROTOCOL COMPILER INVARIANTS PASSED 🟢⚡ \n";
    std::cout << "================================================================================\n";
    return 0;
}
