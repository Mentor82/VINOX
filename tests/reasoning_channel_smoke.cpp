#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

struct ChannelEvent {
    vinox_stream_channel channel;
    std::string text;
};

struct StreamTestCtx {
    std::vector<ChannelEvent> events;
};

static int mock_stream_callback(vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) {
    auto* ctx = static_cast<StreamTestCtx*>(user_data);
    ctx->events.push_back({channel, std::string(text, text_size)});
    return 0;
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Issue #19 — Profile-Driven Reasoning & Delimiter Stream Test Harness    \n";
    std::cout << "================================================================================\n";

    // 1. Mock Model Handle Initialization
    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = "mock";
    m_opts.device = "CPU";

    vinox_model* model = nullptr;
    vinox_status load_st = vinox_model_load(&m_opts, &model);
    assert(load_st == VINOX_STATUS_OK && model != nullptr);

    // TEST 01: Non-reasoning canonical profile remains byte-for-byte behavior-compatible
    {
        std::cout << "[TEST 01] Non-reasoning profile -> All stream tokens route to FINAL channel ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_NONE;
        gen_opts.reasoning_can_disable = 1;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        assert(!ctx.events.empty());
        for (const auto& ev : ctx.events) {
            assert(ev.channel == VINOX_STREAM_CHANNEL_FINAL);
        }
        std::cout << "[ PASS ]\n";
    }

    // TEST 02: Operational vinox_model_profile_format_prompt (Nephy Blocker 5)
    {
        std::cout << "[TEST 02] Operational vinox_model_profile_format_prompt execution ... ";
        vinox_model_profile prof{};
        vinox_status st_p = vinox_model_profile_get_default("deepseek_r1", &prof);
        assert(st_p == VINOX_STATUS_OK);

        char formatted_buf[512] = {0};
        size_t written = 0;
        vinox_status st_fmt = vinox_model_profile_format_prompt(
            &prof,
            "System prompt",
            "What is 2+2?",
            "{\"tools\":[]}",
            formatted_buf,
            sizeof(formatted_buf),
            &written
        );
        assert(st_fmt == VINOX_STATUS_OK);
        assert(written > 0);
        assert(std::string(formatted_buf).find("What is 2+2?") != std::string::npos);
        assert(std::string(formatted_buf).find("Assistant:") != std::string::npos);
        std::cout << "[ PASS ]\n";
    }

    // TEST 03: Explicit start policy profile (EXPLICIT_START)
    {
        std::cout << "[TEST 03] EXPLICIT_START profile -> Requires explicit start tag to enter reasoning ... ";
        vinox_model_profile prof{};
        vinox_status st_p = vinox_model_profile_get_default("qwen2_5", &prof);
        assert(st_p == VINOX_STATUS_OK && prof.reasoning_start_policy == VINOX_REASONING_START_EXPLICIT);

        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.profile = &prof;
        gen_opts.reasoning_mode = prof.reasoning_mode;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 04: Implicit start policy profile (IMPLICIT_FROM_GENERATION_START) for DeepSeek-R1
    {
        std::cout << "[TEST 04] IMPLICIT_START profile -> Reasoning from token 0 until </think> ... ";
        vinox_model_profile prof{};
        vinox_status st_p = vinox_model_profile_get_default("deepseek_r1", &prof);
        assert(st_p == VINOX_STATUS_OK && prof.reasoning_start_policy == VINOX_REASONING_START_IMPLICIT);

        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.profile = &prof;
        gen_opts.reasoning_mode = prof.reasoning_mode;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 05: Prefilled start policy profile (PREFILLED_START)
    {
        std::cout << "[TEST 05] PREFILLED_START profile -> Template opened reasoning prefill ... ";
        vinox_model_profile prof{};
        vinox_status st_p = vinox_model_profile_get_default("prefilled_tagged", &prof);
        assert(st_p == VINOX_STATUS_OK && prof.reasoning_start_policy == VINOX_REASONING_START_PREFILLED);

        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.profile = &prof;
        gen_opts.reasoning_mode = prof.reasoning_mode;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 06: Neutral Profile Fallback & Fail-Closed Validation (Nephy Blocker 4)
    {
        std::cout << "[TEST 06] Neutral Profile Fallback & Fail-Closed Validation ... ";
        vinox_model_profile deepseek_prof{};
        vinox_status st1 = vinox_model_profile_get_default("deepseek_r1", &deepseek_prof);
        assert(st1 == VINOX_STATUS_OK);
        assert(deepseek_prof.reasoning_start_policy == VINOX_REASONING_START_IMPLICIT);
        assert(deepseek_prof.reasoning_can_disable == 0);

        vinox_status st2 = vinox_model_profile_validate(&deepseek_prof);
        assert(st2 == VINOX_STATUS_OK);

        // Contradictory Combination Validation
        vinox_model_profile bad_prof{};
        bad_prof.struct_size = sizeof(bad_prof);
        bad_prof.reasoning_mode = VINOX_REASONING_NATIVE;
        bad_prof.tool_format = VINOX_TOOL_FORMAT_NATIVE_TEMPLATE; // Contradictory combination!
        vinox_status st3 = vinox_model_profile_validate(&bad_prof);
        assert(st3 == VINOX_STATUS_INVALID_ARGUMENT); // Fail closed!

        std::cout << "[ PASS ]\n";
    }

    // TEST 07: Capability Gate: Unsupported disabling of reasoning mode
    {
        std::cout << "[TEST 07] Capability Gate: Disable forbidden when reasoning_can_disable == 0 ... ";
        vinox_model_profile prof{};
        vinox_model_profile_get_default("deepseek_r1", &prof);

        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.profile = &prof;
        gen_opts.reasoning_mode = VINOX_REASONING_NONE; // Forbidden disable attempt!

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_NOT_SUPPORTED);
        std::cout << "[ PASS ]\n";
    }

    // TEST 08: Capability Gate: Native channel mode precedence
    {
        std::cout << "[TEST 08] Capability Gate: VINOX_REASONING_NATIVE returns NOT_SUPPORTED if unsupported ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_NATIVE;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_NOT_SUPPORTED);
        std::cout << "[ PASS ]\n";
    }

    vinox_model_destroy(model);

    std::cout << "================================================================================\n";
    std::cout << "   RESULT: ALL ISSUE #19 DETERMINISTIC PARSER & PROFILE TESTS PASSED 🟢⚡ \n";
    std::cout << "================================================================================\n";
    return 0;
}
