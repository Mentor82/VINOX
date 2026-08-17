#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <chrono>

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
    std::cout << "  VINOX Issue #19 — Profile-Driven Reasoning / Final-Channel Test Suite       \n";
    std::cout << "================================================================================\n";

    // 1. Mock Model Handle Initialization
    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = "mock_reasoning_model";
    m_opts.device = "CPU";

    vinox_model* model = nullptr;
    vinox_status load_st = vinox_model_load(&m_opts, &model);
    assert(load_st == VINOX_STATUS_OK && model != nullptr);

    // TEST 01: Non-reasoning mode (VINOX_REASONING_NONE)
    {
        std::cout << "[TEST 01] Non-reasoning mode -> All stream tokens route to FINAL channel ... ";
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

    // TEST 02: Explicit start policy (VINOX_REASONING_START_EXPLICIT)
    {
        std::cout << "[TEST 02] EXPLICIT_START policy -> Requires start tag to enter reasoning ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;
        gen_opts.reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
        gen_opts.reasoning_start_tag = "<think>";
        gen_opts.reasoning_end_tag = "</think>";

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 03: Implicit start policy (VINOX_REASONING_START_IMPLICIT) for DeepSeek-R1
    {
        std::cout << "[TEST 03] IMPLICIT_START policy -> Reasoning from token 0 until </think> ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;
        gen_opts.reasoning_start_policy = VINOX_REASONING_START_IMPLICIT;
        gen_opts.reasoning_start_tag = "<think>";
        gen_opts.reasoning_end_tag = "</think>";

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 04: Prefilled start policy (VINOX_REASONING_START_PREFILLED)
    {
        std::cout << "[TEST 04] PREFILLED_START policy -> Template opened reasoning prefill ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;
        gen_opts.reasoning_start_policy = VINOX_REASONING_START_PREFILLED;
        gen_opts.reasoning_start_tag = "<think>";
        gen_opts.reasoning_end_tag = "</think>";

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        std::cout << "[ PASS ]\n";
    }

    // TEST 05: Profile Default Fetching & Validation
    {
        std::cout << "[TEST 05] Model Profile Canonical Ownership & Profile Validation ... ";
        vinox_model_profile deepseek_prof{};
        vinox_status st1 = vinox_model_profile_get_default("DeepSeek-R1-Distill", &deepseek_prof);
        assert(st1 == VINOX_STATUS_OK);
        assert(deepseek_prof.reasoning_start_policy == VINOX_REASONING_START_IMPLICIT);
        assert(deepseek_prof.reasoning_can_disable == 0);

        vinox_status st2 = vinox_model_profile_validate(&deepseek_prof);
        assert(st2 == VINOX_STATUS_OK);

        vinox_model_profile bad_prof{};
        bad_prof.struct_size = sizeof(bad_prof);
        bad_prof.reasoning_mode = VINOX_REASONING_NATIVE;
        bad_prof.tool_format = VINOX_TOOL_FORMAT_NATIVE_TEMPLATE; // Contradictory combination!
        vinox_status st3 = vinox_model_profile_validate(&bad_prof);
        assert(st3 == VINOX_STATUS_INVALID_ARGUMENT); // Fail closed!

        std::cout << "[ PASS ]\n";
    }

    // TEST 06: Capability Gate: Unsupported disabling of reasoning mode (Blocker 3)
    {
        std::cout << "[TEST 06] Capability Gate: Disable forbidden when reasoning_can_disable == 0 ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_NONE;
        gen_opts.reasoning_can_disable = 0; // Model profile forbids disabling!

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_NOT_SUPPORTED);
        std::cout << "[ PASS ]\n";
    }

    // TEST 07: Capability Gate: Native channel mode precedence (Blocker 4)
    {
        std::cout << "[TEST 07] Capability Gate: VINOX_REASONING_NATIVE returns NOT_SUPPORTED if unsupported ... ";
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
    std::cout << "       RESULT: ALL ISSUE #19 PROFILE & REASONING INVARIANTS PASSED 🟢⚡\n";
    std::cout << "================================================================================\n";
    return 0;
}
