#include "minja_template_runtime.hpp"

#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

// minja is vendored, third-party, and deliberately left unmodified (see
// src/thirdparty/minja/PROVENANCE.md); it is not built to this project's own
// /W4 bar, so its warnings are suppressed locally rather than silenced
// project-wide.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "chat-template.hpp"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vinox::model {
namespace {

constexpr size_t kDefaultMaxTemplateSize = 65536;
constexpr size_t kDefaultMaxOutputSize = 65536;
constexpr auto kRenderTimeout = std::chrono::milliseconds(3000);

// minja's parser accepts `{% generation %}...{% endgeneration %}` but
// (see minja.hpp's GenerationTemplateToken handling) treats it as a pure
// no-op: it renders the body inline and exposes no span boundaries through
// TemplateNode::render()'s public string-returning API. Since minja is
// vendored verbatim and not modified to add span instrumentation, this
// runtime cannot honor a caller's request for generation/endgeneration spans.
// Per the Issue #21 decision (2026-08-18 review), that gap must fail closed
// rather than silently return text with an empty span list, so any template
// using the construct is rejected as Unsupported.
bool contains_generation_block(const std::string& source) {
    return source.find("{% generation") != std::string::npos ||
           source.find("{%- generation") != std::string::npos ||
           source.find("{%generation") != std::string::npos;
}

nlohmann::ordered_json build_messages_json(const TemplateRenderRequest& request) {
    nlohmann::ordered_json messages = nlohmann::ordered_json::array();
    for (const auto& msg : request.messages) {
        nlohmann::ordered_json m;
        m["role"] = msg.role;
        if (!msg.tool_calls_json.empty()) {
            nlohmann::ordered_json parsed;
            try {
                parsed = nlohmann::ordered_json::parse(msg.tool_calls_json);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("malformed tool_calls_json: ") + e.what());
            }
            m["tool_calls"] = std::move(parsed);
            if (!msg.content.empty()) {
                m["content"] = msg.content;
            }
        } else {
            m["content"] = msg.content;
        }
        if (!msg.tool_call_id.empty()) {
            m["tool_call_id"] = msg.tool_call_id;
        }
        messages.push_back(std::move(m));
    }
    return messages;
}

TemplateRenderResult render_once(const TemplateRenderRequest& request, size_t max_output_size) {
    TemplateRenderResult result;
    try {
        nlohmann::ordered_json tools = request.tools_json.empty()
            ? nlohmann::ordered_json()
            : nlohmann::ordered_json::parse(request.tools_json);

        minja::chat_template tpl(request.template_source, request.bos_token, request.eos_token);

        minja::chat_template_inputs inputs;
        inputs.messages = build_messages_json(request);
        inputs.tools = tools;
        inputs.add_generation_prompt = request.add_generation_prompt;
        inputs.extra_context = nlohmann::ordered_json{{"enable_thinking", request.enable_thinking}};

        // Fail-closed contract (Issue #21): the model package's template is
        // executed exactly as authored. minja's polyfill system exists to
        // paper over templates that don't natively support tools/system-role/
        // typed content by rewriting the message list -- that is precisely
        // the "silently rewritten, simplified, or replaced" behavior the
        // issue forbids, so every polyfill is explicitly disabled here.
        minja::chat_template_options opts;
        opts.apply_polyfills = false;
        opts.polyfill_tools = false;
        opts.polyfill_tool_call_examples = false;
        opts.polyfill_tool_calls = false;
        opts.polyfill_tool_responses = false;
        opts.polyfill_system_role = false;
        opts.polyfill_object_arguments = false;
        opts.polyfill_typed_content = false;
        opts.use_bos_token = !request.bos_token.empty();
        opts.use_eos_token = !request.eos_token.empty();
        opts.define_strftime_now = true;

        std::string rendered = tpl.apply(inputs, opts);

        if (rendered.size() > max_output_size) {
            result.status = TemplateRenderStatus::ResourceExceeded;
            result.error_detail = "rendered output exceeds max_output_size";
            return result;
        }

        result.status = TemplateRenderStatus::Ok;
        result.rendered_text = std::move(rendered);
        return result;
    } catch (const std::exception& e) {
        result.status = TemplateRenderStatus::Invalid;
        result.error_detail = std::string("minja render failed: ") + e.what();
        return result;
    }
}

}  // namespace

TemplateRenderResult MinjaTemplateRuntime::render(const TemplateRenderRequest& request) const {
    TemplateRenderResult result;

    const size_t max_template_size = request.limits.max_template_size != 0
        ? request.limits.max_template_size
        : kDefaultMaxTemplateSize;
    const size_t max_output_size = request.limits.max_output_size != 0
        ? request.limits.max_output_size
        : kDefaultMaxOutputSize;

    if (request.template_source.empty()) {
        result.status = TemplateRenderStatus::Invalid;
        result.error_detail = "template_source is empty";
        return result;
    }
    if (request.template_source.size() > max_template_size) {
        result.status = TemplateRenderStatus::ResourceExceeded;
        result.error_detail = "template_source exceeds max_template_size";
        return result;
    }
    if (contains_generation_block(request.template_source)) {
        result.status = TemplateRenderStatus::Unsupported;
        result.error_detail =
            "template uses {% generation %}/{% endgeneration %} spans, which minja's "
            "render API does not expose; failing closed instead of silently returning "
            "text with no span information";
        return result;
    }

    // minja exposes no loop/iteration counter, no recursion/nesting-depth
    // limit, and no cooperative cancellation hook, and the vendored source is
    // intentionally left unmodified (src/thirdparty/minja/PROVENANCE.md), so
    // request.limits.max_iterations / max_nesting_depth / max_work_units
    // cannot be enforced as literal counters by this implementation -- that
    // is a known, deliberate gap, not an oversight. As the only bound this
    // implementation *can* give for "execution time or deterministic work
    // budget" (Issue #21), render runs on a worker thread under a fixed
    // wall-clock ceiling. This bounds how long the caller waits, not the
    // worker itself: minja gives no way to cancel a render in flight, so a
    // genuinely pathological template's thread keeps running (and holding
    // its memory) after the timeout fires. Callers must not treat a timeout
    // as proof the underlying work has stopped.
    std::future<TemplateRenderResult> future =
        std::async(std::launch::async, render_once, request, max_output_size);
    if (future.wait_for(kRenderTimeout) != std::future_status::ready) {
        result.status = TemplateRenderStatus::ResourceExceeded;
        result.error_detail = "template render exceeded the execution time budget";
        return result;
    }
    return future.get();
}

}  // namespace vinox::model
