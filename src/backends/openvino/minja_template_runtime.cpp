#include "minja_template_runtime.hpp"

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

// minja's parser accepts `{% generation %}...{% endgeneration %}` but
// (see minja.hpp's GenerationTemplateToken handling) treats it as a pure
// no-op: it renders the body inline and exposes no span boundaries through
// TemplateNode::render()'s public string-returning API. Since minja is
// vendored verbatim and not modified to add span instrumentation, this
// runtime cannot honor a caller's request for generation/endgeneration spans.
// Per the Issue #21 decision, that gap must fail closed rather than silently
// return text with an empty span list.
bool contains_generation_block(const std::string& source) {
    return source.find("{% generation") != std::string::npos ||
           source.find("{%- generation") != std::string::npos ||
           source.find("{%generation") != std::string::npos;
}

nlohmann::ordered_json build_messages_json(const TemplateRenderRequest& request) {
    nlohmann::ordered_json messages = nlohmann::ordered_json::array();
    bool tools_attached_to_a_message = false;
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
        // Some packages (e.g. Phi-4-mini) advertise tools as a per-message
        // field on the system turn (`{% if 'tools' in message %}`) rather
        // than a top-level `tools` context variable (Qwen-style
        // `{% if tools %}`). Both conventions get the exact same canonical
        // tool data verbatim as a raw JSON string, matching how this
        // template style always string-concatenates message fields --
        // nothing about the tool definitions themselves is altered, this
        // only widens which convention can see them.
        if (!request.tools_json.empty() && !tools_attached_to_a_message && msg.role == "system") {
            m["tools"] = request.tools_json;
            tools_attached_to_a_message = true;
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

    // minja currently exposes no instrumentation/cancellation hook for hard
    // iteration, nesting-depth, or deterministic work-unit limits. Do not
    // pretend otherwise: std::async+wait_for is not a hard execution bound
    // because destruction of an async future may still wait for the worker,
    // and the underlying render cannot be cancelled. If a caller explicitly
    // requests one of these bounds, reject before executing any template code.
    if (request.limits.max_iterations != 0 ||
        request.limits.max_nesting_depth != 0 ||
        request.limits.max_work_units != 0) {
        result.status = TemplateRenderStatus::Unsupported;
        result.error_detail =
            "requested deterministic template execution bound is not enforceable by the "
            "current unmodified minja backend; failing closed before render";
        return result;
    }

    // Synchronous by design. This backend makes no false wall-clock timeout
    // guarantee until minja gains cooperative cancellation/instrumentation or
    // VINOX moves template execution behind a killable process boundary.
    return render_once(request, max_output_size);
}

}  // namespace vinox::model
