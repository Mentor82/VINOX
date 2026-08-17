// Standalone validation of MinjaTemplateRuntime (Issue #21) against real
// model-package chat_template.jinja files, independent of the still-unwired
// PackageTemplateExecutor / Model Protocol Compiler path (Issue #20). This
// checks the rendering layer alone: does it faithfully execute the package's
// own Jinja, without the duplication/dropped-text corruption the old
// hand-written pattern matcher produced (see tests/template_protocol_evidence_probe.cpp).
//
// Requires the model packages to be present locally under C:\ai\models\OpenVINO
// (same assumption as the other real-fixture tests in this directory).

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "backends/openvino/minja_template_runtime.hpp"

using namespace vinox::model;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0, pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    return count;
}

}  // namespace

int main() {
    std::printf("================================================================================\n");
    std::printf("  MinjaTemplateRuntime Smoke Test (Issue #21)                                    \n");
    std::printf("================================================================================\n\n");

    MinjaTemplateRuntime runtime;

    // TEST 01: DeepSeek-R1-Distill-Qwen-1.5B-ov -- previously rendered the
    // system message 5x and mis-detected the reasoning/tool protocol under
    // the old PackageTemplateExecutor. A faithful renderer must place the
    // system message exactly once and the user's question exactly once.
    {
        std::printf("[TEST 01] DeepSeek-R1-Distill-Qwen-1.5B-ov renders without duplication ... ");
        TemplateRenderRequest req;
        req.template_source = slurp("C:\\ai\\models\\OpenVINO\\DeepSeek-R1-Distill-Qwen-1.5B-ov\\chat_template.jinja");
        req.messages = {
            {"system", "You are a helpful assistant.", "", ""},
            {"user", "What is 2+2?", "", ""},
        };
        req.add_generation_prompt = true;

        TemplateRenderResult res = runtime.render(req);
        assert(res.status == TemplateRenderStatus::Ok);
        size_t sys_count = count_occurrences(res.rendered_text, "helpful assistant");
        size_t user_count = count_occurrences(res.rendered_text, "What is 2+2?");
        assert(sys_count == 1);
        assert(user_count == 1);
        // DeepSeek's own template appends "<think>\n" right after the
        // assistant marker when add_generation_prompt is set -- i.e. reasoning
        // is prefilled, matching Issue #20's own worked example.
        const std::string think_prefill_marker =
            "<" "\xef" "\xbd" "\x9c" "Assistant" "\xef" "\xbd" "\x9c" "><think>";
        bool ends_with_think_prefill = res.rendered_text.find(think_prefill_marker) != std::string::npos;
        assert(ends_with_think_prefill);
        std::printf("[ PASS ] (sys=%zu user=%zu prefilled-think=yes)\n", sys_count, user_count);
    }

    // TEST 02: SmolLM3-3B-ov uses {% generation %}...{% endgeneration %}
    // spans, which minja's public render() API cannot expose. Per the
    // Issue #21 fail-closed decision this must be rejected explicitly, not
    // silently rendered with the spans dropped.
    {
        std::printf("[TEST 02] SmolLM3-3B-ov (generation/endgeneration) -> Unsupported (fail closed) ... ");
        TemplateRenderRequest req;
        req.template_source = slurp("C:\\ai\\models\\OpenVINO\\SmolLM3-3B-ov\\chat_template.jinja");
        req.messages = {
            {"system", "You are a helpful assistant.", "", ""},
            {"user", "What is 2+2?", "", ""},
        };
        req.add_generation_prompt = true;

        TemplateRenderResult res = runtime.render(req);
        assert(res.status == TemplateRenderStatus::Unsupported);
        assert(res.rendered_text.empty());
        std::printf("[ PASS ] (%s)\n", res.error_detail.c_str());
    }

    // TEST 03: Llama3.3-8B-Instruct-Thinking-ov -- previously dropped the
    // user's question entirely and misidentified the generic
    // <|start_header_id|> role-header token as a tool marker. A faithful
    // renderer must at minimum preserve the user's actual question text.
    {
        std::printf("[TEST 03] Llama3.3-8B-Instruct-Thinking-ov preserves user text ... ");
        TemplateRenderRequest req;
        req.template_source = slurp("C:\\ai\\models\\OpenVINO\\Llama3.3-8B-Instruct-Thinking-ov\\chat_template.jinja");
        req.messages = {
            {"system", "You are a helpful assistant.", "", ""},
            {"user", "What is 2+2?", "", ""},
        };
        req.add_generation_prompt = true;

        TemplateRenderResult res = runtime.render(req);
        assert(res.status == TemplateRenderStatus::Ok);
        bool has_user_text = res.rendered_text.find("What is 2+2?") != std::string::npos;
        assert(has_user_text);
        std::printf("[ PASS ] (user text present)\n");
    }

    // TEST 04: Resource bound -- an oversized template source fails closed
    // with ResourceExceeded rather than being parsed.
    {
        std::printf("[TEST 04] Oversized template_source -> ResourceExceeded ... ");
        TemplateRenderRequest req;
        req.template_source = std::string(200000, 'a');
        req.limits.max_template_size = 65536;
        TemplateRenderResult res = runtime.render(req);
        assert(res.status == TemplateRenderStatus::ResourceExceeded);
        std::printf("[ PASS ]\n");
    }

    // TEST 05: Malformed Jinja fails closed as Invalid, not a partial render.
    {
        std::printf("[TEST 05] Malformed Jinja syntax -> Invalid ... ");
        TemplateRenderRequest req;
        req.template_source = "{% if tools %}unclosed if block with no endif";
        TemplateRenderResult res = runtime.render(req);
        assert(res.status == TemplateRenderStatus::Invalid);
        assert(res.rendered_text.empty());
        std::printf("[ PASS ]\n");
    }

    std::printf("\n================================================================================\n");
    std::printf("   MinjaTemplateRuntime smoke tests passed.\n");
    std::printf("================================================================================\n");
    return 0;
}
