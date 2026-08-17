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
    std::cout << "  VINOX Issue #19 — Canonical Reasoning / Final-Channel Invariants Test Harness  \n";
    std::cout << "================================================================================\n";

    // 1. Mock Model Handle Initialization
    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = "mock_reasoning_model";
    m_opts.device = "CPU";

    vinox_model* model = nullptr;
    vinox_status load_st = vinox_model_load(&m_opts, &model);
    assert(load_st == VINOX_STATUS_OK && model != nullptr);

    // TEST 1: Non-reasoning mode (VINOX_REASONING_NONE)
    {
        std::cout << "[TEST 01] Non-reasoning mode -> All stream tokens route to FINAL channel ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_NONE;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        assert(!ctx.events.empty());
        for (const auto& ev : ctx.events) {
            assert(ev.channel == VINOX_STREAM_CHANNEL_FINAL);
        }
        std::cout << "[ PASS ]\n";
    }

    // TEST 2: Tagged reasoning mode (VINOX_REASONING_TAGGED)
    {
        std::cout << "[TEST 02] Tagged reasoning mode -> Distinguishes REASONING and FINAL channels ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OK);
        
        bool saw_reasoning = false;
        bool saw_final = false;
        for (const auto& ev : ctx.events) {
            if (ev.channel == VINOX_STREAM_CHANNEL_REASONING) saw_reasoning = true;
            if (ev.channel == VINOX_STREAM_CHANNEL_FINAL) saw_final = true;
        }
        assert(saw_reasoning && saw_final);
        std::cout << "[ PASS ]\n";
    }

    // TEST 3: Legacy vinox_model_generate filters reasoning channel
    {
        std::cout << "[TEST 03] Legacy vinox_model_generate filters out REASONING channel bytes ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 32;
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;

        std::string text_buf;
        auto legacy_cb = [](const char* text, size_t size, void* user_data) -> int {
            auto* str = static_cast<std::string*>(user_data);
            str->append(text, size);
            return 0;
        };

        vinox_status st = vinox_model_generate(model, &gen_opts, legacy_cb, &text_buf);
        assert(st == VINOX_STATUS_OK);
        assert(text_buf.find("Analyzing prompt") == std::string::npos); // Reasoning text filtered out!
        assert(text_buf.find("Hello from OpenVINO mock!") != std::string::npos); // Final text preserved!
        std::cout << "[ PASS ]\n";
    }

    // TEST 4: Global Hard Cap Invariant (Section L)
    {
        std::cout << "[TEST 04] Global hard cap invariant (max_new_tokens hard limit) ... ";
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(gen_opts);
        gen_opts.prompt = "Test prompt";
        gen_opts.max_new_tokens = 1; // Extremely tiny hard cap!
        gen_opts.reasoning_mode = VINOX_REASONING_TAGGED;

        StreamTestCtx ctx;
        vinox_status st = vinox_model_generate_stream(model, &gen_opts, mock_stream_callback, &ctx);
        assert(st == VINOX_STATUS_OUT_OF_RANGE);
        std::cout << "[ PASS ]\n";
    }

    vinox_model_destroy(model);

    std::cout << "================================================================================\n";
    std::cout << "       RESULT: ALL ISSUE #19 REASONING CHANNEL INVARIANTS PASSED 🟢⚡\n";
    std::cout << "================================================================================\n";
    return 0;
}
